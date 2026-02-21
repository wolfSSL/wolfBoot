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

/* ============================================================================
 * RISC-V Privilege Mode Selection
 *
 *   - Machine mode (direct boot from eNVM) : WOLFBOOT_RISCV_MMODE
 *   - Supervisor mode (running under HSS/SBI) : default
 *
 * ============================================================================ */

 /* Initial stack pointer address (stack grows downward from here) */
#ifndef WOLFBOOT_STACK_TOP
    #ifdef WOLFBOOT_RISCV_MMODE
        /* M-mode: Stack at end of L2 Scratchpad (256KB) */
        #define WOLFBOOT_STACK_TOP 0x0A040000
    #else
        /* S-mode: Stack in DDR */
        #define WOLFBOOT_STACK_TOP 0x80200000
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

#ifdef WOLFBOOT_RISCV_MMODE
#define MODE_PREFIX(__suffix)    m##__suffix
#else
#define MODE_PREFIX(__suffix)    s##__suffix
#endif



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
 * Cache / I-Cache Sync Helpers
 * ============================================================================ */
#ifndef __ASSEMBLER__
static inline void riscv_icache_sync(void)
{
#ifdef __riscv_zifencei
    __asm__ __volatile__("fence.i" ::: "memory");
#endif
}
#endif /* !__ASSEMBLER__ */

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
/* Privilege Levels */
#define PRV_U        0   /* User mode */
#define PRV_S        1   /* Supervisor mode */
#define PRV_M        3   /* Machine mode */

