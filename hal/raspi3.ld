MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x00080000, LENGTH = 0x80000000
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
    . = ALIGN(4);
}

END_STACK = _start_text;
kernel_addr  = 0x0140000;
update_addr  = 0x1140000;
dts_addr     = 0x00a0000;
kernel_load_addr = 0x20000000;
dts_load_addr = 0x21000000;

_wolfboot_partition_boot_address = kernel_addr;
_wolfboot_partition_update_address = update_addr;

