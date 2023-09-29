OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* Boot ROM out of reset mapped to 0xEFFFF000 */
BASE_ADDR = @WOLFBOOT_STAGE1_FLASH_ADDR@;

LOADER_STAGE1_SIZE = @WOLFBOOT_STAGE1_SIZE@;

/* Boot initialization code */
BOOTSTRAP_TLB = 0xEFFFF000;

/* Entry point where RCW directs code to execute from */
BOOTSTRAP_ENTRY = 0xEFFFFFFC;

MEMORY
{
    /* Boot Location */
    FLASH (rx) : ORIGIN = BASE_ADDR, LENGTH = LOADER_STAGE1_SIZE

    /* L1 SRAM - 16KB */
    L1RAM (rwx) : ORIGIN = 0xF8F80000, LENGTH = 0x4000

    /* L2 SRAM - 160KB */
    L2RAM (rwx) : ORIGIN = 0xFDFC0000, LENGTH = 0x28000

    /* DDR - 2GB: Start at 16MB to avoid using 0x0 (NULL) addresses */
    DRAM (rwx) : ORIGIN = 0x1000000, LENGTH = 0x7FFFFFFF - 0x1000000
}

SECTIONS
{
    /* boot code boot_ppc_start.S for _reset */
    .boot BOOTSTRAP_TLB :
    {
        KEEP(*(.boot))
    } = 0xFFFC
    . = ALIGN(4);

    /* entry point branch offset to _reset */
    .reset BOOTSTRAP_ENTRY :
    {
        KEEP(*(.reset))
    } = 0x4
    . = ALIGN(4);

    .text :
    {
        _start_vector = .;
        KEEP(*(.isr_vector))
        . = ALIGN(256);
        _start_text = .;
        *(.text*)
        *(.rodata*)
        *(.sdata*)
    } > FLASH

    /* Read-only sections, merged into text segment: */
    .interp         : { *(.interp) }
    .hash           : { *(.hash) }
    .dynsym         : { *(.dynsym) }
    .dynstr         : { *(.dynstr) }
    .gnu.version    : { *(.gnu.version) }
    .gnu.version_r  : { *(.gnu.version_r) }
    .gnu.hash       : { *(.gnu.hash) }
    .rela.dyn       : { *(.rela.dyn) }


    .reloc :
    {
        _GOT2_TABLE_ = .;
        *(.got2)
        _FIXUP_TABLE_ = .;
        *(.fixup)
    } > FLASH
    __got2_entries = (_FIXUP_TABLE_ - _GOT2_TABLE_) >> 2;
    __fixup_entries = (. - _FIXUP_TABLE_) >> 2;

    __init_end = .;
    . = ALIGN(8);
    _stored_data = .;

    .data : AT (_stored_data)
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
        . = ALIGN(16);
        _end = .;
    } > DRAM

}

PROVIDE(_start_heap = ORIGIN(L2RAM));
PROVIDE(_end_stack  = ORIGIN(L2RAM) + (LENGTH(L2RAM)) );
