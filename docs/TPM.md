# TPM Integration Guide

This guide describes wolfBoot's integration with Trusted Platform Module (TPM) for enhanced security features.

## Features Overview

- Root of Trust (RoT) using TPM
- Cryptographic operation offloading
- Measured boot support
- Secure sealing/unsealing of secrets
- PCR-based policy enforcement

## Configuration Options

### Core TPM Support
| Option | Macro | Description |
|--------|-------|-------------|
| `WOLFTPM=1` | `WOLFBOOT_TPM` | Enable TPM integration |

### Cryptographic Features
| Option | Macro | Description |
|--------|-------|-------------|
| `WOLFBOOT_TPM_VERIFY=1` | `WOLFBOOT_TPM_VERIFY` | Offload RSA2048/ECC256/384 to TPM |
| `WOLFBOOT_TPM_KEYSTORE=1` | `WOLFBOOT_TPM_KEYSTORE` | Enable TPM-based root of trust |

### Storage Configuration
| Option | Macro | Description |
|--------|-------|-------------|
| `WOLFBOOT_TPM_KEYSTORE_NV_BASE=0x` | Same | NV index (0x1400000-0x17FFFFF) |
| `WOLFBOOT_TPM_KEYSTORE_AUTH=secret` | Same | NV access password |
| `WOLFBOOT_TPM_SEAL_NV_BASE=0x01400300` | Same | Sealed blob storage location |
| `WOLFBOOT_TPM_SEAL_AUTH=secret` | Same | Sealing/unsealing password |

### Measured Boot
| Option | Macro | Description |
|--------|-------|-------------|
| `MEASURED_BOOT=1` | `WOLFBOOT_MEASURED_BOOT` | Enable PCR measurements |
| `MEASURED_PCR_A=16` | `WOLFBOOT_MEASURED_PCR_A=16` | PCR index selection |
| `WOLFBOOT_TPM_SEAL=1` | `WOLFBOOT_TPM_SEAL` | Enable PCR-based sealing |

For detailed measured boot information, see [measured_boot.md](/docs/measured_boot.md)

## Feature Details

### Root of Trust (RoT)
Implements secure boot chain using TPM-based key verification.

**Implementation:**
- Uses locked platform NV handle
- Stores public key hash
- Requires authentication for tampering prevention
- Authentication encrypted during transit

