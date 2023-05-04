OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

HEAP_SIZE = 4K; /* heap not used */
STACK_SIZE = 128K;

MEMORY
{
    /* DDR3 - 512MB (offset by destination address and 4KB boot region) */
    DRAM (rwx) : ORIGIN = @WOLFBOOT_STAGE1_LOAD_ADDR@,
                 LENGTH = 0x1FFFFFFF - @WOLFBOOT_STAGE1_LOAD_ADDR@

    /* L1 as SRAM (up to 16KB) */
    RAM (rwx) : ORIGIN = 0xFFD00000, LENGTH = 16K
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

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = _end + HEAP_SIZE + STACK_SIZE );

