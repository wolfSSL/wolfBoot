# Flash partitions

## Flash memory partitions

To integrate wolfBoot you need to partition the flash into
separate areas (partitions), taking into account the geometry of the flash memory.

Images boundaries **must** be aligned to physical sectors, because the
bootloader erases all the flash sectors before storing a new firmware image, and
swaps the content of the two partitions, one sector at a time.

For this reason, before proceeding with partitioning on a target system, the
following aspects must be considered:

  - BOOT partition and UPDATE partition must have the same size, and be able to contain the running system
  - SWAP partition must be as big as the largest sector in both BOOT and UPDATE partition.

The flash memory of the target is partitioned into the following areas:

  - Bootloader partition, at the beginning of the flash, generally very small (16-32KB)
  - Primary slot (BOOT partition) starting at address `WOLFBOOT_PARTITION_BOOT_ADDRESS`
  - Secondary slot (UPDATE partition) starting at address `WOLFBOOT_PARTITION_UPDATE_ADDRESS`
    - both partitions share the same size, defined as `WOLFBOOT_PARTITION_SIZE`
  - Swapping space (SWAP partition) starting at address `WOLFBOOT_PARTITION_SWAP_ADDRESS`
    - the swap space size is defined as `WOLFBOOT_SECTOR_SIZE` and must be as big as the
      largest sector used in either BOOT/UPDATE partitions.
  - (Optional) Self-header partition starting at address `WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS`,
    used when `WOLFBOOT_SELF_HEADER=1` is enabled. See [Self-header partition](#self-header-partition) below.

A proper partitioning configuration must be set up for the specific use, by setting
the values for offsets and sizes in [include/target.h](../include/target.h).

### Bootloader partition

This partition is usually very small, and only contains the bootloader code and data.
Public keys pre-authorized during factory image creations are automatically stored
as part of the firmware image.

### Self-header partition

When `WOLFBOOT_SELF_HEADER=1` is enabled, an additional flash region is
reserved for the bootloader's own signed manifest header. This allows
external components (e.g. wolfHSM server, a secure co-processor, etc.) to read
and cryptographically verify the bootloader.

Configuration:

- `WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS` — the flash address for the
  header. **Must be aligned to `WOLFBOOT_SECTOR_SIZE`.**
- `WOLFBOOT_SELF_HEADER_SIZE` — (optional) the erase span at the header
  address. Defaults to `IMAGE_HEADER_SIZE` and must be at least
  `IMAGE_HEADER_SIZE`.
- `SELF_HEADER_EXT=1` — store the self-header in external flash instead
  of internal flash. Requires `EXT_FLASH=1`.

The self-header partition sits alongside the other partitions in the
flash map:

```
  ┌─────────────────────┐
  │  wolfBoot           │
  ├─────────────────────┤  BOOT_ADDRESS
  │  BOOT partition     │
  ├─────────────────────┤  UPDATE_ADDRESS
  │  UPDATE partition   │
  ├─────────────────────┤  SWAP_ADDRESS
  │  SWAP partition     │
  ├─────────────────────┤  SELF_HEADER_ADDRESS
  │  Self-header        │
  └─────────────────────┘
```

The actual placement is flexible — the self-header can be located
anywhere in the flash map as long as it does not overlap with any other
partition and is sector-aligned.

For full details on the self-header feature — including the update flow,
runtime verification API, and factory programming — see
[firmware_update.md](firmware_update.md#self-header-persisting-the-bootloader-manifest).

### BOOT partition

This is the only partition from where it is possible to chain-load and execute a
firmware image. The firmware image must be linked so that its entry-point is at address
`WOLFBOOT_PARTITION_BOOT_ADDRESS + 256`.

### UPDATE partition

The running firmware is responsible for transferring a new firmware image through a secure channel,
and store it in the secondary slot. If an update is initiated, the bootloader will replace or swap
the firmware in the boot partition at the next reboot.


## Partition status and sector flags

Partitions are used to store firmware images currently in use (BOOT) or ready to swap in (UPDATE).
In order to track the status of the firmware in each partition, a 1-Byte state field is stored at the end of
each partition space. This byte is initialized when the partition is erased and accessed for the first time.

Possible states are:
  - `IMG_STATE_NEW` (0xFF): The image was never staged for boot, or triggered for an update. If an image is present, no flags are active.
  - `IMG_STATE_UPDATING` (0x70): Only valid in the UPDATE partition. The image is marked for update and should replace the current image in BOOT.
  - `IMG_STATE_TESTING` (0x10): Only valid in the BOOT partition. The image has been just updated, and never completed its boot. If present after reboot, it means that the updated image failed to boot, despite being correctly verified. This particular situation triggers a rollback.
  - `IMG_STATE_SUCCESS` (0x00): Only valid in the BOOT partition. The image stored in BOOT has been successfully staged at least once, and the update is now complete.

Starting from the State byte and growing backwards, the bootloader keeps track of the state of each sector, using 4-bits per sector at the end of the UPDATE partition. Whenever an update is initiated, the firmware is transferred from UPDATE to BOOT one sector at a time, and storing a backup of the original firmware from BOOT to UPDATE. Each flash access operation correspond to a different value of the flags for the sector in the sector flags area, so that if the operation is interrupted, it can be resumed upon reboot.

End of flash layout:
 * 4-bits flag (sector 1)
 * 4-bits flag (sector 0)
 * 1-byte partition state
 * 4-byte trailer "BOOT"

If the `FLAGS_HOME` build option is used then all flags are placed at the end of the boot partition:

 ```
                         / -12    /-8       /-4     / END
   |Sn| ... |S2|S1|S0|PU|  MAGIC  |X|X|X|PB| MAGIC |
    ^--sectors   --^  ^--update           ^---boot partition
       flags             partition            flag
                         flag
```

You can use the `CUSTOM_PARTITION_TRAILER` option to implement your own functions for: `get_trailer_at`, `set_trailer_at` and `set_partition_magic`.

To enable:
1) Add the `CUSTOM_PARTITION_TRAILER` build option to your `.config`: `CFLAGS_EXTRA+=--DCUSTOM_PARTITION_TRAILER`
2) Add your own .c file using `OBJS_EXTRA`. For example for your own `src/custom_trailer.c` add this to your `.config`: `OBJS_EXTRA=src/custom_trailer.o`.


## Overview of the content of the FLASH partitions

![wolfBoot partition](png/wolfboot_partition.png)
