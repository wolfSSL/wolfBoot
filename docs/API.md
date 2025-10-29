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

## NSC API

If you're running wolfBoot on an ARM TrustZone-enabled device (see for example
[STM32-TZ](STM32-TZ.md)) you may wish to run your application in non-secure
mode, while keeping the UPDATE and SWAP partitions in the secure domain. In
order to accomplish this, any operation by the application that requires access
to those partitions needs to be performed via wolfBoot code running in the
secure domain. For this purpose, wolfBoot provides Non-Secure Callable (NSC)
APIs that allow code running in the non-secure domain to call into the secure
domain managed by wolfBoot.

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
