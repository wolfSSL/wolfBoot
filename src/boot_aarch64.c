/* boot_aarch64.c
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

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

/* Include platform-specific header for EL configuration defines
 * (EL2_HYPERVISOR, etc.). Must be visible here so the BOOT_EL1 /
 * EL2_HYPERVISOR guards around the EL2->EL1 ERET transition below
 * compile in for the active target. */
#if defined(TARGET_versal)
#include "hal/versal.h"
#elif defined(TARGET_zynq)
#include "hal/zynq.h"
#elif defined(TARGET_nxp_ls1028a)
#include "hal/nxp_ls1028a.h"
#endif

/* Linker exported variables */
extern unsigned int __bss_start__;
extern unsigned int __bss_end__;
#ifndef NO_XIP
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
#endif

extern void main(void);
extern void gicv2_init_secure(void);

/* Asm helper in boot_aarch64_start.S: cleans the entire D-cache to PoC,
 * invalidates the I-cache to PoU, and disables MMU + I-cache + D-cache
 * via SCTLR_EL2, then returns. Required before handoff to any payload
 * that sets up its own translation (Linux kernel, hypervisor, bare-metal
 * RTOS, later bootloader stage), and mandatory for the ARM64 Linux boot
 * protocol. Only built when EL2_HYPERVISOR == 1 is visible to
 * boot_aarch64_start.S (e.g. via hal/zynq.h on ZynqMP). */
#if defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
extern void el2_flush_and_disable_mmu(void);
#endif

/* Clean & invalidate the data cache over [start,end) (in boot_aarch64_start.S).
 * Used before jumping to a freshly-loaded image with MMU/caches still on, so the
 * code reaches memory and the I-cache refills from it, not from stale lines. */
extern void flush_dcache_range(unsigned long start, unsigned long end);

/* SKIP_GIC_INIT - Skip GIC initialization before booting app
 * This is needed for:
 * - Versal: Uses GICv3, not GICv2. BL31 handles GIC setup.
 * - Systems where another bootloader stage handles GIC init
 * NO_QNX also implies SKIP_GIC_INIT for backwards compatibility
 */
#if defined(NO_QNX) && !defined(SKIP_GIC_INIT)
#define SKIP_GIC_INIT
#endif

#ifndef TARGET_versal
/* current_el() is defined in hal/versal.h for Versal */
unsigned int current_el(void)
{
    unsigned long el;
    asm volatile("mrs %0, CurrentEL" : "=r" (el) : : "cc");
    return (unsigned int)((el >> 2) & 0x3U);
}
#endif

#if defined(BOOT_EL1) && defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
/**
 * @brief Transition from EL2 to EL1 and jump to application
 *
 * This function configures the necessary system registers for EL1 operation
 * and performs an exception return (ERET) to drop from EL2 to EL1.
 *
 * Based on ARM Architecture Reference Manual and U-Boot implementation.
 *
 * @param entry_point Address to jump to in EL1
 * @param dts_addr Device tree address (passed in x0 to application)
 */
extern void el2_to_el1_boot(uintptr_t entry_point, uintptr_t dts_addr);
#endif /* BOOT_EL1 && EL2_HYPERVISOR */

void boot_entry_C(void)
{
    register unsigned int *dst, *src;

    /* Initialize the BSS section to 0 */
    dst = &__bss_start__;
    while (dst < (unsigned int *)&__bss_end__) {
        *dst = 0U;
        dst++;
    }

#ifndef NO_XIP
    /* Copy data section from flash to RAM if necessary */
    src = (unsigned int*)&_stored_data;
    dst = (unsigned int*)&_start_data;
    if (src != dst) {
        while (dst < (unsigned int *)&_end_data) {
            *dst = *src;
            dst++;
            src++;
        }
    }
#else
    (void)src;
#endif

    /* Run wolfboot! */
    main();
}


#ifdef MMU
int WEAKFUNCTION hal_dts_fixup(void* dts_addr)
{
    (void)dts_addr;
    return 0;
}
#endif


/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - Call the application entry point
 *
 */

