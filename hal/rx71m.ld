OUTPUT_ARCH(rx)
ENTRY(_PowerON_Reset)

MEMORY
{
    /* Do not use 0x0 RAM base, as it will fail NULL checks */
    RAM : ORIGIN = 0x1000, LENGTH = 524288 - 0x1000
    ROM : ORIGIN = @WOLFBOOT_ORIGIN@ /* 0xFFFF0000 */, LENGTH = @BOOTLOADER_PARTITION_SIZE@ /* 65536 */
    OFS : ORIGIN = 0x00120040, LENGTH = 48
}

SECTIONS
{
    /* Fixed words including ROMCODE at 0xFFFFFF9C */
    .romcode 0xFFFFFF80:
    {
        KEEP(*(.romcode))
    } = 0xFF
    ASSERT(SIZEOF(.romcode) == 0x7C, "RomCodeArea[] must fill 0xFFFFFF80-0xFFFFFFFB")
    . = ALIGN(4);

    .fvectors 0xFFFFFFFC:
    {
        KEEP(*(.fvectors))
    } = 0x4

    .rvectors ORIGIN(ROM) :
    {
        _rvectors_start = .;
        KEEP(*(.rvectors))
        _rvectors_end = .;
    } > ROM

    .exvectors : ALIGN(128)
    {
        KEEP(*(.exvectors))
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

    /* Pin .bss LMA to its VMA; else it chains past ROM at the top of the
     * address space and newer ld errors "LMA wraps around address space". */
    .bss (NOLOAD) : AT (ADDR (.bss))
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

    /* Option-setting memory (SPCC to OFS1), see WOLFBOOT_RENESAS_OSM */
    .ofs_rx71m 0x00120040: AT(0x00120040)
    {
        KEEP(*(.ofs_rx71m))
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
