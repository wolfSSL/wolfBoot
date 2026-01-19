/**
 * RISC-V vector table (32-bit and 64-bit unified)
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

#include "hal/riscv.h"

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

/* ============================================================================
 * Trap Entry/Exit Macros
 * ============================================================================ */

#if __riscv_xlen == 64

/* RV64: Save all caller-saved registers and call handle_trap */
.macro trap_entry
    addi sp, sp, -32 * REGBYTES
    STORE x1,   1 * REGBYTES(sp)
    STORE x2,   2 * REGBYTES(sp)
    STORE x3,   3 * REGBYTES(sp)
    STORE x4,   4 * REGBYTES(sp)
    STORE x5,   5 * REGBYTES(sp)
    STORE x6,   6 * REGBYTES(sp)
    STORE x7,   7 * REGBYTES(sp)
    STORE x8,   8 * REGBYTES(sp)
    STORE x9,   9 * REGBYTES(sp)
    STORE x10, 10 * REGBYTES(sp)
    STORE x11, 11 * REGBYTES(sp)
    STORE x12, 12 * REGBYTES(sp)
    STORE x13, 13 * REGBYTES(sp)
    STORE x14, 14 * REGBYTES(sp)
    STORE x15, 15 * REGBYTES(sp)
    STORE x16, 16 * REGBYTES(sp)
    STORE x17, 17 * REGBYTES(sp)
    STORE x18, 18 * REGBYTES(sp)
    STORE x19, 19 * REGBYTES(sp)
    STORE x20, 20 * REGBYTES(sp)
    STORE x21, 21 * REGBYTES(sp)
    STORE x22, 22 * REGBYTES(sp)
    STORE x23, 23 * REGBYTES(sp)
    STORE x24, 24 * REGBYTES(sp)
    STORE x25, 25 * REGBYTES(sp)
    STORE x26, 26 * REGBYTES(sp)
    STORE x27, 27 * REGBYTES(sp)
    STORE x28, 28 * REGBYTES(sp)
    STORE x29, 29 * REGBYTES(sp)
    STORE x30, 30 * REGBYTES(sp)
    STORE x31, 31 * REGBYTES(sp)

    csrr a0, MODE_PREFIX(cause)
    csrr a1, MODE_PREFIX(epc)
    csrr a2, MODE_PREFIX(tval)

    mv a3, sp
    jal handle_trap
    csrw MODE_PREFIX(epc), a0

.endm

.macro trap_exit
    LOAD x1,   1 * REGBYTES(sp)
    LOAD x3,   3 * REGBYTES(sp)
    LOAD x4,   4 * REGBYTES(sp)
    LOAD x5,   5 * REGBYTES(sp)
    LOAD x6,   6 * REGBYTES(sp)
    LOAD x7,   7 * REGBYTES(sp)
    LOAD x8,   8 * REGBYTES(sp)
    LOAD x9,   9 * REGBYTES(sp)
    LOAD x10, 10 * REGBYTES(sp)
    LOAD x11, 11 * REGBYTES(sp)
    LOAD x12, 12 * REGBYTES(sp)
    LOAD x13, 13 * REGBYTES(sp)
    LOAD x14, 14 * REGBYTES(sp)
    LOAD x15, 15 * REGBYTES(sp)
    LOAD x16, 16 * REGBYTES(sp)
    LOAD x17, 17 * REGBYTES(sp)
    LOAD x18, 18 * REGBYTES(sp)
    LOAD x19, 19 * REGBYTES(sp)
    LOAD x20, 20 * REGBYTES(sp)
    LOAD x21, 21 * REGBYTES(sp)
    LOAD x22, 22 * REGBYTES(sp)
    LOAD x23, 23 * REGBYTES(sp)
    LOAD x24, 24 * REGBYTES(sp)
    LOAD x25, 25 * REGBYTES(sp)
    LOAD x26, 26 * REGBYTES(sp)
    LOAD x27, 27 * REGBYTES(sp)
    LOAD x28, 28 * REGBYTES(sp)
    LOAD x29, 29 * REGBYTES(sp)
    LOAD x30, 30 * REGBYTES(sp)
    LOAD x31, 31 * REGBYTES(sp)
    LOAD x2,   2 * REGBYTES(sp)
    addi sp, sp, 32 * REGBYTES

    MODE_PREFIX(ret)

.endm

#else /* __riscv_xlen == 32 */

/* RV32: Save caller-saved registers (minimal set) */
.macro trap_entry
    addi sp, sp, -64
    STORE x1,  0(sp)
    STORE x5,  4(sp)
    STORE x6,  8(sp)
    STORE x7,  12(sp)
    STORE x10, 16(sp)
    STORE x11, 20(sp)
    STORE x12, 24(sp)
    STORE x13, 28(sp)
    STORE x14, 32(sp)
    STORE x15, 36(sp)
    STORE x16, 40(sp)
    STORE x17, 44(sp)
    STORE x28, 48(sp)
    STORE x29, 52(sp)
    STORE x30, 56(sp)
    STORE x31, 60(sp)
.endm

.macro trap_exit
    LOAD x1,  0(sp)
    LOAD x5,  4(sp)
    LOAD x6,  8(sp)
    LOAD x7,  12(sp)
    LOAD x10, 16(sp)
    LOAD x11, 20(sp)
    LOAD x12, 24(sp)
    LOAD x13, 28(sp)
    LOAD x14, 32(sp)
    LOAD x15, 36(sp)
    LOAD x16, 40(sp)
    LOAD x17, 44(sp)
    LOAD x28, 48(sp)
    LOAD x29, 52(sp)
    LOAD x30, 56(sp)
    LOAD x31, 60(sp)
    addi sp, sp, 64
    mret
.endm

#endif /* __riscv_xlen */

/* ============================================================================
 * Vector Table
 * ============================================================================ */

.section .isr_vector
.align 8

#if __riscv_xlen == 64
.global trap_vector_table
trap_vector_table:
#else
.global IV
IV:
#endif
    j _synctrap
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN
    j trap_empty
    .align VECTOR_ALIGN

_synctrap:
    trap_entry
    trap_exit

trap_empty:
    nop
