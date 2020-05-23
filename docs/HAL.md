# Hardware abstraction layer

In order to run wolfBoot on a target microcontroller, an implementation of the HAL
must be provided.

The HAL's purpose is to allow write/erase operations from the bootloader
and the application initiating the firmware upgrade through the application library, and
ensuring that the MCU is running at full speed during boot (to optimize the
verification of the signatures).

The implementation of the hardware-specific calls for each platform are grouped in 
a single c file in the [hal](../hal) directory.

The directory also contains a platform-specific linker script for each supported MCU,  
with the same name and the `.ld` extension. This is used to link the bootloader's 
firmware on the specific hardware, exporting all the necessary symbols for flash 
and RAM boundaries.

## Supported platforms

The following platforms are supported in the current version:
  - STM32F4, STM32L5, STM32L0, STM32F7, STM32H7, STM32G0
  - nRF52
  - Atmel samR21
  - TI cc26x2
  - Kinetis
  - SiFive HiFive1 RISC-V

## API

The Hardware Abstraction Layer (HAL) consists of six function calls
be implemented for each supported target:

`void hal_init(void)`

This function is called by the bootloader at the very beginning of the execution.
Ideally, the implementation provided configures the clock settings for the target 
microcontroller, to ensure that it runs at at the required speed to shorten the 
time required for the cryptography primitives to verify the firmware images.

`void hal_flash_unlock(void)`

If the IAP interface of the flash memory of the target requires it, this function
is called before every write and erase operations to unlock write access to the
flash. On some targets, this function may be empty.

`int hal_flash_write(uint32_t address, const uint8_t *data, int len)`

This function provides an implementation of the flash write function, using the
target's IAP interface. `address` is the offset from the beginning of the
flash area, `data` is the payload to be stored in the flash using the IAP interface,
and `len` is the size of the payload. `hal_flash_write` should return 0 upon success,
or a negative value in case of failure.

`void hal_flash_lock(void)`

If the IAP interface of the flash memory requires locking/unlocking, this function
restores the flash write protection by excluding write accesses. This function is called
by the bootloader at the end of every write and erase operations.

`int hal_flash_erase(uint32_t address, int len)`

Called by the bootloader to erase part of the flash memory to allow subsequent boots.
Erase operations must be performed via the specific IAP interface of the target microcontroller.
`address` marks the start of the area that the bootloader wants to erase, and `len` specifies
the size of the area to be erased. This function must take into account the geometry of the flash
sectors, and erase all the sectors in between.

`void hal_prepare_boot(void)`

This function is called by the bootloader at a very late stage, before chain-loading the firmware
in the next stage. This can be used to revert all the changes made to the clock settings, to ensure
that the state of the microcontroller is restored to its original settings.

### Optional support for external flash memory

WolfBoot can be compiled with the makefile option `EXT_FLASH=1`. When the external flash support is
enabled, update and swap partitions can be associated to an external memory, and will use alternative
HAL function for read/write/erase access. 
To associate the update or the swap partition to an external memory, define `PART_UPDATE_EXT` and/or 
`PART_SWAP_EXT`, respectively.

The following functions are used to access the external memory, and must be defined when `EXT\_FLASH` 
is on:

`int  ext_flash_write(uintptr_t address, const uint8_t *data, int len)`

This function provides an implementation of the flash write function, using the
external memory's specific interface. `address` is the offset from the beginning of the
addressable space in the device, `data` is the payload to be stored,
and `len` is the size of the payload. `ext_flash_write` should return 0 upon success,
or a negative value in case of failure.

`int  ext_flash_read(uintptr_t address, uint8_t *data, int len)`

This function provides an indirect read of the external memory, using the
driver's specific interface. `address` is the offset from the beginning of the
addressable space in the device, `data` is a pointer where payload is stored upon a successful
call, and `len` is the maximum size allowed for the payload. `ext_flash_read` should return 0 
upon success, or a negative value in case of failure.

`int  ext_flash_erase(uintptr_t address, int len)`

Called by the bootloader to erase part of the external memory.
Erase operations must be performed via the specific interface of the target driver (e.g. SPI flash).
`address` marks the start of the area relative to the device, that the bootloader wants to erase, 
and `len` specifies the size of the area to be erased. This function must take into account the 
geometry of the sectors, and erase all the sectors in between.

`void ext_flash_lock(void)`

If the interface of the external flash memory requires locking/unlocking, this function
may be used to restore the flash write protection or exclude write accesses. This function is called
by the bootloader at the end of every write and erase operations on the external device.


`void ext_flash_unlock(void)`

If the IAP interface of the external memory requires it, this function
is called before every write and erase operations to unlock write access to the
device. On some drivers, this function may be empty.
