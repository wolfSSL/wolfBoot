/* wolfBoot linker script for STM32N6
 *
 * wolfBoot runs from SRAM (Boot ROM copies FSBL from external NOR flash).
 * FLASH region is actually AXISRAM1 at 0x34000000.
 * RAM is placed higher in SRAM for stack/heap.
 */

MEMORY
{
    FLASH (rwx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_data = .;
    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > FLASH

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
    } > FLASH
    . = ALIGN(4);
}

END_STACK = ORIGIN(FLASH) + LENGTH(FLASH);
