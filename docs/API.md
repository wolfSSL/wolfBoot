# Application interface for interactions with the bootloader

wolfBoot offers a small interface to interact with the images stored in the partition,
explicitly initiate an update and confirm the success of a previously scheduled update.

## Compiling and linking with libwolfboot

An application that requires interactions with wolfBoot must include the header file:

`#include <wolfboot/wolfboot.h>`

This exports the API function declarations, and the predefined values for the flags
and tags stored together with the firmware images in the two partitions.

For more information about flash partitions, flags and states see [Flash partitions](flash_partitions.md).

## API

libwolfboot provides low-level access interface to flash partition states. The state
of each partition can be retrieved and altered by the application.

Basic interaction from the application is provided via the following high-level function calls:

`uint32_t wolfBoot_get_image_version(uint8_t part)`

`void wolfBoot_update_trigger(void)`

`void wolfBoot_success(void)`

### Firmware version

Current (boot) firmware and update firmware versions can be retrieved from the application using:

`uint32_t wolfBoot_get_image_version(uint8_t part)`

Or via the shortcut macros:

`wolfBoot_current_firmware_version()`

and

`wolfBoot_update_firmware_version()`

### Trigger an update

  - `wolfBoot_update_trigger()` is used to trigger an update upon the next reboot, and it is normally used by
an update application that has retrieved a new version of the running firmware, and has
stored it in the UPDATE partition on the flash. This function will set the state of the UPDATE partition 
to `STATE_UPDATING`, instructing the bootloader to perform the update upon the next execution (after reboot).

wolfBoot update process swaps the contents of the UPDATE and the BOOT partitions, using a temporary
single-block SWAP space.

### Confirm current image

- `wolfBoot_success()` indicates a successful boot of a new firmware. This can be called by the application
at any time, but it will only be effective to mark the current firmware (in the BOOT partition) with the state
`STATE_SUCCESS`, indicating that no roll-back is required. An application should typically call `wolfBoot_success()`
only after verifying that the basic system features are up and running, including the possibility to retrieve
a new firmware for the next upgrade.

If after an upgrade and reboot wolfBoot detects that the active firmware is still in `STATE_TESTING` state, it means that
a successful boot has not been confirmed for the application, and will attempt to revert the update by swapping 
the two images again.

For more information about the update process, see [Firmware Update](firmware_update.md)

For the image format, see [Firmware Image](firmware_image.md)

### Failure diagnostics

When wolfBoot is compiled with `WOLFBOOT_PERSIST_FAILURE_STATUS`, it records the
cause of update, boot and rollback failures into a dedicated flash region so the
application can read them back after a failed update or an unexpected rollback.

This feature is disabled by default. To enable it, set
`WOLFBOOT_PERSIST_FAILURE_STATUS=1` and provide:

- `WOLFBOOT_DIAGNOSTICS_ADDRESS`: the start address of the region reserved for
  diagnostics (it must be sector-aligned and must not overlap any partition);
- `WOLFBOOT_DIAGNOSTICS_SECTORS`: the size of the region, specified as a number
  of flash sectors (default: 2);
- `WOLFBOOT_DIAGNOSTICS_EXT`: setting this causes the region to live in
  external flash. Only meaningful when `EXT_FLASH` is enabled.
- `WOLFBOOT_DIAGNOSTICS_RECORD_SIZE`: the on-flash slot size for each record and
  the sector header, in bytes (default: 16). It must be a multiple of, and no
  smaller than, the flash write granularity. Raise it on platforms whose flash
  write word is wider than 16 bytes (for example 32 for the 256-bit words on
  STM32H7).

The region is managed as a circular store over its sectors. With two or more
sectors, older records are retained until the log wraps all the way around, so
there is always at least one full sector of history. With a single sector
(`WOLFBOOT_DIAGNOSTICS_SECTORS=1`) the sector is erased and restarted when it
fills, discarding all previous records.

The following events are recorded:

