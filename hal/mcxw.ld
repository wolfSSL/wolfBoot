MEMORY
{
    FLASH (rx) :          ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @WOLFBOOT_NSC_ADDRESS@ - @ARCH_FLASH_OFFSET@
    RAM (rwx) :           ORIGIN = 0x30000000, LENGTH = 0xE000 /* 56K */
    RAM_HEAP (rwx) :      ORIGIN = 0x3000E000, LENGTH = 0x8000 /* 32K */
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

ASSERT(hal_flash_write >= ORIGIN(RAM) && hal_flash_erase >= ORIGIN(RAM),
       "MCX W flash routines must run from RAM (RAM_CODE=1)")

_start_ecc_ram = ORIGIN(RAM);
_end_ecc_ram = ORIGIN(RAM_HEAP) + LENGTH(RAM_HEAP);

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
