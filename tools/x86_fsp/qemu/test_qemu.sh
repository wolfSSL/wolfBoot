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

# fake policy (redo with the value obtained for a run)
./tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key  -pcr=0 -policydigest=3373F61EF59E71D7F40881F5A4A44D4360EFA0B7540130199FDAF98A2E20E8BA

./tools/x86_fsp/tpm_install_policy.sh policy.bin.sig
IMAGE=test-app/image.elf SIGN=--ecc384 ./tools/x86_fsp/qemu/make_hd.sh

# run w/ ./tools/scripts/qemu64/qemu64-tpm.sh
# copy the value of PCR eg:
# Policy signature failed!
# Expected PCR Mask (0x1) and PCR Policy (32)
# 651CC3EDC7D5AD395BB7EF441FC067EAAC325735050071D6406BA49CE9F16B64 

# sign the correct policy
# ./tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key  -pcr=0 -policydigest=651CC3EDC7D5AD395BB7EF441FC067EAAC325735050071D6406BA49CE9F16B64 
# ./tools/x86_fsp/tpm_install_policy.sh policy.bin.sig

# test again
# ./tools/scripts/qemu64/qemu64-tpm.sh
