/* Link the wolfBoot RTL8735B test app at the DDR load address (0x70100000),
 * which must match WOLFBOOT_LOAD_ADDRESS in config/examples/rtl8735b.config. */
MEMORY
{
    DDR (rwx) : ORIGIN = 0x70100000, LENGTH = 0x00080000  /* 512 KB */
}

ENTRY(reset_handler)

SECTIONS
{
    .text 0x70100000 :
    {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > DDR

    .data :
    {
        *(.data*)
        . = ALIGN(4);
    } > DDR

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
    } > DDR

    _stack_top = 0x70180000;   /* MSP: 512 KB above the image base, in DDR */

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) }
}
