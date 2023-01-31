OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

ENTRY( _vector_table )

MEMORY
{
    /*The flash address range on LS1028A RDB is 0x20000000 - 0x23FFFFFF.*/
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = 0x100000
    /* DDR4 - 2GB */
    DRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0x80000000
    /* OCRAM 256K for startup RAM */
    OCRAM (rwx) : ORIGIN = 0x18000000, LENGTH = 256K
}

SECTIONS
{
    PROVIDE (_DDR_ADDRESS = 0x80000000);

    /* Stack size per core */
    PROVIDE (STACK_SIZE = 0x1000);

    .boot :
    {
        PROVIDE(_vector_table = .);
        ._vector_table = .;
        . = ALIGN(8);
        KEEP(*(.vector_table))
        KEEP(*(.boot*))
    } > OCRAM

    .text :
    {
        _start_text = .;
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(8);
        _end_text = .;
    } > OCRAM

    .edidx :
    {
        . = ALIGN(8);
        *(.ARM.exidx*)
    } > OCRAM

    /* Read-only sections, merged into text segment: */
    .interp         : { *(.interp) }
    .hash           : { *(.hash) }
    .dynsym         : { *(.dynsym) }
    .dynstr         : { *(.dynstr) }
    .gnu.version    : { *(.gnu.version) }
    .gnu.version_r  : { *(.gnu.version_r) }
    .gnu.hash       : { *(.gnu.hash) }
    .rela.dyn       : { *(.rela.dyn) }

    _stored_data = .;

    .data : AT (_stored_data)
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

    /* Stack */
    .stack :
    {
        . = ALIGN(16);
        . += STACK_SIZE;
        _stack = .;
        *(.stack)
    } > OCRAM

    PROVIDE(_stack_top = ADDR(.stack));
    PROVIDE(_stack_base = .);
}