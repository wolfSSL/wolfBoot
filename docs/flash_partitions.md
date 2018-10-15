# Flash partitions


## Flash memory partitions

To integrate wolfBoot you need to partition the flash into 
separate areas (slots), according to the geometry of the flash memory.

Images boundaries **must** be aligned to physical sectors, because the 
bootloader erases all the flash sectors before storing a firmware image.

The flash memory of the target is partitioned into the following areas:

  - Bootloader partition, at the beginning of the flash
  - Primary slot (boot partition) starting at address `FLASH_AREA_IMAGE_0_OFFSET`
  - Secondary slot (upgrade partition) starting at address `FLASH_AREA_IMAGE_1_OFFSET`
  - Scratch space (swap partition) starting at address `FLASH_AREA_IMAGE_SCRATCH_OFFSET`

A proper partitioning configuration must be set up for the specific use, by setting
the values for offsets and sizes in [include/target.h](../include/target.h).

### Bootloader partition

This partition is usually very small, and only contains the bootloader code and data.
Public keys pre-authorized during factory image creations are automatically stored
as part of the firmware image.

### Primary slot (boot partition)

This is the only partition from where it is possible to chain-load and execute a 
firmware image. The firmware image must be linked so that its entry-point is at address
`FLASH_AREA_IMAGE_0_OFFSET + 256`. 

### Secondary slot (upgrade partition)

The running firmware is responsible of transferring a new firmware image through a secure channel,
and store it in the secondary slot. If an upgrade is initiated, the bootloader will replace or swap
the firmware in the boot partition at the next reboot.

## Example 512KB partitioning on STM32-F407

The example firmware provided in the `test-app` is configured to boot from the primary partition
starting at address 0x20000. The flash layout is provided by the default example using the following
configuration in `target.h`:

```C
#define FLASH_AREA_IMAGE_0_OFFSET	0x20000
#define FLASH_AREA_IMAGE_0_SIZE		0x20000
#define FLASH_AREA_IMAGE_1_OFFSET	0x40000
#define FLASH_AREA_IMAGE_1_SIZE		0x20000
#define FLASH_AREA_IMAGE_SCRATCH_OFFSET	0x60000
#define FLASH_AREA_IMAGE_SCRATCH_SIZE	0x20000
```

which results in the following partition configuration:

![example partitions](png/example_partitions.png)

This configuration demonstrates one of the possible layouts, with the slots
aligned to the beginning of the physical sector on the flash.

The entry point for all the runnable firmware images on this target will be `0x20100`, 
256 Bytes after the beginning of the first flash partition. This is due to the presence
of the firmware image header at the beginning of the partition, as explained more in details
in [Firmware image](firmware_image.md)


