MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = @BOOTLOADER_PARTITION_SIZE@
    RAM (rwx)  : ORIGIN = 0x08000000, LENGTH = 0x60000
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
        *(EXCLUDE_FILE(*cy_flash.o *cy_ipc_*.o *cy_syslib.o *cy_syslib_ext.o
            *cy_device.o *system_psoc6_cm0plus.o *cy_sysclk.o *cy_smif*.o
            *libgcc.a:*) .text*)
        *(EXCLUDE_FILE(*cy_flash.o *cy_ipc_*.o *cy_syslib.o *cy_syslib_ext.o
            *cy_device.o *system_psoc6_cm0plus.o *cy_sysclk.o *cy_smif*.o
            *libgcc.a:*) .rodata*)
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
        /* PDL flash and QSPI paths, which a bootloader self-update runs after
         * erasing the bootloader region */
        *cy_flash.o(.text* .rodata*)
        *cy_ipc_*.o(.text* .rodata*)
        *cy_syslib.o(.text* .rodata*)
        *cy_syslib_ext.o(.text* .rodata*)
        *cy_device.o(.text* .rodata*)
        *system_psoc6_cm0plus.o(.text* .rodata*)
        *cy_sysclk.o(.text* .rodata*)
        *cy_smif*.o(.text* .rodata*)
        *libgcc.a:*(.text* .rodata*)
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
        . = . + 0xC0;
    } > RAM
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);

__StackTop = ORIGIN(RAM) + LENGTH(RAM);
__StackLimit = __StackTop - 0x4000;
PROVIDE(__stack = __StackTop);

