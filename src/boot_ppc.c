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
#ifdef BOARD_CW_VPX3152
/* CW VPX3-152 board-specific peripheral setup for the 64-bit OS hand-off:
 * the peripheral LAWs (FPGA, NVRAM, BMan/QMan portals, DCSR, PCIe1/4 + IO)
 * and the PCIe outbound ATMU windows that VxWorks 7 / Linux 64-bit expect,
 * mirroring CW U-Boot's law_table_lnx_vx7 + cw_late_mmap_adjust(). These are
 * CW-board addresses, kept OUT of the generic transition below so that path
 * stays board-agnostic. Flash/CCSR/DDR LAWs are already set by boot asm +
 * hal_ddr_init. LONGCALL so the RAMFUNCTION transition can reach it. */
static void LONGCALL_ATTR hal_cw_vpx3152_os64_periph(void)
{
    volatile uint32_t *pex1 = (volatile uint32_t *)0xEF240000UL; /* PCIe1 regs */
    volatile uint32_t *pex4 = (volatile uint32_t *)0xEF270000UL; /* PCIe4 regs */
    /* POT register dword offsets (PEXOTARn/TEARn/WBARn/WARn) */
    #define POT_TAR(n)   ((0xC00 + (n)*0x20) >> 2)
    #define POT_TEAR(n)  ((0xC04 + (n)*0x20) >> 2)
    #define POT_WBAR(n)  ((0xC08 + (n)*0x20) >> 2)
    #define POT_WAR(n)   ((0xC10 + (n)*0x20) >> 2)

    set_law(6,  0xF, 0xEE400000, LAW_TRGT_IFC,  LAW_SIZE_2MB,   1); /* FPGA 32b */
    set_law(7,  0xF, 0xEE600000, LAW_TRGT_IFC,  LAW_SIZE_512KB, 1); /* FPGA 8b */
    set_law(8,  0xF, 0xEE700000, LAW_TRGT_IFC,  LAW_SIZE_512KB, 1); /* NVRAM */
    set_law(9,  0xF, 0xEA000000, LAW_TRGT_BMAN, LAW_SIZE_32MB,  1); /* BMan */
    set_law(10, 0xF, 0xEC000000, LAW_TRGT_QMAN, LAW_SIZE_32MB,  1); /* QMan */
    set_law(11, 0xF, 0xEE000000, LAW_TRGT_DCSR, LAW_SIZE_4MB,   1); /* DCSR */
    set_law(13, 0xC, 0x00000000, LAW_TRGT_PCIE4, LAW_SIZE_2GB,   1); /* PCIe4 mem */
    set_law(14, 0xF, 0xEE800000, LAW_TRGT_PCIE4, LAW_SIZE_256KB, 1); /* PCIe4 IO */
    set_law(15, 0xD, 0x00000000, LAW_TRGT_PCIE1, LAW_SIZE_2GB,   1); /* PCIe1 mem */
    set_law(16, 0xF, 0xEE840000, LAW_TRGT_PCIE1, LAW_SIZE_256KB, 1); /* PCIe1 IO */

    /* PCIe1/PCIe4 outbound ATMU (mirror U-Boot AfterBootM): POT[1]=2GB mem
     * window (bus 0x80000000 -> host PCIe1=0xD_0/PCIe4=0xC_0), POT[2]=IO,
     * POT[3..4] cleared. Without these, kernel PCIe MMIO routes to garbage. */
    pex1[POT_TAR(1)]  = 0x00080000U; pex1[POT_TEAR(1)] = 0x00000000U;
    pex1[POT_WBAR(1)] = 0x00D00000U; pex1[POT_WAR(1)]  = 0x8004401EU;
    pex1[POT_TAR(2)]  = 0x000EE840U; pex1[POT_TEAR(2)] = 0x00000000U;
    pex1[POT_WBAR(2)] = 0x00FEE840U; pex1[POT_WAR(2)]  = 0x80088011U;
    pex1[POT_TAR(3)]  = 0; pex1[POT_TEAR(3)] = 0;
    pex1[POT_WBAR(3)] = 0; pex1[POT_WAR(3)]  = 0;
    pex1[POT_TAR(4)]  = 0; pex1[POT_TEAR(4)] = 0;
    pex1[POT_WBAR(4)] = 0; pex1[POT_WAR(4)]  = 0;
    pex4[POT_TAR(1)]  = 0x00080000U; pex4[POT_TEAR(1)] = 0x00000000U;
    pex4[POT_WBAR(1)] = 0x00C00000U; pex4[POT_WAR(1)]  = 0x8004401EU;
    pex4[POT_TAR(2)]  = 0x000EE800U; pex4[POT_TEAR(2)] = 0x00000000U;
    pex4[POT_WBAR(2)] = 0x00FEE800U; pex4[POT_WAR(2)]  = 0x80088011U;
    pex4[POT_TAR(3)]  = 0; pex4[POT_TEAR(3)] = 0;
    pex4[POT_WBAR(3)] = 0; pex4[POT_WAR(3)]  = 0;
    pex4[POT_TAR(4)]  = 0; pex4[POT_TEAR(4)] = 0;
    pex4[POT_WBAR(4)] = 0; pex4[POT_WAR(4)]  = 0;
    __asm__ __volatile__("sync; isync" ::: "memory");

    #undef POT_TAR
    #undef POT_TEAR
    #undef POT_WBAR
    #undef POT_WAR
}
#endif /* BOARD_CW_VPX3152 */

