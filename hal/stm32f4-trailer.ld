/* STM32F4 with CUSTOM_PARTITION_TRAILER: sectors 1 and 2 (16KB each) hold
 * the partition trailers (see hal/stm32f4.c), so wolfBoot keeps only its
 * vector table in sector 0 and resumes at sector 3. */
MEMORY
{
    VECTORS (rx) : ORIGIN = 0x08000000, LENGTH = 0x4000
    FLASH (rx) : ORIGIN = 0x0800C000, LENGTH = @BOOTLOADER_PARTITION_SIZE@ - 0xC000
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x0001C000
}

SECTIONS
{
    .isr_vector :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > VECTORS

    .text :
    {
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

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
