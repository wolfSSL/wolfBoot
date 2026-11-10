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

Please see [Targets](Targets.md)


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
and `len` is the size of the payload. Implementations of this function must be able to
handle writes of any size and alignment. Targets with a minimum programmable size
\> 1 byte must implement the appropriate read-modify-write logic in order to enable
wolfBoot to perform unaligned single-byte writes, unless wolfBoot is built with
`NVM_FLASH_ECC=1` (see below). `hal_flash_write` should return 0 upon
success, or a negative value in case of failure.

`void hal_flash_lock(void)`

If the IAP interface of the flash memory requires locking/unlocking, this function
restores the flash write protection by excluding write accesses. This function is called
by the bootloader at the end of every write and erase operations.

`int hal_flash_erase(uint32_t address, int len)`

Called by the bootloader to erase part of the flash memory to allow subsequent boots.
Erase operations must be performed via the specific IAP interface of the target microcontroller.
`address` marks the start of the area that the bootloader wants to erase, and `len` specifies
the size of the area to be erased. `address` is guaranteed to be aligned to `WOLFBOOT_SECTOR_SIZE`,
and `len` is guaranteed to be a multiple of `WOLFBOOT_SECTOR_SIZE`. This function must take into account
the geometry of the flash sectors, and erase all the sectors in between. `hal_flash_erase` should
return 0 upon success, or a negative value in case of failure.

The secure PKCS#11 keyvault (`WOLFCRYPT_TZ_PKCS11=1`) reports a failed erase or write to wolfPKCS11,
which then writes all of its records again. Both functions should therefore check the flash
controller's error flags after each operation, and fail for an address outside the flash.

`void hal_prepare_boot(void)`

This function is called by the bootloader at a very late stage, before chain-loading the firmware
in the next stage. This can be used to revert all the changes made to the clock settings, to ensure
that the state of the microcontroller is restored to its original settings. By default most targets will restore the
clock settings. Use the `WOLFBOOT_RESTORE_CLOCK=0` option to disable clock restoration.

`int hal_flash_is_erased(haladdr_t address, int len)`

Optional and only relevant for devices with hardware erase-check functionality.

Returns 1 if every byte in `[address, address + len)` is erased, 0 if any byte is not erased, or a negative value on error.
wolfBoot uses this function to identify erased flash when erased state cannot be determined reliably by reading flash directly.
The trailer journal (`NVM_FLASH_JOURNAL`) uses it to find free record slots, and `NVM_FLASH_ECC` uses it for every erased check.

If not overridden, the weak default in `src/libwolfboot.c` reads the flash and compares it against `FLASH_BYTE_ERASED`.
Override this function on platforms where reading erased flash is unreliable or unsafe. For example, with some ECC flash devices,
reading an erased page may fault or return invalid data, while a programmed unit may still contain the erased value.

`ext_flash_is_erased()` provides the equivalent check for external flash.


### ECC flash (`NVM_FLASH_ECC`) HAL Requirements

When using `NVM_FLASH_ECC`, `WOLFBOOT_FLASH_WRITE_UNIT` must be defined to the HAL's smallest programmable unit, in bytes (default 1).

In order to respect the rules of most ECC flash, wolfBoot expects the HAL to:

1. Program whole, aligned write units, and fail a write to a unit that is not erased without changing it.
2. Erase sector-aligned ranges only.
3. Not trap on a read of erased flash, though the data may be garbage.
4. Report erased units reliably in `hal_flash_is_erased()`.

wolfBoot then never programs a unit twice, never erases part of a sector, and decides nothing from a read of erased flash.

### Optional support for external flash memory

WolfBoot can be compiled with the makefile option `EXT_FLASH=1`. When the external flash support is
enabled, update and swap partitions can be associated to an external memory, and will use alternative
HAL function for read/write/erase access. It can also be used in any scenario where flash reads require
special handling and must be redirected to a custom implementation. Note that `EXT_FLASH=1` is incompatible
with the `NVM_FLASH_WRITEONCE` option: use `NVM_FLASH_JOURNAL` instead.

To associate the update or the swap partition to an external memory, define `PART_UPDATE_EXT` and/or
`PART_SWAP_EXT`, respectively.

The following functions are used to access the external memory, and must be defined when `EXT_FLASH`
is on:

