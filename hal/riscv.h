/* riscv.h
 *
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

#ifndef RISCV_H
#define RISCV_H


/* TODO: Add support for machine mode wolfBoot */
#if 1
#define WOLFBOOT_RISCV_SMODE /* supervisor mode */
#else
#define WOLFBOOT_RISCV_MMODE /* machine mode */
#endif

/* Initial stack pointer address (stack grows downward from here) */
#ifndef WOLFBOOT_STACK_TOP
#ifdef WOLFBOOT_RISCV_SMODE
#define WOLFBOOT_STACK_TOP 0x80200000
#else
#define WOLFBOOT_STACK_TOP 0x80000000
#endif
#endif

/* ============================================================================
 * Generic RISC-V definitions (32-bit and 64-bit)
 * ============================================================================ */

/* ============================================================================
 * XLEN-Dependent Definitions
 * ============================================================================ */

#if __riscv_xlen == 64
    #define STORE   sd
    #define LOAD    ld
    #define REGBYTES 8
    #define VECTOR_ALIGN 3      /* 8-byte alignment for RV64 */
#else
    #define STORE   sw
    #define LOAD    lw
    #define REGBYTES 4
    #define VECTOR_ALIGN 2      /* 4-byte alignment for RV32 */
#endif


/* RISC-V S-mode timer frequency (1 MHz default, can be overridden by platform) */
#ifndef RISCV_SMODE_TIMER_FREQ
#define RISCV_SMODE_TIMER_FREQ 1000000 /* 1 MHz */
#endif

/* ============================================================================
 * Machine Information Registers (CSRs)
 * ============================================================================ */
#define CSR_TIME         0xC01  /* Timer register (read-only) */
#define CSR_TIMEH        0xC81  /* Timer register high (RV32 only) */
#define CSR_MVENDORID    0xF11  /* Vendor ID */
#define CSR_MARCHID      0xF12  /* Architecture ID */
#define CSR_MIMPID       0xF13  /* Implementation ID */
#define CSR_MHARTID      0xF14  /* Hardware thread ID */

/* ============================================================================
 * CSR Access Macros
 * ============================================================================ */

/* Read CSR using inline assembly */
#define csr_read(csr)                                           \
({                                                              \
    register unsigned long __v;                                 \
    __asm__ __volatile__ ("csrr %0, " #csr : "=r"(__v) : );     \
    __v;                                                        \
})

/* Write CSR using inline assembly */
#define csr_write(csr, val)                                     \
({                                                              \
    unsigned long __v = (unsigned long)(val);                   \
    __asm__ __volatile__ ("csrw " #csr ", %0" : : "rK"(__v));   \
})

/* Set bits in CSR */
#define csr_set(csr, val)                                       \
({                                                              \
    unsigned long __v = (unsigned long)(val);                   \
    __asm__ __volatile__ ("csrs " #csr ", %0" : : "rK"(__v));   \
})

/* Clear bits in CSR */
#define csr_clear(csr, val)                                     \
({                                                              \
    unsigned long __v = (unsigned long)(val);                   \
    __asm__ __volatile__ ("csrc " #csr ", %0" : : "rK"(__v));   \
})

/* ============================================================================
 * Interrupt Numbers (for SIE/SIP and MIE/MIP registers)
 * ============================================================================ */
#define IRQ_U_SOFT   0   /* User software interrupt */
#define IRQ_S_SOFT   1   /* Supervisor software interrupt */
#define IRQ_M_SOFT   3   /* Machine software interrupt */
#define IRQ_U_TIMER  4   /* User timer interrupt */
#define IRQ_S_TIMER  5   /* Supervisor timer interrupt */
#define IRQ_M_TIMER  7   /* Machine timer interrupt */
#define IRQ_U_EXT    8   /* User external interrupt */
#define IRQ_S_EXT    9   /* Supervisor external interrupt */
#define IRQ_M_EXT    11  /* Machine external interrupt */

/* ============================================================================
 * Status Register Bits (mstatus/sstatus)
 * ============================================================================ */
#define MSTATUS_MIE  (1 << 3)   /* Machine-mode global interrupt enable */
#define MSTATUS_MPIE (1 << 7)   /* Machine-mode previous interrupt enable */
#define SSTATUS_SIE  (1 << 1)   /* Supervisor-mode global interrupt enable */
#define SSTATUS_SPIE (1 << 5)   /* Supervisor-mode previous interrupt enable */

/* ============================================================================
 * Machine Interrupt Enable (MIE) Register Bits
 * ============================================================================ */
#define MIE_MSIE     (1 << IRQ_M_SOFT)   /* Machine software interrupt enable */
#define MIE_MTIE     (1 << IRQ_M_TIMER)  /* Machine timer interrupt enable */
#define MIE_MEIE     (1 << IRQ_M_EXT)    /* Machine external interrupt enable */

/* ============================================================================
 * Supervisor Interrupt Enable (SIE) Register Bits
 * ============================================================================ */
#define SIE_SSIE     (1 << IRQ_S_SOFT)   /* Supervisor software interrupt enable */
#define SIE_STIE     (1 << IRQ_S_TIMER)  /* Supervisor timer interrupt enable */
#define SIE_SEIE     (1 << IRQ_S_EXT)    /* Supervisor external interrupt enable */

/* ============================================================================
 * Exception Cause Register (MCAUSE/SCAUSE) Definitions
 * ============================================================================ */
#if __riscv_xlen == 64
#define MCAUSE_INT   0x8000000000000000ULL  /* Interrupt bit (MSB) */
#define MCAUSE_CAUSE 0x7FFFFFFFFFFFFFFFULL  /* Exception code mask */
#else
#define MCAUSE_INT   0x80000000UL           /* Interrupt bit (MSB) */
#define MCAUSE_CAUSE 0x7FFFFFFFUL           /* Exception code mask */
#endif

/* Legacy aliases for compatibility */
#define MCAUSE64_INT   0x8000000000000000ULL
#define MCAUSE64_CAUSE 0x7FFFFFFFFFFFFFFFULL
#define MCAUSE32_INT   0x80000000UL
#define MCAUSE32_CAUSE 0x7FFFFFFFUL

#endif /* RISCV_H */

