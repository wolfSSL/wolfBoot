MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx) : ORIGIN = 0x20018000, LENGTH = 16K /* Run in lowmem */
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
