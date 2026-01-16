/* Memory region - address comes from WOLFBOOT_LOAD_ADDRESS, size from WOLFBOOT_TEST_APP_SIZE */
MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_LOAD_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
}

/* Use _start if boot_arm64_start.S is linked, otherwise fall back to main */
ENTRY(_start);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.text.startup))  /* Startup code (_start) if boot_arm64_start.S is used */
        KEEP(*(.boot*))          /* Legacy boot section for compatibility */
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    .data :
    {
        PROVIDE(_stored_data = .);  /* For boot_arm64_start.S data copying */
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > FLASH

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
    } > FLASH

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        . = . + 0x4000;  /* 16KB stack */
        _stack = .;
    } > FLASH

    . = ALIGN(4);
}

/* Stack pointer - use proper stack section if available, otherwise fall back to text start */
PROVIDE(__stack = _stack);
PROVIDE(END_STACK = _start_text);  /* Legacy compatibility for platforms that don't use stack section */

