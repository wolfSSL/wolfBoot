MEMORY
{
    FLASH (rx) : ORIGIN = 0x8040100, LENGTH = 0x1ff00
    RAM (rwx) : ORIGIN = 0x20020000, LENGTH = 128K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        . = ALIGN(8);
        KEEP(*(.isr_vector))
        . = ALIGN(8);
        *(.init)
        *(.fini)
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
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
        . = ALIGN(8);
        KEEP(*(.ramcode))
        . = ALIGN(8);
        _end_data = .;
    } > RAM

    .bss :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
        _end = .;
    } > RAM
}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + LENGTH(RAM));

/* Emu app startup expects these symbols. */
_estack = _end_stack;
_sidata = _stored_data;
_sdata = _start_data;
_edata = _end_data;
_sbss = _start_bss;
_ebss = _end_bss;
