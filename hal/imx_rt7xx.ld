MEMORY
{
    FLASH (rx) : ORIGIN = @ARCH_FLASH_OFFSET@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x20080000, LENGTH = 256K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        . = ALIGN(0x8);
        *(EXCLUDE_FILE(*fsl_*.o) .text*)
        *(EXCLUDE_FILE(*fsl_*.o) .rodata*)
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
        *(.ramfunc*)
        *fsl_*.o(.text* .rodata*)
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
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
