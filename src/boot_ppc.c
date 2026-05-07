/* boot_ppc.c
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

#include "hal/nxp_ppc.h"
#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"
#ifdef MMU
#include "fdt.h"
#endif

extern unsigned int __bss_start__;
extern unsigned int __bss_end__;
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
#ifdef RAM_CODE
/* .ramcode section (RAMFUNCTION) - may be in separate memory region */
extern unsigned int _stored_ramcode;
extern unsigned int _start_ramcode;
extern unsigned int _end_ramcode;
#endif

extern void main(void);
extern void hal_early_init(void);

void RAMFUNCTION write_tlb(uint32_t mas0, uint32_t mas1, uint32_t mas2,
    uint32_t mas3, uint32_t mas7)
{
    mtspr(MAS0, mas0);
    mtspr(MAS1, mas1);
    mtspr(MAS2, mas2);
    mtspr(MAS3, mas3);
    mtspr(MAS7, mas7);
    __asm__ __volatile__("isync;msync;tlbwe;isync");
}

void RAMFUNCTION set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint32_t rpn,
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
    if (reset) {
        set32(LAWAR(idx), 0); /* reset */
    }
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

void WEAKFUNCTION hal_early_init(void)
{

}
#ifdef MMU
int WEAKFUNCTION hal_dts_fixup(void* dts_addr)
{
    (void)dts_addr;
    return 0;
}
#endif

/* forward declaration */
#ifndef BUILD_LOADER_STAGE1
void flush_cache(uint32_t start_addr, uint32_t size);
#endif

void boot_entry_C(void)
{
    volatile unsigned int *dst;
    volatile const unsigned int *src;
    volatile unsigned int *end;

#ifdef RAM_CODE
    /* Copy .ramcode section FIRST - to CPC SRAM which is already available.
     * This makes RAMFUNCTION code (memcpy, memmove) available before DDR.
     * Use volatile to prevent compiler from transforming to memcpy call. */
    src = (volatile const unsigned int*)&_stored_ramcode;
    dst = (volatile unsigned int*)&_start_ramcode;
    end = (volatile unsigned int*)&_end_ramcode;
    while (dst < end) {
        *dst = *src;
        dst++;
        src++;
    }

#ifndef BUILD_LOADER_STAGE1
    /* Flush D-cache and invalidate I-cache for .ramcode in CPC SRAM.
     * PowerPC I/D caches are not coherent — explicit dcbst+icbi required. */
    if ((uint32_t)&_end_ramcode > (uint32_t)&_start_ramcode) {
        flush_cache((uint32_t)&_start_ramcode,
            (uint32_t)&_end_ramcode - (uint32_t)&_start_ramcode);
    }
#endif
#endif /* RAM_CODE */

    /* Now initialize DDR and other hardware */
    hal_early_init();

    /* Copy the .data section from flash to DDR.
     * Use volatile to prevent the compiler from transforming this loop
     * into a memcpy() call. */
    src = (volatile const unsigned int*)&_stored_data;
    dst = (volatile unsigned int*)&_start_data;
    end = (volatile unsigned int*)&_end_data;
    while (dst < end) {
        *dst = *src;
        dst++;
        src++;
    }

#ifndef BUILD_LOADER_STAGE1
    /* Flush D-cache and invalidate I-cache for .data region in DDR. */
    flush_cache((uint32_t)&_start_data,
        (uint32_t)&_end_data - (uint32_t)&_start_data);
#endif

    /* Initialize the BSS section to 0 (volatile prevents memset transform) */
    dst = (volatile unsigned int*)&__bss_start__;
    end = (volatile unsigned int*)&__bss_end__;
    while (dst < end) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfBoot! */
#if defined(ENABLE_DDR) && defined(DDR_STACK_TOP)
    /* DDR is initialized, .data and .bss are set up.
     * Switch stack from CPC SRAM to DDR for:
     * 1. Better performance (DDR stack is cacheable by L1/L2/CPC)
     * 2. More stack space (64KB vs shared CPC SRAM)
     * Uses assembly trampoline since we can't return after stack switch.
     * The CPC SRAM will be released back to L2 cache in hal_init(). */
    {
        extern void ddr_call_with_stack(uint32_t func, uint32_t sp);
        /* Zero DDR stack area using volatile to prevent memset transform */
        volatile uint32_t *p = (volatile uint32_t *)DDR_STACK_BASE;
        volatile uint32_t *e = (volatile uint32_t *)DDR_STACK_TOP;
        while (p < e) { *p++ = 0; }
        ddr_call_with_stack((uint32_t)main, DDR_STACK_TOP - 64);
        /* Does not return */
    }
#else
    main();
#endif
}

#ifndef BUILD_LOADER_STAGE1
void flush_cache(uint32_t start_addr, uint32_t size)
{
    uint32_t addr, start, end;

    start = start_addr & ~(CACHE_LINE_SIZE - 1);
    end = start_addr + size - 1;

    for (addr = start; (addr <= end) && (addr >= start);
            addr += CACHE_LINE_SIZE) {
        __asm__ __volatile__("dcbst 0,%0" : : "r" (addr) : "memory");
    }
    /* wait for all dcbst to complete on bus */
    __asm__ __volatile__("sync" : : : "memory");

    for (addr = start; (addr <= end) && (addr >= start);
            addr += CACHE_LINE_SIZE) {
        __asm__ __volatile__("icbi 0,%0" : : "r" (addr) : "memory");
    }
    __asm__ __volatile__("sync" : : : "memory");
    /* flush prefetch queue */
    __asm__ __volatile__("isync" : : : "memory");
}
#endif

