# wolfBoot TPM support

In wolfBoot we support TPM based root of trust, sealing/unsealing, cryptographic offloading and measured boot using a TPM.

## Build Options

| Config Option | Preprocessor Macro | Description                         |
| ------------- | ------------------ | ----------------------------------- |
| `WOLFTPM=1`   | `WOLFBOOT_TPM`     | Enables wolfTPM support |
| `WOLFBOOT_TPM_VERIFY=1` | `WOLFBOOT_TPM_VERIFY` | Enables cryptographic offloading for RSA2048 and ECC256/384 to the TPM. |
| `WOLFBOOT_TPM_KEYSTORE=1` | `WOLFBOOT_TPM_KEYSTORE` | Enables TPM based root of trust. NV Index must store a hash of the trusted public key. |
| `WOLFBOOT_TPM_KEYSTORE_NV_BASE=0x` | `WOLFBOOT_TPM_KEYSTORE_NV_BASE=0x` | NV index in platform range 0x1400000 - 0x17FFFFF. |
| `WOLFBOOT_TPM_KEYSTORE_AUTH=secret` | `WOLFBOOT_TPM_KEYSTORE_AUTH` | Password for NV access |
| `MEASURED_BOOT=1` | `WOLFBOOT_MEASURED_BOOT` | Enable measured boot. Extend PCR with wolfBoot hash. |
| `MEASURED_PCR_A=16` | `WOLFBOOT_MEASURED_PCR_A=16` | The PCR index to use. See [docs/measured_boot.md](/docs/measured_boot.md). |
| `WOLFBOOT_TPM_SEAL=1` | `WOLFBOOT_TPM_SEAL` | Enables support for sealing/unsealing based on PCR policy signed externally. |
| `WOLFBOOT_TPM_SEAL_NV_BASE=0x01400300` | `WOLFBOOT_TPM_SEAL_NV_BASE` | To override the default sealed blob storage location in the platform hierarchy. |
| `WOLFBOOT_TPM_SEAL_AUTH=secret` | `WOLFBOOT_TPM_SEAL_AUTH` | Password for sealing/unsealing secrets |

## Root of Trust (ROT)

See wolfTPM Secure Root of Trust (ROT) example [here](https://github.com/wolfSSL/wolfTPM/tree/master/examples/boot).

The design uses a platform NV handle that has been locked. The NV stores a hash of the public key. It is recommended to supply a derived "authentication" value to prevent TPM tampering. This authentication value is encrypted on the bus.

## Cryptographic offloading

The RSA2048 and ECC256/384 bit verification can be offloaded to a TPM for code size reduction or performance improvement. Enabled using `WOLFBOOT_TPM_VERIFY`.
NOTE: The TPM's RSA verify requires ASN.1 encoding, so use SIGN=RSA2048ENC

## Measured Boot

The wolfBoot image is hashed and extended to the indicated PCR. This can be used later in the application to prove the boot process was not tampered with. Enabled with `WOLFBOOT_MEASURED_BOOT` and exposes API `wolfBoot_tpm2_extend`.

## Sealing and Unsealing a secret

See the wolfTPM Sealing/Unsealing example [here](https://github.com/wolfSSL/wolfTPM/tree/secret_seal/examples/boot#secure-boot-encryption-key-storage)

Known PCR values must be signed to seal/unseal a secret. The signature for the authorization policy resides in the signed header using the `--policy` argument.
If a signed policy is not in the header then a value cannot be sealed. Instead the PCR(s) values and a PCR policy digest will be printed to sign. You can use `./tools/keytools/sign` or `./tools/tpm/policy_sign` to sign the policy externally.

This exposes two new wolfBoot API's for sealing and unsealing data with blob stored to NV index:
```c
int wolfBoot_seal_auth(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, const uint8_t* secret, int secret_sz, const byte* auth, int authSz);
int wolfBoot_unseal_auth(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, uint8_t* secret, int* secret_sz, const byte* auth, int authSz);
```

By default this index will be based on an NV Index at `(0x01400300 + index)`.
The default NV base can be overridden with `WOLFBOOT_TPM_SEAL_NV_BASE`.

NOTE: The TPM's RSA verify requires ASN.1 encoding, so use SIGN=RSA2048ENC

### Testing seal/unseal with simulator

```sh
% cp config/examples/sim-tpm-seal.config .config
% make keytools
% make tpmtools
% echo aaa > aaa.bin
% ./tools/tpm/pcr_extend 0 aaa.bin
% ./tools/tpm/policy_create -pcr=0
# if ROT enabled
% ./tools/tpm/rot -write [-auth=TestAuth]
% make clean
$ make POLICY_FILE=policy.bin [WOLFBOOT_TPM_KEYSTORE_AUTH=TestAuth] [WOLFBOOT_TPM_SEAL_AUTH=SealAuth]

% ./wolfboot.elf get_version
Simulator assigned ./internal_flash.dd to base 0x103378000
Mfg IBM  (0), Vendor SW   TPM, Fw 8217.4131 (0x163636), FIPS 140-2 1, CC-EAL4 0
Unlocking disk...
Boot partition: 0x1033f8000
Image size 54400
Error 395 reading blob from NV index 1400300 (error TPM_RC_HANDLE)
Error 395 unsealing secret! (TPM_RC_HANDLE)
Sealed secret does not exist!
Creating new secret (32 bytes)
430dee45553c4a8b75fbc6bcd0890765c48cab760b24b1aa6b633dc0538e0159
Wrote 210 bytes to NV index 0x1400300
Read 210 bytes from NV index 0x1400300
Secret Check 32 bytes
430dee45553c4a8b75fbc6bcd0890765c48cab760b24b1aa6b633dc0538e0159
Secret 32 bytes
430dee45553c4a8b75fbc6bcd0890765c48cab760b24b1aa6b633dc0538e0159
Boot partition: 0x1033f8000
Image size 54400
TPM Root of Trust valid (id 0)
Simulator assigned ./internal_flash.dd to base 0x103543000
1

% ./wolfboot.elf get_version
Simulator assigned ./internal_flash.dd to base 0x10c01c000
Mfg IBM  (0), Vendor SW   TPM, Fw 8217.4131 (0x163636), FIPS 140-2 1, CC-EAL4 0
Unlocking disk...
Boot partition: 0x10c09c000
Image size 54400
Read 210 bytes from NV index 0x1400300
Secret 32 bytes
430dee45553c4a8b75fbc6bcd0890765c48cab760b24b1aa6b633dc0538e0159
Boot partition: 0x10c09c000
Image size 54400
TPM Root of Trust valid (id 0)
Simulator assigned ./internal_flash.dd to base 0x10c1e7000
1
```

### Testing seal/unseal on actual hardware

1) Get the actual PCR digest for policy.
2) Sign policy and include in firmware image header.


