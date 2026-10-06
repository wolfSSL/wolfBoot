MEMORY
{
    FLASH (rx) : ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 256K
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
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

_rom_image_size = _stored_data + SIZEOF(.data) - _rom_image_start;
