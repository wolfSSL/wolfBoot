/*
 * Linker script for wolfBoot test application on NXP S32K1xx
 *
 * Application runs from boot partition after wolfBoot header
 *
 * S32K142 SRAM Layout (32KB total, split across two regions):
 *   SRAM_L: 0x1FFFC000 - 0x1FFFFFFF (16 KB) - Lower SRAM
 *   SRAM_U: 0x20000000 - 0x20002FFF (12 KB) - Upper SRAM
 *
 * Using upper SRAM for application data/bss/stack
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x3000  /* 12KB upper SRAM */
}

SECTIONS
{
    .text :
    {
        _start_text = .;
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



/* Partition info for libwolfboot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack and heap indicators */
_start_heap = _end;
_end_stack = ORIGIN(RAM) + LENGTH(RAM);

