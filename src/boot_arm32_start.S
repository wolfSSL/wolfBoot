/* boot_arm32_start.S
 *
 * Generic ARMv7-A 32-bit (Cortex-A5/A7/A8/A9/A15/A17) startup for wolfBoot.
 * Performs the minimum CPU setup that every standalone image needs before
 * running C code:
 *
 *   1. mask IRQ + FIQ, force SVC mode
 *   2. set VBAR to wolfBoot's vector table (so aborts route to us, not
 *      to whatever the bootloader/BootROM left vectors pointing at)
 *   3. clear SCTLR V (high vectors), A (alignment fault), C (D-cache),
 *      I (I-cache) bits. Leave M (MMU) alone so we inherit a flat 1:1
 *      mapping from the bootloader if it set one up - disabling MMU on
 *      ARMv7-A would treat all memory as Strongly-Ordered and fault on
 *      unaligned accesses (e.g. C string ops on 2-byte aligned literals)
 *   4. invalidate TLB, I-cache, branch predictor
 *   5. set up per-mode stacks (SVC/IRQ/FIQ/ABT/UND) carved below _stack_top
 *   6. copy .data, zero .bss, enable async aborts, jump to main
 *
 * Modeled after the Xilinx standalone BSP cortexa9/gcc/boot.S, generalized
 * for any ARMv7-A target. Used by both SAMA5D3 (Cortex-A5) and Zynq-7000
 * (Cortex-A9).
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

    .arm
    .section start, "ax"

    .globl reset_vector_entry
    .align 4
reset_vector_entry:
_vector_table:
    b   isr_reset           /* 0x00 reset */
    b   isr_undef           /* 0x04 undefined */
    b   isr_swi             /* 0x08 swi */
    b   isr_pabt            /* 0x0C prefetch abort */
    b   isr_dabt            /* 0x10 data abort */
    .word _romsize          /* 0x14 (size word, kept for FSBL/BootROM parity) */
    b   isr_irq             /* 0x18 IRQ */
    b   isr_fiq             /* 0x1C FIQ */

isr_undef:  b isr_undef
isr_swi:    b isr_swi
isr_pabt:   b isr_pabt
isr_dabt:   b isr_dabt
isr_irq:    b isr_irq
isr_fiq:    b isr_fiq

isr_reset:
    /* 1. Mask IRQ + FIQ, force SVC mode. */
    cpsid   if
    mrs     r0, cpsr
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13           /* SVC mode */
    msr     cpsr_c, r0

    /* 2. Set VBAR to our vector table (the load address). */
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0

    /* 3. Adjust SCTLR. Keep MMU bit (M) as the bootloader left it - if it
     * set up flat 1:1 mapping (Xilinx FSBL on Zynq-7000 does), we want it
     * on so unaligned LDR/STR from C code doesn't fault. Clear V (high
     * vectors), A (alignment fault check), C (D-cache), I (I-cache). */
    mrc     p15, 0, r1, c1, c0, 0
    bic     r1, r1, #(1 << 13)      /* V bit */
    bic     r1, r1, #(1 << 1)       /* A bit */
    bic     r1, r1, #(1 << 2)       /* C bit (D-cache) */
    bic     r1, r1, #(1 << 12)      /* I bit (I-cache) */
    mcr     p15, 0, r1, c1, c0, 0
    dsb
    isb

    /* 4. Invalidate TLB, I-cache, branch predictor. Leave D-cache alone -
     * if the bootloader has dirty lines we'd need clean+invalidate by
     * set/way (CSSELR/CCSIDR walk), which is risky in startup. */
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0   /* TLBIALL */
    mcr     p15, 0, r0, c7, c5, 0   /* ICIALLU */
    mcr     p15, 0, r0, c7, c5, 6   /* BPIALL */
    dsb
    isb

    /* 5. Set stack pointers for IRQ/FIQ/ABT/UND/SVC modes. Each gets a
     * 1 KB slice carved below _stack_top:
     *   _stack_top      <- top
     *      0x000        SVC (sys/usr) - main wolfBoot stack (largest)
     *      0x800        IRQ
     *      0xC00        FIQ
     *      0x1000       ABT
     *      0x1400       UND
     */
    mrs     r0, cpsr
    bic     r0, r0, #0x1f

    orr     r1, r0, #0x12           /* IRQ */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x800)

    orr     r1, r0, #0x11           /* FIQ */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0xC00)

    orr     r1, r0, #0x17           /* ABT */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x1000)

    orr     r1, r0, #0x1b           /* UND */
    msr     cpsr_c, r1
    ldr     sp, =(_stack_top - 0x1400)

    orr     r1, r0, #0x13           /* SVC (where main runs) */
    msr     cpsr_c, r1
    ldr     sp, =_stack_top

    /* Save BootROM r4 (boot source info on some platforms; ignored by
     * platforms that don't use it). */
    push    {r4}

    /* 6. Copy .data section (LMA -> VMA). LMA == VMA in the standard
     * wolfBoot linker scripts, so this is usually a no-op. */
    ldr     r2, =_lp_data
    ldmia   r2, {r1, r3, r4}
1:  cmp     r3, r4
    ldrcc   r2, [r1], #4
    strcc   r2, [r3], #4
    bcc     1b

    /* Zero .bss */
    adr     r2, _lp_bss
    ldmia   r2, {r3, r4}
    mov     r2, #0
1:  cmp     r3, r4
    strcc   r2, [r3], #4
    bcc     1b

    /* Enable async-abort delivery (clear A bit in CPSR) so we get an
     * exception now if the bus throws one, rather than later when state
     * is harder to recover. */
    mrs     r0, cpsr
    bic     r0, r0, #(1 << 8)
    msr     cpsr_xsf, r0

    /* Jump to main(). */
    ldr     r4, =main
    mov     lr, pc
    bx      r4

_panic:
    b       _panic

    .align
_lp_data:
    .word   _start_data
    .word   _end_data
_lp_bss:
    .word   _start_bss
    .word   _end_bss
