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

/* First stage loader requires GOT for PIC and relocation */
#if defined(NO_XIP) && defined(BUILD_LOADER_STAGE1)
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
#define INTVEC_ADDR(n) isr_empty@l
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

#if defined(CORE_E5500) || defined(CORE_E6500)
        mtspr MAS8, r0 /* clear MAS8 used with Embedded Hypervisor */
#endif

hardware_reg:
        /* Time base, MAS7 and machine check pin enable */
        lis     r0,     (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@h
        ori     r0, r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@l
        mtspr   SPRN_HID0, r0

#if defined(CORE_E500) && !defined(BUILD_LOADER_STAGE1)
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
        lis     r0,     (BUCSR_ENABLE)@h
        ori     r0, r0, (BUCSR_ENABLE)@l
        mtspr   SPRN_BUCSR, r0
#endif

startup_init:

        /* Invalidate L1 instruction and data cache */
        li      r0, L1CSR_CFI
        mtspr   L1CSR1, r0
        mtspr   L1CSR0, r0

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
        rlwinm  r5, r3, 16, MAS0_ESEL_MSK
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
        mtspr   IVPR, r1 /* set the 48-bit high-order prefix address */

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
#if CCSRBAR_DEF != CCSRBAR_PHYS

        /* Use R8 = new, R9 = old virtual */
        lis     r8,     CCSRBAR@h
        ori     r8, r8, CCSRBAR@l
        lis     r9,     (CCSRBAR + 0x1000)@h
        ori     r9, r9, (CCSRBAR + 0x1000)@l

create_temp_ccsr:
        /* Create a temporary TLB entry for new and old location  */
        /* CCSRBAR: TLB 0, Entry 0, Supervisor R/W, IG, TS=0, 4KB */
        set_tlb(0, 0,
                CCSRBAR, CCSRBAR, CCSRBAR_PHYS_HIGH,
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
#ifdef USE_CORENET_INTERFACE
        lwz     r1, 4(r9) /* CCSRBARL */
#else
        lwz     r1, 0(r9) /* CCSRBAR, shifted right by 12 */
        slwi    r1, r1, 12
#endif
        cmpl    0, r0, r1
infinite_debug_loop:
        bne     infinite_debug_loop /* should not get here */

#ifdef USE_CORENET_INTERFACE
ccsr_temp_law:
        /* CCSR - LAW0 (Temp CoreNet 4K) */
        #define CCSR_TEMP_LAW (LAWAR_ENABLE | \
                               LAWAR_TRGT_ID(LAW_TRGT_CORENET) | \
                               LAW_SIZE_4KB)
        lis     r0,     CCSRBAR_PHYS_HIGH@h
        ori     r0, r0, CCSRBAR_PHYS_HIGH@l
        lis     r1,     CCSRBAR_DEF@h
        ori     r1, r1, CCSRBAR_DEF@l
        lis     r2,     CCSR_TEMP_LAW@h
        ori     r2, r2, CCSR_TEMP_LAW@l
        stw     r0, LAWBAR_BASE(0)(r9)   /* LAWBARH */
        stw     r1, LAWBAR_BASE(0)+4(r9) /* LAWBARL */
        sync
        stw     r2, LAWBAR_BASE(0)+8(r9) /* LAWAR */
        /* read back LAWAR (per 2.3.2 Configuring Local Access Windows) */
        lwz     r2, LAWBAR_BASE(0)+8(r9)
        isync

read_old_ccsr:
        lwz     r0, 0(r9)
        lwz     r0, 4(r9)
        isync

write_new_ccsrbar:
        lis     r0,     CCSRBAR_PHYS_HIGH@h
        ori     r0, r0, CCSRBAR_PHYS_HIGH@l
        lis     r1,     CCSRBAR@h
        ori     r1, r1, CCSRBAR@l
        #define CCSRAR_C 0x80000000 /* Commit */
        lis     r2,     CCSRAR_C@h
        ori     r2, r2, CCSRAR_C@l
        stw     r0, 0(r9) /* CCSRBARH */
        sync
        stw     r1, 4(r9) /* CCSRBARL */
        sync

        stw     r2, 8(r9) /* commit */
        sync

#else
write_new_ccsrbar:
        /* Read current value of CCSBAR - forces all accesses to complete */
        sync
        lwz     r0, 0(r9)
        isync
        /* write new CCSBAR */
        lis     r0,     (CCSRBAR_PHYS_HIGH << 20) | (CCSRBAR >> 12)@h
        ori     r0, r0, (CCSRBAR_PHYS_HIGH << 20) | (CCSRBAR >> 12)@l
        stw     r0, 0(r9)
        sync
        isync

        /* Read current value of CCSRBAR from new location */
        lwz     r0, 0(r8)
        isync
#endif

invalidate_temp_tlb:
        /* invalidate TLB 0 */
        /* L2TLB0_FI: TLB0 flash invalidate (write 1 to invalidate) */
        li      r3, 0x04
        mtspr   MMUCSR0, r3
#endif /* CCSRBAR_DEF != CCSRBAR_PHYS */


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
                CCSRBAR, CCSRBAR, CCSRBAR_PHYS_HIGH,
                MAS3_SX | MAS3_SR | MAS3_SW, MAS2_I | MAS2_G, 0,
                CCSRBAR_SIZE, 1, r3);

