/* SRAM below 0x30030000 holds the handoff record and the secure app's RAM. */
MEMORY
{
    FLASH (rx) :    ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @WOLFBOOT_KEYVAULT_ADDRESS@ - @WOLFBOOT_ORIGIN@
    RAM (rwx) :     ORIGIN = 0x30030000, LENGTH = 0x00010000
    FLASH_KEYVAULT(rw): ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(8);
        KEEP(*(.ramcode))
        . = ALIGN(8);
        _end_data = .;
    } > RAM AT > FLASH
    _stored_data = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > RAM
    . = ALIGN(8);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

_min_stack = 0x2000;
ASSERT(_end + _min_stack <= END_STACK, "stm32l5 secure-app RAM: .bss leaves less than _min_stack for the stack")

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);
