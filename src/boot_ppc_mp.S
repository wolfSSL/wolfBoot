/* boot_ppc_mp.S
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

/* Provides a minimum startup for secondary PPC cores and enables a "spin table"
 * for work to be issued.
 */

#include "hal/nxp_ppc.h"

#define TORESET(x)     (x - _secondary_start_page + BOOT_ROM_ADDR)

/* Additional cores (mp) assembly code for core minimum startup and spin table.
 * All code must fit in 4KB, which gets virtually mapped via the TLB1 (MMU) and
 * loaded by core 0. Spin table entry TLB1(0) mapped for work is 64MB.
 */
        .section .bootmp, "ax"
        .globl        _secondary_start_page
_secondary_start_page:
        /* Time base, MAS7 and machine check pin enable */
        lis     r0,     (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@h
        ori     r0, r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7)@l
        mtspr   SPRN_HID0, r0

#ifdef CORE_E500
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

branch_prediction:
        /* enable branch prediction */
        lis     r0,     (BUCSR_ENABLE)@h
        ori     r0, r0, (BUCSR_ENABLE)@l
        mtspr   SPRN_BUCSR, r0

        /* Ensure Timebase is reset to 0 */
        li      r3, 0
        mttbl   r3
        mttbu   r3

        /* Enable/invalidate the I-Cache */
        lis     r2,     (L1CSR_CFI | L1CSR_CLFC)@h
        ori     r2, r2, (L1CSR_CFI | L1CSR_CLFC)@l
        mtspr   L1CSR1, r2
1:
        mfspr   r3, L1CSR1
        and.    r1, r3, r2
        bne     1b

        lis     r3,     (L1CSR_CPE | L1CSR_CE)@h
        ori     r3, r3, (L1CSR_CPE | L1CSR_CE)@l
        mtspr   L1CSR1,r3
        isync
2:
        mfspr   r3, L1CSR1
        andi.   r1, r3, L1CSR_CE@l
        beq     2b

        /* Enable/invalidate the D-Cache */
        lis     r2, (L1CSR_CFI | L1CSR_CLFC)@h
        ori     r2, r2, (L1CSR_CFI | L1CSR_CLFC)@l
        mtspr   L1CSR0, r2
1:
        mfspr   r3, L1CSR0
        and.    r1, r3, r2
        bne     1b

        lis     r3,     (L1CSR_CPE | L1CSR_CE)@h
        ori     r3, r3, (L1CSR_CPE | L1CSR_CE)@l
        mtspr   L1CSR0, r3
        isync
2:
        mfspr   r3, L1CSR0
        andi.   r1, r3, L1CSR_CE@l
        beq     2b

        /* Get our PIR to figure out our table entry */
        lis     r3,     TORESET(_spin_table_addr)@h
        ori     r3, r3, TORESET(_spin_table_addr)@l
        lwz     r3, 0(r3)

        /* Use PIR to determine cluster/core for spin table base at r10 */
        mfspr   r0, SPRN_PIR
#if defined(CORE_E5500) || defined(CORE_E6500)
        rlwinm  r8, r0, 29, 0x03 /* r8 = core within cluster */
        srwi    r10, r0, 5       /* r10 = cluster */

        mulli   r5, r10, CORES_PER_CLUSTER
        add     r5, r5, r8       /* r5 = linear core ID */
        mr      r4, r5           /* r4 = PIR = linear core ID */
#elif defined(CORE_E500MC) /* BOOKE e500mc family */
        rlwinm  r4, r0, 27, 27, 31
        mr      r5, r4
#else /* BOOKE e500 family (like P1021) */
        mr      r4, r0
        mr      r5, r4
#endif
        slwi    r8, r5, 6 /* spin table is padded to 64 bytes */
        /* use r10 for the spin table base address */
        add     r10, r3, r8

        mtspr   SPRN_PIR, r4 /* Set processor information cluster/core (PIT) */

#if defined(CORE_E5500) || defined(CORE_E6500)
        /* set L1 stash id = 32: (coreID * 2) + 32 + L1 CT (0) */
        slwi    r8, r4, 1
        addi    r8, r8, 32
        mtspr   L1CSR2, r8

#if defined(CORE_E6500) /* --- L2 E6500 --- */
ccsr_tlb_mp:
        /* e6500 L2 uses memory-mapped CCSR registers (L2_CLUSTER_BASE).
         * Secondary cores have no TLBs on entry — only the boot page
         * translation provides initial access. Add a temporary CCSR
         * mapping (TLB1 entry 2) so L2 setup can access the registers. */
        set_tlb(1, 2,
                CCSRBAR, CCSRBAR, CCSRBAR_PHYS_HIGH,
                MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
                CCSRBAR_SIZE, 0, r11)
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

        /* set stash id to (coreID * 2) + 32 + L2 (1) */
        addi    r3, r8,1
        stw     r3, L2CSR1(r5)

        /* enable L2 with parity */
        sync
        isync
        lis     r4, (L2CSR0_L2E | L2CSR0_L2PE)@h
        stw     r4, L2CSR0(r5)
        isync

