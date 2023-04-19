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

#include "hal/nxp_ppc.h"
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

/* Stringification */
#ifndef WC_STRINGIFY
#define _WC_STRINGIFY_L2(str) #str
#define WC_STRINGIFY(str) _WC_STRINGIFY_L2(str)
#endif

void write_tlb(uint32_t mas0, uint32_t mas1, uint32_t mas2, uint32_t mas3,
    uint32_t mas7)
{
    MTSPR(WC_STRINGIFY(MAS0), mas0);
    MTSPR(WC_STRINGIFY(MAS1), mas1);
    MTSPR(WC_STRINGIFY(MAS2), mas2);
    MTSPR(WC_STRINGIFY(MAS3), mas3);
    MTSPR(WC_STRINGIFY(MAS7), mas7);
    asm volatile("isync;msync;tlbwe;isync");
}

void set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint64_t rpn,
             uint8_t perms, uint8_t wimge,
             uint8_t ts, uint8_t tsize, uint8_t iprot)
{
    uint32_t _mas0, _mas1, _mas2, _mas3, _mas7;

    _mas0 = BOOKE_MAS0(tlb, esel, 0);
    _mas1 = BOOKE_MAS1(1, iprot, 0, ts, tsize);
    _mas2 = BOOKE_MAS2(epn, wimge);
    _mas3 = BOOKE_MAS3(rpn, 0, perms);
    _mas7 = BOOKE_MAS7(rpn);

    write_tlb(_mas0, _mas1, _mas2, _mas3, _mas7);
}

void invalidate_tlb(int tlb)
{
    if (tlb == 0)
        MTSPR(WC_STRINGIFY(MMUCSR0), 0x4);
    else if (tlb == 1)
        MTSPR(WC_STRINGIFY(MMUCSR0), 0x2);
}

void __attribute((weak)) hal_ddr_init(void)
{

}

void boot_entry_C(void)
{
    register unsigned int *dst, *src, *end;

    hal_ddr_init();

    /* Copy the .data section from flash to RAM */
    src = (unsigned int*)&_stored_data;
    dst = (unsigned int*)&_start_data;
    end = (unsigned int*)&_end_data;
    while (dst < end) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = (unsigned int*)&__bss_start__;
    end = (unsigned int*)&__bss_end__;
    while (dst < end) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfBoot! */
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
