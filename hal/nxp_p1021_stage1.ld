OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* Adjust base address to 0xF8F80000 is debugging (run from L2 cache) */
/* Boot ROM out of reset mapped to 0xFFFFF000 */
BASE_ADDR = 0xFFFFF000; /* 0xF8F80000 */

/* Boot ROM requires it must be < 4KB */
/* If debugging this can be increased */
LOADER_STAGE1_SIZE = @WOLFBOOT_STAGE1_SIZE@; /* 0x4000 */

/* Boot initialization code */
BOOTSTRAP_TLB = BASE_ADDR;

/* Entry point where boot executes from (end of region) */
BOOTSTRAP_ENTRY = BASE_ADDR + LOADER_STAGE1_SIZE - 4;

MEMORY
{
    /* Boot Location */
    FLASH (rx) : ORIGIN = BOOTSTRAP_TLB, LENGTH = LOADER_STAGE1_SIZE

    /* L1 as SRAM (up to 16KB) */
    L1RAM (rwx) : ORIGIN = 0xFFD00000, LENGTH = 16K
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

        /* For PIC the linker script variables come from here */
        *(.got2)
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
    } > L1RAM

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
    } > L1RAM
}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(L1RAM) + (LENGTH(L1RAM)) );
