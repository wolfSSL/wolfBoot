/* boot_riscv.c
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

/* RISC-V boot code (32-bit and 64-bit unified) */

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "printf.h"

/* Include platform-specific headers (may define PLIC_BASE) */
#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

#include "hal/riscv.h"

/* Generic PLIC support is enabled when platform defines PLIC_BASE.
 * The PLIC header is included by the platform header after defining PLIC_BASE. */

extern void trap_entry(void);
extern void trap_exit(void);

/* Linker symbols */
#if __riscv_xlen == 64
extern uint64_t  _start_vector, _stored_data, _start_data, _end_data;
extern uint64_t  _start_bss, _end_bss, _end_stack, _start_heap;
extern uint64_t  _global_pointer;
extern void (* const trap_vector_table[])(void);
#else
extern uint32_t  _start_vector, _stored_data, _start_data, _end_data;
extern uint32_t  _start_bss, _end_bss, _end_stack, _start_heap;
extern uint32_t  _global_pointer;
extern void (* const IV[])(void);
#endif

extern void main(void);
extern void reloc_trap_vector(const uint32_t *address);

/* Trap state saved for debugging */
#if __riscv_xlen == 64
static uint64_t last_cause = 0, last_epc = 0, last_tval = 0;
#else
static uint32_t last_cause = 0, last_epc = 0, last_tval = 0;
#endif

#ifdef PLIC_BASE
/* PLIC generic implementation */

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

void plic_set_threshold(uint32_t threshold)
{
    uint32_t ctx = plic_get_context();
    if (threshold <= PLIC_PRIORITY_MAX) {
        PLIC_THRESHOLD_REG(PLIC_BASE, ctx) = threshold;
    }
}

uint32_t plic_claim(void)
{
    uint32_t ctx = plic_get_context();
    return PLIC_CLAIM_REG(PLIC_BASE, ctx);
}

void plic_complete(uint32_t irq)
{
    uint32_t ctx = plic_get_context();
    PLIC_COMPLETE_REG(PLIC_BASE, ctx) = irq;
}

