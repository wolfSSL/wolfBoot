/* nxp_ppc.h
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

#ifndef _NXP_PPC_H_
#define _NXP_PPC_H_

#ifdef PLATFORM_nxp_p1021
#define CCSRBAR_DEF (0xFF700000) /* P1021RM 4.3 default base */
#define CCSRBAR_SIZE BOOKE_PAGESZ_1M
#define MMU_V1
#define ENABLE_L1_CACHE
#define ENABLE_L2_CACHE

/* Setup L2 as SRAM */
//#define L2SRAM_ADDR  (0xF8F80000)

/* Relocate CCSRBAR */
#define CCSRBAR 0xFFE00000

/* boot address */
#define BOOT_ROM_ADDR 0xFFFFF000UL
#define BOOT_ROM_SIZE (4*1024)

/* Memory used for transferring blocks to/from NAND.
 * Maps to eLBC FCM internal 8KB region (by hardware) */
#define FLASH_BASE_ADDR 0xFC000000

#ifndef BUILD_LOADER_STAGE1
#define ENABLE_INTERRUPTS
#endif

#elif defined(PLATFORM_nxp_t2080)
#define CCSRBAR_DEF (0xFE000000) /* T2080RM 4.3.1 default base */
#define CCSRBAR_SIZE BOOKE_PAGESZ_16M
#define MMU_V2
#define ENABLE_L1_CACHE
#define ENABLE_L2_CACHE
#define L2SRAM_ADDR (0xFEC20000)

/* This flash mapping window is automatically enabled
 * T2080RM: 4.3.3 Boot Space Translation:
 * default boot window (8 MB at 0x0_FF80_0000 to 0x0_FFFF_FFFF)
 */
#define FLASH_BASE_ADDR 0xEF800000

#else
#error Please define MMU version and CCSRBAR for platform
#endif

#define RESET_VECTOR (BOOT_ROM_ADDR + (BOOT_ROM_SIZE - 4))

#ifndef CCSRBAR_DEF
#define CCSRBAR_DEF 0xFE000000
#endif
#ifndef CCSRBAR
#define CCSRBAR     0xFE000000
#endif


#ifdef MMU_V1
/* MMU V1 - e500 */
/* EREF: 7.5.3.2 - TLB Entry Page Size */
#define BOOKE_PAGESZ_4K    1
#define BOOKE_PAGESZ_16K   2
#define BOOKE_PAGESZ_64K   3
#define BOOKE_PAGESZ_256K  4
#define BOOKE_PAGESZ_1M    5
#define BOOKE_PAGESZ_4M    6
#define BOOKE_PAGESZ_16M   7
#define BOOKE_PAGESZ_64M   8
#define BOOKE_PAGESZ_256M  9
#define BOOKE_PAGESZ_1G    10
#define BOOKE_PAGESZ_4G    11

#define MAS1_TSIZE_MASK    0x00000F00
#define MAS1_TSIZE(x)      (((x) << 8) & MAS1_TSIZE_MASK)

#else
/* MMU V2 - e6500 */
/* EREF 2.0: 6.5.3.2 - TLB Entry Page Size */
#define BOOKE_PAGESZ_4K    2
#define BOOKE_PAGESZ_8K    3
#define BOOKE_PAGESZ_16K   4
#define BOOKE_PAGESZ_32K   5
#define BOOKE_PAGESZ_64K   6
#define BOOKE_PAGESZ_128K  7
#define BOOKE_PAGESZ_256K  8
#define BOOKE_PAGESZ_512K  9
#define BOOKE_PAGESZ_1M    10
#define BOOKE_PAGESZ_2M    11
#define BOOKE_PAGESZ_4M    12
#define BOOKE_PAGESZ_8M    13
#define BOOKE_PAGESZ_16M   14
#define BOOKE_PAGESZ_32M   15
#define BOOKE_PAGESZ_64M   16
#define BOOKE_PAGESZ_128M  17
#define BOOKE_PAGESZ_256M  18
#define BOOKE_PAGESZ_512M  19
#define BOOKE_PAGESZ_1G    20
#define BOOKE_PAGESZ_2G    21
#define BOOKE_PAGESZ_4G    22

