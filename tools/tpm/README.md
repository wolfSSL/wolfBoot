# TPM Tools for wolfBoot

Tools to help with TPM setup.

For wolfBoot TPM features see (docs/TPM.md)[/docs/TPM.md].

## Secure Boot Root of Trust (ROT)

Examples:

* `./tools/tpm/rot`: Parse keystore.c keys, hash each and read NV values.
* `./tools/tpm/rot -write`: Also write hash to NV.
* `./tools/tpm/rot -write -lock`: Write and lock the NV
* `./tools/tpm/rot -write -sha384`: Write NV using SHA2-384
* `./tools/tpm/rot -write -auth=test`: Use password for NV access

## Sealing / Unsealing

A PCR policy is signed with the `tools/keytool/sign` tool using the --policy argument.
The policy file comes from the `tools/tpm/policy_create` tool and contains a 4-byte PCR mask and PCR digest.
The authorization for the sealing/unsealing comes from the signed policy and its public key (stored in `keystore.c`).
Typically PCR's 0-15 are used for boot measurements and sealing/unsealing, since they can only be reset on a reboot. The PCR 16 is used for testing.

Examples:

* `./tools/tpm/pcr_reset 16`: Reset PCR 16
* `./tools/tpm/pcr_extend 16 aaa.bin`: Extend PCR 16 with the SHA2-256 hash of "aaa"
* `./tools/tpm/policy_create -pcr=16 -pcrdigest=eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc -out=policy.bin`: Create a policy.bin file with 4-byte PCR mask and 32-byte PCR digest.
* `./tools/keytools/sign --ecc256 --policy policy.bin test-app/image.elf wolfboot_signing_private_key.der 1`: Sign the PCR policy and include in the `HDR_POLICY_SIGNATURE` image header.
* `make POLICY_FILE=policy.bin`: This will perform a build and include the policy file argument to the sign tool. This can also go into the .config file.
