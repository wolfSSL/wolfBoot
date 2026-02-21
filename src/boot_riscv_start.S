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


/* ============================================================================
 * RISC-V Boot Entry Point
 * ============================================================================
 *
 * For RV64 S-mode (typically running under SBI):
 *   Entry conditions (passed by prior boot stage / SBI):
 *     a0 = hart ID (hardware thread identifier)
 *     a1 = pointer to device tree blob (DTB) in memory
 *
 * For RV64 M-mode (direct boot from eNVM):
 *   Runs as first code after reset, reads hart ID from CSR
 *   Must initialize all hardware from scratch
 *
 * For RV32 (typically bare metal):
 *   Starts fresh, reads hart ID from CSR
 *
 * ============================================================================ */
.section .init
.globl _reset
_reset:
#if __riscv_xlen == 64

#ifdef WOLFBOOT_RISCV_MMODE
    /* ========================================================================
     * RV64 Machine Mode Boot Sequence
     *
     * This path is for direct boot from eNVM on PolarFire SoC.
     * Entry point (_reset) is in eNVM at 0x20220100.
     *
     * Boot flow:
     * 1. Run early init from eNVM (this .init section)
     * 2. Copy main code from eNVM to L2 SRAM
     * 3. Jump to L2 SRAM to continue execution
     * 4. Initialize .data and .bss sections
     * 5. Handle multi-hart (E51 runs, U54s park in WFI)
     * ======================================================================== */

#if 0 //def TARGET_mpfs250
    /*
     * Early UART initialization for M-mode boot on MPFS.
     * This enables debug output during the boot sequence.
     */
    li    t0, 0x20000000           /* UART0 base */
    li    t1, 0x20002000           /* SYSREG_BASE */
    /* Enable UART0 clock */
    lw    t2, 0x84(t1)             /* Read SUBBLK_CLOCK_CR */
    ori   t2, t2, 0x20             /* Set bit 5 (MMUART0) */
    sw    t2, 0x84(t1)
    /* Release UART0 from reset */
    lw    t2, 0x88(t1)             /* Read SOFT_RESET_CR */
    li    t3, 0xFFFFFFDF           /* ~0x20 - Clear bit 5 */
    and   t2, t2, t3
    sw    t2, 0x88(t1)
    /* Set up UART for 115200 @ 40MHz: divisor ~ 22 */
    lbu   t2, 0x0c(t0)             /* Read LCR */
    ori   t2, t2, 0x80             /* Set DLAB */
    sb    t2, 0x0c(t0)
    li    t2, 22                   /* Divisor */
    sb    t2, 0x00(t0)             /* DLR */
    sb    zero, 0x04(t0)           /* DMR */
    lbu   t2, 0x0c(t0)
    andi  t2, t2, 0x7f             /* Clear DLAB */
    ori   t2, t2, 0x03             /* 8N1 */
    sb    t2, 0x0c(t0)
#endif

    /*
     * Clear the Return Address Stack (RAS) by executing nested calls.
     * This prevents stale return addresses from causing misprediction.
     */
    call  .L_clear_ras

    /*
     * Read hart ID from CSR (we're the first code running on this core)
     */
    csrr  a0, mhartid
    mv    tp, a0                      /* Save hart ID in tp for later use */

    /*
     * Disable and clear all interrupts during initialization
     */
    li    t0, MSTATUS_MIE
    csrc  mstatus, t0                 /* Clear global interrupt enable */
    csrw  mie, zero                   /* Disable all interrupt sources */
    csrw  mip, zero                   /* Clear any pending interrupts */

    /*
     * Initialize M-mode CSRs
     */
    csrw  mscratch, zero
    csrw  mcause, zero
    csrw  mepc, zero

    /*
     * Clear PMP configuration (allow all access initially)
     */
    csrw  pmpcfg0, zero
    csrw  pmpcfg2, zero

    /*
     * Check if this is the boot hart (hart 0 = E51 on MPFS)
     * Other harts (U54 cores) should wait in eNVM (this .init section)
     */
    mv    a0, tp                      /* Get saved hart ID */
    bnez  a0, .L_secondary_hart_wait_envm