#elif defined(CORE_E5500) /* --- L2 E5500 --- */
l2_setup_cache:
        /* Invalidate and clear locks */
        msync
        lis     r2,     (L2CSR0_L2FI | L2CSR0_L2LFC)@h
        ori     r2, r2, (L2CSR0_L2FI | L2CSR0_L2LFC)@l
        mtspr   L2CSR0, r2

        /* poll till invalidate and lock bits are cleared */
l2_poll_invclear:
        mfspr   r3, L2CSR0
        and.    r1, r3, r2
        bne     l2_poll_invclear

        /* set stash id to (coreID * 2) + 32 + L2 (1) */
        addi    r3, r8, 1
        mtspr   L2CSR1, r3

        /* enable L2 with no parity */
        lis     r3, (L2CSR0_L2E)@h
        mtspr   L2CSR0, r3
        isync
2:
        mfspr   r3, L2CSR0
        andis.  r1, r3, (L2CSR0_L2E)@h
        beq 2b
#endif
#endif /* CORE_E5500 || CORE_E6500 */
3:
        /* setup mapping for the spin table, WIMGE=0b00100 */
        lis     r13, TORESET(_spin_table_addr)@h
        ori     r13, r13, TORESET(_spin_table_addr)@l
        lwz     r13, 0(r13)
        /* mask by 4K */
        rlwinm  r13, r13, 0, 0, 19

        lis     r11, (MAS0_TLBSEL(1) | MAS0_ESEL(1))@h
        mtspr   MAS0, r11
        lis     r11, (MAS1_VALID | MAS1_IPROT)@h
        ori     r11, r11, (MAS1_TS | MAS1_TSIZE(BOOKE_PAGESZ_4K))@l
        mtspr   MAS1, r11
        oris    r11, r13, (MAS2_M | MAS2_G)@h
        ori     r11, r13, (MAS2_M | MAS2_G)@l
        mtspr   MAS2, r11
        oris    r11, r13, (MAS3_SX | MAS3_SW | MAS3_SR)@h
        ori     r11, r13, (MAS3_SX | MAS3_SW | MAS3_SR)@l
        mtspr   MAS3, r11
        li      r11, 0
        mtspr   MAS7, r11
        tlbwe

        /* _bootpg_addr has the address of _second_half_boot_page
         * jump there in AS=1 space with cache enabled
         */
        lis     r13, TORESET(_bootpg_addr)@h
        ori     r13, r13, TORESET(_bootpg_addr)@l
        lwz     r11, 0(r13)
        mtspr   SRR0, r11
        mfmsr   r13
        ori     r12, r13, (MSR_IS | MSR_DS)@l
        mtspr   SRR1, r12
        rfi

        /* Reserve memory to store physical boot page address */
        .align CACHE_LINE_SHIFT
        .globl _bootpg_addr
_bootpg_addr:
        .long   0

        .global _spin_table_addr
_spin_table_addr:
        .long   0

        /* Fill in the empty space.  The actual reset vector is
         * the last word of the page */
_secondary_start_code_end:
        .space (BOOT_ROM_SIZE - 4) - (_secondary_start_code_end - _secondary_start_page)
_secondary_reset_vector:
        b        _secondary_start_page


        /* this is a separated page for the spin table and cacheable boot code */
        .align CACHE_LINE_SHIFT
        .global _second_half_boot_page
_second_half_boot_page:
        lis     r3, (spin_table_compat - _second_half_boot_page)@h
        ori     r3, r3, (spin_table_compat - _second_half_boot_page)@l
        add     r3, r3, r11 /* r11 has the address of _second_half_boot_page */
        lwz     r14, 0(r3)

        /* Setup the spin table entry */
        li      r3, 0
        li      r8, 1
        mfspr   r4, SPRN_PIR
        stw     r3, ENTRY_ADDR_UPPER(r10)
        stw     r3, ENTRY_R3_UPPER(r10)
        stw     r4, ENTRY_R3_LOWER(r10)
        stw     r3, ENTRY_RESV(r10)
        stw     r4, ENTRY_PIR(r10)
        msync
        stw     r8, ENTRY_ADDR_LOWER(r10)

        /* spin waiting for addr */
3:

        cmpwi   r14, 0
        beq     4f
        dcbf    0, r10
        sync
4:

        lwz     r4, ENTRY_ADDR_LOWER(r10)
        andi.   r11, r4, 1
        bne     3b
        isync

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
    #ifdef ENABLE_PPC64
        ld      r3, ENTRY_R3_UPPER(r10)
    #else
        lwz     r3, ENTRY_R3_LOWER(r10)
    #endif
        li      r4, 0
        li      r5, 0
        li      r6, 0
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

        /* Reserve space for spin table entries */
        .align 6 /* 64-bytes */
        .globl _spin_table
_spin_table:
        .space CPU_NUMCORES * ENTRY_SIZE

        /* enable spin table compatibility for older ePAPR 1.1 */
        .align CACHE_LINE_SHIFT
        .global spin_table_compat
spin_table_compat:
        .long        1

        /* Fill remainder of page */
_spin_table_end:
        .space BOOT_ROM_SIZE - (_spin_table_end - _spin_table)
