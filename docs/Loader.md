# wolfBoot Loaders and Updaters

This document describes the different loader and updater implementations available in wolfBoot.

## Loader Types

### Primary Loader (loader.c)
The main wolfBoot entry point that:
- Initializes the secure boot process
- Manages the boot sequence
- Coordinates with updater implementations
- Verifies firmware signatures
- Handles partition management

### Stage 1 Loader (loader_stage1.c)
A specialized first-stage loader for platforms without memory-mapped flash.

**Purpose:**
- Load wolfBoot from flash to RAM
- Initialize minimal hardware
- Jump to wolfBoot main entry

**Use Cases:**
- Non-XIP (Execute-in-Place) flash systems
- Limited boot ROM regions (e.g., PowerPC e500v2 with 4KB boot area)
- External NAND flash configurations

#### Configuration Options

```make
make WOLFBOOT_STAGE1_LOAD_ADDR=0x1000 stage1
```

| Option | Description | Usage |
|--------|-------------|--------|
| `WOLFBOOT_STAGE1_SIZE` | Maximum size of stage 1 loader | Ensure loader fits in boot ROM |
| `WOLFBOOT_STAGE1_FLASH_ADDR` | Flash location of stage 1 loader | Must match boot ROM entry point |
| `WOLFBOOT_STAGE1_BASE_ADDR` | RAM address for stage 1 loader | Temporary execution address |
| `WOLFBOOT_STAGE1_LOAD_ADDR` | RAM address for wolfBoot | Final wolfBoot location |
| `WOLFBOOT_LOAD_ADDRESS` | RAM address for application | Where firmware will be loaded |


## Updater Implementations

wolfBoot provides three different updater implementations to handle various hardware configurations:

### RAM-based Updater (update_ram.c)
Manages updates using system RAM as temporary storage.

**Features:**
- Loads firmware into RAM before verification
- Suitable for systems with sufficient RAM
- Reduces flash wear during update process

### Flash-based Updater (update_flash.c)
Performs updates directly using flash memory.

**Features:**
- In-place update verification
- Efficient for systems with limited RAM
- Uses swap partition for fail-safe updates

### Hardware-assisted Updater (update_flash_hwswap.c)
Leverages hardware-specific features for updates.

**Features:**
- Uses hardware bank-switching capabilities
- Optimized for dual-bank flash systems
- Fastest update mechanism when supported
- No swap partition required

## Implementation Selection

The appropriate updater is selected based on:
- Available system memory
- Flash memory architecture
- Hardware capabilities
- Platform-specific requirements

See [compile.md](compile.md) for configuration options affecting loader selection.