static void handle_external_interrupt(void)
{
    uint32_t irq;

    /* Claim and dispatch interrupts until none pending */
    while ((irq = plic_claim()) != 0) {
        plic_dispatch_irq(irq);
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

    /* Always print and halt on synchronous exceptions to prevent
     * infinite trap-mret loops that appear as silent hangs.
     * NOTE: keep each printf SIMPLE (few args) to minimize the risk of
     * recursive traps if wolfBoot's state is corrupted. */
    if (!(cause & MCAUSE_INT)) {
        wolfBoot_printf("TRAP: cause=%lx epc=%lx tval=%lx\n",
            cause, epc, tval);
#if defined(DEBUG_BOOT)
        unsigned long sp_now;
        __asm__ volatile("mv %0, sp" : "=r"(sp_now));
        wolfBoot_printf("      sp=%lx\n", sp_now);
#if defined(WOLFBOOT_RISCV_MMODE) && defined(TARGET_mpfs250)
        /* Detect stack overflow by comparing SP to linker-defined
         * stack bottom. Trap entry pushes 128 bytes before calling
         * here, so the trapping SP was slightly higher. */
        extern uint8_t _main_hart_stack_bottom[];
        unsigned long bottom = (unsigned long)_main_hart_stack_bottom;
        if (sp_now < bottom) {
            wolfBoot_printf("STACK OVERFLOW: under by %lu\n",
                bottom - sp_now);
        }
#endif
#endif /* DEBUG_BOOT */
        while (1) ; /* halt to prevent infinite trap-mret loop */
    }

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
#ifdef WOLFBOOT_RISCV_MMODE
    /* M-mode: rdtime not available without HSS; use mcycle (CPU clock) */
    return csr_read(mcycle);
#elif __riscv_xlen == 64
    return csr_read(time);
#else
    /* RV32: read timeh+time with wrap-around protection */
    uint32_t hi, lo;

    do {
        hi = csr_read(timeh);
        lo = csr_read(time);
    } while (hi != csr_read(timeh));

    return ((uint64_t)hi << 32) | lo;
#endif
}

uint64_t hal_get_timer_us(void)
{
    uint64_t ticks = hal_get_timer();
    uint32_t rate = RISCV_SMODE_TIMER_FREQ;
    return (ticks * 1000) / (rate / 1000);
}

#ifdef MMU
int WEAKFUNCTION hal_dts_fixup(void* dts_addr)
{
    (void)dts_addr;
    return 0;
}
#endif

#ifdef WOLFBOOT_RISCV_MMODE
/* Configure PMP entry 0: NAPOT full address space, RWX, for S-mode access */
static void setup_pmp_for_smode(void)
{
    csr_write(pmpaddr0, -1UL);  /* all-ones = cover entire address space (NAPOT) */
    csr_write(pmpcfg0, 0x1F);   /* A=NAPOT(3), R=1, W=1, X=1 */
    __asm__ volatile("sfence.vma" ::: "memory");
}

/* Delegate common exceptions and S-mode interrupts to S-mode */
static void delegate_traps_to_smode(void)
{
    /* Delegate exceptions 0-8, 12, 13, 15 (all except S-mode ecall, reserved) */
    csr_write(medeleg, (1 << 0)|(1 << 1)|(1 << 2)|(1 << 3)|
                       (1 << 4)|(1 << 5)|(1 << 6)|(1 << 7)|
                       (1 << 8)|(1 << 12)|(1 << 13)|(1 << 15));
    /* Delegate S-mode software, timer, and external interrupts */
    csr_write(mideleg, (1 << IRQ_S_SOFT)|(1 << IRQ_S_TIMER)|(1 << IRQ_S_EXT));
}

/* Switch to S-mode and jump to entry (never returns). a0=hartid, a1=dtb */
static void __attribute__((noreturn)) enter_smode(unsigned long entry,
                                                  unsigned long hartid,
                                                  unsigned long dtb)
{
    unsigned long mstatus_val;
    csr_write(mepc, entry);
    mstatus_val  = csr_read(mstatus);
    mstatus_val &= ~MSTATUS_MPP_MASK;
    mstatus_val |= MSTATUS_MPP_S | MSTATUS_MPIE;
    mstatus_val &= ~MSTATUS_MIE;
    csr_write(mstatus, mstatus_val);
    csr_write(satp, 0);
    __asm__ volatile(
        "mv a0, %0\n"
        "mv a1, %1\n"
        "mret\n"
        : : "r"(hartid), "r"(dtb) : "a0", "a1"
    );
    __builtin_unreachable();
}
#endif /* WOLFBOOT_RISCV_MMODE */

#if __riscv_xlen == 64
/* Return the hartid saved in tp by boot_riscv_start.S */
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
    hal_dts_fixup((uint32_t*)dts_offset);
    dts_addr = (unsigned long)dts_offset;
#elif defined(WOLFBOOT_RISCV_MMODE) || __riscv_xlen == 64
    unsigned long dts_addr = 0;
#endif

#if __riscv_xlen == 64
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

#ifdef WOLFBOOT_RISCV_MMODE
#ifdef WOLFBOOT_MMODE_SMODE_BOOT
    /* M-mode -> S-mode transition for Linux boot */
    wolfBoot_printf("M->S transition: entry=0x%lx\n", (unsigned long)app_offset);
    setup_pmp_for_smode();
    delegate_traps_to_smode();
    /* This never returns */
    enter_smode((unsigned long)app_offset, hartid, dts_addr);
#else
    /* Direct M-mode jump for bare-metal payloads.
     * Define WOLFBOOT_MMODE_SMODE_BOOT to boot Linux via S-mode transition. */
    wolfBoot_printf("M-mode direct jump to 0x%lx\n", (unsigned long)app_offset);
#ifdef DEBUG_BOOT
    {
        volatile uint8_t lsr = MMUART_LSR(DEBUG_UART_BASE);
        uint32_t *p = (uint32_t*)app_offset;
        wolfBoot_printf("Pre-jump: LSR=0x%x THRE=%d\n",
                        (unsigned)lsr, (lsr & MSS_UART_THRE) ? 1 : 0);
        wolfBoot_printf("App[0]=0x%lx [1]=0x%lx\n",
                        (unsigned long)p[0], (unsigned long)p[1]);
    }
    /* Drain UART TX before jumping (~10 ms at 40 MHz) */
    { volatile int i; for (i = 0; i < 400000; i++) {} }
#endif /* DEBUG_BOOT */
    (void)hartid;
    (void)dts_addr;
    /* fence + fence.i: ensure stores from ELF loading are visible to I-fetch */
    asm volatile("fence" ::: "memory");
    asm volatile("fence.i" ::: "memory");
    asm volatile("jr %0" : : "r"(app_offset));
    __builtin_unreachable();
#endif /* WOLFBOOT_MMODE_SMODE_BOOT */

#elif __riscv_xlen == 64
    /* S-mode / RV64 boot */
    asm volatile("fence" ::: "memory");
    riscv_icache_sync();
    asm volatile(
    #if defined(MMU) && !defined(WOLFBOOT_RISCV_MMODE)
        /* S-mode boot (e.g., when running under HSS/OpenSBI) */
        "csrw satp, zero\n"
        "sfence.vma\n"
    #endif
        "mv a0, %0\n"
        "mv a1, %1\n"
        "jr %2\n"
        : : "r"(hartid), "r"(dts_addr), "r"(app_offset) : "a0", "a1"
    );

#else /* RV32 */
    /* RV32: typically bare-metal without Linux, simpler boot */
    asm volatile("jr %0" : : "r"(app_offset));
#endif

    /* Should never reach here */
    __builtin_unreachable();
}

void isr_empty(void)
{
}

/* Reboot functions */

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

    wolfBoot_panic();
}

#else /* RV64 or non-RAM_CODE */

void WEAKFUNCTION arch_reboot(void)
{
#ifdef TARGET_mpfs250
    SYSREG_MSS_RESET_CR = 0xDEAD;
#endif

    wolfBoot_panic();
}

#endif /* __riscv_xlen == 32 && RAM_CODE */
