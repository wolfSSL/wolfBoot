/* versal.ld - Test application linker script for AMD Versal
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This application is loaded by wolfBoot to WOLFBOOT_LOAD_ADDRESS
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* Memory regions - application loaded by wolfBoot */
MEMORY
{
    /* Application loaded at WOLFBOOT_LOAD_ADDRESS */
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 64M
}

SECTIONS
{
    .text : {
        _start_text = .;
        KEEP(*(.text._start))
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
        _end_text = .;
    } > RAM

    .data : {
        . = ALIGN(8);
        _start_data = .;
        *(.data*)
        . = ALIGN(8);
        _end_data = .;
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    } > RAM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + 0x4000;
        _stack = .;
    } > RAM

    _end = .;
}

PROVIDE(__stack = _stack);

