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


/*
# References
CRM - e6500 Core Reference Manual, Rev 0
EREF - EREF: A Programmer’s Reference Manual for Freescale Power Architecture Processors, Rev. 1 (EIS 2.1)
T2080RM - QorIQ T2080 Reference Manual, Rev. 3, 11/2016
MPC8544ERM - https://www.nxp.com/docs/en/reference-manual/MPC8544ERM.pdf

## Address Space (AS)

There are two adress spaces. For privledged (0) and non-privlaged (1) spaces.
Out of reset the AS=0.

## Early boot

CRM chapter 11
 * Save DBSR reset reason - probably not worth it. MRR in CRM 2.14.9
 * Print CIR for info (alias to SVR)
 * L1, LRAT, MMU capabilities?
 * display PIR and PVR for as cores start?
 * Registers to set
   * BUCSR - branch control
   * L1CSR0, L1CSR1, L1CSR2 - L1 Cache
   * PWRMGTCR0 - power management
   * HID0 - error management

 * Timer state - Not required
 * L2 - For e500v2 and e6500
     * flash invalidate
     * enable
 * L1
   * flash clear
   * enable


* Set up CCSR TLB
* Set up L1 TLB and stack

## Address space considerations
Address Space (AS) == Translation Space (TS)

This also corresponds to the MSR register IS and DS values.

TLB 1 is fully associative and allows different size pages.
TLB 0 is not fully and only allow 4KB page size, but has many more

All TLBs for boot will be in TLB1 and supervisor mode (not user)

### 7.5.2.1 Address Space (AS) Value
Address spaces require different TLB entries

### CCSR
T2080RM - CCSR needs to not be overlapped with flash space
4.3.1.1 Updating CCSRBARs

Also see MPC8544ERM

*/

#include "hal/nxp_ppc.h"

/* readability helpers for assembly to show register versus decimal */
#define r0 0
#define r1 1
#define r2 2
#define r3 3
#define r4 4
#define r5 5
#define r6 6
#define r7 7
#define r8 8
#define r9 9
#define r10 10
#define r11 11
#define r12 12
#define r13 13
#define r14 14

#define r15 15
#define r16 16
#define r17 17
#define r18 18
#define r19 19
#define r20 20
#define r21 21
#define r22 22
#define r23 23

/* variables from  linker script */
.global _start_vector
.global _end_stack
.global isr_empty

/* Workaround to use same ISR for all interrupts */
#ifndef INTVEC_ADDR
#define INTVEC_ADDR(n) (0x0100)
#endif

/* Reset Entry Point */
.section .boot, "ax"
.global _reset

_reset:

#ifdef DEBUG
        /* CRM 9.9.2 and EREF 10.4 enable debug interrupt */
        /* Set MSR DE (Debug Interrupt Enable = 1) */
        li      r1, MSR_DE
        mtmsr   r1
#endif

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

        /* Set addr streaming & broadcast
         * and optimized sync instruction (if rev 5.0 or greater) */
        li      r0, (HID1_ASTME | HID1_ABE)@l
        mfspr   r3, SPRN_PVR
        andi.   r3, r3, 0xFF
        cmpwi   r3, 0x50@l /* if rev 5.0 or greater set MBDD */
        blt     1f
        ori     r0, r0, HID1_MBDD@l
1:      mtspr   SPRN_HID1, r0

branch_prediction:
        /* enable branch prediction */
        lis     r0, (BUCSR_ENABLE)@h
        ori     r0, r0, (BUCSR_ENABLE)@l
        mtspr   SPRN_BUCSR, r0

startup_init:
        /* Invalidate L1 instruction and data cache */
        lis     r0, L1CSR_CFI@h
        ori     r0, r0, L1CSR_CFI@l
        mtspr   L1CSR0, r0 /* data cache */
        mtspr   L1CSR1, r0 /* instruction cache */

        /* Clear debug status register - read and write */
        mfspr   r1, SPRN_DBSR
        mtspr   SPRN_DBSR, r1

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
        #ifdef DEBUG
                /* if debugging use larger temporary resize */
                #define TLB1_NEW_SIZE BOOKE_PAGESZ_256K
        #else
                #define TLB1_NEW_SIZE BOOKE_PAGESZ_4K
        #endif
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

