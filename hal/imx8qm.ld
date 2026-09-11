/* imx8qm.ld - bare-metal link map for the i.MX 8QuadMax BL33 firmware target.
 *
 * DRAM-resident, boot-from-DRAM AArch64 map (same style as hal/tegra234.ld).
 * Keep the addresses here in sync with hal/imx8qm.h.
 */
MEMORY
{
    /* ATF enters BL33 at 0x80020000, where u-boot.bin links on this SoC. Not
     * position-independent. LENGTH is the payload offset, so growing into the
     * bundled payload is a link error. */
    DDR_MEM(rwx): ORIGIN = 0x80020000, LENGTH = 0x200000
}
ENTRY(_vector_table);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > DDR_MEM
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > DDR_MEM

    .data :
    {
        PROVIDE(_stored_data = .);  /* XIP builds copy .data from here */
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DDR_MEM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
    } > DDR_MEM

    /* Image-relative stack, used by the imx8qm sp fixup in
     * src/boot_aarch64_start.S. */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x40000;      /* 256 KB */
        . = ALIGN(16);
        END_STACK = .;
        /* Top of the runtime footprint, so above the stack, not the end of
         * .bss: src/update_ram.c uses it for the RAM-boot overlap guard. */
        _end = .;
    } > DDR_MEM
    . = ALIGN(4);
}

/* No partition symbols here: this target builds WOLFBOOT_NO_PARTITIONS, so the
 * boot/update/DTB addresses come from hal_get_*_address() in hal/imx8qm.c
 * (constants in hal/imx8qm.h) and the RAM staging targets come from
 * WOLFBOOT_LOAD_ADDRESS / WOLFBOOT_LOAD_DTS_ADDRESS in the config. Defining
 * them twice only invites the two copies to disagree. */
