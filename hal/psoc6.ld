MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = ##WOLFBOOT_PARTITION_BOOT_ADDRESS##
    RAM (rwx)  : ORIGIN = 0x08000000, LENGTH = 64K
}

SECTIONS
{

    .text :
    {
        _start_text = .;
        __Vectors = .;
        KEEP(*(.isr_vector))
        KEEP(*(.vectors))
        . = ALIGN(0x400);
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
        __etext = .;
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        __data_start__ = .;

        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(4);

        KEEP(*(.cy_ramfunc*))
        . = ALIGN(4);

        _end_data = .;
        __data_end__ = .;

    } > RAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        __zero_table_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        __zero_table_end__ = .;
        _end = .;
        . = ALIGN(0x100);
        __ramVectors = .;
    } > RAM
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

__StackTop = ORIGIN(RAM) + LENGTH(RAM);
__StackLimit = __StackTop - 0x4000;
PROVIDE(__stack = __StackTop);