#ifdef TARGET_mpfs250
    /*
     * Configure L2 Cache for LIM/Scratchpad usage BEFORE copying to L2
     * Must enable ways AND disable shutdown before accessing L2 scratchpad.
     * L2_WAY_ENABLE (0x02010008) = 0x0B (enable ways 0-3 and 8-11)
     * SYSREG_L2_SHUTDOWN_CR (0x20002174) = 0 (disable shutdown)
     */
    li    t1, 0x02010000           /* L2_CTRL_BASE */
    li    t2, 0x0B                 /* Ways 0-3 (cache) + 8-11 (scratchpad) */
    sd    t2, 8(t1)                /* L2_WAY_ENABLE */
    fence

    li    t1, 0x20002000           /* SYSREG_BASE */
    sw    zero, 0x174(t1)          /* SYSREG_L2_SHUTDOWN_CR = 0 */
    fence
#endif

    /*
     * Copy .text section from eNVM to L2 SRAM
     *
     * NOTE: We CANNOT use 'la' for L2 SRAM addresses because 'la' uses PC-relative
     * addressing (auipc+addi) which fails when the offset exceeds 32-bit range.
     * eNVM is at 0x20220000, L2 SRAM is at 0x0A000000 - the difference wraps around
     * in 32-bit arithmetic causing incorrect addresses.
     *
     * Solution: Use absolute addressing for destination and size.
     * - Source (_stored_text): Use 'la' - same address range as this code
     * - Dest: Use 'li' with hardcoded L2 SRAM base address
     * - Size: Use 'li' with value from linker (updated by linker script)
     *
     * The values are read from the data embedded right after this code.
     */
    la    t0, _stored_text            /* Source: eNVM - la works, same address range */
    la    t3, _copy_params            /* Load parameters from eNVM */
    ld    t1, 0(t3)                   /* t1 = dest addr (from linker) */
    ld    t2, 8(t3)                   /* t2 = byte count (from linker) */
    add   t2, t1, t2                  /* t2 = end address */

.L_copy_text:
    bgeu  t1, t2, .L_copy_text_done   /* if dest >= end, done */
    ld    t3, 0(t0)
    sd    t3, 0(t1)
    addi  t0, t0, 8
    addi  t1, t1, 8
    j     .L_copy_text
.L_copy_text_done:

    /* Flush instruction cache to ensure copied code is visible */
    fence.i

    /*
     * Jump to main initialization code in L2 SRAM
     * From here on, we execute from L2 SRAM (faster than eNVM)
     */
    la    t0, .L_sram_entry
    jr    t0

/*
 * Clear Return Address Stack helper (in .init section, runs from eNVM)
 * Two-deep nested calls to flush RAS prediction state
 */
.L_clear_ras:
    mv    t0, ra
    nop
    call  .L_clear_ras_inner
    nop
    mv    ra, t0
    ret

.L_clear_ras_inner:
    nop
    nop
    ret

/*
 * Copy parameters: These values are used by the copy loop.
 * They are stored as data in the .init section (eNVM) so they can be loaded
 * with 'la' which works for addresses in the same region as the PC.
 * The linker will fill these in at link time.
 */
.align 3
_copy_params:
    .dword _start_text_sram   /* Destination address (L2 SRAM) */
    .dword _text_size         /* Number of bytes to copy */

/*
 * Secondary harts (U54 cores) initialization and WFI loop
 * In M-mode on MPFS, the E51 (hart 0) is the boot hart.
 * U54 cores (harts 1-4) wait here until explicitly woken by E51.
 *
 * The protocol uses Hart Local Storage (HLS) for communication:
 * 1. Secondary hart signals HLS_OTHER_HART_IN_WFI
 * 2. Main hart sends IPI via CLINT_MSIP
 * 3. Secondary hart wakes, signals HLS_OTHER_HART_PASSED_WFI
 * 4. Secondary hart jumps to secondary_hart_entry()
 */
