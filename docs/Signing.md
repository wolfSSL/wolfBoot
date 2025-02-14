# wolfBoot Key Management Tools

This document describes the tools used for key management and firmware signing in wolfBoot.

## Overview

Two primary tools are provided:
- `keygen`: Manages private keys and keystore
- `sign`: Creates signed firmware images

These tools can be used on:
- Development machines
- Build servers
- CI/CD pipelines

## Building the Tools

### Linux/Unix
```bash
# From tools/keytools directory
make

# From wolfBoot root
make keytools
```

### Windows
Use Visual Studio project:
- Project file: `wolfBootSignTool.vcxproj`
- Outputs: `sign.exe` and `keygen.exe`

**Note:** If `target.h` is missing:
- This file is generated from your `.config`
- Required for `WOLFBOOT_SECTOR_SIZE` definition
- Used in delta update calculations


## Keygen Tool

### Basic Usage
```bash
keygen [OPTIONS] [-g new-keypair.der] [-i existing-pubkey.der] [...]
```

### Purpose
Creates and manages keystores containing public keys for firmware verification.

### Key Options
| Option | Description |
|--------|-------------|
| `-g privkey.der` | Generate new keypair, save private key |
| `-i existing.der` | Import existing public key |
| `--der` | Save private key in DER format |
| `--exportpubkey` | Export public key to DER (with `-g`) |
| `--nolocalkeys` | Create keystore entry without key material* |

*Used with wolfHSM for external key reference

### Algorithm Selection
Must specify one:
- `--ed25519`
- `--rsa3072`
- (See "Public Key Options" section below)

### Multiple Keys
- Options can be combined
- Multiple `-g` or `-i` allowed
- Builds keystore with all specified keys

### Output Files
1. `src/keystore.c`
   - C source for linking with wolfBoot
   - Contains embedded public keys

2. `keystore.img`
   - Binary keystore file
   - For alternative storage methods

3. Private Key Files
   - One per `-g` option
   - Used for firmware signing

For detailed keystore information, see [keystore.md](keystore.md)


## Sign Tool

### Basic Usage
```bash
sign [OPTIONS] IMAGE.BIN KEY.DER VERSION
```

### Parameters
| Parameter | Description |
|-----------|-------------|
| `IMAGE.BIN` | Binary firmware/software to sign |
| `KEY.DER` | Private key in DER format |
| `VERSION` | Version number for the firmware |
| `OPTIONS` | Optional configuration flags |

### Header Size Configuration

The manifest header size can be customized:

```bash
# Default: Automatically determined based on configuration
# Override: Set IMAGE_HEADER_SIZE environment variable

# Method 1: Export variable
export IMAGE_HEADER_SIZE=2048
sign [OPTIONS] IMAGE.BIN KEY.DER VERSION

# Method 2: Inline setting
IMAGE_HEADER_SIZE=2048 sign [OPTIONS] IMAGE.BIN KEY.DER VERSION
```

**Note:** Custom header size may be needed for:
- Large signatures (e.g., RSA-4096)
- Multiple signatures (hybrid mode)
- Additional custom TLV fields


### Signature Algorithms

The tool automatically detects key type from KEY.DER if no algorithm is specified.

#### Classical Algorithms
| Option | Algorithm | Description |
|--------|-----------|-------------|
| `--ed25519` | ED25519 | Edwards curve signature |
| `--ed448` | ED448 | Edwards curve signature |
| `--ecc256` | ECDSA P-256 | NIST P-256 curve |
| `--ecc384` | ECDSA P-384 | NIST P-384 curve |
| `--ecc521` | ECDSA P-521 | NIST P-521 curve |
| `--rsa2048` | RSA-2048 | 2048-bit RSA |
| `--rsa3072` | RSA-3072 | 3072-bit RSA |
| `--rsa4096` | RSA-4096 | 4096-bit RSA |

#### Post-Quantum Algorithms
| Option | Algorithm | Description |
|--------|-----------|-------------|
| `--lms` | LMS/HSS | Hash-based signature |
| `--xmss` | XMSS/XMSS^MT | Hash-based signature |

#### Special Options
| Option | Description |
|--------|-------------|
| `--no-sign` | Disable signature verification |

**Note:** With `--no-sign`:
- No KEY.DER required
- No signature verification in bootloader
- Not recommended for production use

### Hash Algorithm Options

Default: `--sha256`

| Option | Algorithm | Description |
|--------|-----------|-------------|
| `--sha256` | SHA-256 | Standard SHA-2 256-bit |
| `--sha384` | SHA-384 | Standard SHA-2 384-bit |
| `--sha3` | SHA3-384 | SHA-3 384-bit variant |

### Target Partition Configuration

Default: `--id=1`

#### Partition IDs
| ID | Purpose | Description |
|----|---------|-------------|
| 0 | Bootloader | Reserved for wolfBoot self-update |
| 1 | Primary Firmware | Default application partition |
| N | Custom | User-defined partitions |

#### Options
| Option | Description |
|--------|-------------|
| `--id N` | Target partition N |
| `--wolfboot-update` | Bootloader update (same as `--id 0`) |

**Use Cases:**
- Single partition systems: Use default `--id=1`
- Multi-partition systems: Specify partition with `--id N`
- Bootloader update: Use `--wolfboot-update` or `--id 0`

### Firmware Encryption

By default, firmware images are signed but not encrypted. For secure external storage, end-to-end encryption is available.

#### Basic Encryption
```bash
--encrypt SHAREDKEY.BIN   # Enable encryption with shared key
```

