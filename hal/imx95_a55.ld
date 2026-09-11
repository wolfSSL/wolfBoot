/* imx95_a55.ld - wolfBoot as BL33, DRAM-resident (shape of tegra234.ld).
 * Keep addresses in sync with hal/imx95_a55.h. */
MEMORY
{
    /* BL31 enters at IMX95_BL33_BASE (not position independent). LENGTH
     * bounds wolfBoot's footprint so growth is a link error, not silent
     * overlap with the staging window above. */
    DDR_MEM(rwx): ORIGIN = 0x90200000, LENGTH = 0x200000
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

    /* Dedicated stack: sp at the image base would grow down into OP-TEE,
     * so startup uses END_STACK. 256 KiB covers ML-DSA-87 verify plus the
     * on-stack header buffer. */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x40000;      /* 256 KB */
        . = ALIGN(16);
        END_STACK = .;
        /* _end = top of the runtime footprint (above the stack); used by
         * the RAM-boot overlap guard and the load-range check. */
        _end = .;
    } > DDR_MEM
    . = ALIGN(4);
}

/* No partition symbols: WOLFBOOT_NO_PARTITIONS - addresses come from the
 * HAL and config. */
