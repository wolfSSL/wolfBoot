#/bin/bash

set -e

WOLFBOOT_DIR=$(pwd)
CONFIG=x86_fsp_qemu_seal.config

make distclean
cp "config/examples/${CONFIG}" .config
./tools/x86_fsp/qemu/qemu_build_fsp.sh
make keytools
rm -f tpm_seal_key.key
# generate one key for images and one for the TPM
./tools/keytools/keygen --ecc384 -g wolfboot_signing_private_key.der --ecc256 -g tpm_seal_key.key -keystoreDir src/

# manual add ECC256 for TPM
make CFLAGS_EXTRA="-DHAVE_ECC256"

# test-app
make test-app/image.elf
make tpmtools

# compute pcr0 value
PCR0=$(python ./tools/x86_fsp/compute_pcr.py --target qemu wolfboot_stage1.bin | tail -n 1)
echo $PCR0
./tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key  -pcr=0 -pcrdigest=$PCR0

./tools/x86_fsp/tpm_install_policy.sh policy.bin.sig
IMAGE=test-app/image.elf SIGN=--ecc384 ./tools/x86_fsp/qemu/make_hd.sh

echo "RUNNING QEMU"
# launch qemu in background
./tools/scripts/qemu64/qemu64-tpm.sh -p > /tmp/qemu_output &
echo "WAITING FOR QEMU TO RUN"
sleep 5

# close qemu
timeout 5 echo 'quit' > /tmp/qemu_mon.in
output=$(cat /tmp/qemu_output)
app=$(echo "$output" | grep -m 1 "wolfBoot QEMU x86 FSP test app")
if [ -n "$app" ]; then
  echo "Found 'wolfBoot QEMU x86 FSP test app' in the output."
else
  echo -e "\e[31mTEST FAILED\e[0m"
  exit 255
fi

output=$(echo "$output" | grep -m 1 "DISK LOCK SECRET:")
if [ -n "$output" ]; then
  echo -e "\e[32mTEST OK\e[0m"
  exit 0
else
  echo -e "\e[31mTEST FAILED\e[0m"
  exit 255
fi
