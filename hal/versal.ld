/* versal_ddr.ld
 *
 * Linker script for wolfBoot on AMD Versal ACAP - DDR Boot
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 *
 * This script is for production boot where:
 *   - PLM/PSM have initialized DDR
 *   - BL31 runs at EL3 and transitions to EL2
 *   - wolfBoot runs at EL2 from DDR at 0x8000000 (replacing U-Boot)
 *
 * Memory Map:
 *   0x00000000 - 0x07FFFFFF : Reserved / Low DDR
 *   0x08000000 - 0x081FFFFF : wolfBoot (2MB)
 *   0x08200000 - 0x0FFFFFFF : Available for scratch/heap
 *   0x10000000              : Linux kernel load address
 *   0x11800000              : Device tree load address (optional)
 *
 * Copyright (C) 2025 wolfSSL Inc.
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_vector_table)

/* Stack and heap sizes - larger for DDR boot */
_STACK_SIZE = DEFINED(_STACK_SIZE) ? _STACK_SIZE : 0x10000;  /* 64KB stack */
_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x20000;     /* 128KB heap */

_EL0_STACK_SIZE = DEFINED(_EL0_STACK_SIZE) ? _EL0_STACK_SIZE : 0x1000;
_EL1_STACK_SIZE = DEFINED(_EL1_STACK_SIZE) ? _EL1_STACK_SIZE : 0x2000;
_EL2_STACK_SIZE = DEFINED(_EL2_STACK_SIZE) ? _EL2_STACK_SIZE : 0x8000;  /* Primary stack at EL2 */

/* Memory regions
 * wolfBoot at 0x8000000 with 2MB allocated
 * This matches U-Boot's load address in PetaLinux BOOT.BIN
 */
MEMORY
{
    DDR (rwx) : ORIGIN = 0x8000000, LENGTH = 0x200000
}

/* Sections */
SECTIONS
{
    /* Code section - must start at entry point */
    .text : {
        _start_text = .;

        /* Vector table and startup code MUST be first */
        KEEP (*(.vectors))
        *(.boot)
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        *(.plt)
        *(.gnu_warning)
        *(.gcc_execpt_table)
        *(.glue_7)
        *(.glue_7t)
        *(.ARM.extab)
        *(.gnu.linkonce.armextab.*)

        . = ALIGN(8);
        _end_text = .;
    } > DDR

    .init (ALIGN(64)) : {
        KEEP (*(.init))
    } > DDR

    .fini (ALIGN(64)) : {
        KEEP (*(.fini))
    } > DDR

    /* Read-only data */
    .rodata : {
        . = ALIGN(64);
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        __rodata_end = .;
    } > DDR

    /* Keystore - public keys for image verification */
    .keystore : {
        . = ALIGN(8);
        *(.keystore)
        . = ALIGN(8);
    } > DDR

    /* Initialized data */
    .data : {
        . = ALIGN(64);
        _start_data = .;
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        *(.jcr)
        *(.got)
        *(.got.plt)
        _end_data = .;
        __data_end = .;
    } > DDR

    .got : {
        *(.got)
    } > DDR

    .got1 : {
        *(.got1)
    } > DDR

    .got2 : {
        *(.got2)
    } > DDR

    /* MMU tables - 4KB aligned for AArch64 */
    .mmu_tbl0 (ALIGN(4096)) : {
        __mmu_tbl0_start = .;
        *(.mmu_tbl0)
        __mmu_tbl0_end = .;
    } > DDR

    .mmu_tbl1 (ALIGN(4096)) : {
        __mmu_tbl1_start = .;
        *(.mmu_tbl1)
        __mmu_tbl1_end = .;
    } > DDR

    .mmu_tbl2 (ALIGN(4096)) : {
        __mmu_tbl2_start = .;
        *(.mmu_tbl2)
        __mmu_tbl2_end = .;
    } > DDR

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        *(.gnu.linkonce.armexidix.*.*)
        __exidx_end = .;
    } > DDR

    /* Small data */
    .sdata : {
        . = ALIGN(64);
        __sdata_start = .;
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.s.*)
        __sdata_end = .;
    } > DDR

    /* RAM functions section */
    .ramcode : {
        . = ALIGN(8);
        _start_ramcode = .;
        *(.ramcode)
        *(.ramcode.*)
        . = ALIGN(8);
        _end_ramcode = .;
    } > DDR

    /* Small BSS */
    .sbss (NOLOAD) : {
        . = ALIGN(64);
        __sbss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.sb.*)
        . = ALIGN(64);
        __sbss_end = .;
    } > DDR

    /* Uninitialized data (BSS) */
    .bss (NOLOAD) : {
        . = ALIGN(64);
        __bss_start__ = .;
        _start_bss = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(64);
        __bss_end__ = .;
        _end_bss = .;
    } > DDR

    /* Heap */
    .heap (NOLOAD) : {
        . = ALIGN(64);
        _heap = .;
        __heap_start = .;
        . += _HEAP_SIZE;
        __heap_end = .;
    } > DDR

    /* Stack - EL2 is primary when booting from BL31 */
    .stack (NOLOAD) : {
        . = ALIGN(64);
        /* EL3 stack (not used when entering at EL2) */
        _el3_stack_end = .;
        . += 0x1000;
        __el3_stack = .;

        /* EL2 stack - primary execution level */
        _el2_stack_end = .;
        . += _EL2_STACK_SIZE;
        . = ALIGN(64);
        __el2_stack = .;

        /* EL1 stack */
        _el1_stack_end = .;
        . += _EL1_STACK_SIZE;
        . = ALIGN(64);
        __el1_stack = .;

        /* EL0 stack */
        _el0_stack_end = .;
        . += _EL0_STACK_SIZE;
        . = ALIGN(64);
        __el0_stack = .;
    } > DDR

    _end = .;
}

/* Provide symbols for startup code */
PROVIDE(_start_vector = _start_text);
PROVIDE(__stack = __el2_stack);  /* Use EL2 stack as default when entering from BL31 */
PROVIDE(_stack_base = .);