/* Generic 64-bit OS hand-off transition (VxWorks 7 / Linux 64-bit), modelled
 * on CW U-Boot's "ossel ostype2" bootm. Board-agnostic: board-specific
 * peripheral LAW/ATMU setup is delegated to a board hook. RAMFUNCTION since
 * it remaps the DDR TLB (must not execute from the VA it tears down). */
static void RAMFUNCTION hal_os64bit_map_transition(void)
{
#ifdef BOARD_CW_VPX3152
    hal_cw_vpx3152_os64_periph();
#endif

    /* No extra peripheral TLBs are installed: VxWorks 7's early stub builds
     * its own peripheral TLBs from the FDT, and pre-mapping them collides
     * with VxWorks's entries (silent fault before "Hello, VxWorks!"). Hand
     * off with only the wolfBoot TLBs (CCSR, flash, .ramcode, DDR) plus the
     * slot-0 DDR copy below. */

    /* Move DDR mapping from TLB1 slot 12 to slot 0: VxWorks 7's early stub
     * iterates TLB1 from slot 1 invalidating each entry, then reads slot 0
     * for the DDR map. wolfBoot pins DDR at slot 12 (boot_ppc_start.S), which
     * the loop wipes -> slot 0 invalid -> VxWorks reads garbage and faults.
     * Invalidate slot 12 BEFORE writing slot 0 (two entries for one EA =
     * multi-hit MC). Safe in this RAMFUNCTION (runs from .ramcode/slot 9; no
     * DDR access between the invalidate and the slot-0 write). */
    mtspr(MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(12));
    mtspr(MAS1, 0);
    mtspr(MAS2, 0);
    mtspr(MAS3, 0);
    mtspr(MAS7, 0);
    __asm__ __volatile__("isync; tlbwe; isync; msync" ::: "memory");
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
            /* Handler exceeds the reserved IVPR landing zone; a truncated
             * copy would crash the OS unpredictably. Fail loudly rather
             * than silently skip the jump. */
            wolfBoot_printf("os64bit_jump: ISR handler too large "
                "(%u > %u)\n", (unsigned int)bytes,
                (unsigned int)WOLFBOOT_OS64BIT_IVPR_MAX);
            wolfBoot_panic();
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
#if defined(CORE_E6500) || defined(CORE_E5500)
    volatile uint32_t *cpc_csr0 = (volatile uint32_t *)(CPC_BASE + CPCCSR0);
    uint32_t reg;
#endif
#endif
#if defined(WOLFBOOT_ARCH_PPC) && defined(BOARD_CW_VPX3152)
    extern void hal_flash_cache_disable_pre_os(void);
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
     * I/D caches are not coherent -- without dcbst over the whole image,
     * the I-cache may fetch stale instructions for any portion that was
     * touched by the loader/signature verification but not yet written
     * back. Previous code only flushed L1_CACHE_SZ (32 KB), far too small
     * for a 6+ MB VxWorks kernel -- likely cause of silent jump failure. */
    flush_cache((uint32_t)app_offset, WOLFBOOT_PARTITION_SIZE);

#if defined(CORE_E6500) || defined(CORE_E5500)
    /* CPC L3 flush + invalidate (CoreNet platform cache; e5500/e6500
     * only -- e500/P1021 has no CPC). L2 ops via L2CSR0 SPR hang
     * wolfBoot on T2080 (likely L2FL completion never signals for the
     * cluster L2 from a single core path). Stick to CPC for now. */
    reg = *cpc_csr0;
    *cpc_csr0 = reg | CPCCSR0_CPCFL;
    __asm__ __volatile__("sync; isync" ::: "memory");
    while (*cpc_csr0 & CPCCSR0_CPCFL);
    reg = *cpc_csr0;
    *cpc_csr0 = reg | CPCCSR0_CPCFI;
    __asm__ __volatile__("sync; isync" ::: "memory");
    while (*cpc_csr0 & CPCCSR0_CPCFI);

    /* Set MSR to match U-Boot's pre-VxWorks state: 0x2200
     * FP(bit13) + DE(bit9) enabled. All others cleared.
     * U-Boot passes MSR=0x2200 when jumping to VxWorks. */
    msr = 0x00002200;
    mtmsr(msr);
#else
    /* e500 (P1021) and other non-CoreNet cores: quiesce critical,
     * machine-check, and debug interrupts before OS entry (original
     * pre-T2080 behavior). */
    msr = mfmsr();
    msr &= ~(MSR_CE | MSR_ME | MSR_DE);
    mtmsr(msr);
#endif
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
    /* Non-OS64BIT path: flash-cache-disable before entry from flash. */
    hal_flash_cache_disable_pre_os();
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
