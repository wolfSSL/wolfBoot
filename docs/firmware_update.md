# Firmware Update Guide

## Overview
This guide details wolfBoot's secure firmware update process, including:
- Update procedures
- Security mechanisms
- Rollback protection
- Integration steps

## Update Process

### High-Level Flow
```
1. Prepare Update ─────► 2. Transfer & Store ─────► 3. Trigger Update ─────► 4. Verify & Install
   - Compile            - Secure transfer         - Mark for update      - Validate signature
   - Sign               - Store in UPDATE         - Schedule swap        - Perform swap
   - Package           partition                 - Reboot              - Boot new firmware
```

### Key Components
- [libwolfboot](API.md) for update management
- [HAL](HAL.md) for flash operations
- Secure boot chain
- Rollback protection

![Update and Rollback](png/wolfboot_update_rollback.png)

## Update Implementation

### Update Sequence

#### 1. Initiation
```c
// Store new firmware in UPDATE partition
// Then trigger update
wolfBoot_update_trigger();
```

#### 2. Bootloader Actions
On next reboot, wolfBoot:
1. Validates UPDATE partition
2. Verifies digital signature
3. Performs partition swap
4. Sets `STATE_TESTING`
5. Boots new firmware

#### 3. Recovery Mechanism
- Automatic resume on interruption
- Maintains system integrity
- Preserves update state

### Update Confirmation

#### Success Path
```c
// After system verification
wolfBoot_success();  // Confirm update
```

#### Rollback Triggers
- Missing `wolfBoot_success()` call
- Reboot before confirmation
- System validation failure

#### Rollback Process
1. Detects unconfirmed state
2. Initiates reverse update
3. Restores original firmware
4. Maintains system availability

## Firmware Build Process

### Image Requirements

#### Position Dependencies
```
Flash Layout
+------------------------+ <- 0x00000000
|      wolfBoot         |
+------------------------+ <- BOOT_PARTITION
| Header (256B)         |    Entry Point = BOOT_PARTITION + 256
| Firmware              |
+------------------------+
```

#### Build Steps
1. Configure entry point
   - Must match BOOT partition
   - Account for header offset

2. Compile and link
   ```bash
   # Example linker configuration
   ENTRY_POINT = BOOT_PARTITION + 256
   ```

3. Sign image
   ```bash
   ./tools/keytools/sign \
       --ed25519 \
       firmware.bin \
       private_key.der \
       version
   ```

### Self-Update Support

#### Configuration
```bash
# Enable self-update support
make RAM_CODE=1
```

#### Key Differences
| Feature | Normal Update | Self-Update |
|---------|---------------|-------------|
| Storage | Swap-based | Direct write |
| Safety | Interrupt-safe | Not interrupt-safe |
| Header | Standard | `--wolfboot-update` |
| Process | Two-stage | Single-stage |

#### Usage
```bash
# Sign bootloader update
./tools/keytools/sign \
    --ed25519 \
    --wolfboot-update \
    new_bootloader.bin \
    private_key.der \
    version
```

**Warning**: Do not interrupt self-update process

wolfBoot can be used to deploy new bootloader versions as well as
update keys.

## Delta Updates

### Overview
wolfBoot supports bandwidth-efficient incremental updates through binary differencing:
- Minimizes update package size
- Maintains security standards
- Supports bidirectional patching
- Uses Bentley/McIlroy algorithm

### Architecture

#### Delta Generation
```
Base Firmware (v1) ──┐
                     ├─► Delta Package ──► Target Device
New Firmware (v2) ───┘
```

#### Package Contents
1. Forward patch (v1 → v2)
2. Reverse patch (v2 → v1)
3. Authentication data
4. Version information

![Delta update](png/delta_updates.png)

### Security Features

#### Dual Verification
1. Package Authentication
   - Signed delta bundle
   - Version verification
   - Integrity checking

2. Result Validation
   - Patch application verification
   - Final image authentication
   - Signature verification


### Implementation Details

#### Update Process Flow
```
1. Delta Bundle ──► 2. Authentication ──► 3. Patch Application ──► 4. Result Verification
   Reception         Bundle Signature     Forward/Reverse        Final Image Check
```

#### Update Confirmation
- Uses standard `wolfBoot_success()`
- Same API as full updates
- Automatic rollback support

![Delta update: details](png/delta_updates_2.png)

### Usage Guide

#### Prerequisites
```bash
# Enable delta updates
make DELTA_UPDATES=1
```

#### Creating Updates

1. Sign base version:
```bash
# Sign version 1
./tools/keytools/sign \
    --ecc256 \
    --sha256 \
    test-app/image.bin \
    wolfboot_signing_private_key.der \
    1
```

2. Create delta update:
```bash
# Generate delta v1->v2
./tools/keytools/sign \
    --delta test-app/image_v1_signed.bin \
    --ecc256 \
    --sha256 \
    test-app/image.bin \
    wolfboot_signing_private_key.der \
    2
```

#### Output Files
| File | Description |
|------|-------------|
| `image_v2_signed.bin` | Complete v2 image |
| `image_v2_signed_diff.bin` | Delta package |

#### Update Flow
1. Transfer delta package
2. Store in UPDATE partition
3. Trigger update
4. Automatic patch application
5. Boot new version
6. Confirm or rollback

**Note**: Failed confirmation triggers automatic rollback using reverse patch


