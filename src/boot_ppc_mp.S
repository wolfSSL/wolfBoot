/* boot_ppc_mp.S
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

/* Provides a minimum startup for secondary PPC cores and enables a "spin table"
 * for work to be issued.
 */

#include "hal/nxp_ppc.h"

#define TORESET(x)     (x - _secondary_start_page + BOOT_ROM_ADDR)

/* e6500 has 64-bit GPRs. When loading 32-bit addresses with bit 31 set
 * (addresses >= 0x80000000), the lis instruction sign-extends, putting
 * 0xFFFFFFFF in the upper 32 bits. This causes memory access failures.
 * Use LOAD_ADDR32 macro to properly load 32-bit addresses on e6500. */
#ifdef CORE_E6500
#define LOAD_ADDR32(reg, addr) \
        li      reg, 0; \
        oris    reg, reg, (addr)@h; \
        ori     reg, reg, (addr)@l
#else
#define LOAD_ADDR32(reg, addr) \
        lis     reg, (addr)@h; \
        ori     reg, reg, (addr)@l
#endif

/* Additional cores (mp) assembly code for core minimum startup and spin table.
 * All code must fit in 4KB, which gets virtually mapped via the TLB1 (MMU) and
 * loaded by core 0. Spin table entry TLB1(0) mapped for work is 64MB.
 */
        .section .bootmp, "ax"
        .globl        _secondary_start_page
_secondary_start_page:
        /* Time base, MAS7 and machine check pin enable.
         * HID0_EMCP=0x80000000 has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r0, (HID0_EMCP | HID0_TBEN | HID0_ENMAS7))
        mtspr   SPRN_HID0, r0

#ifdef TARGET_nxp_t2080
        /* T2080 rev-1 e6500 erratum workarounds — must match primary
         * (boot_ppc_start.S _reset). Mirrors CW U-Boot release.S
         * CONFIG_SYS_FSL_ERRATUM_* set. Without these, secondary cores
         * may exhibit cache speculation / DVA-stale / coherency bugs.
         * VPX3-152 ships rev 1.1 (PVR 0x85380011). */
        mfspr   r3, SPRN_PVR
        rlwinm  r3, r3, 28, 0xf  /* major_rev */
        cmpwi   r3, 0x1
        bne     mp_t2080_errata_done

        /* A004779 — HDBCR2[32]=1, [33:39]=3, [41:47]=3 */
        msync
        isync
        mfspr   r3, SPRN_HDBCR2
        lis     r4, 0xff7f
        andc    r3, r3, r4
        oris    r3, r3, 0x8303
        mtspr   SPRN_HDBCR2, r3
        isync

        /* A004786 — HDBCR7 |= 0x80000000 */
        msync
        isync
        mfspr   r3, SPRN_HDBCR7
        oris    r3, r3, 0x8000
        mtspr   SPRN_HDBCR7, r3
        isync

        /* A004792 — HDBCR0 |= 0x4000 */
        msync
        isync
        mfspr   r3, SPRN_HDBCR0
        ori     r3, r3, 0x4000
        mtspr   SPRN_HDBCR0, r3
        isync

        /* A004806 — HDBCR7 |= 0x20000000 (DVA stale-data fix) */
        msync
        isync
        mfspr   r3, SPRN_HDBCR7
        oris    r3, r3, 0x2000
        mtspr   SPRN_HDBCR7, r3
        isync

        /* A004809 — HDBCR0 |= 0x01008000 */
        msync
        isync
        mfspr   r3, SPRN_HDBCR0
        oris    r3, r3, 0x0100
        ori     r3, r3, 0x8000
        mtspr   SPRN_HDBCR0, r3
        isync

mp_t2080_errata_done:
        /* A003999 NOT applied -- see boot_ppc_start.S for rationale. */
#endif /* TARGET_nxp_t2080 */

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

        /* Get our PIR to figure out our table entry.
         * TORESET(...) resolves to address near BOOT_ROM_ADDR (0xFFFFF000),
         * bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r3, TORESET(_spin_table_addr))
        lwz     r3, 0(r3)

        /* Use PIR to determine cluster/core for spin table base at r10 */
        mfspr   r0, SPRN_PIR
#if defined(CORE_E5500) || defined(CORE_E6500)
        rlwinm  r8, r0, 29, 0x03 /* r8 = core within cluster */
        srwi    r10, r0, 5       /* r10 = cluster */
        mr      r9, r10          /* r9 = cluster (preserved across r10 reuse
                                  *      below as spin-table base) */

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
        slwi    r8, r5, 6 /* multiply by ENTRY_SIZE (64 bytes) */
        /* use r10 for the spin table base address */
        add     r10, r3, r8

        mtspr   SPRN_PIR, r4 /* Set processor information cluster/core (PIT) */

#if defined(CORE_E5500) || defined(CORE_E6500)
        /* set L1 stash id = 32: (coreID * 2) + 32 + L1 CT (0) */
        slwi    r8, r4, 1
        addi    r8, r8, 32
        mtspr   L1CSR2, r8

