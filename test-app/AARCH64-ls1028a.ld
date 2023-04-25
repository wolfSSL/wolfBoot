MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = 256K
    DRAM (rwx) : ORIGIN = 0x80001000 , LENGTH = 0xBFFFFFFF
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
    } > DRAM

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > DRAM

    PROVIDE(_stored_data = .); 

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DRAM

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
    } > DRAM
    . = ALIGN(4);
}

END_STACK = _start_text;
