OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

MEMORY
{
    /* DDR4 - 8GB physical (32-bit addressing limits window to <2GB) */
    DRAM (rwx) : ORIGIN = @WOLFBOOT_STAGE1_LOAD_ADDR@,
                 LENGTH = 0x7FFFFFFF - 4K - @WOLFBOOT_STAGE1_LOAD_ADDR@

    /* L1 locked dcache - 16KB */
    L1RAM (rwx) : ORIGIN = 0xFDFC0000, LENGTH = 0x4000

    /* CPC SRAM - 256KB */
    CPCSRAM (rwx) : ORIGIN = 0xFDFE0000, LENGTH = 0x40000
}

SECTIONS
{
    /* boot code boot_ppc_start.S for _reset */
    .boot :
    {
        KEEP(*(.boot))
    } > DRAM
    . = ALIGN(4);

    /* entry point branch offset to _reset */
    .reset :
    {
        KEEP(*(.reset))
    } > DRAM
    . = ALIGN(4);

    .text :
    {
        _start_vector = .;
        KEEP(*(.isr_vector))
        . = ALIGN(256);
        _start_text = .;
        KEEP(*(.bootmp))
        *(.text*)
        *(.rodata*)
        *(.sdata*)
    } > DRAM

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

/* DDR heap/stack */
PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(DRAM) + (LENGTH(DRAM)));

/* CPC SRAM heap/stack (unused - wolfBoot runs from DDR) */
/* PROVIDE(_start_heap = ORIGIN(CPCSRAM)); */
/* PROVIDE(_end_stack  = ORIGIN(CPCSRAM) + (LENGTH(CPCSRAM))); */