**Example:** See [wolfTPM RoT Example](https://github.com/wolfSSL/wolfTPM/tree/master/examples/boot)

### Cryptographic Offloading
Delegates cryptographic operations to TPM hardware.

**Supported Operations:**
- RSA2048 verification
- ECC256/384 verification

**Notes:**
- Reduces code size
- Improves performance
- Requires `WOLFBOOT_TPM_VERIFY`
- Use `SIGN=RSA2048ENC` for ASN.1 encoding

### Measured Boot
Tracks boot process integrity using PCR measurements.

**Features:**
- Hashes wolfBoot image
- Extends PCR with hash
- Verifiable boot attestation
- API: `wolfBoot_tpm2_extend`

### Secret Management

#### Sealing/Unsealing API
```c
// Seal data with policy and authentication
int wolfBoot_seal_auth(
    const uint8_t* pubkey_hint,    // Public key reference
    const uint8_t* policy,         // Policy data
    uint16_t policySz,             // Policy size
    int index,                     // NV index
    const uint8_t* secret,         // Data to seal
    int secret_sz,                 // Data size
    const byte* auth,              // Authentication
    int authSz                     // Auth size
);

// Unseal previously sealed data
int wolfBoot_unseal_auth(
    const uint8_t* pubkey_hint,    // Public key reference
    const uint8_t* policy,         // Policy data
    uint16_t policySz,             // Policy size
    int index,                     // NV index
    uint8_t* secret,              // Buffer for unsealed data
    int* secret_sz,               // Buffer size
    const byte* auth,             // Authentication
    int authSz                    // Auth size
);
```

**Storage Location:**
- Default: `0x01400300 + index`
- Configurable via `WOLFBOOT_TPM_SEAL_NV_BASE`

For implementation examples, see [wolfTPM Sealing Example](https://github.com/wolfSSL/wolfTPM/tree/master/examples/boot#secure-boot-encryption-key-storage)

NOTE: The TPM's RSA verify requires ASN.1 encoding, so use SIGN=RSA2048ENC

## Testing Guide

### Simulator Testing

#### Setup and Configuration
```bash
# 1. Copy TPM simulator config
cp config/examples/sim-tpm-seal.config .config

# 2. Build required tools
make keytools
make tpmtools

# 3. Create test files
echo aaa > aaa.bin
echo bbb > bbb.bin

# 4. Configure PCR values
./tools/tpm/pcr_extend 0 aaa.bin
./tools/tpm/pcr_extend 1 bbb.bin

# 5. Create policy (PCR 1 then 0)
./tools/tpm/policy_create -pcr=1 -pcr=0 -out=policy.bin

# 6. Setup Root of Trust (optional)
./tools/tpm/rot -write [-auth=TestAuth]

# 7. Build with policy
make clean
make POLICY_FILE=policy.bin \
    [WOLFBOOT_TPM_KEYSTORE_AUTH=TestAuth] \
    [WOLFBOOT_TPM_SEAL_AUTH=SealAuth]
```

#### Testing Sequence
```bash
# First Run - Initial Secret Creation
./wolfboot.elf get_version
```

**Expected Output:**
```
Mfg IBM  (0), Vendor SW   TPM, Fw 8228.293 (0x120000)...
Unlocking disk...
Error 395 reading blob... (First run expected error)
Creating new secret (32 bytes)
7801a7fb716371c975a9a1bca6159a223bc7dba6adb2acf82781421062e498a5
Wrote 242 bytes to NV index 0x1400300
TPM Root of Trust valid (id 0)
```

**Subsequent Runs:**
```bash
./wolfboot.elf get_version
```

**Expected Output:**
```
Unlocking disk...
Read 242 bytes from NV index 0x1400300
Secret 32 bytes
7801a7fb716371c975a9a1bca6159a223bc7dba6adb2acf82781421062e498a5
TPM Root of Trust valid (id 0)
```
```

### Hardware Testing

#### Policy Generation Process

1. Build Tools and Setup
```bash
make tpmtools
./tools/tpm/rot -write
./tools/tpm/pcr_reset 16
```

2. Generate Initial Policy
```bash
./wolfboot.elf get_version
```

**Expected Output:**
```
Policy header not found!
Generating policy based on active PCR's!
Getting active PCR's (0-16)
PCR 16 (counter 20)
8f7ac1d5a5eac58a2305ca459f27c35705a9212c0fb2a9088b1df761f3d5f842
Found 1 active PCR's (mask 0x00010000)
PCR Digest:
f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c
PCR Policy:
0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec
```

#### Policy Creation Methods

1. **Direct Method**
```bash
# Convert hex policy to binary
echo "0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec" | \
    xxd -r -p > policy.bin
```

2. **Using Policy Creation Tool**
```bash
# Method A: Specify PCR Index
./tools/tpm/policy_create \
    -pcr=16 \
    -pcrdigest=f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c \
    -out=policy.bin

# Method B: Specify PCR Mask
./tools/tpm/policy_create \
    -pcrmask=0x00010000 \
    -pcrdigest=f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c \
    -out=policy.bin
```

**Tool Output:**
```
Policy Create Tool
PCR Index(s) (SHA256): 16  (mask 0x00010000)
PCR Digest (32 bytes):
    f84085631f85333ad0338b06c82f16888b7923abaccffb881d5416e389be256c
PCR Policy (36 bytes):
    0000010034ba061436aba2e9a167a1ee46af4a9578a8c6b9f71fdece21607a0cb40468ec
Wrote 36 bytes to policy.bin
```

#### Policy Signing

Two methods are available for signing the policy:

1. **Simple Build Method**
```bash
make POLICY_FILE=policy.bin
```

2. **Manual Signing Tools**
Both tools below sign policy digest without TPM access:
- `tools/tpm/policy_sign`
- `tools/keytools/sign`

##### Using Policy Sign Tool
```bash
./tools/tpm/policy_sign \
    -pcr=0 \
    -pcrdigest=eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc
```

**Output Details:**
```
Sign PCR Policy Tool
Signing Algorithm: ECC256
PCR Index(s): 0
Key: wolfboot_signing_private_key.der

Digests:
1. PCR Digest (32 bytes):
   eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc

2. Policy Digest (32 bytes):
   2d401eb05f45ba2b15c35f628b5896cc7de9745bb6e722363e2dbee804e0500f

3. Policy Digest w/Ref (32 bytes):
   749b3139ece21449a7828f11ee05303b0473ff1a26cf41d6f9ff28b24c717f02

Output:
PCR Mask (0x1) + Signature (68 bytes total):
01000000
5b5f875b3f7ce78b5935abe4fc5a4d8a6e87c4b4ac0836fbab909e232b6d7ca2
3ecfc6be723b695b951ba2886d3c7b83ab2f8cc0e96d766bc84276eaf3f213ee

File: policy.bin.sig (68 bytes written)
```

##### Using Signing Key Tool
```bash
./tools/keytools/sign \
    --ecc256 \
    --policy policy.bin \
    test-app/image.elf \
    wolfboot_signing_private_key.der 1
```

**Process Details:**
```
wolfBoot KeyTools v1100000
Configuration:
- Update Type: Firmware
- Input: test-app/image.elf
- Cipher: ECC256
- Hash: SHA256
- Key: wolfboot_signing_private_key.der
- Output: test-app/image_v1_signed.bin
- Partition: 1
- Header Size: 256 bytes (runtime calculated)

Steps:
1. Calculate SHA256 digest
2. Sign digest
3. Process policy file
4. Sign policy digest
5. Save policy signature
6. Generate final image
```
```
