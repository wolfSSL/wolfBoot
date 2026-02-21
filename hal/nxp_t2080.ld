OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* On initial start, only a limited space(4k) is accessible.
 * Code here bootstraps to enable access to other needed address spaces */
BOOTSTRAP_TLB = 0xEFFFF000;

/* Entry point where RCW directs code to execute from */
BOOTSTRAP_ENTRY = 0xEFFFFFFC;

MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@

    /* CPC as SRAM - 1MB (T2080 supports up to 2MB, using 1MB for P384 stack)
     * Layout: .ramcode at bottom, stack grows down from top */
    RAM (rwx) : ORIGIN = 0xF8F00000, LENGTH = 0x100000

    /* DDR - 2GB */
    DRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x7FFFFFFF
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
        KEEP(*(.bootmp))
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

    /* Store flash location for .ramcode copy */
    _stored_ramcode = .;

    /* RAMFUNCTION code in CPC SRAM - copied before DDR is used
     * This ensures memcpy/memmove are available early */
    .ramcode : AT (_stored_ramcode)
    {
        _start_ramcode = .;
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_ramcode = .;
    } > RAM

    /* Calculate where .data starts in flash (after .ramcode) */
    _stored_data = _stored_ramcode + (_end_ramcode - _start_ramcode);

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        *(.got*)
        *(.got2*)
        *(.plt*)
        *(.dynamic)
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

/* Heap starts after .ramcode in CPC SRAM */
PROVIDE(_start_heap = _end_ramcode);
/* Stack at top of CPC SRAM, grows down */
PROVIDE(_end_stack  = ORIGIN(RAM) + (LENGTH(RAM)) );
