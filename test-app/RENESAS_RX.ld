MEMORY
{
    RAM : ORIGIN = 0x0, LENGTH = 262144 - 16384
    STACK : ORIGIN = 0x3C000, LENGTH = 16384
    RAM2 : ORIGIN = 0x00800000, LENGTH = 393216
    ROM : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@ /* 0xFFE00000 */, LENGTH = @WOLFBOOT_TEST_APP_SIZE@ /* 2097152 */
    OFS : ORIGIN = 0xFE7F5D00, LENGTH = 128
}

SECTIONS
{
    .vectors (0xFFFFFFD0) :
    {
        PROVIDE (__vectors = .);
    }

    .text :
    {
        *(.text)
        *(.text.*)
        *(P)
        etext = .;
    } > ROM

    .init :
    {
        KEEP(*(.init))
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
        __init_array_start = (. + 3) & ~ 3;
        KEEP(*(.init_array))
        KEEP(*(SORT(.init_array.*)))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end = .;
    } > ROM

    .fini :
    {
        KEEP(*(.fini))
    } > ROM

    .got :
    {
        *(.got)
        *(.got.plt)
    } > ROM

    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
        *(C_1)
        *(C_2)
        *(C)
        _erodata = .;
    } > ROM

    gcc_exceptions_table :
    {
        KEEP (*(.gcc_except_table))
        *(.gcc_except_table.*)
    } > ROM

    .eh_frame_hdr :
    {
        *(.eh_frame_hdr)
    } > ROM

    .eh_frame :
    {
        *(.eh_frame)
    } > ROM

    .jcr :
    {
        *(.jcr)
    } > ROM

    .tors :
    {
        __CTOR_LIST__ = .;
        . = ALIGN(2);
        ___ctors = .;
        *(.ctors)
        ___ctors_end = .;
        __CTOR_END__ = .;
        __DTOR_LIST__ = .;
        ___dtors = .;
        *(.dtors)
        ___dtors_end = .;
        __DTOR_END__ = .;
        . = ALIGN(2);
        _mdata = .;
        PROVIDE(__romdatastart = .);
    } > ROM

    .ustack :
    {
        __stack = .;
    } > STACK

    .data : AT(_mdata)
    {
        . = ALIGN(4);
        PROVIDE (__datastart = .);
        _data = .;
        *(.data)
        *(.data.*)
        *(D)
        *(D_1)
        *(D_2)
        _edata = .;
    } > RAM
    PROVIDE (__romdatacopysize = SIZEOF(.data));

    .bss :
    {
        . = ALIGN(4);
        PROVIDE (__bssstart = .);
        _bss = .;
        *(.bss)
        *(.bss.**)
        *(COMMON)
        *(B)
        *(B_1)
        *(B_2)
        _ebss = .;
        . = ALIGN(128);
        _end = .;
    } > RAM
    PROVIDE (__bsssize = SIZEOF(.bss) / 4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
PROVIDE(stack = END_STACK);