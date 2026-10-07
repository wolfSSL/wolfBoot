MEMORY
{
    FLASH (rx) : ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x1C000 /* 112K STCM */
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
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

ASSERT(hal_flash_write >= ORIGIN(RAM) && hal_flash_erase >= ORIGIN(RAM),
       "MCX W flash routines must run from RAM (RAM_CODE=1)")

_start_ecc_ram = ORIGIN(RAM);
_end_ecc_ram = ORIGIN(RAM) + LENGTH(RAM);
