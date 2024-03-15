MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx) : ORIGIN = 0x30000000, LENGTH = 0x00017FFF
    FLASH_NSC(rx): ORIGIN = @WOLFBOOT_ORIGIN@ + 0x20000, LENGTH = 0x20000
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