#if defined(CORE_E5500) || defined(CORE_E6500)
ccsr_law:
        /* CCSR - LAW0 (CoreNet 16MB) */
        #define CCSR_LAW (LAWAR_ENABLE | \
                          LAWAR_TRGT_ID(LAW_TRGT_CORENET) | \
                          LAW_SIZE_16MB)
        lis     r9,     CCSRBAR + LAWBAR_BASE(0)@h
        ori     r9, r9, CCSRBAR + LAWBAR_BASE(0)@l
        lis     r0,     CCSRBAR_PHYS_HIGH@h
        ori     r0, r0, CCSRBAR_PHYS_HIGH@l
        lis     r1,     CCSRBAR@h
        ori     r1, r1, CCSRBAR@l
        lis     r2,     CCSR_LAW@h
        ori     r2, r2, CCSR_LAW@l
        stw     r0, 0(r9) /* LAWBARH */
        stw     r1, 4(r9) /* LAWBARL */
        sync
        stw     r2, 8(r9) /* LAWAR */
        /* read back LAWAR (per 2.3.2 Configuring Local Access Windows) */
        lwz     r2, 8(r9)
        isync
#endif /* CORE_E5500 || CORE_E6500 */

#ifdef FLASH_BASE_ADDR
#if defined(CORE_E5500) || defined(CORE_E6500)
        /* Memory Mapped NOR Flash (64/128MB) at 0xEC000000/0xE8000000 */
flash_law:
        /* FLASH - LAW1 (IFC 64/128MB) */
        #define FLASH_LAW (LAWAR_ENABLE | \
                           LAWAR_TRGT_ID(LAW_TRGT_IFC) | \
                           FLASH_LAW_SIZE)
        lis     r9,     CCSRBAR + LAWBAR_BASE(1)@h
        ori     r9, r9, CCSRBAR + LAWBAR_BASE(1)@l
        lis     r0,     FLASH_BASE_PHYS_HIGH@h
        ori     r0, r0, FLASH_BASE_PHYS_HIGH@l
        lis     r1,     FLASH_BASE_ADDR@h
        ori     r1, r1, FLASH_BASE_ADDR@l
        lis     r2,     FLASH_LAW@h
        ori     r2, r2, FLASH_LAW@l
        stw     r0, 0(r9) /* LAWBARH */
        stw     r1, 4(r9) /* LAWBARL */
        sync
        stw     r2, 8(r9) /* LAWAR */
        /* read back LAWAR (per 2.3.2 Configuring Local Access Windows) */
        lwz     r2, 8(r9)
        isync
