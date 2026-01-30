/* boot_aarch64.c
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

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"

/* Include platform-specific header for EL configuration defines */
#ifdef TARGET_versal
#include "hal/versal.h"
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
#ifdef MMU
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
        el2_to_el1_boot((uintptr_t)app_offset, dts);
    }
#else
    /* Stay at current EL (EL2 or EL3) and jump directly to application */

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
 * Exception Handlers for EL2 (DEBUG_HARDFAULT support)
 * ============================================================================
 * These handlers print diagnostic information when exceptions occur.
 * ESR_EL2 - Exception Syndrome Register (cause of exception)
 * ELR_EL2 - Exception Link Register (address that caused exception)
 * FAR_EL2 - Fault Address Register (for data/instruction aborts)
 */

#if defined(DEBUG_UART) && defined(EL2_HYPERVISOR)

/* Read EL2 exception registers */
static inline uint64_t read_esr_el2(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, ESR_EL2" : "=r"(val));
    return val;
}

static inline uint64_t read_elr_el2(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, ELR_EL2" : "=r"(val));
    return val;
}

static inline uint64_t read_far_el2(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, FAR_EL2" : "=r"(val));
    return val;
}

/* Decode ESR exception class */
static const char* decode_ec(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Unknown";
        case 0x01: return "WFI/WFE trapped";
        case 0x07: return "SVE/SIMD/FP trapped";
        case 0x0E: return "Illegal execution state";
        case 0x15: return "SVC in AArch64";
        case 0x16: return "HVC in AArch64";
        case 0x17: return "SMC in AArch64";
        case 0x20: return "Inst abort (lower EL)";
        case 0x21: return "Inst abort (same EL)";
        case 0x22: return "PC alignment fault";
        case 0x24: return "Data abort (lower EL)";
        case 0x25: return "Data abort (same EL)";
        case 0x26: return "SP alignment fault";
        case 0x2C: return "FP exception";
        case 0x2F: return "SError";
        case 0x30: return "Breakpoint (lower EL)";
        case 0x31: return "Breakpoint (same EL)";
        case 0x32: return "Software step (lower EL)";
        case 0x33: return "Software step (same EL)";
        case 0x34: return "Watchpoint (lower EL)";
        case 0x35: return "Watchpoint (same EL)";
        case 0x3C: return "BRK instruction";
        default:   return "Other";
    }
}

static void print_exception_info(const char *type) {
    uint64_t esr = read_esr_el2();
    uint64_t elr = read_elr_el2();
    uint64_t far = read_far_el2();
    uint32_t ec = (esr >> 26) & 0x3F;
    uint32_t iss = esr & 0x1FFFFFF;

    wolfBoot_printf("\n\n*** %s EXCEPTION ***\n", type);
    wolfBoot_printf("ESR_EL2: 0x%08x%08x\n", (uint32_t)(esr >> 32), (uint32_t)esr);
    wolfBoot_printf("  EC (Exception Class): 0x%x - %s\n", ec, decode_ec(ec));
    wolfBoot_printf("  ISS (Syndrome): 0x%x\n", iss);
    wolfBoot_printf("ELR_EL2 (fault addr): 0x%08x%08x\n", (uint32_t)(elr >> 32), (uint32_t)elr);
    wolfBoot_printf("FAR_EL2 (access addr): 0x%08x%08x\n", (uint32_t)(far >> 32), (uint32_t)far);
    wolfBoot_printf("*** SYSTEM HALTED ***\n");
}

void SynchronousInterrupt(void) {
    print_exception_info("SYNCHRONOUS");
    while(1) { __asm__ volatile("wfi"); }
}

void IRQInterrupt(void) {
    print_exception_info("IRQ");
    while(1) { __asm__ volatile("wfi"); }
}

void FIQInterrupt(void) {
    print_exception_info("FIQ");
    while(1) { __asm__ volatile("wfi"); }
}

void SErrorInterrupt(void) {
    print_exception_info("SERROR");
    while(1) { __asm__ volatile("wfi"); }
}

#else
/* Simple stubs when debug not enabled */
void SynchronousInterrupt(void) { while(1); }
void IRQInterrupt(void) { while(1); }
void FIQInterrupt(void) { while(1); }
void SErrorInterrupt(void) { while(1); }
#endif /* DEBUG_UART && EL2_HYPERVISOR */