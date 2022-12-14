/* boot_ppc.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;

extern void main(void);
extern void hal_ddr_init(void);

#define MTSPR(rn, v) asm volatile("mtspr " rn ",%0" : : "r" (v))

/* e6500 MPC85xx MMU Assist Registers */
#define MAS0 "0x270"
#define MAS1 "0x271"
#define MAS2 "0x272"
#define MAS3 "0x273"
#define MAS7 "0x3B0"
#define MMUCSR0 "0x3F4" /* MMU control and status register 0 */

void write_tlb(uint32_t mas0, uint32_t mas1, uint32_t mas2, uint32_t mas3,
    uint32_t mas7)
{
    MTSPR(MAS0, mas0);
    MTSPR(MAS1, mas1);
    MTSPR(MAS2, mas2);
    MTSPR(MAS3, mas3);
    MTSPR(MAS7, mas7);
    asm volatile("isync;msync;tlbwe;isync");
}

void invalidate_tlb(int tlb)
{
    if (tlb == 0)
        MTSPR(MMUCSR0, 0x4);
    else if (tlb == 1)
        MTSPR(MMUCSR0, 0x2);
}

void __attribute((weak)) hal_ddr_init(void)
{

}

void boot_entry_C(void)
{
    register unsigned int *dst, *src;

    hal_ddr_init();

    /* Copy the .data section from flash to RAM */
    src = (unsigned int*)&_stored_data;
    dst = (unsigned int*)&_start_data;
    while (dst < (unsigned int*)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = (unsigned int*)&__bss_start__;
    while (dst < (unsigned int*)&__bss_end__) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfboot! */
    main();
}

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
#ifdef MMU
    /* TODO: Determine if the dts_offset needs passed as argument */
    (void)dts_offset;
#endif

    asm volatile("mtlr %0; blr":: "r"(app_offset));
}

void arch_reboot(void) {}
