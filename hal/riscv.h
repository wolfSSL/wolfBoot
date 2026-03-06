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

/* RISC-V privilege mode:
 *   M-mode (direct boot from eNVM): WOLFBOOT_RISCV_MMODE
 *   S-mode (running under HSS/SBI): default
 */

#ifndef WOLFBOOT_STACK_TOP
    #ifdef WOLFBOOT_RISCV_MMODE
        #define WOLFBOOT_STACK_TOP 0x0A040000  /* end of L2 Scratchpad (256KB) */
    #else
        #define WOLFBOOT_STACK_TOP 0x80200000  /* DDR */
    #endif
#endif

/* XLEN-dependent load/store mnemonics and register width */
#if __riscv_xlen == 64
    #define STORE    sd
    #define LOAD     ld
    #define REGBYTES 8
    #define VECTOR_ALIGN 3
#else
    #define STORE    sw
    #define LOAD     lw
    #define REGBYTES 4
    #define VECTOR_ALIGN 2
#endif

/* S-mode timer frequency (1 MHz default; platform may override).
 * In M-mode, hal_get_timer() returns mcycle so the platform (e.g. mpfs250.h)
 * sets RISCV_SMODE_TIMER_FREQ to the CPU clock; do not default it here. */
#if !defined(WOLFBOOT_RISCV_MMODE) && !defined(RISCV_SMODE_TIMER_FREQ)
#define RISCV_SMODE_TIMER_FREQ 1000000
#endif

/* Mode-prefixed CSR name helper */
#ifdef WOLFBOOT_RISCV_MMODE
#define MODE_PREFIX(__suffix)    m##__suffix
#else
#define MODE_PREFIX(__suffix)    s##__suffix
#endif

