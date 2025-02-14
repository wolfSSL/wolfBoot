# wolfBoot KeyStore Guide

## Overview
The KeyStore system provides secure storage and management of public keys used for firmware authentication in wolfBoot.

### Key Features
- Multiple key support
- Flexible storage options
- Runtime key access
- Permission management
- HSM integration

## Basic Operation

### Default Setup
```bash
# First build generates default key
make

# Results in:
wolfboot_signing_private_key.der  # Signing key
src/keystore.c                    # Embedded keystore
keystore.bin                      # Binary keystore
```

### Storage Options
| Format | File | Purpose |
|--------|------|---------|
| Source | src/keystore.c | Embedded in bootloader |
| Binary | keystore.bin | External storage support |
| DER | *.der | Key distribution |

### Integration Methods
1. **Built-in Storage**
   - Linked with wolfboot.elf
   - Direct memory access
   - Default approach

2. **External Storage**
   - Custom memory support
   - Driver required
   - Flexible deployment

## Built-in KeyStore Implementation

### Data Structure
```c
struct keystore_slot {
    uint32_t slot_id;      // Sequential ID (0-based)
    uint32_t key_type;     // Algorithm identifier
    uint32_t part_id_mask; // Permission bitmap
    uint32_t pubkey_size;  // Key buffer size
    uint8_t  pubkey[KEYSTORE_PUBKEY_SIZE]; // Key data
};
```

### Field Descriptions

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| slot_id | uint32_t | Sequential identifier | 0, 1, 2... |
| key_type | uint32_t | Crypto algorithm | AUTH_KEY_ECC256 |
| part_id_mask | uint32_t | Access permissions | KEY_VERIFY_ALL |
| pubkey_size | uint32_t | Key length | 32, 64, etc. |
| pubkey | uint8_t[] | Raw key data | Binary buffer |

### Memory Layout
```
KeyStore Memory
+------------------------+
| Slot 0                |
|   - Metadata          |
|   - Key Data          |
+------------------------+
| Slot 1                |
|   - Metadata          |
|   - Key Data          |
+------------------------+
| ...                   |
```

When booting, wolfBoot will automatically select the public key associated to the signed firmware image, check that it matches the permission mask for the partition id where the verification is running and then attempts to authenticate the signature of the image using the selected public key slot.

## Key Management

### Key Generation

#### Command Options
```bash
# Generate new keypair
keygen -g priv.der

# Import existing key
keygen -i pub.der
```

#### Multiple Key Example
```bash
# Create two ED25519 keys
./tools/keytools/keygen \
    --ed25519 \
    -g first.der \
    -g second.der
```

#### Output Files
| File | Description |
|------|-------------|
| first.der | Private key 1 |
| second.der | Private key 2 |
| src/keystore.c | Generated keystore |

### KeyStore Source Example
```c
#define NUM_PUBKEYS 2
const struct keystore_slot PubKeys[NUM_PUBKEYS] = {
    /* Slot 0: first.der */
    {
        .slot_id = 0,
        .key_type = AUTH_KEY_ED25519,
        .part_id_mask = KEY_VERIFY_ALL,
        .pubkey_size = KEYSTORE_PUBKEY_SIZE_ED25519,
        .pubkey = {
            0x21, 0x7B, /* ... */ 0x24, 0x84
        },
    },
    /* Slot 1: second.der */
    {
        .slot_id = 1,
        .key_type = AUTH_KEY_ED25519,
        .part_id_mask = KEY_VERIFY_ALL,
        .pubkey_size = KEYSTORE_PUBKEY_SIZE_ED25519,
        .pubkey = {
            0x41, 0xC8, /* ... */ 0x2A, 0xD5
        },
    }
};

## Access Control

### Permission System

#### Partition Mask
```
Bit Layout (32-bit mask)
31                             3 2 1 0
+--------------------------------+-+-+-+
|             Reserved           |3|2|1|0|
+--------------------------------+-+-+-+
                                 | | | |
                                 | | | +-- wolfBoot self-update
                                 | | +---- Main firmware
                                 | +------ Custom partition 2
                                 +-------- Custom partition 3