flash_tlb:
        /* Flash: TLB 1, Entry 2, Super X/R/W, W/I/G, TS=0, 64/128M, IPROT */
        /* Write is required for Write/Erase using CFI commands to base */
    #ifdef BUILD_LOADER_STAGE1
        /* Using XIP from this flash, so cannot use cache inhibit */
        #define FLASH_TLB_WING (MAS2_W | MAS2_G)
    #else
        /* IFC polling requires cache inhibit */
        #define FLASH_TLB_WING (MAS2_I | MAS2_G)
    #endif
        set_tlb(1, 2,
                FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
                MAS3_SX | MAS3_SW | MAS3_SR, FLASH_TLB_WING, 0,
                FLASH_TLB_PAGESZ, 1, r3);
#else
flash_tlb:
        /* For TS/AS=1 map boot ROM */
        /* Flash: TLB 1, Entry 7, Super X/R/W, IG, TS=0, 1M, IPROT */
        set_tlb(1, 7,
                FLASH_BASE_ADDR, FLASH_BASE_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
                BOOKE_PAGESZ_1M, 1, r3);
#endif /* CORE_E5500 || CORE_E6500 */
#endif /* FLASH_BASE_ADDR */

/* Enable use of the DDR (like 2nd stage) so it can be used for stack */
#ifdef ENABLE_DDR
    #ifdef CORE_E500
        #ifdef BUILD_LOADER_STAGE1
            /* use cache inhibited for first stage loader to avoid
             * L1 cache as SRAM issues */
            #define DDR_WING (MAS2_I | MAS2_G)
        #else
            #define DDR_WING (MAS2_G)
        #endif
    #else
        #define DDR_WING (MAS2_M)
    #endif

        /* Map initial DDR, but can be adjusted later in hal_ddr_init() */
        /* DDR - TBL=1, Entry 12/13 */
        set_tlb(1, 12, DDR_ADDRESS, DDR_ADDRESS, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, DDR_WING,
                0, BOOKE_PAGESZ_1G, 1, r3);
    #if DDR_SIZE > 0x40000000
        set_tlb(1, 13, DDR_ADDRESS + 0x40000000, DDR_ADDRESS + 0x40000000, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, DDR_WING,
                0, BOOKE_PAGESZ_1G, 1, r3);
    #endif
#endif /* ENABLE_DDR */

#ifdef INITIAL_SRAM_ADDR
init_sram_law:
        /* Intial SRAM LAW 2 */
        #define INITIAL_SRAM_LAW (LAWAR_ENABLE | \
                                  LAWAR_TRGT_ID(INITIAL_SRAM_LAW_TRGT) | \
                                  INITIAL_SRAM_LAW_SZ)
        lis     r9,     CCSRBAR + LAWBAR_BASE(2)@h
        ori     r9, r9, CCSRBAR + LAWBAR_BASE(2)@l
        li      r0,     0 /* UPPER=0 */
        lis     r1,     INITIAL_SRAM_ADDR@h
        ori     r1, r1, INITIAL_SRAM_ADDR@l
        lis     r2,     INITIAL_SRAM_LAW@h
        ori     r2, r2, INITIAL_SRAM_LAW@l
        stw     r0, 0(r9) /* LAWBARH */
        stw     r1, 4(r9) /* LAWBARL */
        sync
        stw     r2, 8(r9) /* LAWAR */
        /* read back LAWAR (per 2.3.2 Configuring Local Access Windows) */
        lwz     r2, 8(r9)
        isync

init_sram_tlb:
        /* Inital SRAM: TLB 1, Entry 9, Supervisor X/R/W, M, TS=0, IPROT */
        set_tlb(1, 9,
                INITIAL_SRAM_ADDR, INITIAL_SRAM_ADDR, 0,
                MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0,
                INITIAL_SRAM_BOOKE_SZ, 1, r3);
#endif

#ifdef ENABLE_L2_CACHE