- An update image was rejected before performing the update
  (`WOLFBOOT_FAILURE_PHASE_UPDATE`).
- A boot image failed verification, triggering an emergency update
  (`WOLFBOOT_FAILURE_PHASE_BOOT`).
- The emergency-update image also failed verification, leaving the device
  unbootable (`WOLFBOOT_FAILURE_PHASE_RECOVERY`).
- A rollback was caused by a new image that never confirmed via
  `wolfBoot_success()` (`WOLFBOOT_FAILURE_PHASE_ROLLBACK`).

For verification failures, the cause distinguishes a bad header
(`WOLFBOOT_FAILURE_CAUSE_HEADER`), a failed integrity/hash check
(`WOLFBOOT_FAILURE_CAUSE_HASH`) and a failed signature/authenticity check
(`WOLFBOOT_FAILURE_CAUSE_SIGNATURE`). A rollback uses
`WOLFBOOT_FAILURE_CAUSE_NOT_CONFIRMED`.

The application can read the records, ordered newest-first, through this API:

- `int wolfBoot_get_failure_count(void)`: number of records currently stored.
- `int wolfBoot_get_failure(int index, struct wolfBoot_failure_record *out)`:
  copy record `index` (0 is the most recent) into `out`. Returns 0 on success.
- `int wolfBoot_clear_failures(void)`: erase the diagnostics region.

The read functions only read the region and have no external dependency.
`wolfBoot_clear_failures()`, however, erases flash, so an application that calls
it must have the HAL flash driver (`hal_flash_unlock`, `hal_flash_erase`,
`hal_flash_lock`) similarly to `wolfBoot_success()`.

Each `struct wolfBoot_failure_record` reports `phase`, `cause`, `partition`,
`fw_version` (the version of the offending image, when known) and a monotonic
`seq` number.

```c
struct wolfBoot_failure_record rec;
int i, n = wolfBoot_get_failure_count();
for (i = 0; i < n; i++) {
    if (wolfBoot_get_failure(i, &rec) == 0) {
        printf("failure: phase %u cause %u part %u ver 0x%x\n",
            rec.phase, rec.cause, rec.partition, rec.fw_version);
    }
}
```

## NSC API

If you're running wolfBoot on an ARM TrustZone-enabled device (see for example
[STM32-TZ](STM32-TZ.md)) you may wish to run your application in non-secure
mode, while keeping the UPDATE and SWAP partitions in the secure domain. In
order to accomplish this, any operation by the application that requires access
to those partitions needs to be performed via wolfBoot code running in the
secure domain. For this purpose, wolfBoot provides Non-Secure Callable (NSC)
APIs that allow code running in the non-secure domain to call into the secure
domain managed by wolfBoot.

When `TZEN=1` is enabled, these APIs are available to non-secure applications.

These APIs are listed below.

- `void wolfBoot_nsc_success(void)`: wrapper for `wolfBoot_success()`
- `void wolfBoot_nsc_update_trigger(void)`: wrapper for
  `wolfBoot_update_trigger()`
- `uint32_t wolfBoot_nsc_get_image_version(uint8_t part)`: wrapper for
  `wolfBoot_get_image_version()`
- `uint32_t wolfBoot_nsc_current_firmware_version(void)`: wrapper for
  `wolfBoot_current_firmware_version()`
- `uint32_t wolfBoot_nsc_update_firmware_version(void)`: wrapper for
  `wolfBoot_update_firmware_version()`
- `int wolfBoot_nsc_get_partition_state(uint8_t part, uint8_t *st)`: wrapper
  for `wolfBoot_get_partition_state()`
- `int wolfBoot_nsc_erase_update(uint32_t address, uint32_t len)`: allows the
  application to erase the update partition in secure mode. The `address`
  parameter is an offset from the beginning of the partition.
- `int wolfBoot_nsc_write_update(uint32_t address, const uint8_t *buf, uint32_t
  len)`: allows the application to write to the update partition in secure
  mode. The `address` parameter is an offset from the beginning of the
  partition.
