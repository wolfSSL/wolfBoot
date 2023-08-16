# wolfBoot TPM support

In wolfBoot we support TPM based root of trust, sealing/unsealing, cryptographic offloading and measured boot using a TPM.

## Build Options

| Config Option | Preprocessor Macro | Description                         |
| ------------- | ------------------ | ----------------------------------- |
| `WOLFTPM=1`   | `WOLFBOOT_TPM`     | Enables wolfTPM support and cryptographic offloading for RSA2048 and ECC256/384 |
| `WOLFBOOT_TPM_KEYSTORE=1` | `WOLFBOOT_TPM_KEYSTORE` | Enables TPM based root of trust. NV Index must store a hash of the trusted public key. |
| `WOLFBOOT_TPM_KEYSTORE_NV_INDEX=0x` | `WOLFBOOT_TPM_KEYSTORE_NV_INDEX=0x` | NV index in platform range 0x1400000 - 0x17FFFFF |
| `MEASURED_BOOT=1` | `WOLFBOOT_MEASURED_BOOT` | Enable measured boot. Extend PCR with wolfBoot hash. |
| `MEASURED_PCR_A=16` | `WOLFBOOT_MEASURED_PCR_A=16` | The PCR index to use. See [docs/measured_boot.md](/docs/measured_boot.md) |

## Root of Trust (ROT)

See wolfTPM Secure Root of Trust (ROT) example [here](https://github.com/wolfSSL/wolfTPM/tree/master/examples/boot).

The design uses a platform NV handle that has been locked. The NV stores a hash of the public key. It is recommended to supply a derived "authentication" value to prevent TPM tampering. This authentication value is encrypted on the bus.

## Cryptographic offloading

The RSA2048 and ECC256/384 bit verification can be offloaded to a TPM for code size reduction or performance improvement.

## Measured Boot

The wolfBoot image is hashed and extended to the indicated PCR. This can be used later in the application to prove the boot process was not tampered with.

## Sealing and Unsealing a secret

API's for this will be available soon.
