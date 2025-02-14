# External Partition Encryption in wolfBoot

## Overview
wolfBoot provides encryption capabilities for external partitions to enhance firmware update security. This feature:
- Encrypts entire UPDATE partition content
- Uses pre-shared symmetric keys
- Protects SWAP partition during updates
- Integrates with existing signing process

## Key Features
- Transparent encryption layer
- Secure key storage options
- Multiple encryption algorithms
- Automated encryption/decryption
- Support for incremental updates

## Architecture

### Encryption Layer
```
Application Layer
      ↓
Encryption Layer (ChaCha20/AES)
      ↓
External Flash Interface
      ↓
Physical Storage
```

### Operation Flow
1. **Write Operations**:
   - Encrypt data before writing
   - Store encrypted content in flash
   - Hide actual firmware content

2. **Read Operations**:
   - Read encrypted data
   - Decrypt automatically
   - Return plaintext to bootloader

3. **Update Process**:
   - Sign firmware normally
   - Encrypt signed package
   - Store encrypted in external memory
   - Decrypt during verification


## Key Management

### Storage Options

#### Default Storage
- Internal flash temporary storage
- Protected by read-out protection
- Single-use key mechanism
- Automatic key cleanup

#### Advanced Storage
- Hardware Security Module (HSM)
- Trusted Platform Module (TPM)
- Custom secure storage implementation

### Runtime Key Management

#### Key Lifecycle
1. Key reception from backend
2. Secure storage setup
3. Single update usage
4. Automatic cleanup

## API Reference

### Encryption Control API
```c
// Set encryption key and nonce
int wolfBoot_set_encrypt_key(
    const uint8_t *key,    // Encryption key
    const uint8_t *nonce   // Initialization vector
);

// Clear stored encryption key
int wolfBoot_erase_encrypt_key(void);
```

### Usage Notes
- Keys are single-use only
- Application manages key reception
- Keys cleared after update
- No encryption in direct HAL access
- Supports pre-encrypted updates

## Encryption Algorithms

### Supported Algorithms

| Algorithm | Key Size | IV/Nonce Size | Performance | Configuration |
|-----------|----------|---------------|-------------|---------------|
| ChaCha20-256 (default) | 32 bytes | 12 bytes | Fast | `ENCRYPT_WITH_CHACHA=1` |
| AES-128 CTR | 16 bytes | 16 bytes | Good | `ENCRYPT_WITH_AES128=1` |
| AES-256 CTR | 32 bytes | 16 bytes | Good | `ENCRYPT_WITH_AES256=1` |

### Configuration Options

```bash
# Enable encryption support
make ENCRYPT=1

# Select specific algorithm
make ENCRYPT=1 ENCRYPT_WITH_CHACHA=1    # Default
make ENCRYPT=1 ENCRYPT_WITH_AES128=1    # AES-128
make ENCRYPT=1 ENCRYPT_WITH_AES256=1    # AES-256
```

### Algorithm Details

#### ChaCha20-256
- Default algorithm
- 32-byte key requirement
- 12-byte random nonce
- Optimized for software implementation

#### AES Counter Mode
- AES-128 and AES-256 support
- 16/32-byte keys respectively
- 16-byte IV requirement
- Hardware acceleration where available


## Implementation Examples

### ChaCha20-256 Example

#### 1. Prepare Encryption Key
```bash
# Create key file (32B key + 12B nonce)
# Example key: "0123456789abcdef0123456789abcdef"
# Example nonce: "0123456789ab"
echo -n "0123456789abcdef0123456789abcdef0123456789ab" > enc_key.der
```

#### 2. Sign and Encrypt
```bash
# Generate encrypted firmware
./tools/keytools/sign \
    --encrypt enc_key.der \
    test-app/image.bin \
    wolfboot_signing_private_key.der \
    24
```

Output: `test-app/image_v24_signed_and_encrypted.bin`

### AES-256 Example

#### 1. Prepare Encryption Key
```bash
# Create key file (32B key + 16B IV)
# Example key: "0123456789abcdef0123456789abcdef"
# Example IV: "0123456789abcdef"
echo -n "0123456789abcdef0123456789abcdef0123456789abcdef" > enc_key.der
```

#### 2. Sign and Encrypt
```bash
# Generate encrypted firmware
./tools/keytools/sign \
    --aes256 \
    --encrypt enc_key.der \
    test-app/image.bin \
    wolfboot_signing_private_key.der \
    24
```

Output: `test-app/image_v24_signed_and_encrypted.bin`


## Special Update Cases

### Delta Updates
- Compatible with encryption
- Same workflow as full updates
- Delta image encrypted after generation
- No special configuration needed

### Self-Updates

#### RAM Configuration
Encryption algorithm must run from RAM for self-updates:

1. **Linker Script Modifications**
```ld
/* Flash Section */
.text : {
    *(EXCLUDE_FILE(*chacha.o).text*)
    *(EXCLUDE_FILE(*chacha.o).rodata*)
}

/* RAM Section */
.data : {
    KEEP(*(.ramcode))
    /* ChaCha20 RAM symbols */
    KEEP(*(.text.wc_Chacha*))
    KEEP(*(.text.rotlFixed*))
    KEEP(*(.rodata.sigma))
    KEEP(*(.rodata.tau))
}
```

2. **Build Configuration**
```bash
# Use RAM-optimized linker script
make TARGET=stm32l0 ENCRYPT=1
```

#### Platform Support
- Tested on STM32L0
- Uses `hal/$(TARGET)_chacha_ram.ld`
- ChaCha20 recommended

## Application Integration

### Update Process
1. **Store Encrypted Update**
```c
// Store encrypted firmware directly
ext_flash_write(address, encrypted_data, size);
```

2. **Trigger Update**
```c
// Set encryption key before update
wolfBoot_set_encrypt_key(key, nonce);

// Trigger update
wolfBoot_update_trigger();
```

### Example Implementation
See [STM32WB Example](../test-app/app_stm32wb.c) for complete implementation.

## Related Documentation
- [Firmware Updates](firmware_update.md)
- [HAL Interface](HAL.md)
- [API Reference](API.md)




