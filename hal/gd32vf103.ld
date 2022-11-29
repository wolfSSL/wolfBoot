OUTPUT_ARCH( "riscv" )

ENTRY(_reset)

MEMORY
{
    FLASH (rxai!w) : ORIGIN = 0x08000000, LENGTH = ##WOLFBOOT_PARTITION_BOOT_ADDRESS##
    RAM (wxa!ri) : ORIGIN = 0x20000000, LENGTH = 32K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.init)) /* Only for _reset, relocates IV to 0x200 */
        . = ORIGIN(FLASH) + 0x400;
        _start_vector = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        . = ALIGN(1024);
        _start_data = .;
        _data = .;
        _data_lma = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > RAM
}

PROVIDE(_edata = _end_data);
PROVIDE(__global_pointer$ = _global_pointer);
PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + (LENGTH(RAM)) );
PROVIDE(_sp = _end_stack);
