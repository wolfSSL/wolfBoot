OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x00300000, LENGTH = 0x0007F000
}

ENTRY(reset_vector_entry)
SECTIONS
{
    .text : { 
        _start_text = .;
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

kernel_addr = 0x040000;
update_addr = 0x080000;

_romsize = _end_data - _start_text;
_sramsize = _end_bss - _start_text;
END_STACK = _start_text;
_stack_top = ORIGIN(DDR_MEM) + LENGTH(DDR_MEM);
end = .;  /* define a global symbol marking the end of application */


