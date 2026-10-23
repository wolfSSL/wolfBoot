/**
 * RISC-V bootup (32-bit and 64-bit unified)
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

#include "hal/riscv.h"

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif
#ifdef TARGET_mpfs250_ubl
#include "hal/mpfs250_ubl.h"
#endif


/* RV64 M-mode: direct from eNVM, a0=hartid from CSR.  RV64 S-mode: from SBI
 * with a0=hartid, a1=DTB.  RV32: bare metal, stack/gp from linker symbols. */
.section .init
.globl _reset
_reset:
#if __riscv_xlen == 64

#if defined(WOLFBOOT_RISCV_MMODE) && defined(TARGET_mpfs250_ubl)
    /* Boot mode 2 UBL stub: the SC has copied the image to LIM and released
     * all five harts, so VMA == LMA and nothing is copied. */
    csrr  a0, mhartid
    mv    tp, a0

    li    t0, MSTATUS_MIE
    csrc  mstatus, t0
    csrw  mie, zero
    csrw  mip, zero
    csrw  mscratch, zero
    csrw  mcause, zero
    csrw  mepc, zero
    csrw  pmpcfg0, zero
    csrw  pmpcfg2, zero

    la    t0, trap_vector_table
    csrw  mtvec, t0

    bnez  a0, .L_ubl_secondary_wait

    /* Close the gate before anything slow: an MSS reset leaves the previous
     * magic in DTIM, and the secondaries would jump into unverified code. */
    li    t0, MPFS_UBL_MAGIC_ADDR
    sw    zero, 0(t0)
    fence iorw, iorw

    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop

    la    sp, _end_stack
    li    t0, -16
    and   sp, sp, t0
    mv    s0, sp

    /* LIM resets with uninitialised ECC that only a doubleword store writes,
     * so cover what the SC did not copy: .bss, the heap and the stack. */
    la    t0, _start_bss
    la    t1, _end_stack
.L_ubl_lim_clear:
    beq   t0, t1, .L_ubl_lim_clear_done
    sd    zero, 0(t0)
    addi  t0, t0, 8
    j     .L_ubl_lim_clear
.L_ubl_lim_clear_done:

    mv    a0, tp
    j     main

.L_ubl_secondary_wait:
    li    t1, MPFS_UBL_MAGIC_ADDR
    li    t3, MPFS_UBL_MAGIC
    /* Wait for the boot hart to close the gate, so only a pair published by
     * this boot can release us. */
.L_ubl_wait_closed:
    lwu   t2, 0(t1)
    beq   t2, t3, .L_ubl_wait_closed
.L_ubl_wait_entry:
    lwu   t2, 0(t1)
    bne   t2, t3, .L_ubl_wait_entry
    ld    t0, (MPFS_UBL_ENTRY_ADDR - MPFS_UBL_MAGIC_ADDR)(t1)
    ld    t2, (MPFS_UBL_ENTRY_INV_ADDR - MPFS_UBL_MAGIC_ADDR)(t1)
    not   t2, t2
    bne   t0, t2, .L_ubl_wait_entry
    beqz  t0, .L_ubl_wait_entry

    /* Acknowledge, then jump; the E51 waits for the acks before it follows */
    li    t1, MPFS_UBL_ACK_BASE
    slli  t2, a0, 2
    add   t1, t1, t2
    li    t2, 1
    sw    t2, 0(t1)
    fence iorw, iorw
    fence.i
    jr    t0

#elif defined(WOLFBOOT_RISCV_MMODE)
    /* RV64 M-mode: first code from eNVM at 0x20220100 */
    call  .L_clear_ras                /* flush stale RAS prediction state */

    csrr  a0, mhartid
    mv    tp, a0

    /* Disable interrupts, clear M-mode CSRs */
    li    t0, MSTATUS_MIE
    csrc  mstatus, t0
    csrw  mie, zero
    csrw  mip, zero
    csrw  mscratch, zero
    csrw  mcause, zero
    csrw  mepc, zero
    csrw  pmpcfg0, zero
    csrw  pmpcfg2, zero

    /* Hart 0 (E51) continues; others park in .init until woken */
    mv    a0, tp
    bnez  a0, .L_secondary_hart_wait_envm

