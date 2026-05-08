OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

/* wolfBoot is loaded by Xilinx FSBL into DDR at 0x04000000.
 * Reserve 1 MB for code/data/bss/stack. */
MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x04000000, LENGTH = 0x00100000
}

ENTRY(reset_vector_entry)

SECTIONS
{
    .text : {
        _start_text = .;
        KEEP(*(start))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        *(.glue_7)
        . = ALIGN(4);
        *(.eh_frame)
        . = ALIGN(4);
        _end_text = .;
    } > DDR_MEM

    . = ALIGN(4);
    .dummy : {
        _edummy = .;
    } > DDR_MEM

    .data : AT (LOADADDR(.dummy)) {
        _start_data = .;
        *(.vectors)
        *(.data)
        *(.data.*)
        _end_data = .;
    } > DDR_MEM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _start_bss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _end_bss = .;
        _end = .;
    } > DDR_MEM
}

kernel_addr  = 0x00100000;
update_addr  = 0x00700000;
dts_addr     = 0x00000000;

_romsize  = _end_data - _start_text;
_sramsize = _end_bss  - _start_text;
END_STACK = _start_text;
_stack_top = ORIGIN(DDR_MEM) + LENGTH(DDR_MEM);
end = .;
