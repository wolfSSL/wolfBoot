/* PolarFire SoC MPFS250 Linker Script for wolfBoot in L2-LIM (HSS boot)
 *
 * Boot sequence: HSS (eNVM/E51) loads wolfBoot into L2-LIM on U54 (S-mode).
 * wolfBoot then loads application from SC ext SPI flash into L2-LIM.
 * No DDR - HSS remains resident in lower L2-LIM for SBI services.
 *
 * Memory layout (1.5MB L2-LIM: 0x08000000 - 0x0817FFFF):
 *   0x08000000 - HSS resident runtime (~256KB)
 *   WOLFBOOT_ORIGIN (0x08040000) - wolfBoot code + data
 *   WOLFBOOT_LOAD_ADDRESS (0x08060000) - Application load area
 *   WOLFBOOT_STACK_TOP (0x08180000) - Stack grows downward
 */

OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
    /* L2-LIM on-chip SRAM (cache ways configured as LIM by HSS)
     * HSS loads wolfBoot binary here directly - no VMA/LMA split needed */
    L2_LIM (rwx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @WOLFBOOT_L2LIM_SIZE@
}

PROVIDE(STACK_SIZE_PER_HART = 16k);

SECTIONS
{
    .text : ALIGN(0x10)
    {
        _start_text = .;
        KEEP(*(.init))
        . = ORIGIN(L2_LIM) + 0x100;
        _start_vector = .;
        KEEP(*(.isr_vector))
        . = ALIGN(0x10);
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _end_text = .;
    } > L2_LIM

    .data : ALIGN(0x10)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
        KEEP(*(.keystore*))
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        _end_data = .;
    } > L2_LIM

    .bss (NOLOAD) : ALIGN(0x10)
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > L2_LIM
}

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(L2_LIM) + LENGTH(L2_LIM));