#ifdef TARGET_mpfs250
    /* Enable L2 ways (mask 0x0B: ways 0, 1, 3) and clear shutdown
     * before copying text to L2 scratchpad. */
    li    t1, 0x02010000              /* L2_CACHE_BASE */
    li    t2, 0x0B
    sd    t2, 8(t1)                   /* L2_WAY_ENABLE */
    fence
    li    t1, 0x20002000              /* SYSREG_BASE */
    sw    zero, 0x174(t1)             /* SYSREG_L2_SHUTDOWN_CR = 0 */
    fence

    /* Evict any scratchpad line left in an evictable way: the pin loop below
     * allocates only on a miss, so a hit in ways 0-7 would never move. */
    li    t0, 0x02010200              /* L2_FLUSH64 */
    li    t1, 0x0A000000              /* ZERO_DEVICE_BOTTOM */
    li    t2, 0x0A080000              /* + 512 KB */
.L_prepin_flush:
    sd    t1, 0(t0)
    addi  t1, t1, 64
    bltu  t1, t2, .L_prepin_flush
    fence

    /* Make all 512 KB resident one way at a time, storing every 64 bytes, so
     * each way fills once.  This is HSS config_l2_cache() and its marker. */
    li    t0, 0x02010828              /* L2_WAY_MASK_E51_DCACHE */
    li    t1, 0x100                   /* scratchpad way 8 */
    li    t2, 0x0A000000              /* ZERO_DEVICE_BOTTOM */
    li    t3, 0xC0FFEEBEC0010000      /* INIT_MARKER */
    li    t4, 4                       /* NUM_SCRATCH_PAD_WAYS */
.L_pin_way:
    sd    t1, 0(t0)                   /* evictions allowed from this way only */
    fence
    li    t5, 2048                    /* blocks per way */
.L_pin_block:
    sd    t3, 0(t2)                   /* first 64-bit word of each block */
    addi  t2, t2, 64
    addi  t5, t5, -1
    bnez  t5, .L_pin_block
    slli  t1, t1, 1
    fence
    addi  t4, t4, -1
    bnez  t4, .L_pin_way

    /* Prevent the E51 from evicting from the scratchpad ways. */
    li    t1, 0xFF                    /* cache ways 0-7 */
    sd    t1, 0(t0)
    fence
#endif

    /* Copy .text from eNVM to L2 SRAM.  li/ld, not la: the eNVM-to-L2 gap
     * exceeds the 32-bit PC-relative range. */
    la    t0, _stored_text
    la    t3, _copy_params
    ld    t1, 0(t3)                   /* dest (L2 SRAM base) */
    ld    t2, 8(t3)                   /* byte count */
    add   t2, t1, t2                  /* end address */

.L_copy_text:
    bgeu  t1, t2, .L_copy_text_done   /* if dest >= end, done */
    ld    t3, 0(t0)
    sd    t3, 0(t1)
    addi  t0, t0, 8
    addi  t1, t1, 8
    j     .L_copy_text
.L_copy_text_done:
    fence rw, rw
    fence.i                           /* flush icache before jumping to SRAM */

    lui   t0, %hi(.L_sram_entry)
    addi  t0, t0, %lo(.L_sram_entry)
    jr    t0

/* Two-level nested call to flush the Return Address Stack predictor */
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

/* linker-provided copy params (loaded via la, within eNVM PC range) */
.align 3
_copy_params:
    .dword _start_text_sram   /* destination (L2 SRAM) */
    .dword _text_size         /* byte count */

/* Secondary harts (U54 cores) park here until woken by E51 via CLINT IPI.
 * HLS protocol: signal IN_WFI -> wfi -> clear MSIP -> signal PASSED_WFI -> jump. */
.L_secondary_hart_wait_envm:
    li    t0, MSTATUS_MIE
    csrc  mstatus, t0
    csrw  mie, zero
    csrw  mip, zero
    li    t0, MIE_MSIE                 /* wake only on IPI */
    csrw  mie, t0

    /* Set up per-hart stack: base + hartid*STACK_SIZE_PER_HART */
    csrr  a0, mhartid
    la    t0, _secondary_hart_stack_base
    li    t1, STACK_SIZE_PER_HART
    mul   t2, a0, t1
    add   sp, t0, t2
    li    t0, -16
    and   sp, sp, t0

    addi  sp, sp, -64                  /* allocate 64-byte HLS at top of stack */
    mv    s11, sp
    /* HLS is zeroed after the wait, not here: it lives in L2 scratchpad, which
     * the boot hart has not finished carving out when it releases us. */

    /* Wait for HLS_MAIN_HART_STARTED.  MPFS250 polls the DTIM copy, not the
     * L2-scratch HLS, whose store can be lost on dirty-line eviction. */
    li    t3, 0x12344321
