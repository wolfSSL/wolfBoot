OUTPUT_ARCH( "powerpc" )

ENTRY( _app_entry )

PHDRS
{
    text PT_LOAD;
    bss  PT_LOAD;
}

SECTIONS
{
    . = @WOLFBOOT_LOAD_ADDRESS@;

    .text :
    {
       *(.text._app_entry)
       *(.text.main)
        *(.text*)
        *(.rodata*)
    } :text

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        *(.sdata*)       /* PPC small initialized data */
        . = ALIGN(4);
        KEEP(*(.ramcode))

        /* preinit data */
        . = ALIGN(4);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        /* init data */
        . = ALIGN(4);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        /* finit data */
        . = ALIGN(4);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        _end_data = .;
    }

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(.sbss*)        /* PPC small uninitialized data */
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    }
    . = ALIGN(4);

    /* Heap for _sbrk (used by syscalls.c malloc) */
    _Min_Heap_Size = 0x10000; /* 64KB */

    /* Stack: 64KB, grows downward from _stack_top.
     * wolfCrypt ECC384 operations need deep stack (~11KB per frame). */
    _Min_Stack_Size = 0x10000; /* 64KB */
    _stack_end = _end + _Min_Heap_Size;
    _stack_top = _stack_end + _Min_Stack_Size;
}