#if defined(CORE_E5500) || defined(CORE_E6500) /* --- L2 E5500/E6500 --- */
#ifdef L2SRAM_ADDR
l2_setup_sram:
        /* T2080RM: 8.4.2.2 or T1024RM 13.4.2.2
         * Enabling the CPC after Power-On Reset */
        /* R1 = CPC base */
        lis     r1,     CPC_BASE@h
        ori     r1, r1, CPC_BASE@l

        /* Set CPC SRAM control register */
        /* SRAM high addrress 0x0 */
        li      r0, 0
        stw     r0, CPCSRCR1(r1)
        /* SRAM low address */
        lis     r0,     L2SRAM_ADDR@h
        ori     r0, r0, L2SRAM_ADDR@l
        /* Enable SRAM and set size (must match L2SRAM_SIZE) */
        ori     r0, r0, (CPCSRCR0_SRAMSZ_256 | CPCSRCR0_SRAMEN)
        stw     r0, CPCSRCR0(r1)

        /* Enable memory mapped SRAM */
        lis     r0, CPCCSR0_SRAM_ENABLE@h
        mbar
        isync
        stw     r0, CPCCSR0(r1)
        mbar

        /* Disable speculation */
        lwz     r0, CPCHDBCR0(r1)
        oris    r0, r0, CPCHDBCR0_SPEC_DIS@h
        stw     r0, CPCHDBCR0(r1)
#endif /* L2SRAM_ADDR */

#if defined(CORE_E6500) /* --- L2 E6500 --- */
l2_setup_cache:
        /* E6500CORERM: 11.7 L2 cache state */
        /* R5 = L2 cluster 1 base */
        lis     r5,     L2_CLUSTER_BASE(0)@h
        ori     r5, r5, L2_CLUSTER_BASE(0)@l
        /* Invalidate and clear locks */
        lis     r1,     (L2CSR0_L2FI | L2CSR0_L2LFC)@h
        ori     r1, r1, (L2CSR0_L2FI | L2CSR0_L2LFC)@l
        sync
        stw     r1, L2CSR0(r5)

        /* poll till invalidate and lock bits are cleared */
l2_poll_invclear:
        lwz     r4, L2CSR0(r5)
        and.    r4, r1, r4
        bne     l2_poll_invclear
        isync

        /* set stash id to (coreID * 2) + 32 + L2 (1) - only core 0 here */
        li      r4, (32 + 1)
        stw     r4, L2CSR1(r5)

        /* enable L2 with parity */
        sync
        isync
        lis     r4, (L2CSR0_L2E | L2CSR0_L2PE)@h
        stw     r4, L2CSR0(r5)
        isync

#elif defined(CORE_E5500) /* --- L2 E5500 --- */
l2_setup_cache:
        /* Invalidate and clear locks */
        lis     r1,     (L2CSR0_L2FI | L2CSR0_L2LFC)@h
        ori     r1, r1, (L2CSR0_L2FI | L2CSR0_L2LFC)@l
        sync
        isync
        mtspr   L2CSR0, r1
        isync

        /* poll till invalidate and lock bits are cleared */
l2_poll_invclear:
        mfspr   r4, L2CSR0
        and.    r4, r1, r4
        bne     l2_poll_invclear

        /* set stash id to (coreID * 2) + 32 + L2 (1) - only core 0 here */
        li      r4, (32 + 1)
        mtspr   L2CSR1, r4

        /* enable L2 with no parity */
        lis     r4, (L2CSR0_L2E)@h
        sync
        isync
        mtspr   L2CSR0, r4
        isync
#endif
#endif /* CORE_E5500 || CORE_E6500 */

#if defined(CORE_E500) /* --- L2 E500 --- */
        /* e500 - L2 Cache */
