/* boot_ppc_start.S
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


/*
# References
CRM - e6500 Core Reference Manual, Rev 0
EREF - EREF: A Programmer’s Reference Manual for Freescale Power Architecture Processors, Rev. 1 (EIS 2.1)
T2080RM - QorIQ T2080 Reference Manual, Rev. 3, 11/2016
MPC8544ERM - https://www.nxp.com/docs/en/reference-manual/MPC8544ERM.pdf

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
 * L2 - "Note that enabling either L1 cache without first enabling the L2 cache is not supported."
     * flash invalidate
     * enable
 * L1
   * flash clear
   * enable


* Set up CCSR TLB
* Set up L1 TLB and stack

## Address space considerations

### 7.5.2.1 Address Space (AS) Value
Address spaces require different TLB entries

### CCSR
T2080RM - -CCSR needs to not be overlapped with flash space
4.3.1.1 Updating CCSRBARs

Also see MPC8544ERM

*/

.section .boot, "ax"
.global _reset

_reset:
        /* CRM 9.9.2 and EREF 10.4 enable debug interrupt */
#if defined(DEBUG)
        li      1, (1<<9)
        mtmsr   1
#endif
setup_interrupts:

#define MAS0     0x270
#define MAS1     0x271
#define MAS2     0x272
#define MAS3     0x273
#define MAS7     0x3B0 /* 944 */
#define SCCSRBAR 1022
#define L1CSR0   1010
#define L1CSR1   1011

/* CRM 11.6 and 6.7 */
setup_mmu:
        /* TLBs
         * for CCSR default
         * tlb 0, entry 0
         */
        xor     3, 3, 3
        mtspr   MAS0, 3
        /* V and 1M */
        lis     3, 0x8000
        ori     3, 3, (10<<7)
        mtspr   MAS1, 3
        /* epn wimg(I|G) */
        lis     3, 0xfe00
        ori     3, 3, 0x000a
        mtspr   MAS2, 3
        /* rpn permisisons (super W+R) */
        lis     3, 0xfe00
        ori     3, 3, 0x0005
        mtspr   MAS3, 3
        /* upper rpn */
        li     3, 0x0000
        mtspr   MAS7, 3
        isync
        msync
        tlbwe
        isync

        /* TLBs
         * for CCSR default
         * tlb 1, entry 13
         */
        lis     3, 0x100d
        mtspr   MAS0, 3
        /* V, ts, and 16M */
        lis     3, 0x8000
        ori     3, 3, (14<<7)|(1<<12)
        mtspr   MAS1, 3
        /* epn wimg(I|G) */
        lis     3, 0xfe00
        ori     3, 3, 0x000a
        mtspr   MAS2, 3
        /* rpn permisisons(user W,R) */
        lis     3, 0xfe00
        ori     3, 3, 0x0005
        mtspr   MAS3, 3
        /* upper rpn */
        li     3, 0x0000
        mtspr   MAS7, 3
        isync
        msync
        tlbwe
        isync

flash_tlb:
        /* flash code
         * tlb 1, entry 15
         */
        lis     3, 0x100f
        mtspr   MAS0, 3
        /* V, iprotect, TS, and 8M */
        lis     3, 0xC000
        ori     3, 3, (1<<12)|(13<<7)
        mtspr   MAS1, 3
        /* epn wimg */
        lis     3, 0xef80
        ori     3, 3, 0x000a
        mtspr   MAS2, 3
        /* rpn permisisons */
        lis     3, 0xff80
        ori     3, 3, 0x0015
        mtspr   MAS3, 3
        /* upper rpn */
        li     3, 0
        mtspr   MAS7, 3
        isync
        msync
        tlbwe
        isync

create_ccsr_l2_tlb:
        /* tlb 0 entry 2 */
        lis     3,2
        ori     3,3,0
        mtspr   MAS0,3
        /* V 4K */
        lis     3,0x8000
        ori     3,3,2<<7
        mtspr   MAS1,3
        /* 0xfec2_0000 I G */
        lis     3,0xfec2
        ori     3,3,0x000a
        mtspr   MAS2,3
        /* 0xfec2_0000 SW SR */
        lis     3,0xfec2
        ori     3,3,0x0005
        mtspr   MAS3,3
        /* no extended address */
        xor     3,3,3
        mtspr   MAS7,3
        isync
        msync
        tlbwe
        isync

/* CRM 11.7 */
setup_l2:
        /* L2 data cache invalidation & unlocking
         * create flash invalidate & unlock bit mask (see Table 2-19)
         */
        lis 4,0x0020
        ori 4,4,0x0400
        /* get base address of memory mapped registers */
        mfspr 5,SCCSRBAR
        li 7,24 /* get shift count */
        sld 5,5,7
        lis 6,0x00C2 /* block offset for desired cluster (see Table 2-4) */
        /* subsequent cluster L2 caches may be invalidated & unlocked by adding 0x40000 to 6 */
        add 6,6,5
        /* L2SC0 offset (see Table 2-5), included here only for example */
        /*addi 6,6,0 */
        /* ensure prior memory transactions are performed */
        sync

        // TODO is this required?
        li      5, 33
        stw     5, 4(6)

        sync
        stw 4,0(6) /* write L2SC0 MMR to flash invalidate L2 cache and locks */
l2loop:
        sync
        lwz 5,0(6) /* get current L2SC0 MMR value */
        and. 5,5,4 /* compare to mask to see if complete */
        bne l2loop
        isync


enable_l2_pe:
        lis     5, 0x4000
        sync
        stw     5, 0(6)
