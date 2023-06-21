/* boot_ppc_start.S
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

/* Reset entry point startup code for e500v2 and e6500. Setups up L1, L2, interrupts,
 * TLB and stack. Has functions for timebase, relocating code for PIC and GOT.
 */

/*
## References:

* Book E: Enhanced PowerPC Architecture (BOOK_EUM.pdf)
* EREF: A Programmer’s Reference Manual for Freescale Power Architecture Processors, Rev. 1 (EIS 2.1)

T2080:
  * CRM: e6500 Core Reference Manual, Rev 0
  * T2080RM: QorIQ T2080 Reference Manual, Rev. 3, 11/2016
  * MPC8544ERM: https://www.nxp.com/docs/en/reference-manual/MPC8544ERM.pdf
P1021:
  * e500v2: PowerPCTM e500 Core Family Reference Manual (E500CORERM.pdf)
  * P1021RM: P1021 QorIQ Integrated Processor Reference Manual (P1021RM.pdf)


## Address Space (AS)

There are two address spaces. For privledged (0) and non-privlaged (1) spaces.
Out of reset the AS=0.

Address Space (AS) == Translation Space (TS)
This also corresponds to the MSR register IS and DS values.
Address spaces require different TLB entries


## Early boot

CRM chapter 11
 * L2 - For e500v2 and e6500
     * flash invalidate
     * enable
 * L1
   * flash clear
   * enable

* Set up CCSR TLB
* Set up L1 TLB and stack


## MMU (TLB)

TLB 1 is fully associative and allows different size pages.
TLB 0 is not fully and only allow 4KB page size

All TLBs for boot will be in TLB1 and supervisor mode (not user)
*/

#include "hal/nxp_ppc.h"

/* variables from linker script */
.global _start_vector
.global isr_empty
.global _end_stack

/* NXP P1021 First stage loader requires GOT for PIC and relocation */
#if defined(PLATFORM_nxp_p1021) && defined(BUILD_LOADER_STAGE1)
#define USE_GOT
#endif

#ifdef USE_GOT

/* --- Global Offset Table ---
 * These definitions simplify the declarations necessary for GOT.
 * Based on work from Gabriel Paubert from prepboot/bootldr.h
 * Uses r12 to access the GOT */
#define GOT_ENTRY(NAME)  \
        .L_ ## NAME = . - .LCTOC1 ; .long NAME
#define GOT(NAME)        \
        .L_ ## NAME (r12)
#define GET_GOT                 \
        bl      1f;             \
        .text   2;              \
0:      .long   .LCTOC1-1f;     \
        .text;                  \
1:      mflr    r12;            \
        lwz     r0, 0b-1b(r12); \
        add     r12, r0, r12;

/* Set up GOT */
.section .got2, "aw"
.LCTOC1 = .+0x8000
GOT_ENTRY(_GOT2_TABLE_)
GOT_ENTRY(_FIXUP_TABLE_)
GOT_ENTRY(__init_end)
.text
#endif

#ifndef INTVEC_ADDR
/* workaround to use isr_empty for all interrupts, for real IRQ's adjust the
 * offset and define additional interrupts at those offsets */
#define INTVEC_ADDR(n) (BOOT_ROM_SIZE + (n * 0x0))
#endif


/* Reset Entry Point */
.section .boot, "ax"
.global _reset
_reset:
        /* CRM 9.9.2 and EREF 10.4 enable debug interrupt */
        /* Set MSR DE (Debug Interrupt Enable = 1) */
        li      r1, MSR_DE
        mtmsr   r1

#ifdef PLATFORM_nxp_p1021
        /* Errata: A-005125 - force the core to process all snoops of IO device
         *                    full cache line writes to DDR differently */
        msync
        isync
        mfspr   r3, SPRN_HDBCR0
        oris    r3, r3, 0x0080 /* SPR976[40:41] to b’10 */
        mtspr   SPRN_HDBCR0, r3
#endif

reset_exceptions:
        /* Reset exception registers */
        li      r0, 0x0000
        lis     r1, 0xffff
        mtspr   SPRN_DEC, r0 /* prevent dec exceptions */
        mttbl   r0 /* prevent FIT and WDT exceptions */
        mttbu   r0
        mtspr   SPRN_TSR, r1 /* clear all timer exception status */
        mtspr   SPRN_TCR, r0 /* disable all timers */
        mtspr   SPRN_ESR, r0 /* clear exception syndrome register */
        mtspr   SPRN_MCSR, r0 /* clear machine check syndrome register */
        mtxer   r0 /* clear integer exception register */

