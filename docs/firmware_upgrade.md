# Firmware upgrade

This section documents the complete firmware upgrade procedure, enabling secure boot
for an existing embedded application.

The steps to follow to complete a firmware upgrade with wolfBoot are:
   - Compile the firmware with the correct entry point
   - Sign the firmware
   - Transfer the image using a secure connection, and store it to the secondary firmware slot
   - Trigger the image swap
   - Reboot to let the bootloader begin the image swap


An application can be linked to wolfBoot `bootutil` library, which exports the API to trigger
the upgrade at the next reboot, and some helper functions to access the flash partition for 
erase/write through the target specific [HAL](HAL.md).

## Upgrade mode

wolfBoot can be compiled to provide two different types of upgrade:
   - Overwrite-only mode
   - Swap mode

### Overwrite-only mode

If wolfBoot is compiled with the `SWAP=0` command line option, no swap partition is required.
In this case, every time that a firmware upgrade is triggered, there is no way to revert the upgrade.

After the received firmware image is marked as 'pending' from the application, wolfBoot will verify its
authenticity and integrity, and overwrite the boot partition by storing the new image.

No firmware image rollback option is present in this configuration.

### Swap mode

Using the default configuration, wolfBoot offers the possibility to initiate, confirm or rollback an upgrade.

This mode requires an additional partition (swap) that can be used as a temporary storage. The swap partition
is also able to track the status of the flash operations, so that in case of power failure it can restart 
the interrupted upgrade operation.

The application can trigger temporary or permanent upgrades.


## Building a new firmware image

Firmware images are position-dependent, and can only boot from the origin of the **boot** partition in the FLASH.
This design choice means that the chosen firmware is always stored in the **boot** partition, and the bootloader
is responsible for pre-validating an upgrade image and copy it to the correct address.

All the firmware images must therefore have their entry point set to the address corresponding to the beginning 
of the **boot** partition, plus an offset of 256 Bytes to account for the firmware header.

Once the firmware is compiled and linked, it must be signed using the `ed25519_sign` tool. The tool produces
a signed image that can be transferred to the target using a secure connection, using the same key corresponding
to the public key currently used for verification.

The tool also appends the TLV at the end of the image, containing the signatures and the SHA256 hash of the firmware.

## Embedded application

At any given time, an application or OS running on a wolfBoot system can receive an updated version of itself,
and store the updated image in the second partition in the FLASH memory.

In order to communicate with the bootloader, the application can be linked with the `bootutil` library, by 
including the source file [lib/bootutil/bootutil\_misc.c](../lib/bootutil/bootutil_misc.c) and the back-end of
the [Hardware Abstraction Layer](docs/HAL.md) in use by wolfBoot for the target.

If the running application lacks support for FLASH IAP operations, *bootutil* can also be used to easily access
the flash for erasing/writing.

### Storing the update image

The application can access the flash area using the following API:


#### Flash access

The following functions are declared in `include/flash.h`:    

`int flash_area_open(uint8_t id, const struct flash_area **area)`

Initialize a `flash_area` object associated to the partition associated to identifier
`id`. 

Possible identifiers are:
  - `1` for the **boot** partition
  - `2` for the **upgrade** partition
  - `3` for the **swap** partition, if present.

This function must be called before using any other `flash_area_*` function.
Returns 0 on success.

`void flash_area_close(const struct flash_area *area)`
Stop using a flash area associated to the structure passed as argument.


`int flash_area_read(const struct flash_area *, uint32_t off, void *dst,  uint32_t len)`
`int flash_area_write(const struct flash_area *, uint32_t off, const void *src,  uint32_t len)`
`int flash_area_erase(const struct flash_area *, uint32_t off, uint32_t len)`

Read/Write/Erase a portion of the flash area, starting at offset `off` relative to the beginning
of the partition.

#### Triggering an upgrade

The following function is exported by the bootutil library and declared in `include/bootutil.h`:

`int boot_set_pending(int permanent);`
Trigger a firmware upgrade after the next reboot. If the permanent flag is not active and wolfBoot
is compiled in "swap" mode, the change will require to be confirmed via `boot_set_confirmed()` after
a successful boot, otherwise the original image will be rolled back after another reboot.

`int boot_set_confirmed(void);`
Confirm a previously triggered temporary upgrade. Only available when wolfBoot is compiled in swap mode.

To boot into the image stored in the upgrade partition, it is sufficient to call:

`boot_set_pending(1);`

and reboot the system to initiate the upgrade. 