#ifdef ENABLE_OS64BIT
/* Transition to 64-bit memory map for VxWorks 7 / Linux 64-bit.
 * Equivalent to CW U-Boot's "ossel ostype2" bootm transition.
 * Must run from RAMFUNCTION (DDR) since we modify flash/CCSR LAW and TLB.
 *
 * 64-bit physical layout (matches the CW VPX3-152 cw_152_64.dtb):
 * - DDR (4 GB total): single contiguous PA 0x0_00000000..0x0_FFFFFFFF.
 *   The DTB /memory node lists 3 sub-regions, all with high cell == 0:
 *     0x0..0x40000000 (1 GB), 0x40000000..0x7E400000 (~1 GB minus the
 *     CPU spin-table hole), and 0x80000000..0xFFFFFFFF (upper 2 GB).
 *   No DDR LAW alias at PA 0xC_00000000 -- VxWorks accesses upper DDR
 *   at low PA 0x80000000+ via the existing 4 GB DDR LAW.
 * - Peripheral LAWs (CCSR, flash, BMan/QMan portals, FPGA, NVRAM, PCIe,
 *   DCSR) live in the high-half PA window with BARH=0xF.
 * - PCIe4 (Switch) memory at PA 0xD_00000000.
 *
 * Earlier comments here referenced a "FUM Table 2.5" with DDR upper at
 * PA 0xC_00000000 -- that was a mis-read. The actual map (DTB + CW
 * U-Boot final state after cw_late_mmap_adjust) keeps DDR contiguous
 * at low PA. */
