/* PolarFire SoC MPFS250 Linker Script for wolfBoot */
/* This linker script assumes wolfBoot is loaded by the HSS (FSBL) into DDR */

OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
    /* The first 0x100 bytes of eNVM are used for boot ROM secure boot meta information */
    FLASH_ENVM (rx)  : ORIGIN = 0x20220100, LENGTH = 128k - 0x100
    DDR (rx)         : ORIGIN = 0x80000000, LENGTH = 1028k
    L2_SCRATCH (rwx) : ORIGIN = 0x0A000000, LENGTH = 256k
}

PROVIDE(STACK_SIZE_PER_HART = 16k);

SECTIONS
{
    .text : ALIGN(0x10)
    {
        _start_text = .;
        KEEP(*(.init)) /* Only for _reset, relocates IV to 0x100 */
        . = ORIGIN(DDR) + 0x100;
        _start_vector = .;
        KEEP(*(.isr_vector))
        . = ALIGN(0x10);
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _end_text = .;
    } > DDR

    .data : ALIGN(0x10)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        _end_data = .;
    } > DDR

    .bss (NOLOAD) : ALIGN(0x10)
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > DDR
}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(L2_SCRATCH) + (LENGTH(L2_SCRATCH)) );
