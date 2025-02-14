# wolfBoot Library Integration Guide

## Overview
wolfBoot can be integrated as a secure-boot library into:
- Third-party bootloaders
- Custom staging solutions
- Existing boot chains
- Verification modules

## Core API

### Image Verification Interface

#### Key Components
```c
struct wolfBoot_image {
    // Image metadata and state
    // See wolfboot/include/image.h
};
```

#### Opening Images
```c
int wolfBoot_open_image_address(
    struct wolfBoot_image* img,  // Uninitialized image struct
    uint8_t* image              // Pointer to manifest header
);
```

#### Return Values
| Value | Description |
|-------|-------------|
| 0 | Success |
| -1 | Invalid magic number or size |

#### Size Constraints
- Maximum size: `WOLFBOOT_PARTITION_SIZE`
- Includes manifest header
- Enforced at runtime


### Verification Functions

#### 1. Integrity Check
```c
int wolfBoot_verify_integrity(
    struct wolfBoot_image *img  // Initialized image struct
);
```

**Operation**:
- Calculates SHA hash
- Compares with manifest
- Verifies image content

**Returns**:
| Value | Meaning |
|-------|---------|
| 0 | Integrity verified |
| -1 | Verification failed |

#### 2. Authenticity Verification
```c
int wolfBoot_verify_authenticity(
    struct wolfBoot_image *img  // Initialized image struct
);
```

**Operation**:
- Verifies signature
- Checks against public keys
- Validates trust chain

**Returns**:
| Value | Meaning |
|-------|---------|
| 0 | Authentication successful |
| -1 | Operation error |
| -2 | Invalid signature |

## Example Implementation

### Test Application
Location: `hal/library.c`

#### Features
- Command-line interface
- File-based verification
- Integrity checking
- Signature validation

### Build Instructions

#### 1. Configuration Setup
```bash
# Copy library configuration
cp config/examples/library.config .config
```

#### 2. Target Configuration
Create `include/target.h`:
```c
#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

/* Disable partition support */
#define WOLFBOOT_NO_PARTITIONS

/* Configure memory layout */
#define WOLFBOOT_SECTOR_SIZE     0x20000
#define WOLFBOOT_PARTITION_SIZE  0x20000

#endif /* !H_TARGETS_TARGET_ */
```

**Important**: Adjust `WOLFBOOT_PARTITION_SIZE` based on your requirements:
- Maximum image size = `PARTITION_SIZE - HEADER_SIZE`
- Oversized images rejected automatically


### Testing Process

#### 1. Key Generation
```bash
# Build key tools
make keytools

# Generate ED25519 keypair
./tools/keytools/keygen \
    --ed25519 \
    -g wolfboot_signing_private_key.der
```

#### 2. Image Signing
```bash
# Create test image
touch empty

# Sign image
./tools/keytools/sign \
    --ed25519 \
    --sha256 \
    empty \
    wolfboot_signing_private_key.der \
    1
```

#### 3. Build Test Application
```bash
# Compile with library support
make test-lib
```

#### 4. Verification Test
```bash
# Run verification
./test-lib empty_v1_signed.bin

# Expected output:
# Firmware Valid
# booting 0x5609e3526590(actually exiting)
```

## Related Documentation
- [Firmware Format](firmware_image.md)
- [Signing Process](Signing.md)
- [API Reference](API.md)

