#/bin/bash

set -e

WOLFBOOT_DIR=$(pwd)
# Parse command line options
while getopts "bfr" opt; do
    case "$opt" in
        b)
            REBUILD_ONLY=true
            ;;
        f)
            FS_BOOT=true
            ;;
        r)
            ANTI_ROLLBACK=true
            ;;
        *)
            echo "Usage: $0 [-b] [-f] [-r]"
            echo "-b : rebuild only (skip FSP build, key gen, ecc)"
            echo "-f : boot from a file on a read-only filesystem (DISK_FS)"
            echo "-r : TPM-NV anti-rollback across three boots"
            exit 1
            ;;
    esac
done


if [ "$REBUILD_ONLY" != true ]; then
  if [ "$FS_BOOT" = true ]; then
    CONFIG=x86_fsp_qemu_fs.config
  elif [ "$ANTI_ROLLBACK" = true ]; then
    CONFIG=x86_fsp_qemu_antirollback.config
  else
    CONFIG=x86_fsp_qemu_seal.config
  fi

  make distclean
  cp "config/examples/${CONFIG}" .config
  ./tools/scripts/x86_fsp/qemu/qemu_build_fsp.sh
  make keytools
  if [ "$FS_BOOT" = true ] || [ "$ANTI_ROLLBACK" = true ]; then
    # The filesystem config does not use the TPM, so only the image key is
    # needed here. It must match SIGN= in x86_fsp_qemu_fs.config (ECC256),
    # or the generated src/keystore.c raises a key-algorithm mismatch.
    ./tools/keytools/keygen --force --ecc256 \
        -g wolfboot_signing_private_key.der -keystoreDir src/
  else
    make tpmtools
    # generate one key for images and one for the TPM
    ./tools/keytools/keygen --force --ecc384 -g wolfboot_signing_private_key.der --ecc256 -g tpm_seal_key.key -keystoreDir src/
  fi
fi

if [ "$FS_BOOT" = true ] || [ "$ANTI_ROLLBACK" = true ]; then
  make
else
  # manual add ECC256 for TPM
  make CFLAGS_EXTRA="-DHAVE_ECC256"
fi

# test-app
make test-app/image.elf

if [ "$FS_BOOT" != true ] && [ "$ANTI_ROLLBACK" != true ]; then
  # compute pcr0 value
  PCR0=$(python ./tools/scripts/x86_fsp/compute_pcr.py --target qemu wolfboot_stage1.bin | tail -n 1)
  echo $PCR0
  ./tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key  -pcr=0 -pcrdigest=$PCR0

  ./tools/scripts/x86_fsp/tpm_install_policy.sh policy.bin.sig
fi
if [ "$FS_BOOT" = true ]; then
  FS=1 IMAGE=test-app/image.elf SIGN=--ecc256 \
      ./tools/scripts/x86_fsp/qemu/make_hd.sh
elif [ "$ANTI_ROLLBACK" = true ]; then
  IMAGE=test-app/image.elf SIGN=--ecc256 \
      ./tools/scripts/x86_fsp/qemu/make_hd.sh
else
  IMAGE=test-app/image.elf SIGN=--ecc384 \
      ./tools/scripts/x86_fsp/qemu/make_hd.sh
fi

# Boot once with the swtpm state kept from the previous boot, wait for the
# test app, and leave the console log in /tmp/qemu_output.
run_qemu_tpm() {
  ./tools/scripts/x86_fsp/qemu/qemu.sh -t -p $1 > /tmp/qemu_output 2>&1 &
  for i in $(seq 1 30); do
    sleep 1
    grep -q "wolfBoot QEMU x86 FSP test app" /tmp/qemu_output && break
    grep -q "PANIC" /tmp/qemu_output && break
  done
  timeout 5 echo 'quit' > /tmp/qemu_mon.in || true
  wait
  cat /tmp/qemu_output
}

expect_log() {
  if ! grep -q "$1" /tmp/qemu_output; then
    echo -e "\e[31mTEST FAILED: missing '$1'\e[0m"
    exit 255
  fi
}

reject_log() {
  if grep -q "$1" /tmp/qemu_output; then
    echo -e "\e[31mTEST FAILED: unexpected '$1'\e[0m"
    exit 255
  fi
}

if [ "$ANTI_ROLLBACK" = true ]; then
  # Boot 1, fresh TPM: slot B (version 2) boots and anchors the floor.
  run_qemu_tpm -c
  expect_log "Anti-rollback anchor initialised at version 2"
  expect_log "wolfBoot QEMU x86 FSP test app"

  # Boot 2: both slots v1. It boots, the floor survived the TPM restart and
  # the downgrade is written (platform hierarchy enabled again at startup).
  dd if=image_v1_signed.bin of=app.bin bs=512 seek=34816 conv=notrunc
  run_qemu_tpm
  expect_log "ROLLBACK: booting version 1"
  expect_log "ROLLBACK: total rollbacks recorded 1"
  expect_log "wolfBoot QEMU x86 FSP test app"

  # Boot 3: the counter keeps counting across boots.
  run_qemu_tpm
  expect_log "ROLLBACK: total rollbacks recorded 2"
  expect_log "wolfBoot QEMU x86 FSP test app"
  reject_log "WARNING: could not"
  reject_log "PANIC"
  echo -e "\e[32mTEST OK\e[0m"
  exit 0
fi

echo "RUNNING QEMU"
# launch qemu in background
# -t starts an emulated TPM. The seal path needs it; the filesystem config
# has no TPM support compiled in, so starting one there is pointless and
# adds a swtpm dependency. Worse, if swtpm is missing QEMU blocks forever
# waiting on its socket instead of failing, which costs a CI timeout.
if [ "$FS_BOOT" = true ]; then
  ./tools/scripts/x86_fsp/qemu/qemu.sh -p | tee /tmp/qemu_output &
else
  ./tools/scripts/x86_fsp/qemu/qemu.sh -t -p | tee /tmp/qemu_output &
fi
echo "WAITING FOR QEMU TO RUN"
sleep 5

# close qemu
timeout 5 echo 'quit' > /tmp/qemu_mon.in
output=$(cat /tmp/qemu_output)
set +e
app=$(echo "$output" | grep -m 1 "wolfBoot QEMU x86 FSP test app")
if [ -n "$app" ]; then
  echo "Found 'wolfBoot QEMU x86 FSP test app' in the output."
else
  echo -e "\e[31mTEST FAILED\e[0m"
  exit 255
fi

# The app banner above is the success gate for both paths.
#
# There used to be a grep for "DISK LOCK SECRET:" here. It was already inert
# -- it tested $output rather than the grep result, so it could never fail --
# and the string it looked for no longer exists: 805b16e2 ("remove TPM secret
# debug logging") deleted it from the source, so re-adding the assertion would
# fail a run that is actually correct. On the seal path the banner is itself
# the assertion: the disk is TPM-unlocked, so a failed unseal means the app
# never runs at all.
echo -e "\e[32mTEST OK\e[0m"
exit 0
