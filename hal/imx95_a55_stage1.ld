/* imx95_a55_stage1.ld - wolfBoot stage 1, OCRAM-resident, in place of the
 * U-Boot SPL image in AHAB container 0. Addresses match what the image tool
 * records for that container entry. */
MEMORY
{
    /* The boot ROM loads this image here and enters it at EL3. LENGTH is the
     * same 192 KiB budget U-Boot gives SPL, so growth is a link error rather
     * than a silent overrun into the stack below. Both come from
     * WOLFBOOT_STAGE1_BASE_ADDR / WOLFBOOT_STAGE1_SIZE, so the same sources
     * can also be linked somewhere else for bring-up. */
    OCRAM(rwx): ORIGIN = @WOLFBOOT_STAGE1_BASE_ADDR@, LENGTH = @WOLFBOOT_STAGE1_SIZE@
}
ENTRY(_boot);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
        _end_text = .;
    } > OCRAM

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(8);
        _end_data = .;
    } > OCRAM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
        __bss_end__ = .;
    } > OCRAM
}

/* Stack top, above the loaded image and below the end of the region. At the
 * OCRAM base this is 0x204D6000, the same address U-Boot SPL uses, so it is
 * known to be clear of anything the ROM or the OEI left behind. */
END_STACK = @WOLFBOOT_STAGE1_BASE_ADDR@ + 0x56000;
