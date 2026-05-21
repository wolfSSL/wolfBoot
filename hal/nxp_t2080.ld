OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )

/* On initial start, only a limited space (4K) is accessible.
 * Code here bootstraps to enable access to other needed address spaces.
 * Boot code must be at the top of flash (last 4KB of bootloader partition).
 * Computed from FLASH region so it adapts to any board flash layout:
 *   T2080 RDB (128MB @ 0xE8000000):   0xEFFFF000 / 0xEFFFFFFC
 *   CW VPX3-152 (256MB @ 0xF0000000): 0xFFFFF000 / 0xFFFFFFFC */
BOOTSTRAP_TLB = ORIGIN(FLASH) + LENGTH(FLASH) - 0x1000;

/* Entry point where RCW directs code to execute from */
BOOTSTRAP_ENTRY = ORIGIN(FLASH) + LENGTH(FLASH) - 0x4;

MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@

    /* CPC as SRAM - 1MB (T2080 supports up to 2MB, using 1MB for P384 stack)
     * Layout: .ramcode at bottom, stack grows down from top
     * Address must match L2SRAM_ADDR in nxp_ppc.h:
     *   T2080 RDB:   0xF8F00000
     *   CW VPX3-152: 0xEE900000 (relocated to avoid 256MB flash VA overlap) */
    RAM (rwx) : ORIGIN = @L2SRAM_ADDR@, LENGTH = 0x100000

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
        KEEP(*(.keystore))
    } > FLASH

    /* Dynamic-linking metadata. Bare-metal ELF does not use these at
     * runtime, but binutils 2.42 ld rejects /DISCARD/ patterns for the
     * .gnu.version_r section name -- match the convention used by the
     * sibling T1024 / P1021 / LS1028A linker scripts and emit proper
     * (empty) output sections. */
    .gnu.version    : { *(.gnu.version) }
    .gnu.version_r  : { *(.gnu.version_r) }

    /* Discard the rest -- not needed for bare-metal so JTAG probes
     * (e.g. Ronetix PEEDI) can load the ELF without a dynamic linker. */
    /DISCARD/ :
    {
        *(.interp)
        *(.hash)
        *(.dynsym)
        *(.dynstr)
        *(.gnu.hash)
        *(.rela.dyn)
        *(.rela.plt)
    }

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

    /* Calculate where .data starts in flash (after .ramcode), ensuring
     * at least 16-byte alignment for the .data load address */
    _stored_data = (_stored_ramcode + (_end_ramcode - _start_ramcode) + 15) & ~15;

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
