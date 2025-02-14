# Flash Memory Partitioning Guide

## Overview
This guide explains how to properly partition flash memory for wolfBoot integration, considering:
- Flash geometry requirements
- Sector alignment rules
- Partition size constraints
- Memory layout organization

## Key Requirements

### Sector Alignment
- Image boundaries must align with physical sectors
- Required for:
  - Clean firmware updates
  - Reliable sector erasure
  - Safe partition swapping

### Partition Sizing Rules
| Partition | Size Requirement | Purpose |
|-----------|-----------------|----------|
| BOOT | = UPDATE size | Active firmware |
| UPDATE | = BOOT size | Update staging |
| SWAP | ≥ largest sector | Safe updates |

## Memory Layout

```
Flash Memory Map
+------------------------+ <- 0x00000000
| Bootloader (16-32KB)  |
+------------------------+ <- WOLFBOOT_PARTITION_BOOT_ADDRESS
| BOOT Partition        |
| (Primary Slot)        |
+------------------------+ <- WOLFBOOT_PARTITION_UPDATE_ADDRESS
| UPDATE Partition      |
| (Secondary Slot)      |
+------------------------+ <- WOLFBOOT_PARTITION_SWAP_ADDRESS
| SWAP Space           |
+------------------------+
```

### Configuration Parameters
| Parameter | Description |
|-----------|-------------|
| `WOLFBOOT_PARTITION_BOOT_ADDRESS` | Start of BOOT partition |
| `WOLFBOOT_PARTITION_UPDATE_ADDRESS` | Start of UPDATE partition |
| `WOLFBOOT_PARTITION_SWAP_ADDRESS` | Start of SWAP space |
| `WOLFBOOT_PARTITION_SIZE` | Size of BOOT/UPDATE partitions |
| `WOLFBOOT_SECTOR_SIZE` | Size of SWAP space |

## Partition Details

### Configuration
All partition parameters are defined in [include/target.h](../include/target.h).

### Bootloader Partition
- Size: 16-32KB typical
- Location: Start of flash
- Contents:
  - Bootloader code
  - Runtime data
  - Factory keys (embedded)

### BOOT Partition (Primary)
- Purpose: Active firmware execution
- Requirements:
  - Only bootable partition
  - Entry point = `BOOT_ADDRESS + 256`
  - Sector-aligned boundaries
- Status: Always contains verified image

### UPDATE Partition (Secondary)
- Purpose: Update staging area
- Operation:
  1. Receives new firmware
  2. Verifies signature
  3. Swaps with BOOT on update
- Security: Uses secure channel for updates


## State Management

### Partition States
Each partition maintains a 1-byte state field at its end:

| State | Value | Valid In | Description |
|-------|-------|----------|-------------|
| `IMG_STATE_NEW` | 0xFF | Both | Fresh/unused image |
| `IMG_STATE_UPDATING` | 0x70 | UPDATE | Pending update |
| `IMG_STATE_TESTING` | 0x10 | BOOT | Update verification |
| `IMG_STATE_SUCCESS` | 0x00 | BOOT | Update confirmed |

### Sector Tracking
- Location: End of partition
- Format: 4-bits per sector
- Direction: Grows backwards
- Purpose: Update progress tracking

### Update Process
1. Transfer firmware sector-by-sector
2. Track each sector's state
3. Store backup in UPDATE
4. Resume on interruption

### State Flow
```
NEW ──► UPDATING ──► TESTING ──► SUCCESS
                           │
                           └──► ROLLBACK (if boot fails)
```

## Partition Trailer Format

### Standard Layout
```
End of Partition
+------------------------+ <- END
| "BOOT" Magic (4B)     |
| Partition State (1B)  |
| Sector 0 Flags (4b)   |
| Sector 1 Flags (4b)   |
+------------------------+
```

### FLAGS_HOME Option
Consolidates all flags at boot partition end:
```
                         / -12    /-8       /-4     / END
   |Sn| ... |S2|S1|S0|PU|  MAGIC  |X|X|X|PB| MAGIC |
    ^--sectors   --^  ^--update           ^---boot
       flags             state             state
```

### Custom Implementation
Enable custom trailer handling:

```make
# .config setup
CFLAGS_EXTRA+=--DCUSTOM_PARTITION_TRAILER
OBJS_EXTRA=src/custom_trailer.o
```

#### Required Functions
- `get_trailer_at`
- `set_trailer_at`
- `set_partition_magic`

## Visual Overview
![wolfBoot partition](png/wolfboot_partition.png)

## Related Documentation
- [Firmware Updates](firmware_update.md)
- [Build Configuration](compile.md)
- [Target Configuration](Targets.md)
