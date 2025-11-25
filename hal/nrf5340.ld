MEMORY
{
    FLASH (rx) :          ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @WOLFBOOT_KEYVAULT_ADDRESS@ - @ARCH_FLASH_OFFSET@
    FLASH_NET (rx) :      ORIGIN = 0x01000000, LENGTH = 0x40000 /* 256K */
    RAM (rwx) :           ORIGIN = 0x20000000, LENGTH = 0x10000 /* 64K */
    RAM_HEAP (rw) :       ORIGIN = 0x20010000, LENGTH = 0xc000  /* 48K */
    RAM_KV (rw) :         ORIGIN = 0x2001c000, LENGTH = 0x2000  /* 8K */
    FLASH_KEYVAULT (rw) : ORIGIN = @WOLFBOOT_KEYVAULT_ADDRESS@, LENGTH = @WOLFBOOT_KEYVAULT_SIZE@
    FLASH_NSC(rx) :       ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_NSC_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.boot*)
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

    .gnu.sgstubs :
    {
        . += 0x400;
        . = ALIGN(4);
        *(.gnu.sgstubs*)   /* Secure Gateway Stubs */
        . = ALIGN(4);
    } > FLASH_NSC

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
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

_keyvault_origin = ORIGIN(RAM_KV);
_keyvault_size = LENGTH(RAM_KV);

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
