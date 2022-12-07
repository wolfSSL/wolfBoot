MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00020000 /* mapping TCM only */
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

_wolfboot_partition_boot_address = 0x8020000;
_wolfboot_partition_size = 0xD0000;
_wolfboot_partition_update_address = 0x80F0000;
_wolfboot_partition_swap_address = 0x81C0000;

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