static void RAMFUNCTION hal_os64bit_map_transition(void)
{
    /* Add peripheral LAWs needed by 64-bit OS (VxWorks 7, Linux 64-bit).
     * CCSR, flash, and DDR lower LAWs are already set from boot assembly
     * with CCSRBAR_PHYS_HIGH=0xF and FLASH_BASE_PHYS_HIGH=0xF.
     * The CW U-Boot cw_late_mmap_adjust() only adjusts PCI LAWs/ATMUs;
     * all other LAWs are set during U-Boot board init. wolfBoot adds the
     * equivalent LAWs here. */

    /* LAW table matching CW U-Boot law_table_lnx_vx7 (law.c).
     * Flash (LAW0/1) and CCSR LAWs are set from boot assembly.
     * CPLD LAW is set by hal_cpld_init. Add the remaining entries. */

    /* FPGA 32-bit registers 2MB at PA 0xF_EE400000 */
    set_law(6,  0xF, 0xEE400000, LAW_TRGT_IFC, LAW_SIZE_2MB, 1);
    /* FPGA 8-bit registers 512KB at PA 0xF_EE600000 */
    set_law(7,  0xF, 0xEE600000, LAW_TRGT_IFC, LAW_SIZE_512KB, 1);
    /* NVRAM 512KB at PA 0xF_EE700000 */
    set_law(8,  0xF, 0xEE700000, LAW_TRGT_IFC, LAW_SIZE_512KB, 1);
    /* BMan portals 32MB at PA 0xF_EA000000 */
    set_law(9,  0xF, 0xEA000000, LAW_TRGT_BMAN, LAW_SIZE_32MB, 1);
    /* QMan portals 32MB at PA 0xF_EC000000 */
    set_law(10, 0xF, 0xEC000000, LAW_TRGT_QMAN, LAW_SIZE_32MB, 1);
    /* DCSR 4MB at PA 0xF_EE000000 */
    set_law(11, 0xF, 0xEE000000, LAW_TRGT_DCSR, LAW_SIZE_4MB, 1);
    /* Note: DDR LAW is 4GB at PA 0x0 (set in hal_ddr_init), covering
     * all DDR contiguously. There is NO DDR alias at PA 0xC_00000000
     * (an earlier comment cited "FUM Table 2.5" for that, but the
     * actual cw_152_64.dtb /memory regions all use high cell == 0
     * and CW U-Boot's cw_late_mmap_adjust() does not touch DDR LAWs).
     * VxWorks accesses upper-DDR via low PA 0x80000000+.
     *
     * The PCIe LAWs below match the post-cw_late_mmap_adjust 64-bit
     * positions: PCIe4 memory at PA 0xD_00000000, PCIe1 memory at
     * PA 0xF_E0000000. Production CW U-Boot rewrites these from the
     * 32-bit-intermediate windows when bootm runs with ossel=ostype2;
     * wolfBoot needs the same final layout at handoff. */
    /* PCIe4 (Switch) memory 2GB at PA 0xD_00000000, EA=0xC0000000 */
    set_law(13, 0xD, 0x00000000, LAW_TRGT_PCIE4, LAW_SIZE_2GB, 1);
    /* PCIe4 I/O 256KB at PA 0xF_EE800000 */
    set_law(14, 0xF, 0xEE800000, LAW_TRGT_PCIE4, LAW_SIZE_256KB, 1);
    /* PCIe1 (XMC) memory 256MB at PA 0xF_E0000000, EA=0xE0000000.
     * (Table 2.5 lists 160MB but power-of-two LAW SIZE rounds to 256MB.) */
    set_law(15, 0xF, 0xE0000000, LAW_TRGT_PCIE1, LAW_SIZE_256MB, 1);
    /* PCIe1 I/O 256KB at PA 0xF_EE840000 */
    set_law(16, 0xF, 0xEE840000, LAW_TRGT_PCIE1, LAW_SIZE_256KB, 1);

    /* No additional TLB entries are added here.
     *
     * CW U-Boot's bootm path (cw_late_mmap_adjust) only adjusts PCIe
     * LAWs and ATMUs -- it does NOT install peripheral TLBs for
     * VxWorks. VxWorks 7's early entry stub creates its own peripheral
     * TLBs based on the FDT.
     *
     * Earlier wolfBoot versions populated TLB slots 3-11 with BMan,
     * QMan, DCSR, FPGA/NVRAM, PCIe1, PCIe4, PCIe1-IO, and PCIe4-IO
     * mappings, mirroring what was thought to be VxWorks's expectation.
     * In practice these collide with the entries VxWorks installs and
     * caused the 64-bit OS jump to silently fault before printing
     * "Hello, VxWorks!". Match U-Boot's much simpler behaviour: hand
     * off with only the wolfBoot-needed TLBs (CCSR, flash, .ramcode,
     * DDR) plus the slot-0 DDR copy required by VxWorks's early TLB
     * iteration (below). */
#if 0  /* Speculative VxWorks-needed TLBs - see comment above. */

    /* BMan portals: EA 0xEA000000 → PA 0xF_EA000000, 32MB */
    set_tlb(1, 3, 0xEA000000, 0xEA000000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, 0, 0,
        BOOKE_PAGESZ_32M, 1);
    /* QMan portals: EA 0xEC000000 → PA 0xF_EC000000, 32MB */
    set_tlb(1, 4, 0xEC000000, 0xEC000000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, 0, 0,
        BOOKE_PAGESZ_32M, 1);
    /* DCSR: EA 0xEE000000 → PA 0xF_EE000000, 4MB */
    set_tlb(1, 5, 0xEE000000, 0xEE000000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_4M, 1);
    /* FPGA/NVRAM: EA 0xEE400000 → PA 0xF_EE400000, 4MB */
    set_tlb(1, 6, 0xEE400000, 0xEE400000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_4M, 1);
    /* PCIe4 (Switch) memory: EA 0xC0000000 -> PA 0xD_00000000, 1 GB.
     *
     * BUG NOTE: previous version used BOOKE_PAGESZ_2G here, but a 2 GB
     * BookE page must be aligned to a 2 GB boundary (EA bit [0] only).
     * With EA=0xC0000000 the MMU silently rounds the EPN down to
     * 0x80000000, so the TLB ended up mapping EA 0x80000000-0xFFFFFFFF
     * to PCIe4 -- which collides with the DDR-upper-2GB window
     * VxWorks expects to access at EA 0x80000000 (mapped to low PA
     * 0x80000000 via VxWorks's own kernel-virtual TLB). Result: any
     * DDR access above 2 GB hit PCIe4 instead and the OS jump faulted
     * silently before the first console write.
     *
     * Use 1 GB at EA 0xC0000000 (1 GB-aligned). PCIe4 only consumes
     * 1 GB of the EA window in Table 2.5 anyway; the upper 1 GB at
     * EA 0xE0000000 is PCIe1, mapped separately just below. */
    set_tlb(1, 7, 0xC0000000, 0x00000000, 0xD,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_1G, 1);
    /* PCIe1 (XMC) memory: EA 0xE0000000 -> PA 0xF_E0000000, 256MB
     * (FUM lists 160MB; rounded up for the power-of-two page size). */
    set_tlb(1, 8, 0xE0000000, 0xE0000000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_256M, 1);
    /* Note: DDR upper 2GB (PA 0x80000000-0xFFFFFFFF) is reachable
     * through the 4GB DDR LAW set in hal_ddr_init. VxWorks installs
     * its own kernel-virtual TLB entry for that PA range early in
     * its TLB-rebuild path; wolfBoot does not pre-map it. */
    /* PCIe1 I/O: EA 0xEE840000 → PA 0xF_EE840000, 256KB */
    set_tlb(1, 10, 0xEE840000, 0xEE840000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_256K, 1);
    /* PCIe4 I/O: EA 0xEE800000 → PA 0xF_EE800000, 256KB */
    set_tlb(1, 11, 0xEE800000, 0xEE800000, 0xF,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        BOOKE_PAGESZ_256K, 1);
#endif /* 0 - speculative TLBs disabled */

    /* Move DDR mapping from TLB1 slot 12 to slot 0 for VxWorks compat.
     * VxWorks 7's early entry stub iterates TLB1 from slot 1 up to NTLB
     * invalidating each entry, then reads slot 0 expecting it to contain
     * the DDR mapping (uses MAS2/MAS3 from slot 0 to compute PA-from-VA).
     * U-Boot ends up with DDR at slot 0 by accident of find_free_tlbcam();
     * wolfBoot pins DDR at slot 12 (boot_ppc_start.S), which gets wiped
     * by VxWorks's loop, leaving slot 0 invalid. Result: VxWorks reads
     * garbage MAS regs and silently fails after the OS jump.
     *
     * Two TLB entries cannot map the same EA range (multi-hit -> machine
     * check), so invalidate slot 12 BEFORE writing slot 0.
     *
     * Safe to do here because hal_os64bit_map_transition is RAMFUNCTION
     * (executes from .ramcode at TLB1 slot 9, EA 0xEE900000) — DDR access
     * is not required during these few instructions, only on return. */
    {
        /* Invalidate slot 12 (DDR). MAS1 V=0, IPROT=0. */
        mtspr(MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(12));
        mtspr(MAS1, 0);
        mtspr(MAS2, 0);
        mtspr(MAS3, 0);
        mtspr(MAS7, 0);
        __asm__ __volatile__("isync; tlbwe; isync; msync" ::: "memory");
    }
    /* DDR at slot 0: EA 0 -> PA 0, 2GB, cached (MAS2_M). */
    set_tlb(1, 0, 0x00000000, 0x00000000, 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0,
        BOOKE_PAGESZ_2G, 1);

    __asm__ __volatile__("sync; isync");
}

