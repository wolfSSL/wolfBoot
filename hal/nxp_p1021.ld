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

    . = ALIGN(8);

    _stored_data = .;
    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
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

/* If relocated to DDR already then use stack end from DDR */
/* If debugging and DDR is not ready, use L1 or L2 */
PROVIDE(_end_stack  = _end + HEAP_SIZE + STACK_SIZE );
/* PROVIDE(_end_stack  = ORIGIN(L1RAM) + (LENGTH(L1RAM)) ); */
/* PROVIDE(_end_stack  = ORIGIN(L2RAM) + (LENGTH(L2RAM)) ); */