#define MAS1_TSIZE_MASK    0x00000F80
#define MAS1_TSIZE(x)      (((x) << 7) & MAS1_TSIZE_MASK)

#define L1_CACHE_LINE_SHIFT 6 /* bytes per L1 cache line */
#endif

#ifndef L1_CACHE_ADDR
#define L1_CACHE_ADDR   0xFFD00000
#endif
#ifndef L1_CACHE_SZ
#define L1_CACHE_SZ     (16 * 1024)
#endif
#ifndef L1_CACHE_LINE_SHIFT
#define L1_CACHE_LINE_SHIFT 5 /* bytes per L1 cache line */
#endif


/* MMU Assist Registers */
#define MAS0     0x270
#define MAS1     0x271
#define MAS2     0x272
#define MAS3     0x273
#define MAS6     0x276
#define MAS7     0x3B0
#define MMUCSR0  0x3F4 /* MMU control and status register 0 */

/* L1 Cache */
#define L1CFG0   0x203 /* L1 Cache Configuration Register 0 */
#define L1CSR0   0x3F2 /* L1 Data */
#define L1CSR1   0x3F3 /* L1 Instruction */
#define L1CSR_CPE  0x00010000 /* cache parity enable */
#define L1CSR_CLFR 0x00000100 /* cache lock bits flash reset */
#define L1CSR_CFI  0x00000002 /* cache flash invalidate */
#define L1CSR_CE   0x00000001 /* cache enable */

#define SCCSRBAR  0x3FE /* Shifted CCSRBAR */

#define SPRN_DBSR 0x130 /* Debug Status Register */
#define SPRN_DEC  0x016 /* Decrement Register */
#define SPRN_TSR  0x3D8 /* Timer Status Register */

#define SPRN_TCR  0x3DA /* Timer Control Register */
#define TCR_WIE   0x08000000 /* Watchdog Interrupt Enable */
#define TCR_DIE   0x04000000 /* Decrement Interrupt Enable */

#define SPRN_ESR  0x3D4 /* Exception Syndrome Register */
#define SPRN_MCSR 0x23C /* Machine Check Syndrome Register */
#define SPRN_PVR  0x11F /* Processor Version */
#define SPRN_SVR  0x3FF /* System Version */
#define SPRN_HDBCR0 0x3D0

/* Hardware Implementation-Dependent Registers */
#define SPRN_HID0   0x3F0
#define HID0_TBEN   (1 << 14) /* Time base enable */
#define HID0_ENMAS7 (1 << 7)  /* Enable hot-wire update of MAS7 register */
#define HID0_EMCP   (1 << 31) /* Enable machine check pin */

#define SPRN_HID1   0x3F1
#define HID1_RFXE   (1 << 17) /* Read Fault Exception Enable */
#define HID1_ASTME  (1 << 13) /* Address bus streaming mode */
#define HID1_ABE    (1 << 12) /* Address broadcast enable */
#define HID1_MBDD   (1 << 6)  /* optimized sync instruction */


/* Interrupt Vector Offset Register */
#define IVOR(n) (0x190+(n))
#define IVPR     0x03F   /* Interrupt Vector Prefix Register */

/* Guest Interrupt Vectors */
#define GIVOR2 (0x1B8)
#define GIVOR3 (0x1B9)
#define GIVOR4 (0x1BA)
#define GIVOR8 (0x1BB)
#define GIVOR13 (0x1BC)
#define GIVOR14 (0x1BD)
#define GIVOR35 (0x1D1)

#define SRR0     0x01A   /* Save/Restore Register 0 */
#define SRR1     0x01B   /* Save/Restore Register 1 */

#define MSR_DS   (1<<4)  /* Book E Data address space */
#define MSR_IS   (1<<5)  /* Book E Instruction address space */
#define MSR_DE   (1<<9)  /* Debug Exception Enable */
#define MSR_ME   (1<<12) /* Machine check enable */
#define MSR_CE   (1<<17) /* Critical interrupt enable */
#define MSR_PR   (1<<14) /* User mode (problem state) */

