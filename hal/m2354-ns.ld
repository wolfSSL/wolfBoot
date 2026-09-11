MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    /* SRAM bank 0 only: banks 1 and 2 are unclocked at reset, so the
     * bootloader stack must not live above 0x20008000. hal_init() enables
     * the other banks for the application. */
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00008000 /* 32 KB, bank 0 */
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

    /* ARM exception index. ".edidx" is the wolfBoot convention for this
     * output section; it is empty for bare-metal C with no unwinding. */
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    .keystore :
    {
        . = ALIGN(4);
        KEEP(*(.keystore*))
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
