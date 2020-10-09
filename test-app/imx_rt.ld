MEMORY
{
    FLASH (rx) : ORIGIN = ##WOLFBOOT_TEST_APP_ADDRESS##, LENGTH = ##WOLFBOOT_TEST_APP_SIZE##
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x00010000
}

SECTIONS
{
    __NCACHE_REGION_START = ORIGIN(RAM);
    __NCACHE_REGION_SIZE  = 0;
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.init)
        *(.fini)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .ARM :
    {
      __exidx_start = .;
      *(.ARM.exidx*)
      __exidx_end = .;
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

    .bss :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > RAM
}

PROVIDE(_start_heap = _end);
PROVIDE(end = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + LENGTH(RAM));