/* Branch prediction */
#define SPRN_BUCSR    0x3F5      /* Branch Control and Status Register */
#define BUCSR_STAC_EN 0x01000000 /* Segment target addr cache enable */
#define BUCSR_LS_EN   0x00400000 /* Link stack enable */
#define BUCSR_BBFI    0x00000200 /* Branch buffer flash invalidate */
#define BUCSR_BPEN    0x00000001 /* Branch prediction enable */
#define BUCSR_ENABLE (BUCSR_STAC_EN | BUCSR_LS_EN | BUCSR_BBFI | BUCSR_BPEN)


/* MMU Assist Registers
 * E6500RM 2.13.10
 * E500CORERM 2.12.5
 */
#define MAS0_TLBSEL_MSK 0x30000000
#define MAS0_TLBSEL(x)  (((x) << 28) & MAS0_TLBSEL_MSK)
#define MAS0_ESEL_MSK   0x0FFF0000
#define MAS0_ESEL(x)    (((x) << 16) & MAS0_ESEL_MSK)
#define MAS0_NV(x)      ((x) & 0x00000FFF)

#define MAS1_VALID      0x80000000
#define MAS1_IPROT      0x40000000 /* can not be invalidated by tlbivax */
#define MAS1_TID(x)     (((x) << 16) & 0x3FFF0000)
#define MAS1_TS         0x00001000

#define MAS2_EPN        0xFFFFF000 /* Effective page number */
#define MAS2_X0         0x00000040
#define MAS2_X1         0x00000020
#define MAS2_W          0x00000010 /* Write-through */
#define MAS2_I          0x00000008 /* Caching-inhibited */
#define MAS2_M          0x00000004 /* Memory coherency required */
#define MAS2_G          0x00000002 /* Guarded */
#define MAS2_E          0x00000001 /* Endianness - 0=big, 1=little */

#define MAS3_RPN        0xFFFFF000 /* Real page number */
/* User attribute bits */
#define MAS3_U0         0x00000200
#define MAS3_U1         0x00000100
#define MAS3_U2         0x00000080
#define MAS3_U3         0x00000040
#define MAS3_UX         0x00000020
/* User and supervisor read, write, and execute permission bits */
#define MAS3_SX         0x00000010
#define MAS3_UW         0x00000008
#define MAS3_SW         0x00000004
#define MAS3_UR         0x00000002
#define MAS3_SR         0x00000001

#define MAS7_RPN        0xFF000000 /* Real page number - upper 8-bits */

#define SPRN_TLB0CFG    0x2B0 /* TLB 0 Config Register */
#define SPRN_TLB1CFG    0x2B1 /* TLB 1 Config Register */
#define TLBNCFG_NENTRY_MASK 0x00000FFF
#define TLBIVAX_ALL     4
#define TLBIVAX_TLB0    0


#define BOOKE_MAS0(tlbsel, esel, nv) \
        (MAS0_TLBSEL(tlbsel) | MAS0_ESEL(esel) | MAS0_NV(nv))
#define BOOKE_MAS1(v,iprot,tid,ts,tsize) \
        ((((v) << 31) & MAS1_VALID)         | \
        (((iprot) << 30) & MAS1_IPROT)      | \
        (MAS1_TID(tid))                     | \
        (((ts) << 12) & MAS1_TS)            | \
        (MAS1_TSIZE(tsize)))
#define BOOKE_MAS2(epn, wimge) \
        (((epn) & MAS2_EPN) | (wimge))
#define BOOKE_MAS3(rpn, user, perms) \
        (((rpn) & MAS3_RPN) | (user) | (perms))
#define BOOKE_MAS7(rpn) \
        (((unsigned long long)(rpn) >> 32) & MAS7_RPN)

#define MTSPR(rn, v) asm volatile("mtspr " rn ",%0" : : "r" (v))



