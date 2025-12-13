/**
 * RISC-V 64-bit vector table
 * Copyright (C) 2025 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include "hal/mpfs250.h"

/* RISC-V 64-bit trap/exception handling macros */
.macro trap_entry
    addi sp, sp, -32 * REGBYTES
    sd x1,   1 * REGBYTES(sp)
    sd x2,   2 * REGBYTES(sp)
    sd x3,   3 * REGBYTES(sp)
    sd x4,   4 * REGBYTES(sp)
    sd x5,   5 * REGBYTES(sp)
    sd x6,   6 * REGBYTES(sp)
    sd x7,   7 * REGBYTES(sp)
    sd x8,   8 * REGBYTES(sp)
    sd x9,   9 * REGBYTES(sp)
    sd x10, 10 * REGBYTES(sp)
    sd x11, 11 * REGBYTES(sp)
    sd x12, 12 * REGBYTES(sp)
    sd x13, 13 * REGBYTES(sp)
    sd x14, 14 * REGBYTES(sp)
    sd x15, 15 * REGBYTES(sp)
    sd x16, 16 * REGBYTES(sp)
    sd x17, 17 * REGBYTES(sp)
    sd x18, 18 * REGBYTES(sp)
    sd x19, 19 * REGBYTES(sp)
    sd x20, 20 * REGBYTES(sp)
    sd x21, 21 * REGBYTES(sp)
    sd x22, 22 * REGBYTES(sp)
    sd x23, 23 * REGBYTES(sp)
    sd x24, 24 * REGBYTES(sp)
    sd x25, 25 * REGBYTES(sp)
    sd x26, 26 * REGBYTES(sp)
    sd x27, 27 * REGBYTES(sp)
    sd x28, 28 * REGBYTES(sp)
    sd x29, 29 * REGBYTES(sp)
    sd x30, 30 * REGBYTES(sp)
    sd x31, 31 * REGBYTES(sp)
    csrr a0, MODE_PREFIX(cause)
	csrr a1, MODE_PREFIX(epc)
	csrr a2, MODE_PREFIX(tval)
	mv a3, sp
	jal handle_trap
	csrw MODE_PREFIX(epc), a0
.endm

.macro trap_exit
	ld x1,   1 * REGBYTES(sp)
	ld x3,   3 * REGBYTES(sp)
	ld x4,   4 * REGBYTES(sp)
	ld x5,   5 * REGBYTES(sp)
	ld x6,   6 * REGBYTES(sp)
	ld x7,   7 * REGBYTES(sp)
	ld x8,   8 * REGBYTES(sp)
	ld x9,   9 * REGBYTES(sp)
	ld x10, 10 * REGBYTES(sp)
	ld x11, 11 * REGBYTES(sp)
	ld x12, 12 * REGBYTES(sp)
	ld x13, 13 * REGBYTES(sp)
	ld x14, 14 * REGBYTES(sp)
	ld x15, 15 * REGBYTES(sp)
	ld x16, 16 * REGBYTES(sp)
	ld x17, 17 * REGBYTES(sp)
	ld x18, 18 * REGBYTES(sp)
	ld x19, 19 * REGBYTES(sp)
	ld x20, 20 * REGBYTES(sp)
	ld x21, 21 * REGBYTES(sp)
	ld x22, 22 * REGBYTES(sp)
	ld x23, 23 * REGBYTES(sp)
	ld x24, 24 * REGBYTES(sp)
	ld x25, 25 * REGBYTES(sp)
	ld x26, 26 * REGBYTES(sp)
	ld x27, 27 * REGBYTES(sp)
	ld x28, 28 * REGBYTES(sp)
	ld x29, 29 * REGBYTES(sp)
	ld x30, 30 * REGBYTES(sp)
	ld x31, 31 * REGBYTES(sp)
	ld x2,   2 * REGBYTES(sp)
    addi sp, sp, 32 * REGBYTES
    mret
.endm

.section .isr_vector
.align 8

.global trap_vector_table
trap_vector_table:
    j _synctrap
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3
    j trap_empty
    .align 3

_synctrap:
    trap_entry
    trap_exit

trap_empty:
    nop