#### Getting PCR values

If no signed policy exists, then the seal function will generate and display the active PCR's, PCR digest and policy digest (to sign)

```sh
% make tpmtools
% ./tools/tpm/rot -write
% ./tools/tpm/pcr_reset 16
% ./wolfboot.elf get_version
Simulator assigned ./internal_flash.dd to base 0x101a64000
Mfg IBM  (0), Vendor SW   TPM, Fw 8217.4131 (0x163636), FIPS 140-2 1, CC-EAL4 0
Boot partition: 0x101ae4000
Image size 57192
Policy header not found!
Generating policy based on active PCR's!
Getting active PCR's (0-16)
PCR 16 (counter 20)
8f7ac1d5a5eac58a2305ca459f27c35705a9212c0fb2a9088b1df761f3d5f842
Found 1 active PCR's (mask 0x00010000)
PCR Digest (32 bytes):
f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c
PCR Mask (0x00010000) and PCR Policy Digest (36 bytes):
0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec
Use this policy with the sign tool (--policy arg) or POLICY_FILE config
Image policy signature missing!
Boot partition: 0x101ae4000
Image size 57192
TPM Root of Trust valid (id 0)
Simulator assigned ./internal_flash.dd to base 0x101c2f000
1
```

The `0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec` above can be directly used by the keytool. The

`echo "0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec" | xxd -r -p > policy.bin`

OR use the `tools/tpm/policy_create` tool to generate a digest to be signed. The used PCR(s) must be set using "-pcr=#". The PCR digest can be supplied using "-pcrdigest=" or if not supplied will be read from the TPM directly.

```sh
% ./tools/tpm/policy_create -pcr=16 -pcrdigest=f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c -out=policy.bin
# OR
% ./tools/tpm/policy_create -pcrmask=0x00010000 -pcrdigest=f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c -out=policy.bin
Policy Create Tool
PCR Index(s) (SHA256): 16  (mask 0x00010000)
PCR Digest (32 bytes):
	f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c
PCR Mask (0x00010000) and PCR Policy Digest (36 bytes):
	0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec
Wrote 36 bytes to policy.bin
```

#### Signing Policy

Building firmware with the policy digest to sign:

```sh
% make POLICY_FILE=policy.bin
```

OR manually sign the policy using the `tools/tpm/policy_sign` or `tools/keytools/sign` tools.
These tools do not need access to a TPM, they are signing a policy digest. The result is a 32-bit PCR mask + signature.

Sign with policy_sign tool:

```sh
% ./tools/tpm/policy_sign -pcr=0 -pcrdigest=eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc
Sign PCR Policy Tool
Signing Algorithm: ECC256
PCR Index(s) (SHA256): 0
Policy Signing Key: wolfboot_signing_private_key.der
PCR Digest (32 bytes):
	eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc
PCR Policy Digest (32 bytes):
	2d401eb05f45ba2b15c35f628b5896cc7de9745bb6e722363e2dbee804e0500f
PCR Policy Digest (w/PolicyRef) (32 bytes):
	749b3139ece21449a7828f11ee05303b0473ff1a26cf41d6f9ff28b24c717f02
PCR Mask (0x1) and Policy Signature (68 bytes):
	01000000
	5b5f875b3f7ce78b5935abe4fc5a4d8a6e87c4b4ac0836fbab909e232b6d7ca2
	3ecfc6be723b695b951ba2886d3c7b83ab2f8cc0e96d766bc84276eaf3f213ee
Wrote PCR Mask + Signature (68 bytes) to policy.bin.sig
```

Sign using the signing key tool:

```sh
% ./tools/keytools/sign --ecc256 --policy policy.bin test-app/image.elf wolfboot_signing_private_key.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 1100000
Update type:          Firmware
Input image:          test-app/image.elf
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           wolfboot_signing_private_key.der
Output  image:        test-app/image_v1_signed.bin
Target partition id : 1
image header size calculated at runtime (256 bytes)
Calculating SHA256 digest...
Signing the digest...
Opening policy file policy.bin
Signing the policy digest...
Saving policy signature to policy.bin.sig
Output image(s) successfully created.
```