/* L2 Cache */
#define L2_BASE         (CCSRBAR + 0x20000)
#define L2CTL           (L2_BASE + 0x000) /* 0xFFE20000 - L2 control register */
#define L2SRBAR0        (L2_BASE + 0x100) /* 0xFFE20100 - L2 SRAM base address register */

#define L2CTL_EN        (1 << 31) /* L2 enable */
#define L2CTL_INV       (1 << 30) /* L2 invalidate */
#define L2CTL_L2SRAM(n) (((n) & 0x7) << 16) /* 1=all 256KB, 2=128KB */



#ifndef __ASSEMBLER__

/* The data barrier / coherency safe functions for reading and writing */
static inline int get8(const volatile unsigned char *addr)
{
    int ret;
    __asm__ __volatile__(
        "sync;"
        "lbz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync" : "=r" (ret) : "m" (*addr));
    return ret;
}
static inline void set8(volatile unsigned char *addr, int val)
{
    __asm__ __volatile__(
        "stb%U0%X0 %1,%0;"
        "eieio" : "=m" (*addr) : "r" (val)
    );
}

static inline unsigned get32(const volatile unsigned *addr)
{
    unsigned ret;
    __asm__ __volatile__(
        "sync;"
        "lwz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync" : "=r" (ret) : "m" (*addr)
    );
    return ret;
}
static inline void set32(volatile unsigned *addr, int val)
{
    __asm__ __volatile__(
        "sync;"
        "stw%U0%X0 %1,%0" : "=m" (*addr) : "r" (val)
    );
}

/* C version in boot_ppc.c */
extern void set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint64_t rpn,
    uint8_t perms, uint8_t wimge, uint8_t ts, uint8_t tsize, uint8_t iprot);

#else
/* Assembly version */
#define set_tlb(tlb, esel, epn, rpn, urpn, perms, winge, ts, tsize, iprot, reg) \
    lis   reg, BOOKE_MAS0(tlb, esel, 0)@h; \
    ori   reg, reg, BOOKE_MAS0(tlb, esel, 0)@l; \
    mtspr MAS0, reg;\
    lis   reg, BOOKE_MAS1(1, iprot, 0, ts, tsize)@h; \
    ori   reg, reg, BOOKE_MAS1(1, iprot, 0, ts, tsize)@l; \
    mtspr MAS1, reg; \
    lis   reg, BOOKE_MAS2(epn, winge)@h; \
    ori   reg, reg, BOOKE_MAS2(epn, winge)@l; \
    mtspr MAS2, reg; \
    lis   reg, BOOKE_MAS3(rpn, 0, perms)@h; \
    ori   reg, reg, BOOKE_MAS3(rpn, 0, perms)@l; \
    mtspr MAS3, reg; \
    lis   reg, urpn@h; \
    ori   reg, reg, urpn@l; \
    mtspr MAS7, reg; \
    isync; \
    msync; \
    tlbwe; \
    isync;

/* L1 Cache: Invalidate/Reset
 * l1cstr: SPRN_L1CSR1=instruction, SPRN_L1CSR0=data */
#define l1_cache_invalidate(l1csr) \
        lis   2, (L1CSR_CFI | L1CSR_CLFR)@h; \
        ori   2, 2, (L1CSR_CFI | L1CSR_CLFR)@l; \
        mtspr l1csr, 2; \
1: \
        mfspr 3, l1csr; \
        and.  1, 3, 2; \
        bne   1b;

/* L1 Cache: Enable with Parity
 * l1cstr: SPRN_L1CSR1=instruction, SPRN_L1CSR0=data */
#define l1_cache_enable(l1csr) \
        lis   3, (L1CSR_CPE | L1CSR_CE)@h; \
        ori   3, 3, (L1CSR_CPE | L1CSR_CE)@l; \
        mtspr l1csr, 3; \
        isync; \
1: \
        mfspr 3, l1csr; \
        andi. 1, 3, L1CSR_CE@l; \
        beq 1b;

#endif


#endif /* !_NXP_PPC_H_ */
