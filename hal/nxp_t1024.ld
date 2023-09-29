OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

MEMORY
{
    /* DDR4 - 2GB (offset by destination address and 4KB boot region) */
    DRAM (rwx) : ORIGIN = @WOLFBOOT_STAGE1_LOAD_ADDR@,
                 LENGTH = 0x7FFFFFFF - @WOLFBOOT_STAGE1_LOAD_ADDR@

    /* L1 SRAM - 16KB */
    L1RAM (rwx) : ORIGIN = 0xF8F80000, LENGTH = 0x4000

    /* Platform SRAM - 160KB */
    PSRAM (rwx) : ORIGIN = 0xFDFC0000, LENGTH = 0x28000
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

/* Platform SRAM heap/stack */
/* PROVIDE(_start_heap = ORIGIN(PSRAM)); */
/* PROVIDE(_end_stack  = ORIGIN(PSRAM) + (LENGTH(PSRAM))); */
