/* SRAM below 0x30030000 holds the handoff record and the secure app's RAM. */
MEMORY
{
    FLASH (rx) :    ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @WOLFBOOT_KEYVAULT_ADDRESS@ - @WOLFBOOT_ORIGIN@
    RAM (rwx) :     ORIGIN = 0x30030000, LENGTH = 0x0000C000
    RAM_HEAP (rw):  ORIGIN = 0x3003C000, LENGTH = 0x4000
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

    /* Data is stored in FLASH, not after the NSC: the boot partition
     * follows the NSC directly. */
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

    .gnu.sgstubs ORIGIN(FLASH_NSC) :
    {
        . += 0x400;
        . = ALIGN(4);
        *(.gnu.sgstubs*)   /* Secure Gateway stubs */
        . = ALIGN(4);
    } >FLASH_NSC

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

_flash_keyvault = ORIGIN(FLASH_KEYVAULT);
_flash_keyvault_size = LENGTH(FLASH_KEYVAULT);

_start_heap = ORIGIN(RAM_HEAP);
_heap_size = LENGTH(RAM_HEAP);
