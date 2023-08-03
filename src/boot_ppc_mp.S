/* boot_ppc_mp.S
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

/* Provides a minimum startup for secondary PPC cores and enables a "spin table"
 * for work to be issued.
 */

#include "hal/nxp_ppc.h"

#define TORESET(x)     (x - _mp_page_start + BOOT_ROM_ADDR)

#ifndef INTVEC_ADDR
/* workaround to use isr_empty for all interrupts, for real IRQ's adjust the
 * offset and define additional interrupts at those offsets */
#define INTVEC_ADDR(n) (BOOT_ROM_SIZE + (n * 0x0))
#endif

/* Additional cores (mp) assembly code for core minimum startup and spin table.
 * All code must fit in 4KB, which gets virtually mapped via the TLB1 (MMU) and
 * loaded by core 0. Spin table entry TLB1(0) mapped for work is 64MB. */
.globl        _mp_page_start
.align        12
_mp_page_start:
        /* Time base, MAS7 and machine check pin enable */
        lis     r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@h
        ori     r0, r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@l
        mtspr   SPRN_HID0, r0

        /* enable branch prediction */
        lis     r0, (BUCSR_ENABLE)@h
        ori     r0, r0, (BUCSR_ENABLE)@l
        mtspr   SPRN_BUCSR, r0

        /* Ensure TB is 0 */
        li      r3, 0
        mttbl   r3
        mttbu   r3

        /* Enable/invalidate the I-Cache */
        lis     r2, (L1CSR_CFI|L1CSR_CLFC)@h
        ori     r2, r2, (L1CSR_CFI|L1CSR_CLFC)@l
        mtspr   L1CSR1, r2
1:
        mfspr   r3, L1CSR1
        and.    r1, r3, r2
        bne     1b

        lis     r3, (L1CSR_CPE|L1CSR_CE)@h
        ori     r3, r3, (L1CSR_CPE|L1CSR_CE)@l
        mtspr   L1CSR1,r3
        isync
2:
        mfspr   r3, L1CSR1
        andi.   r1, r3, L1CSR_CE@l
        beq     2b

        /* Enable/invalidate the D-Cache */
        lis     r2, (L1CSR_CFI|L1CSR_CLFC)@h
        ori     r2, r2, (L1CSR_CFI|L1CSR_CLFC)@l
        mtspr   L1CSR0, r2
1:
        mfspr   r3, L1CSR0
        and.    r1, r3, r2
        bne     1b

        lis     r3, (L1CSR_CPE|L1CSR_CE)@h
        ori     r3, r3, (L1CSR_CPE|L1CSR_CE)@l
        mtspr   L1CSR0, r3
        isync
2:
        mfspr   r3, L1CSR0
        andi.   r1, r3, L1CSR_CE@l
        beq     2b

        /* Get our PIR to figure out our table entry */
        lis     r3, TORESET(_spin_table)@h
        ori     r3, r3, TORESET(_spin_table)@l

        /* Determine base address for the core (use r10) */
        mfspr   r0, SPRN_PIR
        mr      r4, r0
        slwi    r8, r4, 5 /* core number * ENTRY_SIZE */
        add     r10, r3, r8

        /* Setup the spin table entry */
        li      r3, 0
        li      r8, 1
        stw     r0, ENTRY_PIR(r10)
        stw     r3, ENTRY_ADDR_UPPER(r10)
        stw     r8, ENTRY_ADDR_LOWER(r10)
        stw     r3, ENTRY_R3_UPPER(r10)
        stw     r4, ENTRY_R3_LOWER(r10)
        stw     r3, ENTRY_R6_UPPER(r10)
        stw     r3, ENTRY_R6_LOWER(r10)

        /* Load r13 with the address of the boot page */
        lis     r13, TORESET(_bootpg_addr)@h
        ori     r13, r13, TORESET(_bootpg_addr)@l
        lwz     r13, 0(r13)

        /* Write TLB1 entry 1 4KB for boot page */
        lis     r11, (MAS0_TLBSEL(1) | MAS0_ESEL(1))@h
        mtspr   MAS0, r11
        lis     r11, (MAS1_VALID | MAS1_IPROT)@h
        ori     r11, r11, (MAS1_TS | MAS1_TSIZE(BOOKE_PAGESZ_4K))@l
        mtspr   MAS1, r11
        oris    r11, r13, (MAS2_I)@h
        ori     r11, r13, (MAS2_I)@l
        mtspr   MAS2, r11
        oris    r11, r13, (MAS3_SX | MAS3_SW | MAS3_SR)@h
        ori     r11, r13, (MAS3_SX | MAS3_SW | MAS3_SR)@l
        mtspr   MAS3, r11
        tlbwe

        bl      1f
