/*
 * Linker script for wolfBoot on MAX32665/MAX32666
 *
 * MAX32666: 1MB Flash (0x10000000 - 0x100FFFFF), 560KB SRAM
 *
 * Flash Memory Layout:
 *   0x10000000 - 0x10007FFF: Bootloader (32 KB)
 *   0x10008000 - 0x10047FFF: Boot Partition (256 KB)
 *   0x10048000 - 0x10087FFF: Update Partition (256 KB)
 *   0x10088000 - 0x10089FFF: Swap Sector (8 KB)
 */

MEMORY
{
    FLASH (rx) : ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x8C000  /* 560KB SRAM */
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
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

/* Partition addresses for wolfBoot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack at end of SRAM (grows down) */
END_STACK = ORIGIN(RAM) + LENGTH(RAM);
