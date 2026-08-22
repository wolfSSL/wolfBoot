#/bin/bash

set -e

WOLFBOOT_DIR=$(pwd)
# Parse command line options
while getopts "bf" opt; do
    case "$opt" in
        b)
            REBUILD_ONLY=true
            ;;
        f)
            FS_BOOT=true
            ;;
        *)
            echo "Usage: $0 [-b] [-f]"
            echo "-b : rebuild only (skip FSP build, key gen, ecc)"
            echo "-f : boot from a file on a read-only filesystem (DISK_FS)"
            exit 1
            ;;
    esac
done


if [ "$REBUILD_ONLY" != true ]; then
  if [ "$FS_BOOT" = true ]; then
    CONFIG=x86_fsp_qemu_fs.config
  else
    CONFIG=x86_fsp_qemu_seal.config
  fi

  make distclean
  cp "config/examples/${CONFIG}" .config
  ./tools/scripts/x86_fsp/qemu/qemu_build_fsp.sh
  make keytools
  if [ "$FS_BOOT" = true ]; then
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

if [ "$FS_BOOT" = true ]; then
  make
else
  # manual add ECC256 for TPM
  make CFLAGS_EXTRA="-DHAVE_ECC256"
fi

# test-app
make test-app/image.elf

if [ "$FS_BOOT" != true ]; then
  # compute pcr0 value
  PCR0=$(python ./tools/scripts/x86_fsp/compute_pcr.py --target qemu wolfboot_stage1.bin | tail -n 1)
  echo $PCR0
  ./tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key  -pcr=0 -pcrdigest=$PCR0

  ./tools/scripts/x86_fsp/tpm_install_policy.sh policy.bin.sig
fi
if [ "$FS_BOOT" = true ]; then
  FS=1 IMAGE=test-app/image.elf SIGN=--ecc256 \
      ./tools/scripts/x86_fsp/qemu/make_hd.sh
else
  IMAGE=test-app/image.elf SIGN=--ecc384 \
      ./tools/scripts/x86_fsp/qemu/make_hd.sh
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
