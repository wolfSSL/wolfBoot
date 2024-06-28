OUTPUT_ARCH(rx)
ENTRY(_PowerON_Reset)

MEMORY
{
    /* Do not use 0x0 RAM base, as it will fail NULL checks */
    RAM : ORIGIN = 0x1000, LENGTH = 262144 - 0x1000
    RAM2 : ORIGIN = 0x00800000, LENGTH = 393216
    ROM : ORIGIN = @WOLFBOOT_ORIGIN@ /* 0xFFE00000 */, LENGTH = @BOOTLOADER_PARTITION_SIZE@ /* 2097152 */
    OFS : ORIGIN = 0xFE7F5D00, LENGTH = 128
}

SECTIONS
{
    .exvectors 0xFFFFFF80:
    {
        KEEP(*(.exvectors))
    } = 0x7F
    . = ALIGN(4);

    .fvectors 0xFFFFFFFC:
    {
        KEEP(*(.fvectors))
    } = 0x4

    .rot 0xFFFF0000 :
    {
        /* Reserved space for Wrapped Public Key (See docs/Renesas.md) */
        KEEP(*(.rot))
    } = 0x2000 /* 8KB (min sector size) */

    .rvectors ORIGIN(ROM) :
    {
        _rvectors_start = .;
        KEEP(*(.rvectors))
        _rvectors_end = .;
    } > ROM

    .text :
    {
        _start_text = .;
        __start_text = .;
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
        _edata = .;
    } > RAM

    .bss :
    {
        _bss = .;
        *(.dynbss)
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

    .ofs1 0xFE7F5D00: AT(0xFE7F5D00)
    {
        KEEP(*(.ofs1))
    } > OFS
    .ofs2 0xFE7F5D10: AT(0xFE7F5D10)
    {
        KEEP(*(.ofs2))
    } > OFS
    .ofs3 0xFE7F5D20: AT(0xFE7F5D20)
    {
        KEEP(*(.ofs3))
    } > OFS
    .ofs4 0xFE7F5D40: AT(0xFE7F5D40)
    {
        KEEP(*(.ofs4))
    } > OFS
    .ofs5 0xFE7F5D48: AT(0xFE7F5D48)
    {
        KEEP(*(.ofs5))
    } > OFS
    .ofs6 0xFE7F5D50: AT(0xFE7F5D50)
    {
        KEEP(*(.ofs6))
    } > OFS
    .ofs7 0xFE7F5D64: AT(0xFE7F5D64)
    {
        KEEP(*(.ofs7))
    } > OFS
    .ofs8 0xFE7F5D70: AT(0xFE7F5D70)
    {
        KEEP(*(.ofs8))
    } > OFS
}


/* reserve 16KB user stack */
PROVIDE(_ustack  = ORIGIN(RAM) + LENGTH(RAM) - 0x4000 );
/* reserve 4KB interrupt stack */
PROVIDE(_istack  = ORIGIN(RAM) + LENGTH(RAM) - 0x4000 - 0x1000 );

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