.L_secondary_hart_wait_envm:
    /* Disable all interrupts initially */
    li    t0, MSTATUS_MIE
    csrc  mstatus, t0
    csrw  mie, zero
    csrw  mip, zero

    /* Enable only machine software interrupt (for IPI wake) */
    li    t0, MIP_MSIP
    csrw  mie, t0

    /* Set up stack for this hart - each hart gets STACK_SIZE_PER_HART bytes
     * Stack layout: hart0 stack, hart1 stack, ... hart4 stack
     * We need to calculate: stack_base + (hartid + 1) * STACK_SIZE_PER_HART */
    csrr  a0, mhartid
    la    t0, _secondary_hart_stack_base
    li    t1, STACK_SIZE_PER_HART
    addi  a1, a0, 1                    /* hartid + 1 */
    mul   t2, a1, t1                   /* (hartid + 1) * STACK_SIZE_PER_HART */
    add   sp, t0, t2                   /* stack top for this hart */

    /* Align stack to 16 bytes */
    li    t0, -16
    and   sp, sp, t0

    /* Allocate HLS at top of stack */
    addi  sp, sp, -64                  /* HLS_DEBUG_AREA_SIZE = 64 */
    mv    s11, sp                      /* Save HLS pointer in s11 (callee-saved) */

    /* Clear HLS area */
    sd    zero, 0(s11)
    sd    zero, 8(s11)
    sd    zero, 16(s11)
    sd    zero, 24(s11)
    sd    zero, 32(s11)
    sd    zero, 40(s11)
    sd    zero, 48(s11)
    sd    zero, 56(s11)

    /* Wait for main hart (E51) to signal it has started
     * Main hart sets HLS_MAIN_HART_STARTED at its HLS location */
    li    t3, 0x12344321               /* HLS_MAIN_HART_STARTED */
    la    t1, _main_hart_hls           /* E51's HLS location */

.L_wait_main_hart:
    lwu   t2, 0(t1)                    /* Read main hart's indicator */
    bne   t3, t2, .L_wait_main_hart    /* Wait until main hart started */

    /* Signal that we're in WFI state */
    li    t0, 0x12345678               /* HLS_OTHER_HART_IN_WFI */
    sw    t0, 0(s11)                   /* Write to our HLS */

    /* Memory barrier to ensure write is visible */
    fence iorw, iorw

.L_secondary_wfi_loop:
    wfi

    /* Check if it was a software interrupt (IPI) */
    csrr  t0, mip
    andi  t0, t0, MIP_MSIP
    beqz  t0, .L_secondary_wfi_loop    /* Keep waiting if not software interrupt */

    /* Clear the software interrupt by writing 0 to CLINT_MSIP */
    csrr  a0, mhartid
    li    t0, 0x02000000               /* CLINT_BASE */
    slli  t1, a0, 2                    /* hartid * 4 */
    add   t0, t0, t1                   /* CLINT_MSIP[hartid] */
    sw    zero, 0(t0)                  /* Clear MSIP */

    /* Signal that we've passed WFI */
    li    t0, 0x87654321               /* HLS_OTHER_HART_PASSED_WFI */
    sw    t0, 0(s11)

    /* Memory barrier */
    fence iorw, iorw

    /* Flush instruction cache */
    fence.i

    /* Jump to secondary hart C entry point
     * a0 = hartid (already in a0 from mhartid read)
     * s11 = HLS pointer
     * Use indirect jump since secondary_hart_entry is in .text (L2 SRAM)
     * and we're in .init (eNVM) - too far for direct jump */
    csrr  a0, mhartid
    mv    a1, s11                      /* Pass HLS pointer as second arg */
    la    t0, secondary_hart_entry
    jr    t0

/*
 * ============================================================================
 * Code below this point runs from L2 SRAM after being copied from eNVM
 * ============================================================================
 */
.section .text
.L_sram_entry:
    /*
     * Setup trap handler (now pointing to SRAM location)
     */
    la    t0, trap_vector_table
    csrw  mtvec, t0
    /* Ensure mtvec is updated before continuing */
1:
    csrr  t1, mtvec
    bne   t0, t1, 1b

    /*
     * Initialize all general-purpose registers to zero
     * (tp/x4 already holds hart ID, skip it)
     */
    li    x1, 0
    li    x2, 0
    li    x3, 0
    /* x4 (tp) = hart ID, don't clear */
    li    x5, 0
    li    x6, 0
    li    x7, 0
    li    x8, 0
    li    x9, 0
    li    x10, 0
    li    x11, 0
    li    x12, 0
    li    x13, 0
    li    x14, 0
    li    x15, 0
    li    x16, 0
    li    x17, 0
    li    x18, 0
    li    x19, 0
    li    x20, 0
    li    x21, 0
    li    x22, 0
    li    x23, 0
    li    x24, 0
    li    x25, 0
    li    x26, 0
    li    x27, 0
    li    x28, 0
    li    x29, 0
    li    x30, 0
    li    x31, 0

    /*
     * Verify XLEN matches compilation (64-bit)
     * MSB of misa is 1 for RV32, 2 for RV64
     */