#ifdef MMU
void RAMFUNCTION do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void RAMFUNCTION do_boot(const uint32_t *app_offset)
#endif
{
    wolfBoot_printf("do_boot: entry=0x%08x, EL=%d\n",
        (uint32_t)(uintptr_t)app_offset, current_el());
#ifdef MMU
    wolfBoot_printf("do_boot: dts=0x%08x\n", (uint32_t)(uintptr_t)dts_offset);
    hal_dts_fixup((uint32_t*)dts_offset);
#endif

#ifndef SKIP_GIC_INIT
    /* Initialize GICv2 for Kernel (ZynqMP and similar platforms)
     * Skip this for:
     * - Versal (uses GICv3, handled by BL31)
     * - Platforms where BL31 or another stage handles GIC
     */
    gicv2_init_secure();
#endif

#if defined(BOOT_EL1) && defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    /* Transition from EL2 to EL1 before jumping to application.
     * This is needed when:
     * - Application expects to run at EL1 (e.g., Linux kernel)
     * - wolfBoot runs at EL2 (hypervisor mode)
     */
    {
    #ifdef MMU
        uintptr_t dts = (uintptr_t)dts_offset;
    #else
        uintptr_t dts = 0;
    #endif
        wolfBoot_printf("do_boot: EL2->EL1 via ERET\n");
        el2_to_el1_boot((uintptr_t)app_offset, dts);
    }
#else
    /* Stay at current EL (EL2 or EL3) and jump directly to application.
     *
     * Before the jump, tear down wolfBoot's EL2 MMU/caches so the next
     * stage enters with a clean state. Mandatory for the ARM64 Linux
     * boot protocol (Linux's arm64_panic_block_init() panics with
     * "Non-EFI boot detected with MMU and caches enabled" otherwise),
     * and correct for any payload that sets up its own translation
     * (hypervisor, RTOS, later bootloader stage). */
#if defined(MMU) && defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    if (current_el() == 2) {
        wolfBoot_printf("do_boot: flushing caches, disabling MMU\n");
        el2_flush_and_disable_mmu();
    }
#endif

    /* Non-Linux EL2 and EL3 path: legacy direct br x4 */

#if defined(TARGET_nxp_ls1028a) && defined(MMU) && \
    (!defined(EL2_HYPERVISOR) || EL2_HYPERVISOR == 0)
    /* LS1028A EL3 path keeps MMU/caches ON (its ENETC needs coherent cacheable
     * DMA). Scoped here because other MMU AArch64 parts (e.g. raspi3) tear the
     * MMU down for Linux and do not define WOLFBOOT_PARTITION_SIZE. Clean the
     * image + DTB from D-cache and invalidate I-cache before the jump, or the
     * core fetches stale DRAM. Bound the app clean by WOLFBOOT_PARTITION_SIZE
     * (over-cleaning is harmless; under-cleaning is the defect). */
    #ifndef WOLFBOOT_MMU_FLUSH_APP_SIZE
    #define WOLFBOOT_MMU_FLUSH_APP_SIZE WOLFBOOT_PARTITION_SIZE
    #endif
    #ifndef WOLFBOOT_MMU_FLUSH_DTS_SIZE
    #define WOLFBOOT_MMU_FLUSH_DTS_SIZE 0x100000UL
    #endif
    flush_dcache_range((unsigned long)(uintptr_t)app_offset,
                       (unsigned long)(uintptr_t)app_offset
                           + (unsigned long)WOLFBOOT_MMU_FLUSH_APP_SIZE);
    if ((uintptr_t)dts_offset != 0) {
        flush_dcache_range((unsigned long)(uintptr_t)dts_offset,
                           (unsigned long)(uintptr_t)dts_offset
                               + (unsigned long)WOLFBOOT_MMU_FLUSH_DTS_SIZE);
    }
    asm volatile("ic iallu");
    asm volatile("dsb ish");
    asm volatile("isb");
#endif

    /* Set application address via x4 */
    asm volatile("mov x4, %0" : : "r"(app_offset));

#ifdef MMU
    /* Move the dts pointer to x5 (as first argument) */
    asm volatile("mov x5, %0" : : "r"(dts_offset));
#else
    asm volatile("mov x5, xzr");
#endif

    /* Zero registers x1, x2, x3 */
    asm volatile("mov x3, xzr");
    asm volatile("mov x2, xzr");
    asm volatile("mov x1, xzr");

    /* Move the dts pointer to x0 (as first argument) */
    asm volatile("mov x0, x5");

    /* Unconditionally jump to app_entry at x4 */
    asm volatile("br x4");
#endif /* BOOT_EL1 */
}

#ifdef RAM_CODE

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

void RAMFUNCTION arch_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;
    wolfBoot_panic();

}
#endif

/* ============================================================================
 * Exception Handlers for EL2 (optional DEBUG_HARDFAULT)
 * ============================================================================
 */

#if defined(EL3_SECURE) && EL3_SECURE == 1 && defined(DEBUG_UART)

/* EL3 exception reporting. Without this, a data abort / SError taken at EL3
 * lands in the silent wfi stub below and looks like a hang. Print the syndrome
 * so the fault class (ESR_EL3.EC) and faulting address (FAR_EL3) are visible.
 * Gated on DEBUG_UART so release EL3 targets keep the minimal silent stub. */
static void print_exception_info_el3(const char *type)
{
    unsigned long esr = 0, elr = 0, far = 0;
    __asm__ volatile("mrs %0, ESR_EL3" : "=r"(esr));
    __asm__ volatile("mrs %0, ELR_EL3" : "=r"(elr));
    __asm__ volatile("mrs %0, FAR_EL3" : "=r"(far));
    wolfBoot_printf("\n*** %s EXCEPTION (EL3) ***\n", type);
    wolfBoot_printf("ESR_EL3: 0x%08x%08x\n",
        (uint32_t)(esr >> 32), (uint32_t)esr);
    wolfBoot_printf("ELR_EL3: 0x%08x%08x\n",
        (uint32_t)(elr >> 32), (uint32_t)elr);
    wolfBoot_printf("FAR_EL3: 0x%08x%08x\n",
        (uint32_t)(far >> 32), (uint32_t)far);
}

