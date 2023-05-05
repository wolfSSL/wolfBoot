OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* Adjust base address to 0xF8F80000 is debugging (run from L2 cache) */
BASE_ADDR = @WOLFBOOT_STAGE1_BASE_ADDR@;

/* for flashing to NAND it must be 4KB */
LOADER_STAGE1_SIZE = @WOLFBOOT_STAGE1_SIZE@;

/* Boot initialization code */
BOOTSTRAP_TLB = BASE_ADDR;

/* Entry point where boot executes from (end of region) */
BOOTSTRAP_ENTRY = BASE_ADDR + LOADER_STAGE1_SIZE - 4;

MEMORY
{
    /* Boot Location */
    FLASH (rx) : ORIGIN = BOOTSTRAP_TLB, LENGTH = LOADER_STAGE1_SIZE

    /* L1 as SRAM (up to 16KB) */
    RAM (rwx) : ORIGIN = 0xffd00000, LENGTH = 16K
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
    } > RAM

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
    } > RAM
}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + (LENGTH(RAM)) );
