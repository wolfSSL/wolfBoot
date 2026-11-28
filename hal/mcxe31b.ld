/*
 * Linker script for wolfBoot on NXP MCXE31B
 *
 * Flash: the boot ROM reads the Image Vector Table (IVT) at 0x00400000 and
 * starts CM7_0 at the vector table it points to (0x00400400 here).
 *
 * RAM: wolfBoot uses only the 64 KB CM7_0 DTCM (0x20000000). DTCM is ECC
 * protected and must be written before it is read; the reset handler clears
 * [_start_ecc_ram, _end_ecc_ram) first (WOLFBOOT_RAM_ECC_INIT). Unlike system
 * SRAM, DTCM accepts 32-bit writes for ECC init and can execute code, so it
 * also holds the RAM_CODE flash routines.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x10000 /* 64 KB DTCM */
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.ivt))
        . = 0x400;
        _start_vector = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > RAM
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

ASSERT(hal_flash_write >= ORIGIN(RAM) && hal_flash_erase >= ORIGIN(RAM),
       "MCXE31B flash routines must run from RAM (RAM_CODE=1)")

_start_ecc_ram = ORIGIN(RAM);
_end_ecc_ram = ORIGIN(RAM) + LENGTH(RAM);

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