hardware_reg:
        /* Time base, MAS7 and machine check pin enable */
        lis     r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@h
        ori     r0, r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@l
        mtspr   SPRN_HID0, r0

#ifndef BUILD_LOADER_STAGE1
        /* Set addr streaming & broadcast
         * and optimized sync instruction (if rev 5.0 or greater) */
        li      r0, (HID1_ASTME | HID1_ABE)@l
        mfspr   r3, SPRN_PVR
        andi.   r3, r3, 0xFF
        cmpwi   r3, 0x50@l /* if rev 5.0 or greater set MBDD */
        blt     1f
        ori     r0, r0, HID1_MBDD@l
1:      mtspr   SPRN_HID1, r0
#endif

#ifndef BUILD_LOADER_STAGE1
branch_prediction:
        /* enable branch prediction */
        lis     r0, (BUCSR_ENABLE)@h
        ori     r0, r0, (BUCSR_ENABLE)@l
        mtspr   SPRN_BUCSR, r0
#endif

startup_init:
        /* Invalidate L1 instruction and data cache */
        bl invalidate_icache;
        bl invalidate_dcache;

        /* Clear debug status register - read and write */
        mfspr   r1, SPRN_DBSR
        mtspr   SPRN_DBSR, r1

#ifndef BUILD_LOADER_STAGE1
#ifndef TLB1_NEW_SIZE
#define TLB1_NEW_SIZE BOOKE_PAGESZ_256K
#endif
shrink_default_tlb1:
        /* Shrink the current TLB1 entry */
        bl find_pc
find_pc:
        mflr    r1
        /* Set MAS6 SPID0=0 and SAS=0 */
        li      r2, 0
        mtspr   MAS6, r2
        isync
        msync

        /* Search for current TLB address in R1 */
        tlbsx   0, r1 /* must succeed */

        mfspr   r14, MAS0 /* save ESEL in R14 */
        rlwinm  r14, r14, 16, 0xFFF

        /* Resize TLB */
        mfspr   r3, MAS1
        li      r2, MAS1_TSIZE_MASK
        andc    r3, r3, r2 /* Clear TSIZE */
        ori     r3, r3, MAS1_TSIZE(TLB1_NEW_SIZE)@l
        oris    r3, r3, MAS1_IPROT@h
        mtspr   MAS1, r3

        /* Find page for PC (R1) */
        lis     r3, MAS2_EPN@h
        ori     r3, r3, MAS2_EPN@l
        and     r1, r1, r3

        /* Set the real and virtual page for this TLB */
        mfspr   r2, MAS2
        andc    r2, r2, r3
        or      r2, r2, r1
        mtspr   MAS2, r2 /* EPN */
        mfspr   r2, MAS3
        andc    r2, r2, r3
        or      r2, r2, r1
        mtspr   MAS3, r2 /* RPN */
        isync
        msync
        tlbwe

        /* Clear all other TLB's (except ours in R14) */
        li      r0, (TLBIVAX_ALL | TLBIVAX_TLB0)
        tlbivax 0, r0
        tlbsync

        mfspr   r4, SPRN_TLB1CFG
        rlwinm  r4, r4, 0, TLBNCFG_NENTRY_MASK

        li      r3, 0
        mtspr   MAS1, r3
1:      cmpw    r3, r14
        rlwinm  r5, r5, 16, MAS0_ESEL_MSK
        addi    r3, r3, 1
        beq     2f /* skip the TLB in R14 */

        oris    r5, r5, MAS0_TLBSEL(1)@h
        mtspr   MAS0, r5
        isync
        tlbwe
        isync
        msync

2:      cmpw    r3, r4
        blt     1b
#endif /* !BUILD_LOADER_STAGE1 */

setup_interrupts:

        /* Setup interrupt vectors */
        lis     r1, (_start_vector)@h
        mtspr   IVPR, r1

