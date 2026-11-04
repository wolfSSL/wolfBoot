OUTPUT_ARCH(rx)
ENTRY(_PowerON_Reset)

MEMORY
{
    /* Do not use 0x0 RAM base, as it will fail NULL checks */
    RAM : ORIGIN = 0x10, LENGTH = 16384 - 0x10
    ROM : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
}

SECTIONS
{
    .fvectors :
    {
        KEEP(*(.fvectors))
    } > ROM

    .exvectors :
    {
        KEEP(*(.exvectors))
    } > ROM

    .rvectors :
    {
        _rvectors_start = .;
        KEEP(*(.rvectors))
        _rvectors_end = .;
    } > ROM

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
        . = ALIGN(4);
        _mdata = .;
    } > ROM

    .data : AT(_mdata)
    {
        _data = .;
        . = ALIGN(4);
        KEEP(*(.ramcode))
        *(.data)
        *(.data.*)
        *(D)
        *(D_1)
        *(D_2)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
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
}

/* The test-app runs on the interrupt stack (PSW.U stays 0) */
PROVIDE(_istack  = ORIGIN(RAM) + LENGTH(RAM) );
PROVIDE(_ustack  = _istack );
ASSERT(_istack - _end >= 0x1000, "less than 4KB of RAM left for the test-app stack")

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
