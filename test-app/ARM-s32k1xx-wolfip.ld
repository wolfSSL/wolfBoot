/*
 * Linker script for the wolfBoot test application on NXP S32K148/MCXE247 with wolfIP
 *
 * The application runs from the boot partition after the wolfBoot header.
 * RAM is all of SRAM_U, which the ENET DMA uses and is outside the code cache.
 *
 * SRAM is ECC protected and holds no valid ECC after power-on. With
 * APP_RAM_ECC_INIT the reset handler clears [_start_ecc_ram, _end_ecc_ram), all
 * of RAM including the heap and stack, with word stores before the stack is
 * used. This keeps the ECC valid and the ERM free of error events.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x1F000  /* 124KB SRAM_U */
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
        _end = .;
        _end_bss = .;
        __bss_end__ = .;
    } > RAM
}

/* Partition info for libwolfboot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack and heap indicators */
_start_heap = _end;
_end_stack = ORIGIN(RAM) + LENGTH(RAM);

/* RAM cleared at reset with APP_RAM_ECC_INIT */
_start_ecc_ram = ORIGIN(RAM);
_end_ecc_ram = ORIGIN(RAM) + LENGTH(RAM);

/* Keep room above .bss for the stack and the heap used by printf */
ASSERT(_end_stack - _end >= 0x2000, "not enough RAM left for the stack")