#ifdef ENABLE_INTERRUPTS
        li      r1, INTVEC_ADDR(0)
        mtspr   IVOR(0), r1        /* 0: Critical input */
        li      r1, INTVEC_ADDR(1)
        mtspr   IVOR(1), r1        /* 1: Machine check */
        li      r1, INTVEC_ADDR(2)
        mtspr   IVOR(2), r1        /* 2: Data storage */
        li      r1, INTVEC_ADDR(3)
        mtspr   IVOR(3), r1        /* 3: Instruction storage */
        li      r1, INTVEC_ADDR(4)
        mtspr   IVOR(4), r1        /* 4: External interrupt */
        li      r1, INTVEC_ADDR(5)
        mtspr   IVOR(5), r1        /* 5: Alignment */
        li      r1, INTVEC_ADDR(6)
        mtspr   IVOR(6), r1        /* 6: Program check */
        li      r1, INTVEC_ADDR(7)
        mtspr   IVOR(7), r1        /* 7: floating point unavailable */
        li      r1, INTVEC_ADDR(8)
        mtspr   IVOR(8), r1        /* 8: System call */
        /* 9: Auxiliary processor unavailable(unsupported) */
        li      r1, INTVEC_ADDR(10)
        mtspr   IVOR(10), r1        /* 10: Decrementer */
        li      r1, INTVEC_ADDR(11)
        mtspr   IVOR(11), r1        /* 11: Interval timer */
        li      r1, INTVEC_ADDR(12)
        mtspr   IVOR(12), r1        /* 12: Watchdog timer */
        li      r1, INTVEC_ADDR(13)
        mtspr   IVOR(13), r1        /* 13: Data TLB error */
        li      r1, INTVEC_ADDR(14)
        mtspr   IVOR(14), r1        /* 14: Instruction TLB error */
        li      r1, INTVEC_ADDR(15)
        mtspr   IVOR(15), r1        /* 15: Debug */
#endif

/* If needed, relocate CCSRBAR */
#if CCSRBAR_DEF != CCSRBAR
        /* Use R8 = new, R9 = old virtual */
        lis    r8, CCSRBAR@h
        ori    r8, r8, CCSRBAR@l
        lis    r9, (CCSRBAR + 0x1000)@h
        ori    r9, r9, (CCSRBAR + 0x1000)@l

create_temp_ccsr:
        /* Create a temporary TLB entry for new and old location  */
        /* CCSRBAR: TLB 0, Entry 0, Supervisor R/W, IG, TS=0, 4KB */
        set_tlb(0, 0,
                CCSRBAR, CCSRBAR, 0,
                MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0,
                BOOKE_PAGESZ_4K, 0, r3);

        set_tlb(0, 0,
                CCSRBAR + 0x1000, CCSRBAR_DEF, 0,
                MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0,
                BOOKE_PAGESZ_4K, 0, r3);

verify_old_ccsr:
        /* verify the TLB is for old one */
        lis     r0, CCSRBAR_DEF@h
        ori     r0, r0, CCSRBAR_DEF@l
        lwz     r1, 0(r9)
        slwi    r1, r1, 12
        cmpl    0, r0, r1
infinite_debug_loop:
        bne     infinite_debug_loop /* should not get here */

write_new_ccsrbar:
        /* Read current value of CCSBAR - forces all accesses to complete */
        sync
        lwz     r0, 0(r9)
        isync
        /* write new CCSBAR */
        lis     r0, (CCSRBAR >> 12)@h
        ori     r0, r0, (CCSRBAR >> 12)@l
        stw     r0, 0(r9)
        sync
        isync

        /* Read current value of CCSRBAR from new location */
        lwz     r0, 0(r8)
        isync

        /* invalidate TLB 0 */
        li      r3, 0x04 /* L2TLB0_FI: TLB0 flash invalidate (write 1 to invalidate) */
        mtspr   MMUCSR0, r3
#endif

#ifndef BUILD_LOADER_STAGE1
/* TLBs */
boot_page:
        /* make sure we have the default boot page added to MMU */
        /* BOOT_PAGE: TLB 1, Entry 0, Supervisor X/R/W, I, TS=0, 4KB, IPROT */
        set_tlb(1, 0,
                BOOT_ROM_ADDR, BOOT_ROM_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I, 0,
                BOOKE_PAGESZ_4K, 1, r3);
#endif

ccsr_tlb:
        /* CCSRBAR: TLB 1, Entry 1, Supervisor R/W, IG, TS=0, 1M/16M, IPROT */
        set_tlb(1, 1,
                CCSRBAR, CCSRBAR, 0,
                MAS3_SX | MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0,
                CCSRBAR_SIZE, 1, r3);

#ifdef FLASH_BASE_ADDR
flash_tlb:
        /* For TS/AS=1 map boot ROM */
        /* Flash: TLB 1, Entry 7, Super X/R/W, IG, TS=0, 1M, IPROT */
        set_tlb(1, 7,
                FLASH_BASE_ADDR, FLASH_BASE_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
                BOOKE_PAGESZ_1M, 1, r3);
