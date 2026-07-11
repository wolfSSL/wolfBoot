/* cm4.ld
 *
 * Linker script for the Raspberry Pi Compute Module 4 (BCM2711, Cortex-A72).
 *
 * The VideoCore GPU firmware loads kernel8.img to 0x80000 and releases the
 * A72 cores there. DDR_MEM length covers the 1GB low-peripheral view common
 * to all CM4 RAM variants (1/2/4/8 GB); the peripheral block sits above at
 * 0xFE000000 and is not mapped here.
 */
MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x00080000, LENGTH = 0x3c000000
}
ENTRY(_vector_table);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > DDR_MEM
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > DDR_MEM

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DDR_MEM

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
    } > DDR_MEM
    . = ALIGN(8);
    /* Heap start for the newlib sbrk (FIPS module malloc). Grows up into free
     * DDR; the stack lives at END_STACK (0x80000) and grows down. */
    PROVIDE(end = .);
    PROVIDE(__end__ = .);
    PROVIDE(_heap_start = .);
}

END_STACK = _start_text;
kernel_addr  = 0x0140000;
update_addr  = 0x1140000;
dts_addr     = 0x00a0000;
kernel_load_addr = 0x20000000;
dts_load_addr = 0x21000000;

_wolfboot_partition_boot_address = kernel_addr;
_wolfboot_partition_update_address = update_addr;