l2_setup_cache:
    #ifdef L2SRAM_ADDR /* as SRAM (1=256KB) */
        #define L2CTL_VAL (L2CTL_EN | L2CTL_INV | L2CTL_SIZ(2) | L2CTL_L2SRAM(1))
    #else
        #define L2CTL_VAL (L2CTL_EN | L2CTL_INV | L2CTL_SIZ(2))
    #endif
        /* Configure the L2 Cache */
        lis     r5,     L2_BASE@h
        ori     r5, r5, L2_BASE@l
        lis     r1,     L2CTL_VAL@h
        ori     r1, r1, L2CTL_VAL@l
        msync
        isync
        stw     r1, L2CTL(r5)
        msync
        isync
        lwz     r1, L2CTL(r5) /* read back (per P1021 RM) */

#ifdef L2SRAM_ADDR
l2_setup_sram:
        /* Set the L2SRAM base address */
        mbar
        isync
        lis     r1,     L2SRAM_ADDR@h
        ori     r1, r1, L2SRAM_ADDR@l
        stw     r1, L2SRBAR0(r5)
        mbar
#endif /* L2SRAM_ADDR */
#endif /* CORE_E500 */
#endif /* ENABLE_L2_CACHE */


#ifdef ENABLE_L1_CACHE
setup_l1:
#if defined(CORE_E5500) || defined(CORE_E6500)
        /* set L1 stash id = 32: (coreID * 2) + 32 + L1 CT (0) */
        li      r2, 32
        mtspr   L1CSR2, r2
#endif
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

        #define CACHE_SRAM_ADDR L1_CACHE_ADDR
#elif defined(L2SRAM_ADDR)
        #define CACHE_SRAM_ADDR L2SRAM_ADDR
#endif
#endif /* ENABLE_L1_CACHE */

#ifdef CACHE_SRAM_ADDR
cache_sram_init:
        lis     r3,     CACHE_SRAM_ADDR@h
        ori     r3, r3, CACHE_SRAM_ADDR@l
        /* read the cache size */
        mfspr   r2, L1CFG0
        andi.   r2, r2, 0x1FF
        /* calculate (cache size * 1024 / (2 * L1 line size)) */
        slwi    r2, r2, (10 - 1 - CACHE_LINE_SHIFT)
        mtctr   r2 /* load counter */
        li      r0, 0
cache_sram_init_loop:
        /* Data cache block zero */
        dcbz    r0, r3
        /* Data cache block touch and lock set */
#if defined(CORE_E6500)
        dcbtls  2, r0, r3
        dcbtls  0, r0, r3
#else
        dcbtls  0, r0, r3
#endif
        addi    r3, r3, CACHE_LINE_SIZE
        bdnz    cache_sram_init_loop
#endif /* CACHE_SRAM_ADDR */

setup_stack:
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
        lis     r0,     RESET_VECTOR@h /* Address of reset vector */
        ori     r0, r0, RESET_VECTOR@l
        stwu    r1, -8(r1)   /* Save back chain and move SP */
        stw     r0, +12(r1)  /* Save return addr (underflow vect) */

        /* switch back to AS/TS=0 */
        lis     r3,     (MSR_CE | MSR_ME | MSR_DE)@h
        ori     r3, r3, (MSR_CE | MSR_ME | MSR_DE)@l
        mtmsr   r3
        isync

#ifdef USE_GOT
        GET_GOT
#endif

#ifdef USE_LONG_JUMP
        /* load absolute address into "LR" and branch return to it */
        /* Enables long jump in 32-bit */
        lis     r3,     boot_entry_C@h
        ori     r3, r3, boot_entry_C@l
        mtlr    r3
        blr
#else
        /* jump to wolfboot */
        b       boot_entry_C /* no return */
#endif


/* -- Assembly Functions -- */
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

/* L1 Cache Helpers */
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
        li      r6, CACHE_LINE_SIZE /* Cache Line size */

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

#ifdef ENABLE_INTERRUPTS
/* Interrupt functions */
.section .isr_vector
.align 8
isr_empty:
        nop
        rfi
#endif

/* reset entry point - must be at end of .S */
.section .reset, "ax"
        b _reset