#ifdef TARGET_mpfs250
    li    t1, MPFS_DTIM_MAIN_STARTED_ADDR
#else
    la    t1, _main_hart_hls
#endif
.L_wait_main_hart:
    lwu   t2, 0(t1)
    bne   t3, t2, .L_wait_main_hart

    /* A warm reset does not clear CLINT, so drop a stale MSIP before arming
     * the wait or it falls straight through into a previous hand-off. */
    csrr  a0, mhartid
    li    t0, 0x02000000               /* CLINT_BASE */
    slli  t1, a0, 2
    add   t0, t0, t1
    sw    zero, 0(t0)                  /* clear stale MSIP */

    /* Scratchpad is resident now; zero the HLS block, then publish. */
    sd    zero,  0(s11)
    sd    zero,  8(s11)
    sd    zero, 16(s11)
    sd    zero, 24(s11)
    sd    zero, 32(s11)
    sd    zero, 40(s11)
    sd    zero, 48(s11)
    sd    zero, 56(s11)

    li    t0, 0x12345678               /* HLS_OTHER_HART_IN_WFI */
    sw    t0, 0(s11)
    fence iorw, iorw

.L_secondary_wfi_loop:
    wfi
    csrr  t0, mip
    andi  t0, t0, MIP_MSIP
    beqz  t0, .L_secondary_wfi_loop

    csrr  a0, mhartid
    li    t0, 0x02000000               /* CLINT_BASE */
    slli  t1, a0, 2
    add   t0, t0, t1
    sw    zero, 0(t0)                  /* clear MSIP */

    li    t0, 0x87654321               /* HLS_OTHER_HART_PASSED_WFI */
    sw    t0, 0(s11)
    fence iorw, iorw
    fence.i

    /* The C code on this hart addresses small globals gp-relative, and only
     * the E51's .L_sram_entry path sets gp. */
    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop

    csrr  a0, mhartid
    mv    a1, s11
    la    t0, secondary_hart_entry
    jr    t0

/* .text section follows - runs from L2 SRAM after copy from eNVM */
.section .text
.L_sram_entry:
    la    t0, trap_vector_table
    csrw  mtvec, t0
1:  csrr  t1, mtvec
    bne   t0, t1, 1b

    /* Zero all GPRs (tp = hart ID preserved) */
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

    /* Spin until misa confirms RV64 (MSB set = negative when sign-extended) */
.L_xlen_check:
    csrr  t0, misa
    bltz  t0, .L_xlen_ok
    j     .L_xlen_check
.L_xlen_ok:

    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop

    la    sp, _end_stack
    li    t0, -16
    and   sp, sp, t0
    mv    s0, sp

    /* Copy .data from flash to RAM */
    la    t0, _stored_data
    la    t1, _start_data
    la    t2, _end_data
    beq   t0, t1, .L_data_copy_done
.L_data_copy:
    beq   t1, t2, .L_data_copy_done
    ld    t3, 0(t0)
    sd    t3, 0(t1)
    addi  t0, t0, 8
    addi  t1, t1, 8
    j     .L_data_copy
.L_data_copy_done:

    /* Clear .bss */
    la    t0, _start_bss
    la    t1, _end_bss
.L_bss_clear:
    beq   t0, t1, .L_bss_clear_done
    sd    zero, 0(t0)
    addi  t0, t0, 8
    j     .L_bss_clear
.L_bss_clear_done:

#ifndef TARGET_mpfs250
    /* Clear SiFive bus error unit accrued registers (not present on MPFS) */
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

    mv    a0, tp
    j     main

#else
    /* RV64 S-mode: a0=hartid, a1=DTB pointer (passed by SBI) */
    mv    tp, a0
    mv    s1, a1
    mv    gp, zero

    la    t0, trap_vector_table
    csrw  stvec, t0
    csrw  sie, zero
    li    t0, (SIE_SSIE | SIE_SEIE)
    csrs  sie, t0
    li    t0, SSTATUS_SIE
    csrs  sstatus, t0

    li    t0, -16
    li    t1, WOLFBOOT_STACK_TOP
    and   sp, t1, t0
    mv    s0, sp
    mv    gp, s0

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

/* reloc_trap_vector: update tvec to app image trap table (a0 = base uint32_t*) */
.globl reloc_trap_vector
reloc_trap_vector:
    addi  a0, a0, 4                  /* address + 1 (uint32_t* = +4 bytes) */
#if __riscv_xlen == 64
    csrw  MODE_PREFIX(tvec), a0
#else
    csrw  mtvec, a0
#endif
    ret

