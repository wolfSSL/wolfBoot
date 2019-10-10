OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
  FLASH(rxai!w) : ORIGIN = ##WOLFBOOT_TEST_APP_ADDRESS##, LENGTH = ##WOLFBOOT_TEST_APP_SIZE##
  RAM(wxa!ri) : ORIGIN = 0x80001000, LENGTH = 12K
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
        *(.srodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        . = ALIGN(4096);
        _start_data = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
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