void SynchronousInterrupt(void)
    { print_exception_info_el3("SYNCHRONOUS"); while (1) { __asm__ volatile("wfi"); } }
void IRQInterrupt(void)
    { print_exception_info_el3("IRQ"); while (1) { __asm__ volatile("wfi"); } }
void FIQInterrupt(void)
    { print_exception_info_el3("FIQ"); while (1) { __asm__ volatile("wfi"); } }
void SErrorInterrupt(void)
    { print_exception_info_el3("SERROR"); while (1) { __asm__ volatile("wfi"); } }

#elif defined(DEBUG_HARDFAULT) && defined(DEBUG_UART) && defined(EL2_HYPERVISOR)

#define READ_SYSREG(_out, _reg) __asm__ volatile("mrs %0, " #_reg : "=r"(_out))

static void print_exception_info(const char *type)
{
    uint64_t esr, elr, far;

    READ_SYSREG(esr, ESR_EL2);
    READ_SYSREG(elr, ELR_EL2);
    READ_SYSREG(far, FAR_EL2);

    wolfBoot_printf("\n\n*** %s EXCEPTION ***\n", type);
    wolfBoot_printf("ESR_EL2: 0x%08x%08x\n", (uint32_t)(esr >> 32), (uint32_t)esr);
    wolfBoot_printf("ELR_EL2: 0x%08x%08x\n", (uint32_t)(elr >> 32), (uint32_t)elr);
    wolfBoot_printf("FAR_EL2: 0x%08x%08x\n", (uint32_t)(far >> 32), (uint32_t)far);
    wolfBoot_printf("*** SYSTEM HALTED ***\n");
}

static void hardfault_halt(const char *type)
{
    print_exception_info(type);
    while (1) { __asm__ volatile("wfi"); }
}

void SynchronousInterrupt(void) { hardfault_halt("SYNCHRONOUS"); }
void IRQInterrupt(void) { hardfault_halt("IRQ"); }
void FIQInterrupt(void) { hardfault_halt("FIQ"); }
void SErrorInterrupt(void) { hardfault_halt("SERROR"); }

#elif defined(DEBUG_UART) && (!defined(EL2_HYPERVISOR) || EL2_HYPERVISOR == 0)
/* EL3 exception diagnostic: print the syndrome then halt -- catches a faulting
 * datapath (ENETC DMA abort/SError) over UART before a watchdog reset masks it. */
#ifndef READ_SYSREG
#define READ_SYSREG(_out, _reg) __asm__ volatile("mrs %0, " #_reg : "=r"(_out))
#endif
static void el3_fault_halt(const char *type)
{
    uint64_t esr = 0, elr = 0, far = 0;
    unsigned int el = current_el();

    /* ESR_EL3/ELR_EL3/FAR_EL3 are only legal at EL3; guard on the runtime EL so
     * a non-EL3 AArch64 target built with DEBUG_UART does not nested-trap. */
    if (el == 3) {
        READ_SYSREG(esr, ESR_EL3);
        READ_SYSREG(elr, ELR_EL3);
        READ_SYSREG(far, FAR_EL3);
        wolfBoot_printf("\n*** %s EXCEPTION (EL3) ***\n", type);
        wolfBoot_printf("ESR_EL3: 0x%08x%08x\n", (uint32_t)(esr >> 32), (uint32_t)esr);
        wolfBoot_printf("ELR_EL3: 0x%08x%08x\n", (uint32_t)(elr >> 32), (uint32_t)elr);
        wolfBoot_printf("FAR_EL3: 0x%08x%08x\n", (uint32_t)(far >> 32), (uint32_t)far);
    }
    else {
        wolfBoot_printf("\n*** %s EXCEPTION (EL%d) ***\n", type, el);
    }
    while (1) { __asm__ volatile("wfi"); }
}
void SynchronousInterrupt(void) { el3_fault_halt("SYNCHRONOUS"); }
void IRQInterrupt(void) { el3_fault_halt("IRQ"); }
void FIQInterrupt(void) { el3_fault_halt("FIQ"); }
void SErrorInterrupt(void) { el3_fault_halt("SERROR"); }
#else
/* Simple stubs when debug not enabled */
void SynchronousInterrupt(void) { while (1) { __asm__ volatile("wfi"); } }
void IRQInterrupt(void) { while (1) { __asm__ volatile("wfi"); } }
void FIQInterrupt(void) { while (1) { __asm__ volatile("wfi"); } }
void SErrorInterrupt(void) { while (1) { __asm__ volatile("wfi"); } }
#endif /* DEBUG_HARDFAULT && DEBUG_UART && EL2_HYPERVISOR */
