/* boot_riscv.c
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

/* RISC-V boot code (32-bit and 64-bit unified) */

#include <stdint.h>

#include "image.h"
#include "loader.h"
#ifdef DEBUG_BOOT
#include "printf.h"
#endif

/* Include platform-specific headers (may define PLIC_BASE) */
#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

#include "hal/riscv.h"

/* Generic PLIC support is enabled when platform defines PLIC_BASE.
 * The PLIC header is included by the platform header after defining PLIC_BASE. */

extern void trap_entry(void);
extern void trap_exit(void);

/* Linker symbols - use native pointer-sized types */
#if __riscv_xlen == 64
extern uint64_t  _start_vector;
extern uint64_t  _stored_data;
extern uint64_t  _start_data;
extern uint64_t  _end_data;
extern uint64_t  _start_bss;
extern uint64_t  _end_bss;
extern uint64_t  _end_stack;
extern uint64_t  _start_heap;
extern uint64_t  _global_pointer;
extern void (* const trap_vector_table[])(void);
#else
extern uint32_t  _start_vector;
extern uint32_t  _stored_data;
extern uint32_t  _start_data;
extern uint32_t  _end_data;
extern uint32_t  _start_bss;
extern uint32_t  _end_bss;
extern uint32_t  _end_stack;
extern uint32_t  _start_heap;
extern uint32_t  _global_pointer;
extern void (* const IV[])(void);
#endif

extern void main(void);

/* reloc_trap_vector is implemented in boot_riscv_start.S */
extern void reloc_trap_vector(const uint32_t *address);

/* ============================================================================
 * Trap Handling
 * ============================================================================ */

#if __riscv_xlen == 64
static uint64_t last_cause = 0;
static uint64_t last_epc = 0;
static uint64_t last_tval = 0;
#else
static uint32_t last_cause = 0;
static uint32_t last_epc = 0;
static uint32_t last_tval = 0;
#endif

#ifdef PLIC_BASE
/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (Generic Implementation)
 * ============================================================================ */

/* Set priority for an interrupt source */
void plic_set_priority(uint32_t irq, uint32_t priority)
{
    if (irq > 0 && priority <= PLIC_PRIORITY_MAX) {
#ifdef PLIC_NUM_SOURCES
        if (irq >= PLIC_NUM_SOURCES)
            return;
#endif
        PLIC_PRIORITY_REG(PLIC_BASE, irq) = priority;
    }
}

/* Enable an interrupt for the current hart's context */
void plic_enable_interrupt(uint32_t irq)
{
    uint32_t ctx = plic_get_context();
    if (irq > 0) {
#ifdef PLIC_NUM_SOURCES
        if (irq >= PLIC_NUM_SOURCES)
            return;
#endif
        PLIC_ENABLE_REG(PLIC_BASE, ctx, irq) |= PLIC_ENABLE_BIT(irq);
    }
}

/* Disable an interrupt for the current hart's context */
void plic_disable_interrupt(uint32_t irq)
{
    uint32_t ctx = plic_get_context();
    if (irq > 0) {
#ifdef PLIC_NUM_SOURCES
        if (irq >= PLIC_NUM_SOURCES)
            return;
#endif
        PLIC_ENABLE_REG(PLIC_BASE, ctx, irq) &= ~PLIC_ENABLE_BIT(irq);
    }
}

/* Set the priority threshold for the current hart's context */
void plic_set_threshold(uint32_t threshold)
{
    uint32_t ctx = plic_get_context();
    if (threshold <= PLIC_PRIORITY_MAX) {
        PLIC_THRESHOLD_REG(PLIC_BASE, ctx) = threshold;
    }
}

/* Claim the highest priority pending interrupt */
uint32_t plic_claim(void)
{
    uint32_t ctx = plic_get_context();
    return PLIC_CLAIM_REG(PLIC_BASE, ctx);
}

/* Signal completion of interrupt handling */
void plic_complete(uint32_t irq)
{
    uint32_t ctx = plic_get_context();
    PLIC_COMPLETE_REG(PLIC_BASE, ctx) = irq;
}

/* Handle external interrupts via PLIC */
static void handle_external_interrupt(void)
{
    uint32_t irq;

    /* Claim and dispatch interrupts until none pending */
    while ((irq = plic_claim()) != 0) {
        /* Platform-provided dispatch function */
        plic_dispatch_irq(irq);

        /* Signal completion to PLIC */
        plic_complete(irq);
    }
}
#endif /* PLIC_BASE */

unsigned long WEAKFUNCTION handle_trap(unsigned long cause, unsigned long epc,
    unsigned long tval)
{
    last_cause = cause;
    last_epc = epc;
    last_tval = tval;

#ifdef PLIC_BASE
    /* Check if this is an interrupt (MSB set) */
    if (cause & MCAUSE_INT) {
        unsigned long exception_code = cause & MCAUSE_CAUSE;

        /* Check for external interrupt (S-mode external = 9, M-mode external = 11) */
        if (exception_code == IRQ_S_EXT || exception_code == IRQ_M_EXT) {
            handle_external_interrupt();
        }
        /* Other interrupts (timer, software) can be handled here if needed */
    }
    /* Synchronous exceptions are not handled - just record them */
#endif

    return epc;
}

/* ============================================================================
 * Timer Functions
 * ============================================================================ */