l2_pe_loop:
        sync
        lwz     4,0(6)
        cmplw   4, 5
        bne     l2_pe_loop
        isync

enable_l2e:
        lis     5, 0xC000
        sync
        stw     5, 0(6)
l2e_loop:
        sync
        lwz     4,0(6)
        cmplw   4, 5
        bne     l2e_loop
        isync

setup_l1:
        /* L1 data cache */
        xor 4,4,4     /* set 4 to 0 */
        ori 5,4,0x0102 /* set CFLC and CFI bits */
        sync
        isync            /* synchronize setting of L1CS0 */
        mtspr L1CSR0,5   /* flash invalidate L1 data cache */
        isync            /* synchronize setting of L1CS0 */
dloop:
        mfspr 4,L1CSR0  /* get current value */
        and. 4,4,5    /* test written bits */
        bne dloop        /* check again if not complete */
        isync            /* discard prefetched instructions */

        /* L1 instruction cache */
        xor 4,4,4     /* set 4 to 0 */
        ori 5,4,0x0102 /* set ICFLC and ICFI bits */
        sync
        isync            /* synchronize setting of L1CS1 */
        mtspr L1CSR1,5  /* flash invalidate L1 instruction cache */
        isync            /* synchronize setting of L1CS1 */
iloop:
        mfspr 4,L1CSR1  /* get current value */
        and. 4,4,5    /* test written bits */
        bne iloop        /* check again if not complete */
        isync            /* discard prefetched instructions */

enable_l1_data:
        /* L1 data cache  */
        xor 4,4,4     /* set 4 to 0 */
        lis     5, 0x0001 /* CPE */
        ori     5,5,0x0001 /*  CE */
        sync
        isync            /* synchronize setting of L1CS0 */
        mtspr L1CSR0,5  /* enaable L1 data cache */
        isync            /* synchronize setting of L1CS0 */
data_en_loop:
        mfspr 4,L1CSR0  /* get current value */
        cmplw   4,5    /* test written bits */
        bne data_en_loop        /* check again if not complete */
        isync            /* discard prefetched instructions */

enable_l1_i:
        /* L1 instruction cache */
        xor 4,4,4     /* set 4 to 0 */
        lis     5, 0x0001 /* CPE */
        ori     5,5,0x0001 /*  CE */
        sync
        isync            /* synchronize setting of L1CS1 */
        mtspr L1CSR1,5  /* enable L1 instruction cache */
        isync            /* synchronize setting of L1CS1 */
i_enable_loop:
        mfspr 4,L1CSR1   /* get current value */
        cmplw     4,5    /* test written bits */
        bne i_enable_loop        /* check again if not complete */
        isync            /* discard prefetched instructions */

stack_tlb:
        /* stack
         * tlb 1, entry 14
         */
        lis     3, 0x100e
        mtspr   MAS0, 3
        /* V, iprotect, TS, and 16k */
        lis     3, 0xC000
        ori     3, 3, (4<<7)|(1<<12)
        mtspr   MAS1, 3
        /* epn wimg */
        lis     3, 0xfdd0
        ori     3, 3, 0x0000
        mtspr   MAS2, 3
        /* rpn permisisons rpn=0xf_fe03_c000 */
        lis     3, 0xfe03
        ori     3, 3, 0xc015
        mtspr   MAS3, 3
        /* upper rpn */
        li     3, 0xf
        mtspr   MAS7, 3
        isync
        msync
        tlbwe
        isync

set_stack_as:
        lis     3, 2
        ori     3, 3, (1<<12)|(1<<9)
        mtmsr   3

        /* stack size of 16k */
        lis     3, 0xfdd0
        ori     3, 3, 0x4000
        mr      1, 3


#define SPRN_SRR0       0x01A   /* Save/Restore Register 0 */
#define SPRN_SRR1       0x01B   /* Save/Restore Register 1 */
#define MSR_IS          (1<<5)  /* Book E Instruction space */
#define MSR_DS          (1<<4)  /* Book E Data space */
#define MSR_DE          (1<<9)  /* Debug Exception Enable */

        lis     6, MSR_IS|MSR_DS|MSR_DE@h
        ori     6, 6, MSR_IS|MSR_DS|MSR_DE@l
        lis     7, switch_as@h
        ori     7, 7, switch_as@l

        mtspr   SPRN_SRR0,7
        mtspr   SPRN_SRR1,6

        /* retrun from interrupt call, switches to AS=1 */
        rfi

switch_as:

ram_q:
        lis     3,0xfdd0
        mfspr   2, 0x203 /* L1CFG0 */
        andi.   2, 2, 0x1ff
        /* cache size * 1024 / (2 * L1 line size) */
        slwi    2, 2, (10 - 1 - 6)
        mtctr   2
        li      0,0
1:
        dcbz    0,3
        dcbtls  2, 0, 3
        dcbtls  0, 0, 3
        addi    3,3, 64 /* CONFIG_SYS_CACHELINE_SIZE */
        bdnz    1b

        /* PowerPC e500 Application Binary Interface User’s Guide
         * 2.3.5.1.1 Minimal Stack Frame: No Local Variables or Saved Parameters
         */
        xor     0,0,0
        stwu    0, -4(1) /* Back chain */
        stwu    0, -4(1) /* padding 1 */
        stwu    0, -4(1) /* padding 2 */
        stwu    0, -4(1) /* LR */

        /* jump to wolfboot */
        b       boot_entry_C

.section .reset, "ax"
        b _reset
