OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
  FLASH(rxai!w) : ORIGIN = 0x20400000, LENGTH = 512K
  RAM(wxa!ri) : ORIGIN = 0x80000000, LENGTH = 16K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.init))
        . = ORIGIN(FLASH) + 0x100;
        _start_vector = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        . = ALIGN(4096);
        _start_data = .;
        *(.data*)
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    .bss :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > RAM

}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + (LENGTH(RAM)) );