#endif

#ifdef ENABLE_DDR
        #ifdef BUILD_LOADER_STAGE1
            /* use cache inhibited for first stage loader to avoid
            * L1 cache as SRAM issues */
            #define DDR_WING (MAS2_I | MAS2_G)
        #else
            #define DDR_WING (MAS2_G)
        #endif

        /* DDR - TBL=1, Entry 11 and 12, Super X/R/W, IG, TS=0, 512MB */
        set_tlb(1, 11,
                DDR_ADDRESS, DDR_ADDRESS, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, DDR_WING, 0,
                BOOKE_PAGESZ_256M, 0, r3);
        set_tlb(1, 12,
                DDR_ADDRESS + (256*1024*1024), DDR_ADDRESS + (256*1024*1024), 0,
                MAS3_SX | MAS3_SW | MAS3_SR, DDR_WING, 0,
                BOOKE_PAGESZ_256M, 0, r3);
#endif

#ifdef ENABLE_L2_CACHE

#ifdef MMU_V2

/* e6500 - must have L2 initialized before L1 */
/* E6500RM 5.6.2 Enabling and disabling the L1 caches:
 * "Note that enabling either L1 cache without first enabling the L2 cache
 * is not supported."
 */
create_ccsr_l2_tlb:
        /* L2 0xFEC20000: TLB 1, Entry 9, Supervisor X/R/W, G, TS=0, 256KB, IPROT */
        set_tlb(1, 9,
                L2SRAM_ADDR, L2SRAM_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_G, 0,
                BOOKE_PAGESZ_256K, 1, r3);

/* CRM 11.7 */
setup_l2:

        /* L2 data cache invalidation & unlocking
         * create flash invalidate & unlock bit mask (see Table 2-19)
         */
        lis     r4, 0x0020
        ori     r4, r4, 0x0400
        /* get base address of memory mapped registers */
        mfspr   r5, SCCSRBAR
        li      r7, 24 /* get shift count */
        sld     r5, r5, r7
        lis     r6, 0x00C2 /* block offset for desired cluster (see Table 2-4) */
        /* subsequent cluster L2 caches may be invalidated & unlocked by adding 0x40000 to 6 */
        add     r6, r6, r5
        /* L2SC0 offset (see Table 2-5), included here only for example */
        /*addi r6, r6, r0 */
        /* ensure prior memory transactions are performed */
        sync

        // TODO is this required?
        li      r5, 33
        stw     r5, 4(r6)

        sync
        stw     r4, 0(r6) /* write L2SC0 MMR to flash invalidate L2 cache and locks */
l2loop:
        sync
        lwz     r5, 0(r6) /* get current L2SC0 MMR value */
        and.    r5, r5, r4 /* compare to mask to see if complete */
        bne     l2loop
        isync

enable_l2_pe:
        lis     r5, 0x4000
        sync
        stw     r5, 0(r6)
l2_pe_loop:
        sync
        lwz     r4, 0(r6)
        cmplw   r4, r5
        bne     l2_pe_loop
        isync

enable_l2e:
        lis     r5, 0xC000
        sync
        stw     r5, 0(r6)
l2e_loop:
        sync
        lwz     r4, 0(r6)
        cmplw   r4, r5
        bne     l2e_loop
        isync
#endif /* MMU_V2 */
#endif /* ENABLE_L2_CACHE */

#ifdef MMU_V2
set_stack_as:

        lis     r6, (MSR_IS | MSR_DS | MSR_DE)@h
        ori     r6, r6, (MSR_IS | MSR_DS | MSR_DE)@l
        lis     r7, switch_as@h
        ori     r7, r7, switch_as@l

        mtspr   SRR0, r7
        mtspr   SRR1, r6

        /* return from interrupt call and switch to AS=1 (aka TS=1) */
        rfi

switch_as:
#endif /* MMU_V2 */

setup_l1:
#ifdef ENABLE_L1_CACHE
#ifndef BUILD_LOADER_STAGE1
        /* L1 Instruction Cache */
        bl icache_enable;
#endif
        /* L1 Data Cache */
        bl dcache_enable;