/* CSR access macros */
#define csr_read(csr) \
({ \
    register unsigned long __v; \
    __asm__ __volatile__ ("csrr %0, " #csr : "=r"(__v) : ); \
    __v; \
})

#define csr_write(csr, val) \
({ \
    unsigned long __v = (unsigned long)(val); \
    __asm__ __volatile__ ("csrw " #csr ", %0" : : "rK"(__v)); \
})

#define csr_set(csr, val) \
({ \
    unsigned long __v = (unsigned long)(val); \
    __asm__ __volatile__ ("csrs " #csr ", %0" : : "rK"(__v)); \
})

#define csr_clear(csr, val) \
({ \
    unsigned long __v = (unsigned long)(val); \
    __asm__ __volatile__ ("csrc " #csr ", %0" : : "rK"(__v)); \
})

#ifndef __ASSEMBLER__
static inline void riscv_icache_sync(void)
{
#ifdef __riscv_zifencei
    __asm__ __volatile__("fence.i" ::: "memory");
#endif
}
#endif

/* Interrupt numbers (for MIE/MIP/SIE/SIP registers) */
#define IRQ_S_SOFT   1
#define IRQ_M_SOFT   3
#define IRQ_S_TIMER  5
#define IRQ_M_TIMER  7
#define IRQ_S_EXT    9
#define IRQ_M_EXT    11

/* Privilege levels */
#define PRV_S        1
#define PRV_M        3

/* MSTATUS register bits */
#define MSTATUS_SIE  (1UL << 1)
#define MSTATUS_MIE  (1UL << 3)
#define MSTATUS_SPIE (1UL << 5)
#define MSTATUS_MPIE (1UL << 7)
#define MSTATUS_SPP  (1UL << 8)
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP_MASK (3UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M    (PRV_M << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_S    (PRV_S << MSTATUS_MPP_SHIFT)

/* SSTATUS bits (S-mode visible subset) */
#define SSTATUS_SIE  (1UL << 1)
#define SSTATUS_SPIE (1UL << 5)

/* MIP register bits (mip CSR: pending interrupt flags, written by hardware) */
#define MIP_MSIP     (1 << IRQ_M_SOFT)

/* MIE register bits (mie CSR: interrupt enable, always written by software.
 * Bit positions match MIP but use these names when targeting the mie register
 * to avoid confusing the two CSRs.) */
#define MIE_MSIE     (1 << IRQ_M_SOFT)
#define MIE_MTIE     (1 << IRQ_M_TIMER)
#define MIE_MEIE     (1 << IRQ_M_EXT)

/* SIE register bits */
#define SIE_SSIE     (1 << IRQ_S_SOFT)
#define SIE_STIE     (1 << IRQ_S_TIMER)
#define SIE_SEIE     (1 << IRQ_S_EXT)

/* MCAUSE / SCAUSE interrupt bit and exception code mask */
#if __riscv_xlen == 64
#define MCAUSE_INT   0x8000000000000000ULL
#define MCAUSE_CAUSE 0x7FFFFFFFFFFFFFFFULL
#else
#define MCAUSE_INT   0x80000000UL
#define MCAUSE_CAUSE 0x7FFFFFFFUL
#endif

/* PLIC - Platform-Level Interrupt Controller
 * Generic implementation, enabled when platform defines PLIC_BASE.
 *
 * Standard memory map offsets:
 *   0x000000: Priority registers (1 word/source)
 *   0x002000: Enable bits (per context, 1 bit/source)
 *   0x200000: Context registers (threshold + claim/complete per context)
 *
 * Platform must define PLIC_BASE (and optionally PLIC_NUM_SOURCES). */

#define PLIC_PRIORITY_OFFSET    0x000000UL
#define PLIC_ENABLE_OFFSET      0x002000UL
#define PLIC_ENABLE_STRIDE      0x80UL
#define PLIC_CONTEXT_OFFSET     0x200000UL
#define PLIC_CONTEXT_STRIDE     0x1000UL

#define PLIC_PRIORITY_DISABLED  0
#define PLIC_PRIORITY_MIN       1
#define PLIC_PRIORITY_MAX       7
#define PLIC_PRIORITY_DEFAULT   4

#ifndef __ASSEMBLER__
#define PLIC_PRIORITY_REG(base, irq) \
    (*((volatile uint32_t*)((base) + PLIC_PRIORITY_OFFSET + ((irq) * 4))))

#define PLIC_ENABLE_REG(base, ctx, irq) \
    (*((volatile uint32_t*)((base) + PLIC_ENABLE_OFFSET + \
        ((ctx) * PLIC_ENABLE_STRIDE) + (((irq) / 32) * 4))))
#define PLIC_ENABLE_BIT(irq)    (1U << ((irq) % 32))

#define PLIC_THRESHOLD_REG(base, ctx) \
    (*((volatile uint32_t*)((base) + PLIC_CONTEXT_OFFSET + \
        ((ctx) * PLIC_CONTEXT_STRIDE) + 0x00)))
#define PLIC_CLAIM_REG(base, ctx) \
    (*((volatile uint32_t*)((base) + PLIC_CONTEXT_OFFSET + \
        ((ctx) * PLIC_CONTEXT_STRIDE) + 0x04)))
#define PLIC_COMPLETE_REG(base, ctx) PLIC_CLAIM_REG(base, ctx)

#endif /* !__ASSEMBLER__ */

/* PLIC function declarations (boot_riscv.c, when PLIC_BASE is defined).
 * Platform must provide plic_get_context() and plic_dispatch_irq(). */
#if defined(PLIC_BASE) && !defined(__ASSEMBLER__)

#include <stdint.h>

/* Platform-provided: Get PLIC context ID for current hart
 * Returns the context number (e.g., hart 1 S-mode = context 2) */
extern uint32_t plic_get_context(void);
void plic_set_priority(uint32_t irq, uint32_t priority);
void plic_enable_interrupt(uint32_t irq);
void plic_disable_interrupt(uint32_t irq);
void plic_set_threshold(uint32_t threshold);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);
extern void plic_dispatch_irq(uint32_t irq);

#endif /* PLIC_BASE && !__ASSEMBLER__ */

/* CLINT - Core Local Interruptor (M-mode only).
 * Provides software IPIs (MSIP) and timer (MTIME/MTIMECMP). */
#ifdef WOLFBOOT_RISCV_MMODE

#ifndef CLINT_BASE
#define CLINT_BASE          0x02000000UL
#endif

#define CLINT_MSIP_OFFSET   0x0000UL

#ifndef __ASSEMBLER__
#define CLINT_MSIP(hart) \
    (*((volatile uint32_t*)(CLINT_BASE + CLINT_MSIP_OFFSET + ((hart) * 4))))
#endif

#endif /* WOLFBOOT_RISCV_MMODE */

#endif /* RISCV_H */
