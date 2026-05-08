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

#ifdef MMU
void RAMFUNCTION do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void RAMFUNCTION do_boot(const uint32_t *app_offset)
#endif
{
    /* Single asm block so the compiler cannot insert any code between
     * the register set-up and the bx that would clobber r0..r4. Inputs
     * are tied to the matching named operands; r0..r4 are listed in the
     * clobber set so the compiler does not assume their values survive.
     *
     * When MMU=1 we always emit the ARM Linux boot ABI
     * (r0=0, r1=~0, r2=DTB_phys, r3=0). Bare-metal payloads do not read
     * r0..r3 so the same ABI works for them; Linux requires it. This
     * removes the need for a separate LINUX_PAYLOAD switch per target.
     *
     * Without MMU there is no DTB to pass, so we fall back to a minimal
     * handoff (all GPRs cleared) used by targets like sama5d3. */
#ifdef MMU
    register const uint32_t *dts_in = dts_offset;
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
    asm volatile (
        "mov r4, %[entry]\n"
        "mov r0, #0\n"
        "mov r1, #0\n"
        "mov r2, #0\n"
        "mov r3, #0\n"
        "bx  r4\n"
        :
        : [entry] "r" (app_offset)
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
