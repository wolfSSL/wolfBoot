/* imx95_a55_stage1.ld - wolfBoot stage 1, OCRAM-resident, in place of the
 * U-Boot SPL image in AHAB container 0. Addresses match what the image tool
 * records for that container entry. */
MEMORY
{
    /* The boot ROM loads this here and enters at EL3. LENGTH is the same 192 KiB
     * budget U-Boot gives SPL, so growth past it is a link error. The stack is
     * NOT inside this region; see END_STACK. */
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

/* Stack top, deliberately ABOVE the MEMORY region, in the OCRAM that extends
 * past this image's budget - 0x204D6000 at the OCRAM base, the address U-Boot
 * SPL uses. A bare symbol gets no bounds check, hence the assert below. */
END_STACK = @WOLFBOOT_STAGE1_BASE_ADDR@ + 0x56000;
ASSERT(END_STACK > @WOLFBOOT_STAGE1_BASE_ADDR@ + @WOLFBOOT_STAGE1_SIZE@,
       "stage 1 stack top must stay above the OCRAM image region")
