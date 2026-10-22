MEMORY
{
    FLASH (rx) :          ORIGIN = 0x10000000, LENGTH = @WOLFBOOT_NSC_ADDRESS@ - 0x10000000
    RAM (rwx) :           ORIGIN = 0x30000000, LENGTH = 0x10000 /* 64K */
    RAM_HEAP (rwx) :      ORIGIN = 0x30010000, LENGTH = 0xE000 /* 56K */
    FLASH_KEYVAULT (rw) : ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
    FLASH_NSC (rx) :      ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_NSC_SIZE@
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

    .gnu.sgstubs ORIGIN(FLASH_NSC) :
    {
        . += 0x400;
        . = ALIGN(4);
        *(.gnu.sgstubs*) /* Secure Gateway Stubs */
        . = ALIGN(4);
    } > FLASH_NSC
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

/* A self-update erases the flash these run from */
ASSERT(FLASH_Erase >= ORIGIN(RAM) && FLASH_Program >= ORIGIN(RAM) &&
       FLASH_VerifyErase >= ORIGIN(RAM) && CLOCK_GetFreq >= ORIGIN(RAM),
       "LPC55S69 ROM flash API wrappers must run from RAM")

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
