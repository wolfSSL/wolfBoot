# Application Interface for wolfBoot

wolfBoot provides an API through libwolfboot that allows applications to:
- Query firmware versions in both BOOT and UPDATE partitions
- Initiate firmware updates
- Confirm successful firmware boots
- Access partition states

## Compiling and Linking with libwolfboot

Applications using wolfBoot functionality must:

1. Include the wolfBoot header:
   ```c
   #include <wolfboot/wolfboot.h>`
   ```

2. Link against libwolfboot library

The header provides:
- API function declarations
- Predefined partition flag values
- Image tag definitions
- Partition state constants

For details about flash partitions, flags and states see [Flash partitions](flash_partitions.md).

## Core API Functions

libwolfboot provides both high-level and low-level interfaces to manage firmware updates:

### High-Level Functions

```c
uint32_t wolfBoot_get_image_version(uint8_t part)
void wolfBoot_update_trigger(void)
void wolfBoot_success(void)
```

### Version Management Functions

#### Get Image Version
```c
uint32_t wolfBoot_get_image_version(uint8_t part)
```
Retrieves the version number of the firmware in the specified partition.

Parameters:
- `part`: Partition ID (PART_BOOT or PART_UPDATE)

Returns:
- Version number of the firmware in the specified partition
- 0 if no valid firmware exists in the partition

#### Convenience Macros
```c
wolfBoot_current_firmware_version()  // Get version of firmware in BOOT partition
wolfBoot_update_firmware_version()   // Get version of firmware in UPDATE partition
```

### Update Management Functions

#### Trigger Update
```c
void wolfBoot_update_trigger(void)
```
Initiates the firmware update process for the next boot.

Operation:
1. Sets UPDATE partition state to `STATE_UPDATING`
2. On next boot, wolfBoot will:
   - Verify the update image signature
   - Swap BOOT and UPDATE partition contents using SWAP space
   - Set new firmware state to `STATE_TESTING`
   - Boot into new firmware

Note: The update image must be stored in the UPDATE partition before calling this function.

### Boot Confirmation Functions

#### Confirm Successful Boot
```c
void wolfBoot_success(void)
```
Confirms successful boot of the current firmware.

Operation:
1. Marks current firmware in BOOT partition as `STATE_SUCCESS`
2. Prevents automatic rollback on next boot

Important:
- Should be called only after verifying critical system functionality
- Recommended to verify:
  - Core system features work correctly
  - Update capability is functional
  - Any required peripherals are accessible

Rollback Behavior:
- If firmware remains in `STATE_TESTING` state after reboot
- wolfBoot will automatically rollback to previous version
- Accomplished by re-swapping BOOT and UPDATE partitions

Related Documentation:
- [Firmware Update Process](firmware_update.md)
- [Firmware Image Format](firmware_image.md)