/* Final OS-handoff trampoline. Lives in .ramcode (DDR) so the bctr to
 * the OS entry executes from DDR -- matching production U-Boot's
 * pattern (U-Boot relocates itself to DDR before bootm).
 *
 * Steps:
 *   1. Relocate the exception handler (isr_empty) from flash to DDR
 *      and re-point IVPR. wolfBoot is XIP from flash; the handler
 *      lives at flash 0xFFFE0000. Once the next step puts flash in
 *      cache-inhibit + guarded, the e6500 fetcher cannot service the
 *      handler instruction stream and any exception silent-hangs.
 *      Copying the handler to DDR and re-pointing IVPR there fixes
 *      this and is what U-Boot effectively does (its IVPR points to
 *      its DDR-relocated code, not flash).
 *   2. Call hal_flash_cache_disable_pre_os to switch the flash TLB to
 *      MAS2_I|MAS2_G (matches the OS's pre-init state).
 *   3. Drain the UART, sync the pipeline, indirect-jump to entry. */
typedef void (*os64bit_entry_t)(uintptr_t r3, uintptr_t r4, uintptr_t r5,
    uintptr_t r6, uintptr_t r7, uintptr_t r8, uintptr_t r9);

/* Address of the DDR-relocated exception handler. Must be 4KB-aligned
 * for IVPR and well above the kernel-image load region (typical OS
 * load is 0x100000-0x700000). */
#ifndef WOLFBOOT_OS64BIT_IVPR_DDR
#define WOLFBOOT_OS64BIT_IVPR_DDR  0x00800000UL
#endif

void RAMFUNCTION wolfBoot_os64bit_jump(os64bit_entry_t entry,
    uintptr_t r3, uintptr_t r6, uintptr_t r7)
{
    extern void hal_flash_cache_disable_pre_os(void);
    extern unsigned int isr_empty;
    extern unsigned int isr_empty_end;
    {
        volatile uint32_t *src = (volatile uint32_t *)&isr_empty;
        volatile uint32_t *dst = (volatile uint32_t *)
            WOLFBOOT_OS64BIT_IVPR_DDR;
        uintptr_t bytes = (uintptr_t)&isr_empty_end -
            (uintptr_t)&isr_empty;
        /* Word count, rounded up. Cache-line range, rounded up to
         * 64-byte lines (one e6500 dcbz/dcbst granule). */
        uintptr_t copy_words = (bytes + sizeof(uint32_t) - 1U) /
            sizeof(uint32_t);
        uintptr_t cache_words = ((copy_words + 15U) / 16U) * 16U;
        uintptr_t i;

        /* Reserve enough headroom in the IVPR landing zone for the
         * handler to grow without silently truncating the copy. */
        #define WOLFBOOT_OS64BIT_IVPR_MAX 0x400U /* 1 KB */
        if (bytes > WOLFBOOT_OS64BIT_IVPR_MAX) {
            return; /* refuse to jump with a truncated handler */
        }

        for (i = 0; i < copy_words; i++) {
            dst[i] = src[i];
        }
        /* Make the DDR copy visible to the I-cache fetcher. */
        for (i = 0; i < cache_words; i += 16U) { /* 64 bytes per line */
            __asm__ __volatile__("dcbst 0,%0" :: "r"(&dst[i]) : "memory");
        }
        __asm__ __volatile__("sync" ::: "memory");
        for (i = 0; i < cache_words; i += 16U) {
            __asm__ __volatile__("icbi 0,%0" :: "r"(&dst[i]) : "memory");
        }
        __asm__ __volatile__("sync; isync" ::: "memory");
        /* IVOR offsets stay at 0 (set during early reset) so every
         * interrupt type lands at this single handler. */
        mtspr(IVPR, (uint32_t)dst);
        __asm__ __volatile__("isync" ::: "memory");
    }
    hal_flash_cache_disable_pre_os();
    __asm__ __volatile__("sync; isync" ::: "memory");
    entry(r3, 0, 0, r6, r7, 0, 0);
}
#endif /* ENABLE_OS64BIT */

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

#ifdef MMU
    hal_dts_fixup((uint32_t*)dts_offset);
#endif

#if defined(DEBUG_UART) && defined(WOLFBOOT_ARCH_PPC)
    wolfBoot_printf("do_boot: entry=%p, dts=%p\n",
        app_offset,
    #ifdef MMU
        dts_offset
    #else
        (void*)0
    #endif
    );
#endif

