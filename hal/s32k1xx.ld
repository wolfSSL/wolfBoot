/*
 * Linker script for wolfBoot on NXP S32K1xx
 *
 * S32K142: 256KB Flash (0x00000000 - 0x0003FFFF), 32KB SRAM (split)
 *
 * SRAM Layout (S32K142 - 32KB total, split across two regions):
 *   SRAM_L: 0x1FFFC000 - 0x1FFFFFFF (16 KB) - Lower SRAM
 *   SRAM_U: 0x20000000 - 0x20002FFF (12 KB) - Upper SRAM
 *
 * Flash Memory Layout:
 *   0x00000000 - 0x0000BFFF: Bootloader (48 KB)
 *   0x0000C000 - 0x00024FFF: Boot Partition (100 KB)
 *   0x00025000 - 0x0003DFFF: Update Partition (100 KB)
 *   0x0003E000 - 0x0003E7FF: Swap Sector (2 KB)
 *
 * Note: Using SRAM_L for data/bss and SRAM_U for stack
 *       Stack grows down from 0x20003000
 */

MEMORY
{
    FLASH (rx)   : ORIGIN = 0x00000000, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    SRAM_L (rwx) : ORIGIN = 0x1FFFC000, LENGTH = 0x4000  /* 16KB lower SRAM */
    SRAM_U (rwx) : ORIGIN = 0x20000000, LENGTH = 0x3000  /* 12KB upper SRAM */
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))

        /* Flash Configuration Field (FCF) at 0x400 */
        . = 0x400;
        KEEP(*(.flash_config))
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
    } > SRAM_L

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
    } > SRAM_L
    . = ALIGN(4);
}

/* Partition addresses for wolfBoot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack at end of SRAM_U (grows down from 0x20003000) */
END_STACK = ORIGIN(SRAM_U) + LENGTH(SRAM_U);
