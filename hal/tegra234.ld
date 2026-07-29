/* tegra234.ld - bare-metal link map for the Tegra234 BL33 firmware target.
 *
 * DRAM-resident, boot-from-DRAM AArch64 map (same style as hal/raspi3.ld).
 * Keep the addresses here in sync with hal/tegra234.h.
 */
MEMORY
{
    /* MB2 loads the cpubl (BL33) slot at 0x272000000 and BL31 enters it there,
     * near the top of the 8GB DRAM map (non-ECC 0x80000000..0x280000000);
     * confirmed on hardware 2026-07-27. wolfBoot is not position-independent,
     * so it must link/run at that base.
     *
     * LENGTH is deliberately the payload offset (TEGRA234_BUNDLE_OFFSET, 2 MB)
     * rather than the whole window: growing wolfBoot into the bundled payload
     * is then a link error instead of an image the bundling scripts silently
     * assemble wrong. */
    DDR_MEM(rwx): ORIGIN = 0x272000000, LENGTH = 0x200000
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

    /* Dedicated stack region, image-relative so the stack top is valid
     * wherever the prior stage loads wolfBoot. Used by the tegra234 sp fixup
     * in src/boot_aarch64_start.S. */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x40000;      /* 256 KB */
        . = ALIGN(16);
        END_STACK = .;
        /* _end marks the top of wolfBoot's runtime footprint, so it sits
         * above the stack rather than at the end of .bss. src/update_ram.c
         * compares against it in the RAM-boot overlap guard (built only with
         * EXT_FLASH+NO_XIP), and it is the conventional heap start. */
        _end = .;
    } > DDR_MEM
    . = ALIGN(4);
}

/* No partition symbols here: this target builds WOLFBOOT_NO_PARTITIONS, so the
 * boot/update/DTB addresses come from hal_get_*_address() in hal/tegra234.c
 * (constants in hal/tegra234.h) and the RAM staging targets come from
 * WOLFBOOT_LOAD_ADDRESS / WOLFBOOT_LOAD_DTS_ADDRESS in the config. Defining
 * them twice only invites the two copies to disagree. */