#ifndef BUILD_LOADER_STAGE1
    /* Flush the entire OS image region from D-cache before jump. PowerPC
     * I/D caches are not coherent — without dcbst over the whole image,
     * the I-cache may fetch stale instructions for any portion that was
     * touched by the loader/signature verification but not yet written
     * back. Previous code only flushed L1_CACHE_SZ (32 KB), far too small
     * for a 6+ MB VxWorks kernel — likely cause of silent jump failure. */
    flush_cache((uint32_t)app_offset, WOLFBOOT_PARTITION_SIZE);

    /* Set MSR to match U-Boot's pre-VxWorks state: 0x2200
     * FP(bit13) + DE(bit9) enabled. All others cleared.
     * U-Boot passes MSR=0x2200 when jumping to VxWorks. */
    msr = 0x00002200;
    mtmsr(msr);
#endif

#if defined(DEBUG_UART) && defined(WOLFBOOT_ARCH_PPC)
    wolfBoot_printf("do_boot: pre-transition\n");
#endif

#if defined(MMU) && !defined(BUILD_LOADER_STAGE1)
    /* Flush FDT from D-cache to DDR so the OS sees all fixup changes.
     * Must be done after hal_dts_fixup and before entry(). Stage1
     * doesn't run hal_dts_fixup, so the flush isn't needed (and
     * flush_cache itself is excluded from stage1). */
    flush_cache((uint32_t)dts_offset, WOLFBOOT_DTS_MAX_SIZE);
#endif

#ifdef ENABLE_OS64BIT
    /* Transition LAWs and TLBs to 64-bit physical addressing. */
    hal_os64bit_map_transition();
#endif

