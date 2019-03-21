# Compiling wolfBoot

WolfBoot is portable across different types of embedded systems. The platform-specific code
is contained in a single file under the `hal` directory, and implements the hardware-specific functions.

To enable specific compile options, use environment variables while calling make, e.g.

`make CORTEX_M0=1`

## Platform selection

If supported natively, the target platform can be specified using the `TARGET` variable.
Make will automatically select the correct compile option, and include the corresponding HAL for
the selected target. 

For a list of the platforms currently supported, see the [HAL documentation](./HAL.md).

To add a new platform, simply create the corresponding HAL driver and linker script file 
in the [hal](../hal) directory.

Default option if none specified: `TARGET=stm32f4`

Some platform will require extra options, specific for the architecture.
By default, wolfBoot is compiled for ARM Cortex-M3/4/7. To compile for Cortex-M0, use:

`CORTEX_M0=1`

### Flash partitions

The file [include/target.h](../include/target.h) must be modified according to flash geometry,
partitions size and offset of the target system. The following values must be set to provide the
desired flash configuration:

`WOLFBOOT_SECTOR_SIZE` 

This variable determines the size of the physical sector on the flash memory. If areas with different
block sizes are used for the two partitions (e.g. update partition on an external flash), this variable
should indicate the size of the biggest sector shared between the two partitions.

WolfBoot uses this value as minimum unit when swapping the firmware images in place. For this reason,
this value is also used to set the size of the SWAP partition. 

`WOLFBOOT_PARTITION_BOOT_ADDRESS`

This is the start address of the boot partition, aligned to the beginning of a new flash sector.
The application code starts after a further offset, equal to the partition header size (256B 
for Ed25519 and ECC signature headers).

`WOLFBOOT_PARTITION_UPDATE_ADDRESS`

This is the start address of the update partition. If an external memory is used via the 
`EXT_FLASH` option, this variable contains the offset of the update partition from the
beginning of the external memory addressable space.

`WOLFBOOT_PARTITION_SWAP_ADDRESS`

The address for the swap spaced used by wolfBoot to swap the two firmware images in place,
in order to perform a reversable update. The size of the SWAP partition is exactly one sector on the flash.
If an external memory is used, the variable contains the offset of the SWAP area from the beginning
of its addressable space.

`WOLFBOOT_PARTITION_SIZE`

The size of the BOOT and UPDATE partition. The size is the same for both partitions.

## Bootloader features

A number of characteristics can be turned on/off during wolfBoot compilation. Bootloader size,
performance and activated features are affected by compile-time flags.

### Change DSA algorithm

By default, wolfBoot is compiled to use Ed25519 DSA. The implementation of ed25519 is smaller,
while giving a good compromise in terms of boot-up time.

Better performance can be achieved using ECDSA with curve p-256. To activate ECC256 support, use

`SIGN=ECC256`

when invoking `make`.

The default option, if no value is provided for the `SIGN` variable, is

`SIGN=ED25519`

Changing the DSA algorithm will also result in compiling a different set of tools for key generation
and firmware signature.

Find the corresponding key generation and firmware signing tools in the [tools](../tools) directory.

### Enable debug symbols

To debug the bootloader, simply compile with `DEBUG=1`. The size of the bootloade will increase
consistently, so ensure that you have enough space at the beginning of the flash before 
`WOLFBOOT_PARTITION_BOOT_ADDRESS`.

### Disable interrupt vector relocation

On some platforms, it might be convenient to avoid the interrupt vector relocation before boot-up.
This is required when a component on the system already manages the interrupt relocation at a different 
stage, or on these platform that do not support interrupt vector relocation.

To disable interrupt vector table relocation, compile with `VTOR=0`. By default, wolfBoot will relocate the
interrupt vector by setting the offset in the vector relocation offset register (VTOR).

### Enable workaround for 'write once' flash memories

On some microcontrollers, the internal flash memory does not allow subsequent writes (adding zeroes) to a
sector, after the entire sector has been erased. WolfBoot relies on the mechanism of adding zeroes to the
'flags' fields at the end of both partitions to provide a fail-safe swap mechanism.

To enable the workaround for 'write once' internal flash, compile with

`NVM_FLASH_WRITEONCE=1`

**warning** When this option is enabled, the fail-safe swap is not guaranteed, i.e. the microcontroller
cannot be safely powered down or restarted during a swap operation.

### Allow version roll-back

WolfBoot will not allow updates to a firmware with a version number smaller than the current one. To allow 
downgrades, compile with `ALLOW_DOWNGRADE=1`. 

Warning: this option will disable version checking before the updates, thus exposing the system to potential
forced downgrade attacks.

### Enable optional support for external flash memory

WolfBoot can be compiled with the makefile option `EXT_FLASH=1`. When the external flash support is
enabled, update and swap partitions can be associated to an external memory, and will use alternative
HAL function for read/write/erase access. 
To associate the update or the swap partition to an external memory, define `PART_UPDATE_EXT` and/or 
`PART_SWAP_EXT`, respectively. By default, the makefile assumes that if an external memory is present,
both `PART_UPDATE_EXT` and `PART_SWAP_EXT` are defined.

When external memory is used, the HAL API must be extended to define methods to access the custom memory.
Refer to the [HAL](HAL.md) page for the description of the `ext_flash_*` API.

