OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* Boot ROM out of reset mapped to 0xFFFFF000 */
/* The eLBC FCM buffer TLB/LAW is mapped to 0xFFF00000 */
/* For debugging use L2SRAM base address 0xF8F80000 */
BASE_ADDR = 0xFFFFF000;

/* Boot ROM requires it must be < 4KB */
/* If debugging this can be increased (0x4000) */
LOADER_STAGE1_SIZE = @WOLFBOOT_STAGE1_SIZE@;

/* Boot initialization code */
BOOTSTRAP_TLB = BASE_ADDR;

/* Entry point where boot executes from (end of region) */
BOOTSTRAP_ENTRY = BASE_ADDR + LOADER_STAGE1_SIZE - 4;

MEMORY
{
    /* Boot Location */
    FLASH (rx) : ORIGIN = BOOTSTRAP_TLB, LENGTH = LOADER_STAGE1_SIZE

    /* DDR3 - 512MB (for loader use 16MB offset and 64KB section) */
    DRAM (rwx) : ORIGIN = 0x1000000, LENGTH = 64K

    /* L1 as SRAM (up to 16KB) */
    L1RAM (rwx) : ORIGIN = 0xFFD00000, LENGTH = 16K

    /* L2 as SRAM (up to 256KB) */
    L2RAM (rwx) : ORIGIN = 0xF8F80000, LENGTH = 256K
}

SECTIONS
{
    /* boot code boot_ppc_start.S for _reset */
    .boot :
    {
        KEEP(*(.boot))
        _start_vector = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        *(.sdata*)
        *(.dynamic)
        *(.glink)
    } > FLASH
    . = ALIGN(4);

    /* Read-only sections, merged into text segment: */
    .interp         : { *(.interp) }
    .hash           : { *(.hash) }
    .dynsym         : { *(.dynsym) }
    .dynstr         : { *(.dynstr) }
    .gnu.version    : { *(.gnu.version) }
    .gnu.version_r  : { *(.gnu.version_r) }
    .gnu.hash       : { *(.gnu.hash) }
    .rela.dyn       : { *(.rela.dyn) }
    .plt            : { *(.plt) }

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

    /* entry point branch offset to _reset */
    .reset BOOTSTRAP_ENTRY :
    {
        KEEP(*(.reset))
    } = 0x4
    . = ALIGN(4);

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

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(L1RAM) + (LENGTH(L1RAM)) );