.L_xlen_check:
    csrr  t0, misa
    bltz  t0, .L_xlen_ok              /* RV64: MSB set means negative */
    j     .L_xlen_check               /* Loop if not RV64 */
.L_xlen_ok:

    /*
     * Initialize global pointer for efficient small data access
     * The linker provides __global_pointer$ at .sdata + 0x800
     */
    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop

    /*
     * Initialize stack pointer from linker symbol
     * Stack is at end of L2_SCRATCH, grows downward
     */
    la    sp, _end_stack
    li    t0, -16
    and   sp, sp, t0                  /* Ensure 16-byte alignment */
    mv    s0, sp                      /* Set frame pointer */

    /*
     * Primary hart (E51, hart 0) continues with initialization
     * (Secondary harts already parked in .init section)
     */

    /*
     * Copy .data section from flash to RAM
     */
    la    t0, _stored_data            /* Source: flash */
    la    t1, _start_data             /* Dest: RAM */
    la    t2, _end_data
    beq   t0, t1, .L_data_copy_done   /* Skip if already in place */
.L_data_copy:
    beq   t1, t2, .L_data_copy_done
    ld    t3, 0(t0)
    sd    t3, 0(t1)
    addi  t0, t0, 8
    addi  t1, t1, 8
    j     .L_data_copy
.L_data_copy_done:

    /*
     * Clear .bss section
     */
    la    t0, _start_bss
    la    t1, _end_bss
.L_bss_clear:
    beq   t0, t1, .L_bss_clear_done
    sd    zero, 0(t0)
    addi  t0, t0, 8
    j     .L_bss_clear
.L_bss_clear_done:

#ifndef TARGET_mpfs250
    /*
     * Clear bus error unit accrued register on start-up
     * This is cleared by the first hart only
     * NOTE: Only for SiFive cores - MPFS does not have BEU at these addresses
     */
    la a4,0x01700020UL
    sb   x0, 0(a4)
    la a4,0x01701020UL
    sb   x0, 0(a4)
    la a4,0x01702020UL
    sb   x0, 0(a4)
    la a4,0x01703020UL
    sb   x0, 0(a4)
    la a4,0x01704020UL
    sb   x0, 0(a4)
#endif

    /*
     * Jump to C entry point
     * a0 = hart ID
     */
    mv    a0, tp
    j     main

#else
    /* ========================================================================
     * RV64 Supervisor Mode Boot Sequence
     *
     * Entry conditions (passed by prior boot stage / SBI):
     *   a0 = hart ID (hardware thread identifier)
     *   a1 = pointer to device tree blob (DTB) in memory
     * ======================================================================== */

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
     * register (stvec in S-mode). All synchronous exceptions
     * and interrupts will vector through this table.
     */
    la    t0, trap_vector_table
    csrw  stvec, t0

    /*
     * Disable all interrupt sources initially by clearing the
     * interrupt-enable register (sie). This prevents spurious
     * interrupts during early initialization.
     */
    csrw  sie, zero

    /*
     * Enable interrupt sources:
     * - Software Interrupts (IPIs) for multi-hart boot coordination
     * - External Interrupts for PLIC-routed peripheral interrupts (e.g., MMC)
     */
    li    t0, (SIE_SSIE | SIE_SEIE)
    csrs  sie, t0

    /*
     * Enable global interrupts by setting the SIE bit in sstatus.
     * Without this, the CPU will never take interrupts regardless of the
     * per-source enables in sie.
     */
    li    t0, SSTATUS_SIE
    csrs  sstatus, t0

    /*
     * Initialize stack pointer:
     *   WOLFBOOT_STACK_TOP = 0x80200000 (S-mode, in DDR)
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

#endif /* WOLFBOOT_RISCV_MMODE */

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