#if defined(CORE_E6500) /* --- L2 E6500 --- */
        /* e6500 L2 is per-cluster. Primary core already enabled cluster 0's
         * L2. Secondary cores in cluster 0 (siblings to primary) MUST NOT
         * L2FI -- it discards primary's dirty L2 lines (stack, return
         * addresses, cached code) and crashes the primary (typically
         * SRR0=0 from corrupted return address).
         *
         * Secondaries in clusters > 0 (no primary present) MUST do L2FI
         * + enable, otherwise their cluster's L2 is left in unknown reset
         * state and any cached access from those cores hits cold. U-Boot's
         * release.S unconditionally L2FIs, but it's safe there because
         * U-Boot has already jumped to the OS by the time secondaries
         * release -- primary's wolfBoot stack/code is not in use anymore.
         * wolfBoot releases secondaries during hal_mp_init before the OS
         * jump, so we MUST gate L2FI on (cluster != primary's cluster).
         *
         * r9 holds the cluster ID (preserved before r10 was repurposed as
         * the spin-table base). Primary boots on cluster 0, so skip L2
         * init when r9 == 0. Note: gating on r5 (linear core ID) would be
         * wrong -- secondaries in cluster 0 have cluster=0 but
         * linear core ID != 0 and would still hit L2FI. */
        cmpwi   r9, 0
        beq     l2_init_skip_e6500

        msync
        lis     r2,     (L2CSR0_L2FI | L2CSR0_L2LFC)@h
        ori     r2, r2, (L2CSR0_L2FI | L2CSR0_L2LFC)@l
        mtspr   L2CSR0, r2
l2_poll_invclear_e6500:
        mfspr   r3, L2CSR0
        and.    r1, r3, r2
        bne     l2_poll_invclear_e6500

        /* Set L2 stash ID = (coreID * 2) + 32 + L2 (1) on this cluster. */
        addi    r3, r8, 1
        mtspr   L2CSR1, r3

        /* Enable L2, no parity. L2CSR0_L2E = 0x80000000. */
        LOAD_ADDR32(r3, L2CSR0_L2E)
        mtspr   L2CSR0, r3
        isync
l2_poll_enable_e6500:
        mfspr   r3, L2CSR0
        andis.  r1, r3, (L2CSR0_L2E)@h
        beq     l2_poll_enable_e6500
l2_init_skip_e6500:

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

        /* enable L2 with no parity.
         * L2CSR0_L2E=0x80000000 has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r3, L2CSR0_L2E)
        mtspr   L2CSR0, r3
        isync
2:
        mfspr   r3, L2CSR0
        andis.  r1, r3, (L2CSR0_L2E)@h
        beq 2b
#endif
#endif /* CORE_E5500 || CORE_E6500 */
3:
        /* setup mapping for the spin table, WIMGE=0b00100.
         * TORESET(...) has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r13, TORESET(_spin_table_addr))
        lwz     r13, 0(r13)
        /* mask by 4K */
        rlwinm  r13, r13, 0, 0, 19

        lis     r11, (MAS0_TLBSEL(1) | MAS0_ESEL(1))@h
        mtspr   MAS0, r11
        /* MAS1_VALID=0x80000000 has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r11, (MAS1_VALID | MAS1_IPROT | MAS1_TS |
                          MAS1_TSIZE(BOOKE_PAGESZ_4K)))
        mtspr   MAS1, r11
        /* Build MAS2 = r13 (spin table base, 4K aligned) | MAS2_M | MAS2_G.
         * Note: both oris and ori must use r11 as source for the second op
         * (the original code erroneously used r13 on the second op, which
         * overwrote the upper 16 bits from the first oris). */
        oris    r11, r13, (MAS2_M | MAS2_G)@h
        ori     r11, r11, (MAS2_M | MAS2_G)@l
        mtspr   MAS2, r11
        oris    r11, r13, (MAS3_SX | MAS3_SW | MAS3_SR)@h
        ori     r11, r11, (MAS3_SX | MAS3_SW | MAS3_SR)@l
        mtspr   MAS3, r11
        li      r11, 0
        mtspr   MAS7, r11
        tlbwe

        /* _bootpg_addr has the address of _second_half_boot_page
         * jump there in AS=1 space with cache enabled.
         * TORESET(...) has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r13, TORESET(_bootpg_addr))
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

        /* Align branch address to 64MB boundary for TLB mapping below */
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
        lis     r7, (64 * 1024 * 1024)@h /* r7 = IMA size (64MB per ePAPR) */
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
        /* MAS1_VALID=0x80000000 has bit 31 set; use LOAD_ADDR32 for e6500. */
        LOAD_ADDR32(r10, (MAS1_VALID | MAS1_IPROT | MAS1_TSIZE(BOOKE_PAGESZ_64M)))
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
        .align 6 /* 64-byte alignment for spin table entries (ENTRY_SIZE) */
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