```

#### Default Access
- Mask: `KEY_VERIFY_ALL`
- Allows: All partition access
- Usage: General purpose keys

#### Restricted Access
```bash
# Create keys with specific permissions
keygen --ecc256 \
    -g generic.key \        # All access
    --id 1,2,3 \
    -g restricted.key       # Limited access
```

#### Permission Examples
| Key | Mask | Binary | Access |
|-----|------|--------|---------|
| generic.key | KEY_VERIFY_ALL | 11111111 | All partitions |
| restricted.key | 0x000E | 00001110 | Partitions 1,2,3 |


## Advanced Key Management

### Key Import
```bash
# Import existing public key
keygen -i existing_pub.der
```

### Universal KeyStore

#### Configuration
```bash
# Enable mixed key types
make WOLFBOOT_UNIVERSAL_KEYSTORE=1
```

#### Mixed Key Example
```bash
# Create multi-algorithm keystore
keygen \
    --ecc256 -g a.key \     # ECC-256 key
    --ecc384 -g b.key \     # ECC-384 key
    --rsa2048 -i rsa-pub.der  # Import RSA-2048
```

#### Supported Algorithms
| Type | Option | Key Size |
|------|---------|----------|
| ECC-256 | --ecc256 | 256-bit |
| ECC-384 | --ecc384 | 384-bit |
| RSA-2048 | --rsa2048 | 2048-bit |
| ED25519 | --ed25519 | 256-bit |

**Note**: Additional algorithms require explicit inclusion via `SIGN=` option

### Key Export

#### Export Command
```bash
# Generate and export keypair
keygen --ecc256 --exportpubkey -g mykey.der
```

#### Output Files
| File | Description | Format |
|------|-------------|---------|
| mykey.der | Private key | DER |
| mykey_pub.der | Public key | DER |

#### Usage Notes
- Requires `-g` option
- Automatic name generation
- DER format output
- Suitable for HSM import

## External KeyStore Integration

### Overview
Support for external key storage:
- Hardware security modules
- External NVM
- Key vaults
- Custom storage solutions

### API Reference

#### Core Functions
```c
// Get total key count
int keystore_num_pubkeys(void);

// Get key size for slot
int keystore_get_size(int id);

// Access key buffer
uint8_t *keystore_get_buffer(int id);

// Get permission mask
uint32_t keystore_get_mask(int id);
```

#### Function Details

| Function | Return Type | Description | Error Handling |
|----------|-------------|-------------|----------------|
| keystore_num_pubkeys | int | Total slots | Must be ≥ 1 |
| keystore_get_size | int | Key size | Negative on error |
| keystore_get_buffer | uint8_t* | Key data | NULL on error |
| keystore_get_mask | uint32_t | Permissions | 0 on error |

#### Implementation Notes
- Sequential slot numbering
- Zero-based indexing
- Error checking required
- Thread-safe access

## HSM Integration

### Overview
Support for Hardware Security Modules (HSMs):
- Secure key storage
- Cryptographic operations
- Runtime verification
- Field updates

### Architecture
```
Application
    ↓
wolfBoot ←→ KeyStore
    ↓         ↓
   HSM  ←→  Metadata
```

### Implementation

#### Key Generation
```bash
# Generate HSM-compatible keys
keygen --ecc256 \
    --nolocalkeys \
    --exportpubkey \
    -g hsm_key.der
```

#### Features
| Feature | Description |
|---------|-------------|
| Zero-copy | No key material in keystore |
| Metadata | Size and type information |
| Field updates | No rebuild required |
| Security | Keys never exposed |

#### Requirements
- Compatible HSM (e.g., wolfHSM)
- Manual key loading
- Matching algorithms
- Proper key sizes

### Security Benefits
- Keys never leave HSM
- Hardware-backed crypto
- Tamper resistance
- Secure key updates

For detailed HSM setup, see [wolfHSM Documentation](wolfHSM.md)