uint64_t hal_get_timer(void)
{
#if __riscv_xlen == 64
    /* For RV64, CSR time contains full 64-bit value */
    return csr_read(time);
#else
    /* For RV32, read both timeh and time with wrap-around protection */
    uint32_t hi, lo;

    do {
        hi = csr_read(timeh);
        lo = csr_read(time);
    } while (hi != csr_read(timeh));

    return ((uint64_t)hi << 32) | lo;
#endif
}

/* Get timer value in microseconds
 * Formula: time_us = (ticks * 1000) / (rate / 1000)
 *        = (ticks * 1000000) / rate
 */
uint64_t hal_get_timer_us(void)
{
    uint64_t ticks = hal_get_timer();
    uint32_t rate = RISCV_SMODE_TIMER_FREQ;

    /* Avoid overflow: (ticks * 1000) / (rate / 1000) */
    return (ticks * 1000) / (rate / 1000);
}

/* ============================================================================
 * Boot Functions
 * ============================================================================ */

#ifdef MMU
int WEAKFUNCTION hal_dts_fixup(void* dts_addr)
{
    (void)dts_addr;
    return 0;
}
#endif

#if __riscv_xlen == 64
/* Get the hartid saved by boot_riscv_start.S in the tp register */
unsigned long get_boot_hartid(void)
{
    unsigned long hartid;
    asm volatile("mv %0, tp" : "=r"(hartid));
    return hartid;
}
#endif

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
#if __riscv_xlen == 64
    unsigned long hartid;
#endif
#ifdef MMU
    unsigned long dts_addr;
#endif

#ifdef MMU
    hal_dts_fixup((uint32_t*)dts_offset);
    dts_addr = (unsigned long)dts_offset;
#endif

#if __riscv_xlen == 64
    /* Get the hartid that was saved by boot_riscv_start.S in tp register.
     * This is the hartid passed to wolfBoot by the prior boot stage (e.g., HSS).
     * For MPFS, this should be 1-4 (U54 cores), never 0 (E51 monitor core). */
    hartid = get_boot_hartid();
#endif

#ifdef DEBUG_BOOT
    wolfBoot_printf("do_boot: entry=0x%lx", (unsigned long)app_offset);
#if __riscv_xlen == 64
    wolfBoot_printf(", hartid=%lu", hartid);
#endif
#ifdef MMU
    wolfBoot_printf(", dts=0x%lx", dts_addr);
#endif
    wolfBoot_printf("\n");
#endif

    /* Relocate trap vector table to application */
    reloc_trap_vector(app_offset);

    /*
     * RISC-V Linux kernel boot requirements (Documentation/arch/riscv/boot.rst):
     *   a0 = hartid of the current core
     *   a1 = physical address of the device tree blob (DTB)
     *   satp = 0 (MMU disabled)
     *
     * For SMP systems using ordered booting (preferred), only the boot hart
     * enters the kernel. Secondary harts are started via SBI HSM extension.
     */

#if __riscv_xlen == 64
#ifdef MMU
    asm volatile(
    #ifdef WOLFBOOT_RISCV_SMODE
        "csrw satp, zero\n"
        "sfence.vma\n"
    #endif
        "mv a0, %0\n"
        "mv a1, %1\n"
        "jr %2\n"
        : : "r"(hartid), "r"(dts_addr), "r"(app_offset) : "a0", "a1"
    );
#else
    asm volatile(
        "mv a0, %0\n"
        "mv a1, zero\n"
        "jr %1\n"
        : : "r"(hartid), "r"(app_offset) : "a0", "a1"
    );
#endif
#else /* RV32 */
    /* RV32: typically bare-metal without Linux, simpler boot */
    asm volatile("jr %0" : : "r"(app_offset));
#endif

    /* Should never reach here */
    __builtin_unreachable();
}

void isr_empty(void)
{
    /* Empty interrupt handler */
}

/* ============================================================================
 * Reboot Functions
 * ============================================================================ */

#if __riscv_xlen == 32 && defined(RAM_CODE)
/* RV32 HiFive1 watchdog-based reboot */
#define AON_WDOGCFG  *(volatile uint32_t *)(0x10000000UL)
#define AON_WDOGKEY  *(volatile uint32_t *)(0x1000001CUL)
#define AON_WDOGFEED *(volatile uint32_t *)(0x10000018UL)
#define AON_WDOGCMP  *(volatile uint32_t *)(0x10000020UL)

#define AON_WDOGKEY_VALUE       0x0051F15E
#define AON_WDOGCFG_SCALE       0x0000000F
#define AON_WDOGCFG_RSTEN       0x00000100
#define AON_WDOGCFG_ZEROCMP     0x00000200
#define AON_WDOGCFG_ENALWAYS    0x00001000

void RAMFUNCTION arch_reboot(void)
{
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGCMP = 0;
    /* wdogconfig: wdogrsten | enablealways | reset to 0 | max scale */
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGCFG |= (AON_WDOGCFG_RSTEN | AON_WDOGCFG_ENALWAYS |
                   AON_WDOGCFG_ZEROCMP | AON_WDOGCFG_SCALE);
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGFEED = 1;

    while(1)
        ;
    wolfBoot_panic();
}

#else /* RV64 or non-RAM_CODE */

void WEAKFUNCTION arch_reboot(void)
{
#ifdef TARGET_mpfs250
    SYSREG_MSS_RESET_CR = 0xDEAD;
#endif

    while(1)
        ;
    wolfBoot_panic();
}

#endif /* __riscv_xlen == 32 && RAM_CODE */
