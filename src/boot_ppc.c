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
extern void hal_early_init(void);

void write_tlb(uint32_t mas0, uint32_t mas1, uint32_t mas2, uint32_t mas3,
    uint32_t mas7)
{
    mtspr(MAS0, mas0);
    mtspr(MAS1, mas1);
    mtspr(MAS2, mas2);
    mtspr(MAS3, mas3);
    mtspr(MAS7, mas7);
    asm volatile("isync;msync;tlbwe;isync");
}

void set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint32_t rpn,
    uint32_t urpn, uint8_t perms, uint8_t wimge, uint8_t ts, uint8_t tsize,
    uint8_t iprot)
{
    uint32_t _mas0, _mas1, _mas2, _mas3, _mas7;

    _mas0 = BOOKE_MAS0(tlb, esel, 0);
    _mas1 = BOOKE_MAS1(1, iprot, 0, ts, tsize);
    _mas2 = BOOKE_MAS2(epn, wimge);
    _mas3 = BOOKE_MAS3(rpn, 0, perms);
    _mas7 = BOOKE_MAS7(urpn);

    write_tlb(_mas0, _mas1, _mas2, _mas3, _mas7);
}

void disable_tlb1(uint8_t esel)
{
    uint32_t _mas0, _mas1, _mas2, _mas3, _mas7;

    _mas0 = BOOKE_MAS0(1, esel, 0);
    _mas1 = 0;
    _mas2 = 0;
    _mas3 = 0;
    _mas7 = 0;

    write_tlb(_mas0, _mas1, _mas2, _mas3, _mas7);
}

void invalidate_tlb(int tlb)
{
    if (tlb == 0)
        mtspr(MMUCSR0, 0x4);
    else if (tlb == 1)
        mtspr(MMUCSR0, 0x2);
}

void set_law(uint8_t idx, uint32_t addr_h, uint32_t addr_l, uint32_t trgt_id,
    uint32_t law_sz, int reset)
{
    if (reset)
        set32(LAWAR(idx), 0); /* reset */
    #ifdef CORE_E500
        (void)addr_h; /* not used */
        set32(LAWBAR(idx), addr_l >> 12);
    #else
        set32(LAWBARH(idx), addr_h);
        set32(LAWBARL(idx), addr_l);
    #endif
    set32(LAWAR(idx), LAWAR_ENABLE | LAWAR_TRGT_ID(trgt_id) | law_sz);

    /* Read back so that we sync the writes */
    (void)get32(LAWAR(idx));
}

void __attribute((weak)) hal_early_init(void)
{

}

void boot_entry_C(void)
{
    register unsigned int *dst, *src, *end;

    hal_early_init();

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

#ifndef BUILD_LOADER_STAGE1
void flush_cache(uint32_t start_addr, uint32_t size)
{
    uint32_t addr, start, end;

    start = start_addr & ~(CACHE_LINE_SIZE - 1);
    end = start_addr + size - 1;

    for (addr = start; (addr <= end) && (addr >= start);
            addr += CACHE_LINE_SIZE) {
        asm volatile("dcbst 0,%0" : : "r" (addr) : "memory");
    }
    /* wait for all dcbst to complete on bus */
    asm volatile("sync" : : : "memory");

    for (addr = start; (addr <= end) && (addr >= start);
            addr += CACHE_LINE_SIZE) {
        asm volatile("icbi 0,%0" : : "r" (addr) : "memory");
    }
    asm volatile("sync" : : : "memory");
    /* flush prefetch queue */
    asm volatile("isync" : : : "memory");
}
#endif

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
#ifndef BUILD_LOADER_STAGE1
    uint32_t msr;
#endif
    typedef void (*boot_entry)(uintptr_t r3, uintptr_t r4, uintptr_t r5, uintptr_t r6,
                               uintptr_t r7, uintptr_t r8, uintptr_t r9);
    boot_entry entry = (boot_entry)app_offset;

#ifndef BUILD_LOADER_STAGE1
    /* invalidate cache */
    flush_cache((uint32_t)app_offset, L1_CACHE_SZ);

    /* Disable all async interrupts */
    msr = mfmsr();
    msr &= ~(MSR_CE | MSR_ME | MSR_DE);
    mtmsr(msr);
#endif

    /* ePAPR (Embedded Power Architecture Platform Requirements)
     * https://elinux.org/images/c/cf/Power_ePAPR_APPROVED_v1.1.pdf
     */
    entry(
    #ifdef MMU
        (uintptr_t)dts_offset,   /* r3 = dts address */
    #else
        0,
    #endif
        0, 0,
        EPAPR_MAGIC,             /* r6 = ePAPR magic */
        WOLFBOOT_PARTITION_SIZE, /* r7 = Size of Initial Mapped Area (IMA) */
        0, 0
    );
}

void arch_reboot(void) {}
