/* imx95_a55_stage1_start.S - entry for the wolfBoot i.MX95 stage 1.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/* Entered at EL3 with whatever MMU and cache state the previous stage left. The
 * MMU and both caches go off up front and stay off, after writing back what was
 * dirty - turning the cache off does not do that, and those lines would be lost. */

    .section ".boot","ax"
    .global _boot
_boot:
    /* Vectors first, so a fault during setup stops here instead of running
     * into whatever the previous stage left at address 0. */
    adr     x0, _stage1_vectors
    msr     VBAR_EL3, x0
    isb

    /* Let the console's printf use SIMD: its variadic prologue saves q0-q7,
     * which traps unless EL3 has FP enabled. The previous stage may or may not
     * have done this. */
    msr     CPTR_EL3, xzr
    isb

    bl      invalidate_dcaches

    /* SCTLR_EL3: clear M (MMU), C (data cache) and I (instruction cache). */
    mrs     x0, SCTLR_EL3
    mov     x1, #1                  /* M */
    orr     x1, x1, #(1 << 2)       /* C */
    orr     x1, x1, #(1 << 12)      /* I */
    bic     x0, x0, x1
    msr     SCTLR_EL3, x0
    isb

    tlbi    alle3
    ic      iallu
    dsb     sy
    isb

    ldr     x0, =END_STACK
    mov     sp, x0

    /* Zero .bss. The linker script aligns both ends to 8. */
    ldr     x0, =_start_bss
    ldr     x1, =_end_bss
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b

2:  bl      imx95_stage1_main
    /* Only reached if the load failed; imx95_stage1_main does not return on
     * success. */
_stage1_halt:
    wfi
    b       _stage1_halt

/* Every exception reports itself and then halts. The console is up for all but
 * the first few instructions, and a fault that names its cause is worth far
 * more during bring-up than a silent spin. */
_stage1_fault:
    mrs     x0, ESR_EL3
    mrs     x1, ELR_EL3
    mrs     x2, FAR_EL3
    ldr     x3, =END_STACK
    mov     sp, x3
    bl      imx95_stage1_fault
    b       _stage1_halt

    .macro  stage1_vector
    .align  7
    b       _stage1_fault
    .endm

    .align  11
_stage1_vectors:
    stage1_vector       /* Current EL, SP0:  Synchronous */
    stage1_vector       /*                   IRQ         */
    stage1_vector       /*                   FIQ         */
    stage1_vector       /*                   SError      */
    stage1_vector       /* Current EL, SPx:  Synchronous */
    stage1_vector       /*                   IRQ         */
    stage1_vector       /*                   FIQ         */
    stage1_vector       /*                   SError      */
    stage1_vector       /* Lower EL, AArch64 Synchronous */
    stage1_vector       /*                   IRQ         */
    stage1_vector       /*                   FIQ         */
    stage1_vector       /*                   SError      */
    stage1_vector       /* Lower EL, AArch32 Synchronous */
    stage1_vector       /*                   IRQ         */
    stage1_vector       /*                   FIQ         */
    stage1_vector       /*                   SError      */
