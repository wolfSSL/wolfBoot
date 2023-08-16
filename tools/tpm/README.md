# TPM Tools for wolfBoot

Tools to help with TPM setup.

For wolfBoot TPM features see (docs/TPM.md)[/docs/TPM.md].

# Secure Boot Root of Trust (ROT)

Examples:

* `./tools/tpm/rot`: Parse keystore.c keys, hash each and read NV values.
* `./tools/tpm/rot -write`: Also write hash to NV.
* `./tools/tpm/rot -write -lock`: Write and lock the NV
* `./tools/tpm/rot -write -sha384`: Write NV using SHA2-384
* `./tools/tpm/rot -write -auth=test`: Use password for NV access
