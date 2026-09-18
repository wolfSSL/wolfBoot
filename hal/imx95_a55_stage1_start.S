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

/* Entered at EL3 with whatever MMU and cache state the previous stage was
 * using. Everything this stage touches is either Device memory or DRAM that a
 * later stage reads with its own caches off, so the MMU and both caches are
 * turned off up front and left off: no maintenance to get wrong, and every
 * store has reached memory by the time BL31 is entered. What the previous
 * stage left dirty is written back first - turning the cache off does not do
 * that, and anything still in it would simply be lost. */

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

    bl      _stage1_flush_dcache

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

/* Clean and invalidate every data cache level to the point of coherency, by
 * set/way. Only correct because this runs on the one core that is out of
 * reset; it is the standard sequence for the moment before the cache is
 * turned off. */
_stage1_flush_dcache:
    dsb     sy
    mrs     x0, CLIDR_EL1
    ubfx    w2, w0, #24, #3         /* level of coherency */
    cbz     w2, 5f
    mov     w1, #0                  /* level iterator */
1:  add     w3, w1, w1, lsl #1
    lsr     w3, w0, w3
    ubfx    w3, w3, #0, #3          /* cache type at this level */
    cmp     w3, #2
    b.lt    4f
    lsl     w4, w1, #1
    msr     CSSELR_EL1, x4
    isb
    mrs     x4, CCSIDR_EL1
    ubfx    w3, w4, #0, #3
    add     w3, w3, #2              /* log2(line size) */
    ubfx    w5, w4, #13, #15        /* last set */
    ubfx    w4, w4, #3, #10         /* last way */
    clz     w6, w4
2:  mov     w8, w4
3:  lsl     w7, w1, #1
    lsl     w9, w5, w3
    orr     w7, w7, w9
    lsl     w9, w8, w6
    orr     w7, w7, w9
    dc      CISW, x7
    subs    w8, w8, #1
    b.ge    3b
    subs    w5, w5, #1
    b.ge    2b
4:  add     w1, w1, #1
    cmp     w2, w1
    b.gt    1b
5:  dsb     sy
    isb
    ret

    /* Place the literal pool here rather than letting the assembler put it
     * after the vector table, where it would sit in the padding of a vector
     * entry. */
    .ltorg

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