#if defined(DEBUG_UART) && defined(WOLFBOOT_ARCH_PPC) && \
    defined(WOLFBOOT_PPC_PRE_OS_DUMP)
    /* Comprehensive pre-OS state dump (SPRs, CCSR, LAWs, TLB1, FDT, IFC,
     * DDR, DUART, spin-table, ePAPR args, kernel-entry bytes). Useful
     * for diff'ing wolfBoot's pre-jump state against a parallel dump
     * from a known-working U-Boot bootm path when porting a new OS.
     * Opt-in via -DWOLFBOOT_PPC_PRE_OS_DUMP -- the DUART DLAB toggle
     * inside the dump can interfere with the active console and was
     * observed to keep VxWorks 7 silent on CW VPX3-152 when enabled. */
    {
        uint32_t _spr_pir   = mfspr(SPRN_PIR);
        uint32_t _spr_pvr   = mfspr(SPRN_PVR);
        uint32_t _spr_svr   = mfspr(SPRN_SVR);
        uint32_t _spr_hid0  = mfspr(SPRN_HID0);
        uint32_t _spr_bucsr = mfspr(SPRN_BUCSR);
        uint32_t _spr_hdbcr0 = mfspr(SPRN_HDBCR0);
        uint32_t _spr_hdbcr1 = mfspr(SPRN_HDBCR1);
        uint32_t _spr_hdbcr2 = mfspr(SPRN_HDBCR2);
        uint32_t _spr_hdbcr7 = mfspr(SPRN_HDBCR7);
        uint32_t _spr_l1csr0 = mfspr(L1CSR0);
        uint32_t _spr_l1csr1 = mfspr(L1CSR1);
        /* L2CSR0/1 (SPR 1017/1018) faults in U-Boot's MSR context with
         * a hang/checkstop -- skip in both bootloaders for parity. */
        uint32_t _spr_tcr = mfspr(SPRN_TCR);
        uint32_t _spr_tsr = mfspr(SPRN_TSR);
        uint32_t _spr_esr = mfspr(SPRN_ESR);
        uint32_t _spr_mcsr = mfspr(SPRN_MCSR);
        uint32_t _spr_dbcr0 = mfspr(SPRN_DBCR0);
        uint32_t _spr_dbsr = mfspr(SPRN_DBSR);
        uint32_t _spr_tlb1cfg = mfspr(SPRN_TLB1CFG);
        uint32_t _spr_mmucfg = mfspr(SPRN_MMUCFG);
        /* Extended SPR set: MAS4, debug compares, decrementer, TB, PIDs,
         * SPRGs, LR/CTR/XER/CR. NOTE: HID1 (1009), EPCR (307), MMUCSR0
         * (1012), PID1 (633), PID2 (634) and DECAR (54) all fault here
         * even after hal_os64bit_map_transition's MSR clear (likely
         * MSR[GS]/[CM] residuals or hypervisor-only access on e6500),
         * so they're omitted to keep the dump path alive. */
        uint32_t _spr_mas4   = mfspr(0x274);
        uint32_t _spr_dbcr1  = mfspr(SPRN_DBCR1);
        uint32_t _spr_dbcr2  = mfspr(0x136);
        uint32_t _spr_iac1   = mfspr(0x138);
        uint32_t _spr_iac2   = mfspr(0x139);
        uint32_t _spr_dac1   = mfspr(0x13C);
        uint32_t _spr_dac2   = mfspr(0x13D);
        uint32_t _spr_dec    = mfspr(SPRN_DEC);
        uint32_t _spr_tbl    = mfspr(0x10C);
        uint32_t _spr_tbu    = mfspr(0x10D);
        uint32_t _spr_pid0   = mfspr(SPRN_PID);
        uint32_t _spr_sprg0  = mfspr(0x110);
        uint32_t _spr_sprg1  = mfspr(0x111);
        uint32_t _spr_sprg2  = mfspr(0x112);
        uint32_t _spr_sprg3  = mfspr(0x113);
        uint32_t _spr_sprg4  = mfspr(0x114);
        uint32_t _spr_sprg5  = mfspr(0x115);
        uint32_t _spr_sprg6  = mfspr(0x116);
        uint32_t _spr_sprg7  = mfspr(0x117);
        uint32_t _spr_lr     = mfspr(0x008);
        uint32_t _spr_ctr    = mfspr(0x009);
        uint32_t _spr_xer    = mfspr(0x001);
        uint32_t _spr_cr;
        uint32_t _spr_r1;
        __asm__ __volatile__("mfcr %0" : "=r"(_spr_cr));
        __asm__ __volatile__("mr %0, 1" : "=r"(_spr_r1));
        wolfBoot_printf("SPR PIR=%x PVR=%x SVR=%x HID0=%x BUCSR=%x\n",
            _spr_pir, _spr_pvr, _spr_svr, _spr_hid0, _spr_bucsr);
        wolfBoot_printf("SPR HDBCR0=%x HDBCR1=%x HDBCR2=%x HDBCR7=%x\n",
            _spr_hdbcr0, _spr_hdbcr1, _spr_hdbcr2, _spr_hdbcr7);
        wolfBoot_printf("SPR L1CSR0=%x L1CSR1=%x\n",
            _spr_l1csr0, _spr_l1csr1);
        wolfBoot_printf("SPR TCR=%x TSR=%x DEC=%x TBL=%x TBU=%x\n",
            _spr_tcr, _spr_tsr, _spr_dec, _spr_tbl, _spr_tbu);
        wolfBoot_printf("SPR ESR=%x MCSR=%x DBCR0=%x DBCR1=%x DBCR2=%x DBSR=%x\n",
            _spr_esr, _spr_mcsr, _spr_dbcr0, _spr_dbcr1, _spr_dbcr2, _spr_dbsr);
        wolfBoot_printf("SPR IAC1=%x IAC2=%x DAC1=%x DAC2=%x\n",
            _spr_iac1, _spr_iac2, _spr_dac1, _spr_dac2);
        wolfBoot_printf("SPR TLB1CFG=%x MMUCFG=%x MAS4=%x PID0=%x\n",
            _spr_tlb1cfg, _spr_mmucfg, _spr_mas4, _spr_pid0);
        wolfBoot_printf("SPR SPRG0=%x SPRG1=%x SPRG2=%x SPRG3=%x\n",
            _spr_sprg0, _spr_sprg1, _spr_sprg2, _spr_sprg3);
        wolfBoot_printf("SPR SPRG4=%x SPRG5=%x SPRG6=%x SPRG7=%x\n",
            _spr_sprg4, _spr_sprg5, _spr_sprg6, _spr_sprg7);
        wolfBoot_printf("SPR LR=%x CTR=%x XER=%x CR=%x R1=%x\n",
            _spr_lr, _spr_ctr, _spr_xer, _spr_cr, _spr_r1);
        /* CCSR snapshot — use raw offsets so we don't depend on
         * nxp_t2080.h here (boot_ppc.c is generic across PPC targets). */
        wolfBoot_printf("CCSR LCC_BSTRH=%x BSTRL=%x BSTAR=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0x20)),
            get32((volatile uint32_t*)(CCSRBAR + 0x24)),
            get32((volatile uint32_t*)(CCSRBAR + 0x28)));
        wolfBoot_printf("CCSR DCFG_BRR=%x RCPM_PCTBENR=%x PIC_WHOAMI=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0xE00E4)),  /* DCFG_BRR */
            get32((volatile uint32_t*)(CCSRBAR + 0xE21A0)),  /* RCPM_PCTBENR */
            get32((volatile uint32_t*)(CCSRBAR + 0x40090))); /* PIC_WHOAMI */
        wolfBoot_printf("CCSR DCFG_DEVDISR=%x DDR_CFG=%x DDR_CS0=%x DDR_CS1=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0xE0070)),  /* DCFG_DEVDISR1 */
            get32((volatile uint32_t*)(CCSRBAR + 0x8110)),
            get32((volatile uint32_t*)(CCSRBAR + 0x8000)),
            get32((volatile uint32_t*)(CCSRBAR + 0x8008)));
    }

    /* Dump final state AFTER transition + MSR clear */
    {
        int _j;
        uint32_t _msr2;
        uint32_t lawar;
        uint32_t mas1v, mas2v, mas3v, mas7v;
    #ifdef MMU
        const uint8_t *_fdt_p;
        uint32_t _fdt_sz;
        uint32_t _k;
    #endif
        __asm__ __volatile__("mfmsr %0" : "=r" (_msr2));
        wolfBoot_printf("FINAL MSR=0x%x\n", _msr2);
        wolfBoot_printf("FINAL LAW:\n");
        for (_j = 0; _j < 20; _j++) {
            lawar = get32(LAWAR(_j));
            if (lawar & LAWAR_ENABLE) {
                wolfBoot_printf(" %d: H=%x L=%x W=%x\n", _j,
                    get32(LAWBARH(_j)), get32(LAWBARL(_j)), lawar);
            }
        }
        /* Dump TLB1 (compare against U-Boot's pre-VxWorks TLB state) */
        wolfBoot_printf("FINAL TLB1:\n");
        for (_j = 0; _j < 64; _j++) {
            mtspr(MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(_j));
            __asm__ __volatile__("tlbre; isync" ::: "memory");
            mas1v = mfspr(MAS1);
            if ((mas1v & MAS1_VALID) == 0)
                continue;
            mas2v = mfspr(MAS2);
            mas3v = mfspr(MAS3);
            mas7v = mfspr(MAS7);
            wolfBoot_printf(" %d: M1=%x M2=%x M3=%x M7=%x\n",
                _j, mas1v, mas2v, mas3v, mas7v);
        }
    #ifdef MMU
        /* FDT body dump removed -- byte-level diff vs U-Boot already
         * verified the patched FDT matches U-Boot's structurally. Emit
         * just a header line + size + END marker so existing test scripts
         * can still grep for "FDT END" as their progress checkpoint. */
        _fdt_p = (const uint8_t *)dts_offset;
        _fdt_sz = (uint32_t)fdt_totalsize(dts_offset);
        wolfBoot_printf("FDT magic=%02x%02x%02x%02x size=%u\n",
            _fdt_p[0], _fdt_p[1], _fdt_p[2], _fdt_p[3], _fdt_sz);

        /* Hex-dump the entire post-fixup FDT so it can be reconstructed
         * from the UART log via:
         *   awk '/^F[0-9a-f]+:/{...}' wolfboot.log > wolfboot_fdt.bin
         * and diffed against the on-flash cw_152_64.dtb. 16 bytes per
         * line; address tagged with "F" prefix to be greppable. */
        for (_k = 0; _k < _fdt_sz; _k += 16) {
            uint32_t _b;
            uint32_t _bend = _k + 16;
            if (_bend > _fdt_sz)
                _bend = _fdt_sz;
            wolfBoot_printf("F%05x:", _k);
            for (_b = _k; _b < _bend; _b++) {
                wolfBoot_printf(" %02x", _fdt_p[_b]);
            }
            wolfBoot_printf("\n");
        }
        wolfBoot_printf("=== FDT END ===\n");
    #endif

        /* Comprehensive IFC chip-select dump. CCSRBAR + 0x124000.
         * Per T2080RM and U-Boot's struct fsl_ifc, all per-CS register
         * blocks (CSPR/AMASK/CSOR) use stride 0xC, NOT 0x20:
         *   CSPR_EXT(n) @ 0xC + n*0xC, CSPR(n) @ 0x10 + n*0xC
         *   AMASK(n)    @ 0xA0 + n*0xC
         *   CSOR(n)     @ 0x130 + n*0xC, CSOR_EXT(n) @ 0x134 + n*0xC */
        wolfBoot_printf("IFC:\n");
        for (_j = 0; _j < 4; _j++) {
            uint32_t cspr = get32((volatile uint32_t*)
                (CCSRBAR + 0x124010 + _j*0xC));
            if (cspr & 1) {
                wolfBoot_printf(" CS%d: E=%x C=%x A=%x SOR=%x SX=%x\n", _j,
                    get32((volatile uint32_t*)(CCSRBAR + 0x12400C + _j*0xC)),
                    cspr,
                    get32((volatile uint32_t*)(CCSRBAR + 0x1240A0 + _j*0xC)),
                    get32((volatile uint32_t*)(CCSRBAR + 0x124130 + _j*0xC)),
                    get32((volatile uint32_t*)(CCSRBAR + 0x124134 + _j*0xC)));
            }
        }

        /* DDR controller (CCSRBAR + 0x8000). T2080RM lists in offsets
         * relative to DDR base. Dump the registers most likely to differ
         * between U-Boot and wolfBoot for a 4GB ostype2 build. */
        wolfBoot_printf("DDR: CS0B=%x CS0C=%x CS1B=%x CS1C=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0x8000)), /* CS0_BNDS */
            get32((volatile uint32_t*)(CCSRBAR + 0x8080)), /* CS0_CONFIG */
            get32((volatile uint32_t*)(CCSRBAR + 0x8008)), /* CS1_BNDS */
            get32((volatile uint32_t*)(CCSRBAR + 0x8084)));/* CS1_CONFIG */
        /* T2080 DDR timing register offsets:
         *   TIMING_CFG_3 @ 0x100, _0 @ 0x104, _1 @ 0x108, _2 @ 0x10C */
        wolfBoot_printf("DDR: TC3=%x TC0=%x TC1=%x TC2=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0x8100)),
            get32((volatile uint32_t*)(CCSRBAR + 0x8104)),
            get32((volatile uint32_t*)(CCSRBAR + 0x8108)),
            get32((volatile uint32_t*)(CCSRBAR + 0x810C)));
        wolfBoot_printf("DDR: CFG=%x CFG2=%x MODE=%x INT=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0x8110)), /* SDRAM_CFG */
            get32((volatile uint32_t*)(CCSRBAR + 0x8114)), /* SDRAM_CFG_2 */
            get32((volatile uint32_t*)(CCSRBAR + 0x8118)), /* SDRAM_MODE */
            get32((volatile uint32_t*)(CCSRBAR + 0x8124)));/* SDRAM_INTERVAL */
        wolfBoot_printf("DDR: ERR_DET=%x ERR_DIS=%x DATA_INIT=%x\n",
            get32((volatile uint32_t*)(CCSRBAR + 0x8E40)), /* ERR_DETECT */
            get32((volatile uint32_t*)(CCSRBAR + 0x8E44)), /* ERR_DISABLE */
            get32((volatile uint32_t*)(CCSRBAR + 0x8128)));/* SDRAM_DATA_INIT */

        /* DUART block (NS16550-style). T2080: DUART1 @ +0x11C500,
         * DUART2 @ +0x11D500 (per nxp_t2080.h UART_BASE stride).
         * CRITICAL: don't toggle DLAB while printf is mid-flight on the
         * SAME UART -- doing so makes printf write to DLL instead of THR
         * and corrupts the baud rate / output. So:
         *   1. Read non-destructive regs into locals
         *   2. Print them (DLAB=0 so printf works normally)
         *   3. Toggle DLAB, snapshot DLL/DLM into locals
         *   4. Restore LCR (printf works again)
         *   5. Print DLL/DLM from the saved values */
        {
            int _u;
            volatile uint8_t *base;
            uint8_t lcr, mcr, lsr, msr_r, scr, dll, dlm;
            for (_u = 0; _u < 2; _u++) {
                base = (volatile uint8_t *)(CCSRBAR + 0x11C500 + _u * 0x1000);
                lcr = base[3];
                mcr = base[4];
                lsr = base[5];
                msr_r = base[6];
                scr = base[7];
                wolfBoot_printf("DUART%d: LCR=%x MCR=%x LSR=%x MSR=%x SCR=%x",
                    _u + 1, lcr, mcr, lsr, msr_r, scr);
                base[3] = lcr | 0x80; /* DLAB=1, capture divisor */
                dll = base[0];
                dlm = base[1];
                base[3] = lcr;        /* restore -- printf safe now */
                wolfBoot_printf(" DLL=%x DLM=%x\n", dll, dlm);
            }
        }

        /* Spin-table dump. g_spin_table_ddr is set by hal_mp_init() to the
         * DDR-resident copy used for cpu-release-addr in the FDT. Each entry
         * is ENTRY_SIZE (64) bytes; we dump the first 32 bytes per core
         * (covers ADDR_UPPER/LOWER, R3, R6/RESV, PIR). */
        {
            extern uint32_t g_spin_table_ddr;
            volatile uint32_t *_st;
            int _c, _w;
            wolfBoot_printf("SPIN_TABLE @ %x:\n", g_spin_table_ddr);
            if (g_spin_table_ddr != 0) {
                for (_c = 0; _c < 4; _c++) {
                    _st = (volatile uint32_t *)
                        (g_spin_table_ddr + _c * 64);
                    wolfBoot_printf(" C%d:", _c);
                    for (_w = 0; _w < 8; _w++) {
                        wolfBoot_printf(" %x", _st[_w]);
                    }
                    wolfBoot_printf("\n");
                }
            }
        }
    }

    /* Args/jump-target summary line — what VxWorks will see at entry.
     * U-Boot prints "## Starting vxWorks at 0x%lx, device tree at 0x%lx"
     * — print the equivalent here for direct comparison. */
    wolfBoot_printf("JUMP: entry=%p dts=%p r6=%x r7=%x\n",
        app_offset,
    #ifdef MMU
        dts_offset,
    #else
        (void*)0,
    #endif
        EPAPR_MAGIC, WOLFBOOT_BOOTMAPSZ);
    /* Sanity dump of jump target -- first 256 bytes (4 lines x 64 bytes).
     * Tag with K (kernel) prefix so byte-level diff against U-Boot capture
     * is mechanical via the FDT-style parser. */
    {
        const uint8_t *_pc = (const uint8_t *)app_offset;
        uint32_t _kk;
        wolfBoot_printf("=== PC BYTES (256 bytes from entry) ===\n");
        for (_kk = 0; _kk < 256; _kk++) {
            if ((_kk & 0x1F) == 0)
                wolfBoot_printf("\nK%05x:", _kk);
            wolfBoot_printf(" %02x", _pc[_kk]);
        }
        wolfBoot_printf("\n=== PC END ===\n");
    }
