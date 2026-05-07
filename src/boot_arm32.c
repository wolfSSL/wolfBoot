/* boot_arm32.c
 *
 * Bring up, vectors and do_boot for 32bit Cortex-A microprocessors.
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
#include "wolfboot/wolfboot.h"

extern unsigned int __bss_start__;
extern unsigned int __bss_end__;
static volatile unsigned int cpu_id;
extern unsigned int *END_STACK;

extern void main(void);

void boot_entry_C(void)
{
    register unsigned int *dst;
    /* Initialize the BSS section to 0 */
    dst = &__bss_start__;
    while (dst < (unsigned int *)&__bss_end__) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfboot! */
    main();
}

/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - Call the application entry point
 *
 */

#if defined(WOLFBOOT_LINUX_PAYLOAD) && !defined(MMU)
#error "WOLFBOOT_LINUX_PAYLOAD requires MMU=1 - the ARM Linux boot ABI " \
       "needs r2 to be a valid DTB physical address, which only the MMU " \
       "branch of do_boot can supply. Set MMU=1 in your config."
#endif

#ifdef MMU
void RAMFUNCTION do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void RAMFUNCTION do_boot(const uint32_t *app_offset)
#endif
{
    /* Single asm block so the compiler cannot insert any code between
     * the register set-up and the bx that would clobber r0..r4. Inputs
     * are tied to the matching named operands; r0..r4 are listed in the
     * clobber set so the compiler does not assume their values survive. */
#ifdef MMU
    register const uint32_t *dts_in = dts_offset;
#else
    register const uint32_t *dts_in = (const uint32_t *)0;
#endif
#ifdef WOLFBOOT_LINUX_PAYLOAD
    /* ARM Linux boot ABI: r0 = 0, r1 = ~0 (no machine ID, use DTB),
     * r2 = DTB physical address, r3 = 0, entry in r4. */
    asm volatile (
        "mov r4, %[entry]\n"
        "mov r2, %[dts]\n"
        "mov r0, #0\n"
        "mvn r1, #0\n"
        "mov r3, #0\n"
        "bx  r4\n"
        :
        : [entry] "r" (app_offset), [dts] "r" (dts_in)
        : "r0", "r1", "r2", "r3", "r4", "memory"
    );
#else
    /* wolfBoot legacy DTS handoff: r0 = dts pointer, r1=r2=r3=0,
     * entry in r4. */
    asm volatile (
        "mov r4, %[entry]\n"
        "mov r0, %[dts]\n"
        "mov r1, #0\n"
        "mov r2, #0\n"
        "mov r3, #0\n"
        "bx  r4\n"
        :
        : [entry] "r" (app_offset), [dts] "r" (dts_in)
        : "r0", "r1", "r2", "r3", "r4", "memory"
    );
#endif
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
