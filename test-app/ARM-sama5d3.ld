OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x20100800, LENGTH = 0x100000
    STACK_MEM(rw): ORIGIN = 0x20000000, LENGTH = 0x00100000
}

ENTRY(reset_vector_entry)
SECTIONS
{
    .text : AT (ORIGIN(DDR_MEM)) {
        _start_text = .;
        *(.iv)
        *(.text)
            *(.rodata)
            *(.rodata*)
            . = ALIGN(4);
        *(.glue_7)
        . = ALIGN(4);		
        *(.eh_frame)
        . = ALIGN(4);
        _end_text = . ;
	}

	/* collect all initialized .data sections */
	/* .data : AT ( ADDR (.text) + SIZEOF (.text) SIZEOF (.ARM.*) {  */

    . = ALIGN(4);
    .dummy : {
        _edummy = .;
    }

    .data : AT (LOADADDR(.dummy)) {
		_start_data = .;
		*(.vectors)
		*(.data)
		_end_data = .;
	}

	/* collect all uninitialized .bss sections */
	.bss (NOLOAD) : {
		. = ALIGN(4);
		_start_bss = .;
		*(.bss)
		_end_bss = .;
	}
}
_romsize = _end_data - _start_text;
_sramsize = _end_bss - _start_text;
END_STACK = _start_text;
_stack_top = ORIGIN(STACK_MEM) + LENGTH(STACK_MEM);
end = .;  /* define a global symbol marking the end of application */

