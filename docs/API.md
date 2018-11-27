# Application interface for interactions with the bootloader

wolfBoot offers a small interface to interact with the images stored in the partition,
explicitly initiate an update and confirm the success of a previously scheduled update.

## Compiling and linking with libwolfboot

An application that requires interactions with wolfBoot must include the header file:

`#include <wolfboot/wolfboot.h>`

Which exports the API function declarations, and the predefined values for the flags
and the tags stored together with the firmware images in the two partitions.

For more information about flash partitions, flags and states see [Flash partitions](flash_partitions.md).

## API

libwolfboot provides low-level access interface to flash partition states. The state
of each partition can be retrieved and altered by the application.

Basic interaction from the application is provided via the two high-level function calls:

`void wolfBoot_update(void)`

and

`void wolfBoot_success(void)`

### Trigger an update

  - `wolfBoot_update()` is used to trigger an update upon the next reboot, and it is normally used by
an update application that has retrieved a new version of the running firmware, and has
stored it in the UPDATE partition on the flash. This function will set the state of the UPDATE partition 
to `ST_UPDATE`, instructing the bootloader to perform the update upon the next execution (after reboot).

wolfBoot update process consist in swapping the content of the UPDATE and the BOOT partitions, using a temporary
single-block SWAP space.

- `wolfBoot_success()` indicates a successful boot of a new firmware. This can be called by the application
at any time, but it will only be effective to mark the current firmware (in the BOOT partition) with the state
`ST_SUCCESS`, indicating that no roll-back is required. An application should typically call `wolfBoot_success()`
only after verifying that the basic system features are up and running, including the possibility to retrieve
a new firmware for the next upgrade.

If after an upgrade wolfBoot detects that the active firmware is still in `ST_TESTING` state, it means that
a successful boot has not been confirmed for the application, and will attempt to revert the update by swapping 
the two images again.

For more information about the update process, see [Firmware Update](firmware_update.md)