#ifdef L1_CACHE_ADDR
l1_tlb:

        /* L1: TLB 0, Supervisor X/R/W, TS=0, 16K */
        /* TLB0 must all be 4KB and index is automatically assigned */
        set_tlb(0, 0, L1_CACHE_ADDR, L1_CACHE_ADDR, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x1000, L1_CACHE_ADDR+0x1000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x2000, L1_CACHE_ADDR+0x2000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x3000, L1_CACHE_ADDR+0x3000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);

l1_cache:
        /* setup L1 cache */
        lis     r3, L1_CACHE_ADDR@h
        ori     r3, r3, L1_CACHE_ADDR@l
        /* read the cache size */
        mfspr   r2, L1CFG0
        andi.   r2, r2, 0x1FF
        /* calculate (cache size * 1024 / (2 * L1 line size)) */
        slwi    r2, r2, (10 - 1 - L1_CACHE_LINE_SHIFT)
        mtctr   r2
        li      r0, 0

l1_cache_init:
        dcbz    r0, r3    /* Data cache block zero */
        dcbtls  0, r0, r3 /* Data cache block touch and lock set */
        addi    r3, r3, L1_CACHE_LINE_SIZE
        bdnz    l1_cache_init
#endif /* L1_CACHE_ADDR */
#endif /* ENABLE_L1_CACHE */

setup_ts0:
        /* Build top of stack address */
        /* Reserve 64 bytes of initial data (must be 16 byte aligned) */
        lis     r1, (_end_stack-64)@h
        ori     r1, r1, (_end_stack-64)@l

        /* PowerPC e500 Application Binary Interface User's Guide
         * 2.3.5.1.1 Minimal Stack Frame: No Local Variables or Saved Parameters
         */
        li      r0,  0
        stwu    r0, -4(r1)
        stwu    r0, -4(r1)   /* Terminate Back chain */
        stwu    r1, -8(r1)   /* Save back chain and move SP */
        lis     r0, RESET_VECTOR@h /* Address of reset vector */
        ori     r0, r0, RESET_VECTOR@l
        stwu    r1, -8(r1)   /* Save back chain and move SP */
        stw     r0, +12(r1)  /* Save return addr (underflow vect) */

        /* switch back to AS/TS=0 */
        lis     r3, (MSR_CE | MSR_ME | MSR_DE)@h
        ori     r3, r3, (MSR_CE | MSR_ME | MSR_DE)@l
        mtmsr   r3
        isync

#ifdef USE_GOT
        GET_GOT
#endif

        /* jump to wolfboot */
        b       boot_entry_C /* no return */

#ifdef ENABLE_INTERRUPTS
/* Interrupt functions */
.section .isr_vector
.align 8
isr_empty:
        nop
        rfi
#endif


/*
 * unsigned long long get_ticks(void);
 *
 * read timebase as "long long"
 */
.global  get_ticks
get_ticks:
1:      mftbu   r3
        mftb    r4
        mftbu   r5
        cmp     0, r3, r5
        bne     1b
        blr

/*
 * Delay for a number of ticks
 */
.global  wait_ticks
wait_ticks:
        mflr    r8               /* save link register */
        mr      r7, r3           /* save tick count */
        bl      get_ticks        /* Get start time */

        /* Calculate end time */
        addc    r7, r4, r7       /* Compute end time lower */
        addze   r6, r3           /*     and end time upper */

1:      bl      get_ticks        /* Get current time */
        subfc   r4, r4, r7       /* Subtract current time from end time */
        subfe.  r3, r3, r6
        bge     1b               /* Loop until time expired */

        mtlr    r8               /* restore link register */
        blr

/* return the address we are running at */
.global get_pc
get_pc:
        mflr    r0
        bl      1f
1:      mflr    r3
        mtlr    r0
        blr

.global invalidate_icache
invalidate_icache:
        mfspr   r4, L1CSR1
        ori     r4, r4, L1CSR_CFI
        msync
        isync
        mtspr   L1CSR1, r4
        isync
        blr

.global invalidate_dcache
invalidate_dcache:
        mfspr   r4, L1CSR0
        ori     r4, r4, L1CSR_CFI
        msync
        isync
        mtspr   L1CSR0, r4
        isync
        blr

#ifndef BUILD_LOADER_STAGE1
.global icache_enable
icache_enable:
        mflr    r8
        bl      invalidate_icache
        mtlr    r8
        isync
        mfspr   r4, L1CSR1
        ori     r4, r4, L1CSR_CE
        oris    r4, r4, L1CSR_CE
        mtspr   L1CSR1, r4
        isync
        blr
#endif