#endif

#if defined(DEBUG_UART) && defined(WOLFBOOT_ARCH_PPC)
    wolfBoot_printf("do_boot: jumping\n");
#endif

    /* ePAPR (Embedded Power Architecture Platform Requirements)
     * https://elinux.org/images/c/cf/Power_ePAPR_APPROVED_v1.1.pdf
     *
     * For ENABLE_OS64BIT (CW VPX3-152 VxWorks 7), use the RAMFUNCTION
     * trampoline `wolfBoot_os64bit_jump` so the final cache-disable +
     * indirect bctr execute from DDR rather than flash. wolfBoot is XIP
     * from flash by default; production CW U-Boot relocates itself to
     * DDR before bootm. Mirroring that placement removes one more
     * difference at the OS-jump moment. */
#if defined(ENABLE_OS64BIT) && defined(WOLFBOOT_ARCH_PPC)
    wolfBoot_os64bit_jump((os64bit_entry_t)entry,
    #ifdef MMU
        (uintptr_t)dts_offset,
    #else
        0,
    #endif
        EPAPR_MAGIC,
        WOLFBOOT_BOOTMAPSZ);
#else
#if defined(WOLFBOOT_ARCH_PPC) && defined(BOARD_CW_VPX3152)
    /* Non-OS64BIT path: flash-cache-disable + entry from flash. */
    {
        extern void hal_flash_cache_disable_pre_os(void);
        hal_flash_cache_disable_pre_os();
    }
#endif

    entry(
    #ifdef MMU
        (uintptr_t)dts_offset,   /* r3 = dts address */
    #else
        0,
    #endif
        0, 0,
        EPAPR_MAGIC,             /* r6 = ePAPR magic */
        WOLFBOOT_BOOTMAPSZ,      /* r7 = Size of Initial Mapped Area (IMA) */
        0, 0
    );
#endif
}

void arch_reboot(void)
{

}
