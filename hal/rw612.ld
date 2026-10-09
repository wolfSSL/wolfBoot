MEMORY
{
    FLASH (rx) :          ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @WOLFBOOT_NSC_ADDRESS@ - @ARCH_FLASH_OFFSET@
    RAM (rwx) :           ORIGIN = 0x30000000, LENGTH = 0x20000
    RAM_HEAP (rwx) :      ORIGIN = 0x30020000, LENGTH = 0x20000
    FLASH_KEYVAULT (rw) : ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
    FLASH_NSC (rx) :      ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_NSC_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        . = 0x400;
        KEEP(*(.flash_conf))
        . = 0x1000;
        _rom_image_start = .;
        KEEP(*(.isr_vector))
        *(.keystore*)
        *(.text*)
        *(.rodata*)
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

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
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

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);

_rom_image_size = _stored_data + SIZEOF(.data) - _rom_image_start;