1:      mflr    r11
        /* Create a boot page mask to fixup the spin table */
        ori     r13, r13, 0xfff
        and     r11, r11, r13
        and     r10, r10, r13

        addi    r11, r11, (2f-1b)
        mfmsr   r13
        ori     r12, r13, (MSR_IS | MSR_DS)@l

        mtspr   SRR0, r11
        mtspr   SRR1, r12
        rfi

2:      /* spin waiting for addr */
        lwz     r4, ENTRY_ADDR_LOWER(r10)
        andi.   r11, r4, 1
        bne     2b
        isync

        /* setup interrupts */
        li      r3, INTVEC_ADDR(0)
        mtspr   IVOR(0), r3        /* 0: Critical input */
        li      r3, INTVEC_ADDR(1)
        mtspr   IVOR(1), r3        /* 1: Machine check */
        li      r3, INTVEC_ADDR(2)
        mtspr   IVOR(2), r3        /* 2: Data storage */
        li      r3, INTVEC_ADDR(3)
        mtspr   IVOR(3), r3        /* 3: Instruction storage */
        li      r3, INTVEC_ADDR(4)
        mtspr   IVOR(4), r3        /* 4: External interrupt */
        li      r3, INTVEC_ADDR(5)
        mtspr   IVOR(5), r3        /* 5: Alignment */
        li      r3, INTVEC_ADDR(6)
        mtspr   IVOR(6), r3        /* 6: Program check */
        li      r3, INTVEC_ADDR(7)
        mtspr   IVOR(7), r3        /* 7: floating point unavailable */
        li      r3, INTVEC_ADDR(8)
        mtspr   IVOR(8), r3        /* 8: System call */
        /* 9: Auxiliary processor unavailable(unsupported) */
        li      r3, INTVEC_ADDR(10)
        mtspr   IVOR(10), r3        /* 10: Decrementer */
        li      r3, INTVEC_ADDR(11)
        mtspr   IVOR(11), r3        /* 11: Interval timer */
        li      r3, INTVEC_ADDR(12)
        mtspr   IVOR(12), r3        /* 12: Watchdog timer */
        li      r3, INTVEC_ADDR(13)
        mtspr   IVOR(13), r3        /* 13: Data TLB error */
        li      r3, INTVEC_ADDR(14)
        mtspr   IVOR(14), r3        /* 14: Instruction TLB error */
        li      r3, INTVEC_ADDR(15)
        mtspr   IVOR(15), r3        /* 15: Debug */

        /* get the upper bits of the addr */
        lwz     r11, ENTRY_ADDR_UPPER(r10)

        /* setup branch addr */
        mtspr   SRR0, r4

        /* mark the entry as done */
        li      r8, 3
        stw     r8, ENTRY_ADDR_LOWER(r10)

        /* mask branch address (64MB) to setup tlb */
        rlwinm  r12, r4, 0, 0, 5

        /* setup registers before jump */
        lwz     r3, ENTRY_R3_LOWER(r10)
        li      r4, 0
        li      r5, 0
        lwz     r6, ENTRY_R6_LOWER(r10)
        lis     r7, (64 * 1024 * 1024)@h
        li      r8, 0
        li      r9, 0

        /* Load entry PIR */
        lwz     r0, ENTRY_PIR(r10)
        mtspr   SPRN_PIR, r0
        mfspr   r0, SPRN_PIR
        stw     r0, ENTRY_PIR(r10)

        mtspr   IVPR, r12

        /* Add tlb 1 entry 0 64MB for new entry */
        lis     r10, (MAS0_TLBSEL(1) | MAS0_ESEL(0))@h
        mtspr   MAS0, r10
        lis     r10, (MAS1_VALID | MAS1_IPROT)@h
        ori     r10, r10, (MAS1_TSIZE(BOOKE_PAGESZ_64M))@l
        mtspr   MAS1, r10
        mtspr   MAS2, r12 /* WIMGE = 0 */
        ori     r12, r12, (MAS3_SX | MAS3_SW | MAS3_SR)
        mtspr   MAS3, r12
        mtspr   MAS7, r11
        tlbwe

        /* jump to new page mapping */
        mtspr   SRR1, r13
        rfi

        /* Reserve memory to store physical boot page address */
        .globl _bootpg_addr
_bootpg_addr:
        .long        0

        .align CACHE_LINE_SHIFT

        /* Reserve space for spin table entries */
        .globl _spin_table
_spin_table:
        .space CPU_NUMCORES * ENTRY_SIZE

        /* Fill remainder of page */
_mp_page_end:
        .space 4092 - (_mp_page_end - _mp_page_start)
_mp_reset:
        b        _mp_page_start
