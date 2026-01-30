/**
 * RISC-V bootup (32-bit and 64-bit unified)
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

/* MODE_PREFIX is now defined in hal/riscv.h */

/* ============================================================================
 * RISC-V Boot Entry Point
 * ============================================================================
 *
 * For RV64 (typically running under SBI):
 *   Entry conditions (passed by prior boot stage / SBI):
 *     a0 = hart ID (hardware thread identifier)
 *     a1 = pointer to device tree blob (DTB) in memory
 *
 * For RV32 (typically bare metal):
 *   Starts fresh, reads hart ID from CSR
 *
 * ============================================================================ */
.section .init
.globl _reset
_reset:
#if __riscv_xlen == 64
    /* ---------- RV64 Boot Sequence ---------- */

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
     * Enable interrupt sources:
     * - Software Interrupts (IPIs) for multi-hart boot coordination
     * - External Interrupts for PLIC-routed peripheral interrupts (e.g., MMC)
     *
     * M-mode: MSIE (Software) + MEIE (External)
     * S-mode: SSIE (Software) + SEIE (External)
     */
#ifndef WOLFBOOT_RISCV_MMODE
    li    t0, (SIE_SSIE | SIE_SEIE)
#else
    li    t0, (MIE_MSIE | MIE_MEIE)
#endif
    csrs  MODE_PREFIX(ie), t0

    /*
     * Enable global interrupts by setting the SIE/MIE bit in sstatus/mstatus.
     * Without this, the CPU will never take interrupts regardless of the
     * per-source enables in sie/mie.
     *
     * M-mode: mstatus.MIE (bit 3)
     * S-mode: sstatus.SIE (bit 1)
     */
#ifndef WOLFBOOT_RISCV_MMODE
    li    t0, SSTATUS_SIE
#else
    li    t0, MSTATUS_MIE
#endif
    csrs  MODE_PREFIX(status), t0

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

#else /* __riscv_xlen == 32 */
    /* ---------- RV32 Boot Sequence ---------- */

    /* Initialize global pointer for position-independent access to globals */
    la gp, _global_pointer

    /* Initialize stack pointer to top of stack */
    la sp, _end_stack

    /* Set up vectored interrupt, with IV starting at offset 0x100 */
    la    t0, _start_vector
    addi  t0, t0, 1
    csrw  mtvec, t0

    /* Copy the .data section from flash to RAM */
    la    t0, _stored_data
    la    t1, _start_data
    la    t2, _end_data
1:
    bge   t1, t2, 2f
    LOAD  t3, 0(t0)
    STORE t3, 0(t1)
    addi  t0, t0, REGBYTES
    addi  t1, t1, REGBYTES
    j     1b
2:

    /* Initialize the BSS section to 0 */
    la    t0, _start_bss
    la    t1, _end_bss
3:
    bge   t0, t1, 4f
    STORE zero, 0(t0)
    addi  t0, t0, REGBYTES
    j     3b
4:

    /* Run wolfboot - main() should never return */
    j     main

#endif /* __riscv_xlen */

/* ============================================================================
 * reloc_trap_vector - Relocate trap vector table
 *
 * Parameters:
 *   a0 = pointer to new trap vector table base (uint32_t*)
 *
 * Sets the trap-vector base-address register to (address + 4),
 * accounting for the header offset in the application image.
 * ============================================================================ */
.globl reloc_trap_vector
reloc_trap_vector:
    addi  a0, a0, 4                  /* address + 1 (uint32_t* = +4 bytes) */
#if __riscv_xlen == 64
    csrw  MODE_PREFIX(tvec), a0
#else
    csrw  mtvec, a0
#endif
    ret

