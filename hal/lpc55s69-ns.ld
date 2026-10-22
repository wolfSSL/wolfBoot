MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = @WOLFBOOT_PARTITION_BOOT_ADDRESS@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        . = 0x200;
        *(.keystore*)
        *(EXCLUDE_FILE(*fsl_iap.o *fsl_clock.o) .text*)
        *(EXCLUDE_FILE(*fsl_iap.o *fsl_clock.o) .rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_data = .;

    /* The ROM flash API wrappers run from RAM with the RAMFUNCTIONs */
    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        *fsl_iap.o(.text* .rodata*)
        *fsl_clock.o(.text* .rodata*)
        . = ALIGN(4);
        KEEP(*(.data*))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > RAM
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

/* A self-update erases the flash these run from */
ASSERT(FLASH_Erase >= ORIGIN(RAM) && FLASH_Program >= ORIGIN(RAM) &&
       FLASH_VerifyErase >= ORIGIN(RAM) && CLOCK_GetFreq >= ORIGIN(RAM),
       "LPC55S69 ROM flash API wrappers must run from RAM")
