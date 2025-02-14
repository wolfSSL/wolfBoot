# Firmware Image Format

## Overview
This document describes the wolfBoot firmware image format, including:
- Image header structure
- Entry point requirements
- Partition management
- Signing and verification

## Memory Layout

### Entry Point Requirements
- Fixed memory location required
- Specified in linker script
- Must match FLASH origin
- Located in first partition

### Partition Structure
```
Flash Memory
+------------------------+ <- Partition Start
|    Image Header       | 256B aligned
+------------------------+ <- Entry Point
|    Firmware Image     |
|                      |
+------------------------+
```

### Key Characteristics
- 256B header offset
- Multiple partition support
- Automatic image relocation
- Chain-load execution model

## Image Header

### Structure Overview
The image header contains metadata and security information for the firmware:

```
+------------------------+ <- Header Start
|     Magic Number      | 4 bytes
|     Image Size        | 4 bytes
|     Tags Section      | Variable size
|     Padding           | To 256B alignment
+------------------------+ <- IMAGE_HEADER_SIZE
|     Firmware Start    |
```

### Header Characteristics
- Variable total size based on:
  - Digest algorithm
  - Signature size
  - Key length
- 256-byte alignment enforced
- Size defined by `IMAGE_HEADER_SIZE`
- Supports vector table relocation

### Implementation Notes
- Size determined at build time
- Reported by signing tools
- Critical for interrupt handling
- Must match across toolchain

**Important**: When porting to custom build systems, ensure `IMAGE_HEADER_SIZE` matches the `sign` tool output.

### Tag System

#### Base Format
All values stored in little-endian format.

##### Fixed Fields
| Field | Size | Description |
|-------|------|-------------|
| Magic Number | 4 bytes | Header identifier |
| Image Size | 4 bytes | Firmware size (excluding header) |

##### Tag Structure
```c
struct tag {
    uint16_t type;    // Tag identifier
    uint16_t size;    // Content size
    uint8_t  data[];  // Variable length content
};
```

#### Mandatory Tags

| Type | Name | Size | Description |
|------|------|------|-------------|
| 0x0001 | Version | 4B | Firmware version number |
| 0x0002 | Timestamp | 8B | Creation time (Unix seconds) |
| 0x0003 | SHA Digest | 32B* | Integrity check (SHA256) |
| 0x0020 | Signature | 64B | Firmware authentication |
| 0x0030 | Type | 2B | Firmware type & auth method |
| 0x0010 | Key Hint | 32B | Public key SHA digest |

\* Size varies with hash algorithm

#### Special Cases
- Type field = 0xFF: Padding byte
  - No size field
  - Next byte is new Type
  - Used for alignment

#### Key Management
- Key hint enables multi-key support
- Bootloader uses hint for key selection
- Required for signature verification

wolfBoot will, in all cases, refuse to boot an image that cannot be verified and authenticated using the built-in digital signature authentication mechanism.

## Custom Fields Support

### Overview
wolfBoot supports extending the manifest header with custom fields:
- Secured by signature verification
- Flexible field types
- Runtime accessible
- Little-endian format

### Adding Custom Fields

#### Command Line Usage
```bash
# Add custom TLV field
./tools/keytools/sign \
    --ed25519 \
    --custom-tlv 0x34 4 0xAABBCCDD \
    test-app/image.bin \
    wolfboot_signing_private_key.der \
    4
```

#### Field Structure
```c
struct custom_field {
    uint16_t tag;     // Custom identifier
    uint16_t length;  // Field size
    uint8_t  value[]; // Field content
};
```

### Runtime Access

#### API Usage
```c
// Access custom field
uint32_t value;
uint8_t* ptr = NULL;
uint16_t tlv = 0x34;
uint8_t* imageHdr = (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS + 
                    IMAGE_HEADER_OFFSET;

// Find and read field
uint16_t size = wolfBoot_find_header(imageHdr, tlv, &ptr);
if (size > 0 && ptr != NULL) {
    // Field found - read value
    memcpy(&value, ptr, size);
    printf("TLV 0x%x=0x%x\n", tlv, value);
}
else {
    // Field not found
    handle_error();
}
```

#### Security Notes
- Fields included in signature
- Protected against tampering
- Alignment handled automatically

For detailed syntax, see [Signing Documentation](Signing.md#adding-custom-fields-to-the-manifest-header)

## Image Management

### Signing Process

#### Tool Overview
The signing tool provides:
- Automatic header generation
- Required tag creation
- Signature calculation
- Output file creation

#### Usage Flow
1. Compile firmware
2. Generate header
3. Sign image
4. Store or transmit

### Storage Management

#### Partition Layout
```
Flash Memory
+------------------------+
|    BOOT Partition     |
| [Header + Firmware A] |
+------------------------+
|   UPDATE Partition    |
| [Header + Firmware B] |
+------------------------+
```

#### Key Points
- Headers required in all partitions
- Boot only from BOOT partition
- Swap mechanism for updates
- Secure update process

For detailed partition information, see:
- [Flash Partitions](flash_partitions.md)
- [Firmware Updates](firmware_update.md)
- [Security Features](Signing.md)