`int  ext_flash_write(uintptr_t address, const uint8_t *data, int len)`

This function provides an implementation of the flash write function, using the
external memory's specific interface. `address` is the offset from the beginning of the
addressable space in the device, `data` is the payload to be stored,
and `len` is the size of the payload. The function is subject to the same restrictions as
`hal_flash_write()`. `ext_flash_write` should return 0 upon success,
or a negative value in case of failure.

`int  ext_flash_read(uintptr_t address, uint8_t *data, int len)`

This function provides an indirect read of the external memory, using the
driver's specific interface. `address` is the offset from the beginning of the
addressable space in the device, `data` is a pointer where payload is stored upon a successful
call, and `len` is the maximum size allowed for the payload. This function must be able to handle
reads of any size and alignment. `ext_flash_read` should return the number of bytes read
on success, or a negative value in case of failure.

`int  ext_flash_erase(uintptr_t address, int len)`

Called by the bootloader to erase part of the external memory.
Erase operations must be performed via the specific interface of the target driver (e.g. SPI flash).
`address` marks the start of the area relative to the device, that the bootloader wants to erase,
and `len` specifies the size of the area to be erased. This function is subject to the same restrictions
as `hal_flash_erase()` and must take into account the geometry of the sectors, and erase all the sectors
in between.

`void ext_flash_lock(void)`

If the interface of the external flash memory requires locking/unlocking, this function
may be used to restore the flash write protection or exclude write accesses. This function is called
by the bootloader at the end of every write and erase operations on the external device.


`void ext_flash_unlock(void)`

If the IAP interface of the external memory requires it, this function
is called before every write and erase operations to unlock write access to the
device. On some drivers, this function may be empty.


### Additional functions required by `DUALBANK_SWAP` option

If the target device supports hardware-assisted bank swapping, it is appropriate
to provide two additional functions in the port:

`void hal_flash_dualbank_swap(void)`

Called by the bootloader when the two banks must be swapped. On some architectures
this operation implies a reboot, so this function may also never return.


`void fork_bootloader(void)`

This function is called to provide a second copy of the bootloader. Wolfboot will
clone itself if the content does not already match. `fork_bootloader()`
implementation in new ports must return immediately without performing any actions
if the content of the bootloader partition in the two banks already match.


### Optional support for non-coherent DMA

`int hal_dma_set_noncached(uintptr_t start, uintptr_t end)`

Re-attribute `[start, end)` as non-cacheable. Only needed by ports that run with the MMU and D-cache enabled and that hand memory to a bus master which is not coherent with the CPU caches, such as an Ethernet MAC driven from a hook. Returns 0 on success, or negative if the port cannot satisfy the request.

A weak default in `src/libwolfboot.c` returns an error, so the symbol always links. It deliberately does **not** succeed silently: a no-op would leave the caller sharing write-back memory with a non-coherent master, which is the failure this function exists to prevent.

A port implementing it must:

- Round the range **outward** to whatever granule its translation tables can express. On ZynqMP (`hal/zynq.c`) that is a 2MB block, so the caller has to give the region an aligned block of its own rather than placing it next to other data.
- Clean and invalidate the affected range **before** changing the attribute, so a line still dirty at the switch cannot later be written back over what the bus master has since put there.
- Push the modified table entries out and invalidate the TLB at the exception level that owns the translation.

Callers place their buffers with a dedicated linker section. The ZynqMP port provides `.dma_buffers` in `hal/zynq.ld`, based at `WOLFBOOT_DMA_BUFFER_ADDRESS` (default `0x8200000`, clear of the kernel, the FIT staging area and the DTS) and exporting `_dma_buffers_start` / `_dma_buffers_end`.

Because the attribute change unmaps and remaps whole blocks, a range outside that window would take the bootloader's own code or stack down with it. The ZynqMP port therefore rejects any range not contained in `[_dma_buffers_start, _dma_buffers_end)`, and the linker script refuses at build time to place the window unaligned or overlapping wolfBoot's own DDR block. A port implementing this function for other hardware should fail closed in the same way rather than trusting the caller.

### wolfHSM HAL extensions

Refer to [wolfHSM.md](wolfHSM.md) for the wolfHSM-specific HAL functions and an overview of wolfHSM compatibility.