#### Encryption Algorithms

Default: `--chacha` (when using `--encrypt`)

| Algorithm | Option | Key File Size | Key Size | IV Size |
|-----------|--------|---------------|----------|----------|
| ChaCha20 | `--chacha` | 44 bytes | 32 bytes | 12 bytes |
| AES-128-CTR | `--aes128` | 32 bytes | 16 bytes | 16 bytes |
| AES-256-CTR | `--aes256` | 48 bytes | 32 bytes | 16 bytes |

**Use Cases:**
- External flash storage
- Over-the-air updates
- Sensitive firmware protection

**Note:** Key file (SHAREDKEY.BIN) must be pre-shared and kept secure

### Delta Updates

Create incremental updates between firmware versions to minimize update size.

#### Basic Usage
```bash
--delta BASE_SIGNED_IMG.BIN   # Create incremental update
```

**Process:**
1. Takes base image (`BASE_SIGNED_IMG.BIN`)
2. Compares with new image (`IMAGE.BIN`)
3. Creates diff file (`*_signed_diff.bin`)
4. Uses Bentley-McIlroy compression

#### Options
| Option | Description | Impact |
|--------|-------------|---------|
| Default | Includes base image SHA | +32-48 bytes header |
| `--no-base-sha` | Skips base image SHA | Smaller header |

**Considerations:**
- Base image SHA ensures correct update base
- `--no-base-sha` for legacy header size compatibility
- Reduces update bandwidth and storage requirements


### TPM Policy Signing

Enable TPM-based sealing/unsealing with signed policies.

#### Basic Usage
```bash
--policy policy.bin   # Add signed TPM policy
```

#### Policy File Format
1. Default Mode:
   - 4-byte PCR mask
   - SHA2-256 PCR digest (to be signed)

2. Manual Sign Mode:
   - 4-byte PCR mask
   - Pre-computed signature

#### Outputs
- Policy signature in `HDR_POLICY_SIGNATURE` header
- Signed policy (with PCR mask) in `[inputname].sig`

**Note:** May require increased `IMAGE_HEADER_SIZE` for dual signatures

### Custom Manifest Fields

Add custom Type-Length-Value (TLV) entries to the manifest header.

#### TLV Tag Range
- Valid tags: `0x0030` to `0xFEFE`
- 16-bit values

#### Options

1. **Fixed-Length Values**
```bash
--custom-tlv <tag> <len> <val>
```
- Numeric values (decimal/hex)
- Fixed length in bytes

2. **Buffer Values**
```bash
--custom-tlv-buffer <tag> <hex-string>
```
Example:
```bash
--custom-tlv-buffer 0x0030 AABBCCDDEE
# Creates: tag=0x0030, len=5, val=0xAABBCCDDEE
```

3. **String Values**
```bash
--custom-tlv-string <tag> "<ascii-string>"
```
Example:
```bash
--custom-tlv-string 0x0030 "Version-1"
# Creates: tag=0x0030, len=9, val="Version-1"
```

### External Signing Process

For scenarios where private keys are managed by HSMs or external services, the signing process can be split into three phases.

#### Phase 1: Generate Digest
```bash
--sha-only   # Generate intermediate digest file
```
**Inputs:**
- `IMAGE.BIN`: Firmware to sign
- `KEY.DER`: Public key only

**Output:**
- `*_digest.bin`: Intermediate file for signing

#### Phase 2: External Signing
Sign the digest using external tools:
- Hardware Security Module (HSM)
- Cloud Key Management Service
- Third-party signing service

**Output:**
- `IMAGE_SIGNATURE.SIG`: Raw signature file

#### Phase 3: Create Final Image
```bash
--manual-sign IMAGE_SIGNATURE.SIG   # Combine components
```
**Inputs:**
- Original `IMAGE.BIN`
- Public key (`KEY.DER`)
- Version number
- `IMAGE_SIGNATURE.SIG`

**Output:**
- Complete signed firmware with manifest

**Note:** See Examples section below for detailed workflow

## Usage Examples

### Basic Firmware Signing

#### Direct Signing with Local Key
```bash
# 1. Prepare private key
cp signing_key.der wolfboot_signing_private_key.der

# 2. Sign firmware
./tools/keytools/sign --rsa2048 --sha256 \
    test-app/image.bin \
    wolfboot_signing_private_key.der 1
```

### HSM Integration Example

#### Complete HSM Workflow
```bash
# 1. Export public key
openssl rsa -inform DER -outform DER \
    -in my_key.der \
    -out rsa2048_pub.der -pubout

# 2. Add to wolfBoot keystore
./tools/keytools/keygen --rsa2048 -i rsa2048_pub.der

# 3. Generate digest for HSM
./tools/keytools/sign --rsa2048 --sha-only --sha256 \
    test-app/image.bin rsa2048_pub.der 1

# 4. Sign with HSM (example using OpenSSL)
openssl pkeyutl -sign -keyform der \
    -inkey my_key.der \
    -in test-app/image_v1_digest.bin \
    -out test-app/image_v1.sig

# 5. Create signed firmware
./tools/keytools/sign --rsa2048 --sha256 --manual-sign \
    test-app/image.bin rsa2048_pub.der 1 \
    test-app/image_v1.sig

# 6. Create factory image
tools/bin-assemble/bin-assemble factory.bin \
    0x0 wolfboot.bin \
    0xc0000 test-app/image_v1_signed.bin
```

### Cloud Service Integration
For Azure Key Vault integration, see [azure_keyvault.md](/docs/azure_keyvault.md)
