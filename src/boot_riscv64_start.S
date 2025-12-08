/**
 * RISC-V 64-bit bootup
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

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

/*
 * ============================================================
 * RISC-V 64-bit Boot Entry Point
 * ============================================================
 * Entry conditions (passed by prior boot stage / SBI):
 *   a0 = hart ID (hardware thread identifier)
 *   a1 = pointer to device tree blob (DTB) in memory
 * ============================================================
 */
.section .init
.globl _reset
_reset:
#ifdef WOLFBOOT_RISCV_MMODE
    /*
     * Machine Mode: Read hart ID from CSR since we're the first code
     * running on this core. In Supervisor mode, the SBI passes it in a0.
     */
    csrr  a0, CSR_MHARTID
#endif

    /*
     * Preserve boot parameters in callee-saved registers:
     *   tp (x4) = hart ID - Used for multi-hart coordination. The RISC-V ABI
     *             reserves tp as thread-local storage pointer, which C code
     *             won't clobber, making it safe for inter-hart communication.
     *   s1 (x9) = DTB pointer - Saved for later use (e.g., passing to kernel)
     */
    mv    tp, a0
    mv    s1, a1

    /*
     * Initialize global pointer to zero as a safety measure.
     * If an exception occurs before the C runtime is initialized,
     * gp=0 ensures predictable behavior rather than random memory access.
     */
    mv    gp, zero

    /*
     * Configure trap/exception handler:
     * Load address of trap_vector_table into the trap-vector base-address
     * register (mtvec in M-mode, stvec in S-mode). All synchronous exceptions
     * and interrupts will vector through this table.
     */
    la    t0, trap_vector_table
    csrw  MODE_PREFIX(tvec), t0

    /*
     * Disable all interrupt sources initially by clearing the
     * interrupt-enable register (mie/sie). This prevents spurious
     * interrupts during early initialization.
     */
    csrw  MODE_PREFIX(ie), zero

    /*
     * Selectively enable Software Interrupts (IPIs) only.
     * IPIs are required for multi-hart boot coordination - secondary harts
     * may need to be woken via software interrupt after initialization.
     * - M-mode: Set MSIE (Machine Software Interrupt Enable) bit
     * - S-mode: Set SSIE (Supervisor Software Interrupt Enable) bit
     */
#ifdef WOLFBOOT_RISCV_SMODE
    li    t0, SIE_SSIE
#else
    li    t0, MIE_MSIE
#endif
    csrs  MODE_PREFIX(ie), t0

    /*
     * Initialize stack pointer:
     *   WOLFBOOT_STACK_TOP = 0x80000000 (M-mode) or 0x80200000 (S-mode)
     *
     * The stack grows downward from this address. RISC-V calling convention
     * requires 16-byte stack alignment, enforced by AND with -16 (0xFFFF...FFF0).
     *
     * s0 (frame pointer) is set to match sp for the initial stack frame.
     */
    li    t0, -16
    li    t1, WOLFBOOT_STACK_TOP
    and   sp, t1, t0
    mv    s0, sp

    /*
     * Set global pointer (gp) to the stack base.
     * Note: This is platform-specific. Typically gp points to a .sdata
     * section for efficient global variable access, but here it's used
     * as a known reference point for early boot.
     */
    mv    gp, s0

    /*
     * Jump to C entry point:
     *   a0 = hart ID (restored from tp for main's first argument)
     *
     * Using 'j' (jump) instead of 'call' since main() should never return.
     * The DTB pointer in s1 can be retrieved later if needed.
     */
    mv    a0, tp
    j     main

/*
 * reloc_trap_vector - Relocate trap vector table
 *
 * Parameters:
 *   a0 = pointer to new trap vector table base (uint32_t*)
 *
 * Sets the trap-vector base-address register to (address + 4),
 * accounting for the header offset in the application image.
 */
.globl reloc_trap_vector
reloc_trap_vector:
    addi  a0, a0, 4                  /* address + 1 (uint32_t* = +4 bytes) */
    csrw  MODE_PREFIX(tvec), a0
    ret
