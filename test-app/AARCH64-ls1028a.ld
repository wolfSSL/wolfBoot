MEMORY
{
    FLASH (rx) : = ##WOLFBOOT_TEST_APP_ADDRESS##, LENGTH = ##WOLFBOOT_TEST_APP_SIZE##
    DRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0xffffffff
    OCRAM (rwx) : ORIGIN = 0x18000000, LENGTH = 256K
}

ENTRY(main);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > OCRAM

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > OCRAM

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > OCRAM

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
    } > OCRAM
    . = ALIGN(4);
}

END_STACK = _start_text;
