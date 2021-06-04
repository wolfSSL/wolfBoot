OUTPUT_ARCH( "powerpc" )

ENTRY( main )

PHDRS
{
    text PT_LOAD;
    bss  PT_LOAD;
}

SECTIONS
{
    . = ##WOLFBOOT_TEST_APP_ADDRESS##;

    .text :
    {
       *(.text.main)
        *(.text*)
        *(.rodata*)
    } :text

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    }

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
    }
    . = ALIGN(4);

}