#ifdef ENABLE_INTERRUPTS
setup_interrupts:

        /* Setup interrupt vectors */
        lis       r1, (_start_vector)@h
        mtspr     IVPR, r1

        li        r1, INTVEC_ADDR(0)
        mtspr     IVOR(0), r1        /* 0: Critical input */
        li        r1, INTVEC_ADDR(1)
        mtspr     IVOR(1), r1        /* 1: Machine check */
        li        r1, INTVEC_ADDR(2)
        mtspr     IVOR(2), r1        /* 2: Data storage */
        li        r1, INTVEC_ADDR(3)
        mtspr     IVOR(3), r1        /* 3: Instruction storage */
        li        r1, INTVEC_ADDR(4)
        mtspr     IVOR(4), r1        /* 4: External interrupt */
        li        r1, INTVEC_ADDR(5)
        mtspr     IVOR(5), r1        /* 5: Alignment */
        li        r1, INTVEC_ADDR(6)
        mtspr     IVOR(6), r1        /* 6: Program check */
        li        r1, INTVEC_ADDR(7)
        mtspr     IVOR(7), r1        /* 7: floating point unavailable */
        li        r1, INTVEC_ADDR(8)
        mtspr     IVOR(8), r1        /* 8: System call */
        /* 9: Auxiliary processor unavailable(unsupported) */
        li        r1, INTVEC_ADDR(10)
        mtspr     IVOR(10), r1        /* 10: Decrementer */
        li        r1, INTVEC_ADDR(11)
        mtspr     IVOR(11), r1        /* 11: Interval timer */
        li        r1, INTVEC_ADDR(12)
        mtspr     IVOR(12), r1        /* 12: Watchdog timer */
        li        r1, INTVEC_ADDR(13)
        mtspr     IVOR(13), r1        /* 13: Data TLB error */
        li        r1, INTVEC_ADDR(14)
        mtspr     IVOR(14), r1        /* 14: Instruction TLB error */
        li        r1, INTVEC_ADDR(15)
        mtspr     IVOR(15), r1        /* 15: Debug */
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
        set_tlb(0, 0, CCSRBAR, CCSRBAR, 0,
                MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_4K, 0, r3);

        set_tlb(0, 0, CCSRBAR + 0x1000, CCSRBAR_DEF, 0,
                MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_4K, 0, r3);

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
        /* Read current value of CCSBAR - forces all acesses to complete */
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

/* TLBs */
boot_page:
        /* make sure we have the default boot page added to MMU */
        /* BOOT_PAGE: TLB 1, Entry 0, Supervisor X/R/W, I, TS=0, 4KB, IPROT */
        set_tlb(1, 0, BOOT_ROM_ADDR, BOOT_ROM_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I, 0, BOOKE_PAGESZ_4K, 1, r3);

ccsr_tlb:
        /* CCSRBAR: TLB 1, Entry 1, Supervisor R/W, IG, TS=0, 1M/16M, IPROT */
        set_tlb(1, 1, CCSRBAR, CCSRBAR, 0, MAS3_SX | MAS3_SR | MAS3_SW,
                MAS2_I | MAS2_G, 0, CCSRBAR_SIZE, 1, r3);

#ifdef FLASH_BASE_ADDR
flash_tlb:
        /* For TS/AS=1 map boot ROM */
        /* Flash: TLB 1, Entry 7, Super X/R/W, IG, TS=0, 1M, IPROT */
        set_tlb(1, 7, FLASH_BASE_ADDR, FLASH_BASE_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR,
                MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_1M, 1, 3);
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
        set_tlb(1, 9, L2SRAM_ADDR, L2SRAM_ADDR, 0, MAS3_SX | MAS3_SW | MAS3_SR,
                MAS2_G, 0, BOOKE_PAGESZ_256K, 1, r3);

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
#endif
#endif /* ENABLE_L2_CACHE */

