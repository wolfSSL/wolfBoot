OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    .text :
    {
        _start_text = .;
        *(.text*)
        _end_text = .;
    }

    .data :
    {
        _start_data = .;
        *(.data*)
        _end_data = .;
    }

    .bss (NOLOAD) :
    {
        _start_bss = .;
        *(.bss*)
        . = ALIGN(4);
        _end_bss = .;
    }
}
