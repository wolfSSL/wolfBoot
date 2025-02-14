# Hardware Abstraction Layer (HAL)

The Hardware Abstraction Layer (HAL) provides the interface between wolfBoot and target hardware. Each supported microcontroller requires its own HAL implementation.

## Overview

The HAL serves two primary purposes:
1. Provide hardware-specific flash operations (read/write/erase) for both:
   - Bootloader operations
   - Application-initiated firmware updates
2. Ensure optimal MCU performance during boot for fast signature verification

## Implementation Structure

HAL implementations are organized as follows:

1. Platform-specific C files:
   - Located in the [hal](../hal) directory
   - One file per supported platform
   - Contains all hardware-specific function implementations

2. Platform-specific linker scripts:
   - Located alongside HAL C files
   - Named `platform_name.ld`
   - Defines:
     - Flash memory layout
     - RAM boundaries
     - Required symbol exports

## Supported platforms

Please see [Targets](Targets.md)


## Core HAL API

Every HAL implementation must provide these six core functions:

### System Initialization
```c
void hal_init(void)
```
Called at bootloader startup to initialize the hardware.

**Responsibilities:**
- Configure system clock for optimal performance
- Initialize critical peripherals
- Set up flash controller
- Configure any required hardware accelerators

### Flash Memory Operations

#### Unlock Flash
```c
void hal_flash_unlock(void)
```
Prepares flash memory for write/erase operations.

**Notes:**
- Called before any flash write/erase operation
- May be empty if target doesn't require explicit unlocking
- Must handle any required flash controller configuration

#### Write to Flash
```c
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
```
Writes data to internal flash memory.

**Parameters:**
- `address`: Offset from flash start address
- `data`: Pointer to data buffer to write
- `len`: Number of bytes to write

**Requirements:**
- Must handle any size/alignment of writes
- Must implement read-modify-write if needed
- Must work with minimum programmable unit size

**Returns:**
- 0 on success
- Negative value on failure

#### Lock Flash
```c
void hal_flash_lock(void)
```
Re-enables flash write protection after operations.

**Notes:**
- Called after flash write/erase operations complete
- Restores flash controller to protected state
- Must reverse any changes made by `hal_flash_unlock()`

#### Erase Flash
```c
int hal_flash_erase(uint32_t address, int len)
```
Erases a section of internal flash memory.

**Parameters:**
- `address`: Start address to erase (aligned to `WOLFBOOT_SECTOR_SIZE`)
- `len`: Number of bytes to erase (multiple of `WOLFBOOT_SECTOR_SIZE`)

**Requirements:**
- Must handle flash sector geometry
- Must erase all sectors in specified range
- Must use target's IAP interface

**Returns:**
- 0 on success
- Negative value on failure

### Boot Preparation
```c
void hal_prepare_boot(void)
```
Prepares system for firmware execution.

**Called:**
- Just before chain-loading firmware
- After all bootloader operations complete

**Responsibilities:**
- Restore default clock settings
- Reset modified peripherals
- Ensure clean hardware state for firmware

## Optional HAL Extensions

### External Flash Support

Enable with: `make EXT_FLASH=1`

This extension allows:
- Using external memory for UPDATE/SWAP partitions
- Custom handling of flash read operations
- Support for special flash interfaces

**Configuration:**
- `PART_UPDATE_EXT`: Store UPDATE partition in external memory
- `PART_SWAP_EXT`: Store SWAP partition in external memory

**Important Notes:**
- Incompatible with `NVM_FLASH_WRITEONCE`
- Requires implementing additional HAL functions
- Can be used for special flash handling needs

### External Flash API

When `EXT_FLASH=1` is enabled, the following functions must be implemented:

#### Write to External Flash
```c
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
```
Writes data to external flash memory.

**Parameters:**
- `address`: Offset from external memory base address
- `data`: Pointer to data buffer to write
- `len`: Number of bytes to write

**Requirements:**
- Must follow same restrictions as `hal_flash_write()`
- Must handle device-specific protocols (e.g., SPI)

**Returns:**
- 0 on success
- Negative value on failure

#### Read from External Flash
```c
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
```
Reads data from external flash memory.

**Parameters:**
- `address`: Offset from external memory base address
- `data`: Buffer to store read data
- `len`: Number of bytes to read

**Requirements:**
- Must handle any size/alignment of reads
- Must implement device-specific read protocol

**Returns:**
- 0 on success
- Negative value on failure

#### Erase External Flash
```c
int ext_flash_erase(uintptr_t address, int len)
```
Erases a section of external flash memory.

**Parameters:**
- `address`: Start address to erase
- `len`: Number of bytes to erase

**Requirements:**
- Must follow same restrictions as `hal_flash_erase()`
- Must handle device-specific sector geometry
- Must implement proper erase commands

**Returns:**
- 0 on success
- Negative value on failure

#### External Flash Protection

```c
void ext_flash_lock(void)
```
Re-enables write protection for external flash.

**Called:**
- After external flash operations complete
- Before returning control to application

```c
void ext_flash_unlock(void)
```
Disables write protection for external flash.

**Called:**
- Before external flash write/erase operations
- May be empty if device doesn't require unlocking


### Dual-Bank Support

Some MCUs support hardware-assisted bank swapping for fail-safe updates. Enable with `DUALBANK_SWAP=1`.

#### Required Functions

```c
void hal_flash_dualbank_swap(void)
```
Performs hardware bank swap operation.

**Notes:**
- Called to switch between flash banks
- May trigger system reboot
- Implementation is platform-specific
- May not return on some architectures

```c
void fork_bootloader(void)
```
Manages bootloader redundancy across banks.

**Responsibilities:**
- Creates backup copy of bootloader if needed
- Ensures both banks have identical bootloader code
- Returns immediately if banks already match

**Usage:**
- Called during bank management operations
- Critical for fail-safe dual-bank operation
- Must be carefully implemented per platform