.global dcache_enable
dcache_enable:
        mflr    r8
        bl      invalidate_dcache
        mtlr    r8
        isync
        mfspr   r4, L1CSR0
        ori     r4, r4, L1CSR_CE
        oris    r4, r4, L1CSR_CE
        msync
        isync
        mtspr   L1CSR0, r4
        isync
        blr

#ifndef BUILD_LOADER_STAGE1
.global dcache_disable
dcache_disable:
        mfspr   r3, L1CSR0
        lis     r4, 0
        ori     r4, r4, L1CSR_CE
        andc    r3, r3, r4
        mtspr   L1CSR0, r0
        isync
        blr
#endif

#ifdef USE_GOT

/* function to relocate code, handling cache flushing and continue to
 * relocated destination.
 *
 * void relocate_code (dest, src, length)
 * r3 = dest
 * r4 = src
 * r5 = length in bytes
 * r6 = cachelinesize
 */
.global relocate_code
relocate_code:
        mr      r9,  r3 /* Save copy of Destination Address */
        mr      r10, r4 /* Save copy of Source Address */
        li      r6, L1_CACHE_LINE_SIZE /* Cache Line size */

        /* Fix GOT pointer */
        GET_GOT
        lwz     r5, GOT(__init_end)
        sub     r5, r5, r10   /* GOT end - Source */
        sub     r15, r9, r10  /* Dest - Source */
        add     r12, r12, r15 /* Add our own GOT (R12) */
        add     r30, r30, r15 /* Add C code GOT (R30) */

        /* We are done. Do not return, instead branch to second part of board
         * initialization, now running from RAM. */
        addi    r0, r9, in_ram - _reset

        /* Re-point the IVPR at RAM */
        mtspr   IVPR, r9

        mtlr    r0
        blr /* jumps below to in_ram - does not return */

.section .boot, "ax"
.global in_ram
in_ram:

        /* Relocation Function, r12 point to got2+0x8000
         *
         * Adjust got2 pointers, no need to check for 0, this code
         * already puts a few entries in the table. */
        li      r0, __got2_entries@sectoff@l
        la      r3, GOT(_GOT2_TABLE_)
        lwz     r11, GOT(_GOT2_TABLE_)
        mtctr   r0
        addi    r3, r3, -20 /* FIXUP: GOT location off by 20? */
        sub     r11, r3, r11
        addi    r3, r3, -4
1:      lwzu    r0, 4(r3)
        cmpwi   r0, 0
        beq-    2f
        add     r0, r0, r11
        stw     r0, 0(r3)
2:      bdnz    1b

        /*
         * Now adjust the fixups and the pointers to the fixups
         * in case we need to move ourselves again.
         */
        li      r0, __fixup_entries@sectoff@l
        lwz     r3, GOT(_FIXUP_TABLE_)
        cmpwi   r0, 0
        mtctr   r0
        addi    r3, r3, -4
        beq     4f
3:      lwzu    r4, 4(r3)
        lwzux   r0, r4, r11
        cmpwi   r0, 0
        add     r0, r0, r11
        stw     r4, 0(r3)
        beq-    5f
        stw     r0, 0(r4)
5:      bdnz    3b
4:
        /* disable FMR[BOOT] - Set to 0 */
        lis     r4, ((CCSRBAR + 0x5000) + 0xE0)@h
        ori     r4, r4, ((CCSRBAR + 0x5000) + 0xE0)@l
        li      r3, 0
        sync
        stw     r3, 0(r4)

        /* Build top of stack address using destination address in R9 */
        /* Reserve 64 bytes of initial data (must be 16 byte aligned) */
        mr      r1, r9
        addi    r1, r1, -64

        /* PowerPC e500 Application Binary Interface User's Guide
         * 2.3.5.1.1 Minimal Stack Frame: No Local Variables or Saved Parameters
         */
        li      r0,  0
        stwu    r0, -4(r1)
        stwu    r0, -4(r1)   /* Terminate Back chain */
        stwu    r1, -8(r1)   /* Save back chain and move SP */
        lis     r0, RESET_VECTOR@h /* Address of reset vector */
        ori     r0, r0, RESET_VECTOR@l
        stwu    r1, -8(r1)   /* Save back chain and move SP */
        stw     r0, +12(r1)  /* Save return addr (underflow vect) */

        b       boot_entry_C /* no return */

#endif /* USE_GOT */

/* reset entry point - must be at end of .S */
.section .reset, "ax"
        b _reset
