/* agilex5.ld
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_vector_table)

_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x20000;
_EL0_STACK_SIZE = 0x1000;
_EL1_STACK_SIZE = 0x2000;
_EL2_STACK_SIZE = 0x8000;

MEMORY
{
    /* Matches CONFIG_TEXT_BASE and the BL33 load/entry in GSRD u-boot.itb. */
    DDR (rwx) : ORIGIN = 0x80200000, LENGTH = 0x200000
}

SECTIONS
{
    .text : {
        _start_text = .;
        KEEP (*(.vectors))
        *(.boot)
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        *(.plt)
        *(.ARM.extab*)
        _end_text = .;
    } > DDR

    .rodata : {
        . = ALIGN(64);
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
    } > DDR

    .keystore : {
        . = ALIGN(8);
        KEEP (*(.keystore))
    } > DDR

    .data : {
        . = ALIGN(64);
        _start_data = .;
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
        _end_data = .;
        __data_end = .;
    } > DDR

    .mmu_tbl0 (ALIGN(4096)) : {
        *(.mmu_tbl0)
    } > DDR
    .mmu_tbl1 (ALIGN(4096)) : {
        *(.mmu_tbl1)
    } > DDR
    .mmu_tbl2 (ALIGN(4096)) : {
        *(.mmu_tbl2)
    } > DDR

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > DDR

    .ramcode : {
        . = ALIGN(64);
        *(.ramcode .ramcode.*)
    } > DDR

    .bss (NOLOAD) : {
        . = ALIGN(64);
        __bss_start__ = .;
        _start_bss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(64);
        __bss_end__ = .;
        _end_bss = .;
    } > DDR

    .heap (NOLOAD) : {
        . = ALIGN(64);
        _heap = .;
        . += _HEAP_SIZE;
        __heap_end = .;
    } > DDR

    .stack (NOLOAD) : {
        . = ALIGN(64);
        . += 0x1000;
        __el3_stack = .;
        . += _EL2_STACK_SIZE;
        __el2_stack = .;
        . += _EL1_STACK_SIZE;
        __el1_stack = .;
        . += _EL0_STACK_SIZE;
        __el0_stack = .;
    } > DDR

    _end = .;
    ASSERT(_end <= ORIGIN(DDR) + LENGTH(DDR),
        "wolfBoot Agilex 5 image exceeds the 2 MiB BL33 reservation")
}

PROVIDE(_start_vector = _start_text);
PROVIDE(__stack = __el2_stack);
PROVIDE(_stack_base = .);