/* MSTATUS Register Bits */
#define MSTATUS_UIE  (1UL << 0)    /* User interrupt enable */
#define MSTATUS_SIE  (1UL << 1)    /* Supervisor interrupt enable */
#define MSTATUS_MIE  (1UL << 3)    /* Machine interrupt enable */
#define MSTATUS_UPIE (1UL << 4)    /* User previous interrupt enable */
#define MSTATUS_SPIE (1UL << 5)    /* Supervisor previous interrupt enable */
#define MSTATUS_MPIE (1UL << 7)    /* Machine previous interrupt enable */
#define MSTATUS_SPP  (1UL << 8)    /* Supervisor previous privilege (1 bit) */
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP_MASK (3UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M    (PRV_M << MSTATUS_MPP_SHIFT)  /* MPP = Machine */
#define MSTATUS_MPP_S    (PRV_S << MSTATUS_MPP_SHIFT)  /* MPP = Supervisor */
#define MSTATUS_MPP_U    (PRV_U << MSTATUS_MPP_SHIFT)  /* MPP = User */
#define MSTATUS_FS_SHIFT 13
#define MSTATUS_FS_MASK  (3UL << MSTATUS_FS_SHIFT)
#define MSTATUS_FS_OFF   (0UL << MSTATUS_FS_SHIFT)    /* FPU off */
#define MSTATUS_FS_INIT  (1UL << MSTATUS_FS_SHIFT)    /* FPU initial */
#define MSTATUS_FS_CLEAN (2UL << MSTATUS_FS_SHIFT)    /* FPU clean */
#define MSTATUS_FS_DIRTY (3UL << MSTATUS_FS_SHIFT)    /* FPU dirty */
#define MSTATUS_MPRV (1UL << 17)   /* Modify privilege */
#define MSTATUS_SUM  (1UL << 18)   /* Supervisor user memory access */
#define MSTATUS_MXR  (1UL << 19)   /* Make executable readable */
#define MSTATUS_TVM  (1UL << 20)   /* Trap virtual memory */
#define MSTATUS_TW   (1UL << 21)   /* Timeout wait */
#define MSTATUS_TSR  (1UL << 22)   /* Trap SRET */

/* SSTATUS Register Bits (subset visible to S-mode) */
#define SSTATUS_SIE  (1UL << 1)   /* Supervisor-mode global interrupt enable */
#define SSTATUS_SPIE (1UL << 5)   /* Supervisor-mode previous interrupt enable */

/* ============================================================================
 * Machine Interrupt Enable (MIE) Register Bits
 * ============================================================================ */
#define MIE_MSIE     (1 << IRQ_M_SOFT)   /* Machine software interrupt enable */
#define MIE_MTIE     (1 << IRQ_M_TIMER)  /* Machine timer interrupt enable */
#define MIE_MEIE     (1 << IRQ_M_EXT)    /* Machine external interrupt enable */

/* ============================================================================
 * Machine Interrupt Pending (MIP) Register Bits
 * Same bit positions as MIE, used to check/set pending interrupts
 * ============================================================================ */
#define MIP_MSIP     (1 << IRQ_M_SOFT)   /* Machine software interrupt pending */
#define MIP_MTIP     (1 << IRQ_M_TIMER)  /* Machine timer interrupt pending */
#define MIP_MEIP     (1 << IRQ_M_EXT)    /* Machine external interrupt pending */

/* ============================================================================
 * Supervisor Interrupt Enable (SIE) Register Bits
 * ============================================================================ */
#define SIE_SSIE     (1 << IRQ_S_SOFT)   /* Supervisor software interrupt enable */
#define SIE_STIE     (1 << IRQ_S_TIMER)  /* Supervisor timer interrupt enable */
#define SIE_SEIE     (1 << IRQ_S_EXT)    /* Supervisor external interrupt enable */

/* ============================================================================
 * Supervisor Interrupt Pending (SIP) Register Bits
 * ============================================================================ */
#define SIP_SSIP     (1 << IRQ_S_SOFT)   /* Supervisor software interrupt pending */
#define SIP_STIP     (1 << IRQ_S_TIMER)  /* Supervisor timer interrupt pending */
#define SIP_SEIP     (1 << IRQ_S_EXT)    /* Supervisor external interrupt pending */

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

/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (Generic)
 * Reference: RISC-V Platform-Level Interrupt Controller Specification v1.0
 * ============================================================================
 *
 * The PLIC is the standard external interrupt controller for RISC-V systems.
 * It aggregates external interrupt sources and presents them to harts.
 *
 * PLIC Memory Map (standard offsets from PLIC_BASE):
 *   0x000000-0x000FFF: Priority registers (1 word per source, source 0 reserved)
 *   0x001000-0x001FFF: Pending bits (1 bit per source, packed in 32-bit words)
 *   0x002000-0x1FFFFF: Enable bits (per context, 1 bit per source, packed)
 *   0x200000-0x3FFFFF: Context registers (threshold + claim/complete per context)
 *
 * Each hart typically has 2 contexts: M-mode and S-mode.
 *
 * Platform must define before using PLIC functions:
 *   PLIC_BASE         - Base address of PLIC registers
 *   PLIC_NUM_SOURCES  - Number of interrupt sources (optional, for bounds check)
 * ============================================================================ */

/* PLIC Register Offsets (usable in both C and assembly) */
#define PLIC_PRIORITY_OFFSET    0x000000UL
#define PLIC_PENDING_OFFSET     0x001000UL
#define PLIC_ENABLE_OFFSET      0x002000UL
#define PLIC_ENABLE_STRIDE      0x80UL
#define PLIC_CONTEXT_OFFSET     0x200000UL
#define PLIC_CONTEXT_STRIDE     0x1000UL

/* PLIC Priority Levels (standard values) */
#define PLIC_PRIORITY_DISABLED  0   /* Priority 0 = interrupt disabled */
#define PLIC_PRIORITY_MIN       1   /* Minimum active priority */
#define PLIC_PRIORITY_MAX       7   /* Maximum priority (7 levels typical) */
#define PLIC_PRIORITY_DEFAULT   4   /* Default/medium priority */

/* ============================================================================
 * PLIC Register Access Macros (C code only)
 * ============================================================================ */
#ifndef __ASSEMBLER__

/* Priority registers: one 32-bit word per interrupt source (source 0 reserved) */
#define PLIC_PRIORITY_REG(base, irq) \
    (*((volatile uint32_t*)((base) + PLIC_PRIORITY_OFFSET + ((irq) * 4))))

/* Pending bits: 32 interrupts per 32-bit word */
#define PLIC_PENDING_REG(base, irq) \
    (*((volatile uint32_t*)((base) + PLIC_PENDING_OFFSET + (((irq) / 32) * 4))))
#define PLIC_PENDING_BIT(irq)   (1U << ((irq) % 32))

/* Enable bits: per context, 32 interrupts per 32-bit word
 * Each context has 0x80 bytes (32 words * 32 bits = 1024 sources max) */
#define PLIC_ENABLE_REG(base, ctx, irq) \
    (*((volatile uint32_t*)((base) + PLIC_ENABLE_OFFSET + \
        ((ctx) * PLIC_ENABLE_STRIDE) + (((irq) / 32) * 4))))
#define PLIC_ENABLE_BIT(irq)    (1U << ((irq) % 32))

/* Context registers: threshold and claim/complete
 * Each context has 0x1000 bytes, with threshold at offset 0 and claim at offset 4 */
#define PLIC_THRESHOLD_REG(base, ctx) \
    (*((volatile uint32_t*)((base) + PLIC_CONTEXT_OFFSET + \
        ((ctx) * PLIC_CONTEXT_STRIDE) + 0x00)))
#define PLIC_CLAIM_REG(base, ctx) \
    (*((volatile uint32_t*)((base) + PLIC_CONTEXT_OFFSET + \
        ((ctx) * PLIC_CONTEXT_STRIDE) + 0x04)))
/* Complete uses the same register as claim (write IRQ number to complete) */
#define PLIC_COMPLETE_REG(base, ctx) PLIC_CLAIM_REG(base, ctx)

#endif /* !__ASSEMBLER__ */

/* ============================================================================
 * PLIC Function Declarations (C code only, when PLIC_BASE is defined)
 *
 * These functions are implemented in boot_riscv.c when PLIC_BASE is defined.
 * Platform must provide plic_get_context() to map current hart to PLIC context.
 * ============================================================================ */
#if defined(PLIC_BASE) && !defined(__ASSEMBLER__)

#include <stdint.h>

/* Platform-provided: Get PLIC context ID for current hart
 * Returns the context number (e.g., hart 1 S-mode = context 2) */
extern uint32_t plic_get_context(void);

/* Set priority for an interrupt source (0 = disabled, 1-7 = priority levels) */
void plic_set_priority(uint32_t irq, uint32_t priority);

/* Enable an interrupt for the current hart's context */
void plic_enable_interrupt(uint32_t irq);

/* Disable an interrupt for the current hart's context */
void plic_disable_interrupt(uint32_t irq);

/* Set the priority threshold for the current hart's context
 * Interrupts with priority <= threshold are masked */
void plic_set_threshold(uint32_t threshold);

/* Claim the highest priority pending interrupt
 * Returns IRQ number, or 0 if no interrupt pending */
uint32_t plic_claim(void);

/* Signal completion of interrupt handling */
void plic_complete(uint32_t irq);

/* Platform-provided: Dispatch IRQ to appropriate handler
 * Called by generic external interrupt handler for each claimed IRQ */
extern void plic_dispatch_irq(uint32_t irq);

#endif /* PLIC_BASE && !__ASSEMBLER__ */

/* ============================================================================
 * CLINT - Core Local Interruptor (M-mode only)
 *
 * The CLINT provides software interrupts (IPI) and timer functionality
 * for machine mode. Used for inter-hart communication and timer-based delays.
 *
 * CLINT Memory Map (standard offsets from CLINT_BASE):
 *   0x0000-0x3FFF: MSIP registers (1 word per hart, software interrupt pending)
 *   0x4000-0xBFF7: MTIMECMP registers (8 bytes per hart, timer compare)
 *   0xBFF8-0xBFFF: MTIME register (8 bytes, global timer counter)
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE

#ifndef CLINT_BASE
#define CLINT_BASE          0x02000000UL
#endif

#define CLINT_MSIP_OFFSET   0x0000UL
#define CLINT_MTIMECMP_OFFSET 0x4000UL
#define CLINT_MTIME_OFFSET  0xBFF8UL

#ifndef __ASSEMBLER__

/* MSIP (Machine Software Interrupt Pending) - one per hart */
#define CLINT_MSIP(hart) \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MSIP_OFFSET + ((hart) * 4))))

/* MTIMECMP - 64-bit timer compare value, one per hart */
#define CLINT_MTIMECMP_LO(hart) \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MTIMECMP_OFFSET + ((hart) * 8))))
#define CLINT_MTIMECMP_HI(hart) \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MTIMECMP_OFFSET + ((hart) * 8) + 4)))

/* MTIME - 64-bit global timer counter (shared across all harts) */
#define CLINT_MTIME_LO \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MTIME_OFFSET)))
#define CLINT_MTIME_HI \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MTIME_OFFSET + 4)))

#endif /* !__ASSEMBLER__ */

#endif /* WOLFBOOT_RISCV_MMODE */

/* ============================================================================
 * L2 Cache Controller (M-mode only)
 *
 * The L2 cache controller manages the shared L2 cache and LIM (Loosely
 * Integrated Memory) / Scratchpad configuration.
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE

#ifndef L2_CACHE_CTRL_BASE
#define L2_CACHE_CTRL_BASE  0x02010000UL
#endif

/* L2 Cache Controller register offsets */
#define L2_CONFIG_OFFSET    0x000UL
#define L2_WAYENABLE_OFFSET 0x008UL
#define L2_FLUSH64_OFFSET   0x200UL

#ifndef __ASSEMBLER__

#define L2_CONFIG_REG \
    (*((volatile uint32_t*)(L2_CACHE_CTRL_BASE + L2_CONFIG_OFFSET)))
#define L2_WAYENABLE_REG \
    (*((volatile uint32_t*)(L2_CACHE_CTRL_BASE + L2_WAYENABLE_OFFSET)))
#define L2_FLUSH64_REG \
    (*((volatile uint64_t*)(L2_CACHE_CTRL_BASE + L2_FLUSH64_OFFSET)))

#endif /* !__ASSEMBLER__ */

#endif /* WOLFBOOT_RISCV_MMODE */

#endif /* RISCV_H */