#if 0
set_stack_as:

        lis     r6, MSR_IS|MSR_DS@h
        ori     r6, r6, MSR_IS|MSR_DS@l
        lis     r7, switch_as@h
        ori     r7, r7, switch_as@l

        mtspr   SRR0, r7
        mtspr   SRR1, r6

        /* return from interrupt call and switch to AS=1 (aka TS=1) */
        rfi

switch_as:
#endif

setup_l1:
        /* L1 Instruction Cache */
        l1_cache_invalidate(L1CSR1);
        #ifdef ENABLE_L1_CACHE
        l1_cache_enable(L1CSR1);
        #endif
        /* L1 Data Cache */
        l1_cache_invalidate(L1CSR0);
        #ifdef ENABLE_L1_CACHE
        l1_cache_enable(L1CSR0);
        #endif

#ifdef ENABLE_L1_CACHE
l1_tlb:

        /* L1: TLB 0, Supervisor X/R/W, TS=0, 16K */
        /* TLB0 must all be 4KB and index is automatically assigned */
        set_tlb(0, 0, L1_CACHE_ADDR, L1_CACHE_ADDR, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x1000, L1_CACHE_ADDR+0x1000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x2000, L1_CACHE_ADDR+0x2000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);
        set_tlb(0, 0, L1_CACHE_ADDR+0x3000, L1_CACHE_ADDR+0x1000, 0,
                (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_4K, 0, r3);

l1_cache:
        /* setup L1 cache */
        lis     r3, L1_CACHE_ADDR@h
        ori     r3, r3, L1_CACHE_ADDR@l
        mfspr   r2, L1CFG0
        andi.   r2, r2, 0x1FF
        /* cache size * 1024 / (2 * L1 line size) */
        slwi    r2, r2, (10 - 1 - L1_CACHE_LINE_SHIFT)
        mtctr   r2
        li      r0, 0

l1_cache_init:
        dcbz    r0, r3
        dcbtls  0, r0, r3
        addi    r3, r3, (1 << L1_CACHE_LINE_SHIFT)
        bdnz    l1_cache_init
#endif /* ENABLE_L1_CACHE */

#ifdef MMU_V1
/* e500v2 L2 Cache as SRAM */

    #ifdef L2SRAM_ADDR
        /* L2 L2SRAM_ADDR: TLB 1, Entry 9, Supervisor X/R/W, G, TS=0, 256KB, IPROT */
        set_tlb(1, 9, L2SRAM_ADDR, L2SRAM_ADDR, 0, MAS3_SX | MAS3_SW | MAS3_SR,
                MAS2_G, 0, BOOKE_PAGESZ_256K, 1, r3);

        /* Configure the L2 Cache as SRAM (2=128KB) */
        sync
        lis     r0, L2CTL@h
        ori     r0, r0, L2CTL@l
        lis     r1, (L2CTL_EN | L2CTL_INV | L2CTL_L2SRAM(2))@h
        ori     r1, r1, (L2CTL_EN | L2CTL_INV | L2CTL_L2SRAM(2))@l
        sync
        stw     r1, 0(r0)

        lis     r0, L2SRBAR0@h
        ori     r0, r0, L2SRBAR0@l
        lis     r1, L2SRAM_ADDR@h
        ori     r1, r1, L2SRAM_ADDR@l
        sync
        stw     r1, 0(r0)
    #endif
#endif

setup_ts0:
        /* Build top of stack address */
        /* Reserve 128 bytes of initial data (must be 16 byte aligned) */
        lis     r1, (_end_stack-128)@h
        ori     r1, r1, (_end_stack-128)@l

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

        /* jump to wolfboot */
        b       boot_entry_C

#ifdef ENABLE_INTERRUPTS
/* Interrupt functions */
.section .isr_vector
.align 8
. = 0x100 /* offset */
isr_empty:
        nop
        rfi
#endif

/* reset entry point - must be at end of .S */
.section .reset, "ax"
        b _reset
