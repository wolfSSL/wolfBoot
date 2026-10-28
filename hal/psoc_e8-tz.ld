MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @WOLFBOOT_NSC_ADDRESS@ - @WOLFBOOT_ORIGIN@
    FLASH_NSC (rx) : ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_ORIGIN@ + @BOOTLOADER_PARTITION_SIZE@ - @WOLFBOOT_NSC_ADDRESS@
    RAM (rwx) : ORIGIN = 0x34002000, LENGTH = 0x00056000
    RAM_HEAP (rw) : ORIGIN = 0x34058000, LENGTH = 0x00028000
    FLASH_KEYVAULT (rw) : ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    .gnu.sgstubs ORIGIN(FLASH_NSC) :
    {
        . = ALIGN(32);
        *(.gnu.sgstubs*)
        . = ALIGN(4);
    } > FLASH_NSC

    .keystore :
    {
        . = ALIGN(4);
        KEEP(*(.keystore*))
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
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

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);
_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
