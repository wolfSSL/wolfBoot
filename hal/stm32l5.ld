MEMORY
{
    FLASH (rx) :    ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @WOLFBOOT_KEYVAULT_ADDRESS@ - @ARCH_FLASH_OFFSET@
    RAM (rwx) :     ORIGIN = 0x30000000, LENGTH = 0x00012000
    RAM_HEAP (rw):  ORIGIN = 0x30012000, LENGTH = 0xc000 /* 49152 B Heap for wolfcrypt/PKCS11 */
    RAM_KV (rw):    ORIGIN = 0x3001e000, LENGTH = 0x2000
    FLASH_KEYVAULT(rw): ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
    FLASH_NSC(rx): ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_NSC_SIZE@
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

    .gnu.sgstubs :
    {
        . += 0x400;
        . = ALIGN(4);
        *(.gnu.sgstubs*)   /* Secure Gateway stubs */
        . = ALIGN(4);
    } >FLASH_NSC

    _stored_data = .;
    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(8);
        KEEP(*(.ramcode))
        . = ALIGN(8);
        _end_data = .;
    } > RAM

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

_keyvault_origin = ORIGIN(RAM_KV);
_keyvault_size = LENGTH(RAM_KV);

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
