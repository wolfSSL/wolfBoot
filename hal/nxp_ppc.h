/* nxp_ppc.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef _NXP_PPC_H_
#define _NXP_PPC_H_

#ifdef TARGET_nxp_p1021
    /* NXP P1021 */
    #define CPU_NUMCORES 2
    #define CORE_E500
    #define LAW_MAX_ENTRIES 12

    #define CCSRBAR_DEF (0xFF700000UL) /* P1021RM 4.3 default base */
    #define CCSRBAR_SIZE BOOKE_PAGESZ_1M

    #define ENABLE_DDR
    #ifndef DDR_SIZE
    #define DDR_SIZE (512UL * 1024UL * 1024UL)
    #endif

    /* Memory used for transferring blocks to/from NAND.
     * Maps to eLBC FCM internal 8KB region (by hardware) */
    #define FLASH_BASE_ADDR 0xFC000000UL

    #ifdef BUILD_LOADER_STAGE1
        /* First stage loader features */

        #define ENABLE_L2_CACHE
        #define L2SRAM_ADDR   (0xF8F80000UL) /* L2 as SRAM */
        #define L2SRAM_SIZE   (256UL * 1024UL)

        #define INITIAL_SRAM_ADDR     L2SRAM_ADDR
        #define INITIAL_SRAM_LAW_SZ   LAW_SIZE_256KB
        #define INITIAL_SRAM_LAW_TRGT LAW_TRGT_ELBC
        #define INITIAL_SRAM_BOOKE_SZ BOOKE_PAGESZ_256K
    #else
        /* For wolfBoot features */
        #define ENABLE_L1_CACHE
        #define ENABLE_L2_CACHE

        /* Relocate CCSRBAR */
        #define CCSRBAR 0xFFE00000UL

        #define ENABLE_INTERRUPTS
    #endif

#elif defined(TARGET_nxp_t1024)
    /* NXP T1024 */
    #define CORE_E5500
    #define CPU_NUMCORES 2
    #define CORES_PER_CLUSTER 1
    #define LAW_MAX_ENTRIES 16

    #define CCSRBAR_DEF (0xFE000000) /* T1024RM 4.4.1 default base */
    #define CCSRBAR_SIZE BOOKE_PAGESZ_16M

    #define INITIAL_SRAM_ADDR     0xFDFC0000
    #define INITIAL_SRAM_LAW_SZ   LAW_SIZE_256KB
    #define INITIAL_SRAM_LAW_TRGT LAW_TRGT_PSRAM
    #define INITIAL_SRAM_BOOKE_SZ BOOKE_PAGESZ_256K

    #define ENABLE_L1_CACHE
    #define ENABLE_INTERRUPTS

    #ifdef BUILD_LOADER_STAGE1
        #define ENABLE_L2_CACHE /* setup and enable L2 in first stage only */
    #else
        /* relocate to 64-bit 0xF_ */
        #define CCSRBAR_PHYS_HIGH 0xFULL
        #define CCSRBAR_PHYS (CCSRBAR_PHYS_HIGH + CCSRBAR_DEF)
    #endif

    #define ENABLE_DDR
    #ifndef DDR_SIZE
    #define DDR_SIZE (2048ULL * 1024ULL * 1024ULL)
    #endif

    #define FLASH_BASE_ADDR      0xEC000000UL
    #define FLASH_BASE_PHYS_HIGH 0xFULL
    #define FLASH_LAW_SIZE       LAW_SIZE_64MB
    #define FLASH_TLB_PAGESZ     BOOKE_PAGESZ_64M

    #define USE_LONG_JUMP

#elif defined(TARGET_nxp_t2080)
    /* NXP T0280 */
    #define CORE_E6500
    #define CPU_NUMCORES 4
    #define CORES_PER_CLUSTER 4
    #define LAW_MAX_ENTRIES 32
    #define ENABLE_PPC64

    #define CCSRBAR_DEF (0xFE000000UL) /* T2080RM 4.3.1 default base */
    #define CCSRBAR_SIZE BOOKE_PAGESZ_16M

    /* relocate to 64-bit 0xE_ */
    //#define CCSRBAR_PHYS_HIGH 0xEULL
    //#define CCSRBAR_PHYS (CCSRBAR_PHYS_HIGH + CCSRBAR_DEF)

    #define ENABLE_L1_CACHE
    #define ENABLE_L2_CACHE

    #define L2SRAM_ADDR   (0xF8F80000UL) /* L2 as SRAM */
    #define L2SRAM_SIZE   (256UL * 1024UL)

    #define INITIAL_SRAM_ADDR     L2SRAM_ADDR
    #define INITIAL_SRAM_LAW_SZ   LAW_SIZE_256KB
    #define INITIAL_SRAM_LAW_TRGT LAW_TRGT_DDR_1
    #define INITIAL_SRAM_BOOKE_SZ BOOKE_PAGESZ_256K

    #define ENABLE_INTERRUPTS

    #define ENABLE_DDR
    #ifndef DDR_SIZE
    #define DDR_SIZE (8192UL * 1024UL * 1024UL)
    #endif

    #define FLASH_BASE_ADDR      0xE8000000UL
    #define FLASH_BASE_PHYS_HIGH 0x0ULL
    #define FLASH_LAW_SIZE       LAW_SIZE_128MB
    #define FLASH_TLB_PAGESZ     BOOKE_PAGESZ_128M

    #define USE_LONG_JUMP
#else
    #error Please define platform PowerPC core version and CCSRBAR
#endif



/* boot address */
#ifndef BOOT_ROM_ADDR
#define BOOT_ROM_ADDR 0xFFFFF000UL
#endif
#ifndef BOOT_ROM_SIZE
#define BOOT_ROM_SIZE (4UL*1024UL)
#endif

/* reset vector */
#define RESET_VECTOR (BOOT_ROM_ADDR + (BOOT_ROM_SIZE - 4))

/* CCSRBAR */
#ifndef CCSRBAR_DEF
#define CCSRBAR_DEF  0xFE000000UL
#endif
#ifndef CCSRBAR
#define CCSRBAR      CCSRBAR_DEF
#endif
#ifndef CCSRBAR_PHYS
#define CCSRBAR_PHYS CCSRBAR
#endif
#ifndef CCSRBAR_PHYS_HIGH
#define CCSRBAR_PHYS_HIGH 0
#endif

/* DDR */
#ifndef DDR_ADDRESS
#define DDR_ADDRESS  0x00000000UL
#endif

/* L1 */
#ifndef L1_CACHE_SZ
#define L1_CACHE_SZ     (32 * 1024)
#endif

#if defined(CORE_E500) || defined(CORE_E5500)
    /* E500CORERM: 2.12.5.2 MAS Register 1 (MAS1)
     * E5500RM: 2.16.6.2 MAS Register 1 (MAS1) */
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
#elif defined(CORE_E6500)
    /* E6500RM: 2.13.10.2 MMU Assist 1 (MAS1)
     * EREF 2.0: 6.5.3.2 - TLB Entry Page Size */
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
#endif

#ifdef CORE_E500
    /* PowerPC e500 */

    #define CACHE_LINE_SHIFT 5 /* 32 bytes per L1 cache line */

    /* P1021 LAW - Local Access Window (Memory Map) - RM 2.4 */
    #define LAWBAR_BASE(n) (0xC08 + (n * 0x20))
    #define LAWBAR(n)      ((volatile uint32_t*)(CCSRBAR + LAWBAR_BASE(n) + 0x0))
    #define LAWAR(n)       ((volatile uint32_t*)(CCSRBAR + LAWBAR_BASE(n) + 0x8))

    #define LAWAR_ENABLE      (1<<31)
    #define LAWAR_TRGT_ID(id) (id<<20)

    /* P1021 Global Source/Target ID Assignments - RM Table 2-7 */
    #define LAW_TRGT_PCIE2 0x01
    #define LAW_TRGT_PCIE1 0x02
    #define LAW_TRGT_ELBC  0x04 /* eLBC (Enhanced Local Bus Controller) */
    #define LAW_TRGT_DDR   0x0F /* DDR Memory Controller */

    /* P1021 2.4.2 - size is equal to 2^(enum + 1) */
    #define LAW_SIZE_4KB   0x0B
    #define LAW_SIZE_8KB   0x0C
    #define LAW_SIZE_16KB  0x0D
    #define LAW_SIZE_32KB  0x0E
    #define LAW_SIZE_64KB  0x0F
    #define LAW_SIZE_128KB 0x10
    #define LAW_SIZE_256KB 0x11
    #define LAW_SIZE_512KB 0x12
    #define LAW_SIZE_1MB   0x13
    #define LAW_SIZE_2MB   0x14
    #define LAW_SIZE_4MB   0x15
    #define LAW_SIZE_8MB   0x16
    #define LAW_SIZE_16MB  0x17
    #define LAW_SIZE_32MB  0x18
    #define LAW_SIZE_64MB  0x19
    #define LAW_SIZE_128MB 0x1A
    #define LAW_SIZE_256MB 0x1B
    #define LAW_SIZE_512MB 0x1C
    #define LAW_SIZE_1GB   0x1D
    #define LAW_SIZE_2GB   0x1E
    #define LAW_SIZE_4GB   0x1F
    #define LAW_SIZE_8GB   0x20
    #define LAW_SIZE_16GB  0x21
    #define LAW_SIZE_32GB  0x22


#elif defined(CORE_E6500) || defined(CORE_E5500)
    /* PowerPC e5500/e6500 */

    /* CoreNet on-chip interface between the core cluster and rest of SoC */
    #define USE_CORENET_INTERFACE
    #define HAS_EMBEDDED_HYPERVISOR /* E.HV Supported */

    #define CACHE_LINE_SHIFT 6 /* 64 bytes per L1 cache line */

    /* CoreNet Platform Cache Base */
    #define CPC_BASE            (CCSRBAR + 0x10000)
    /* 8.2 CoreNet Platform Cache (CPC) Memory Map */
    #define CPCCSR0             (0x000)
    #define CPCSRCR1            (0x100)
    #define CPCSRCR0            (0x104)
    #define CPCHDBCR0           (0xF00)

    #define CPCCSR0_CPCE        (0x80000000 >> 0)
    #define CPCCSR0_CPCPE       (0x80000000 >> 1)
    #define CPCCSR0_CPCFI       (0x80000000 >> 10)
    #define CPCCSR0_CPCLFC      (0x80000000 >> 21)
    #define CPCCSR0_SRAM_ENABLE (CPCCSR0_CPCE | CPCCSR0_CPCPE)

    #ifdef CORE_E6500
        #define CPCSRCR0_SRAMSZ_64  (0x1 << 1) /* ways 14-15 */
        #define CPCSRCR0_SRAMSZ_256 (0x3 << 1) /* ways 8-15 */
        #define CPCSRCR0_SRAMSZ_512 (0x4 << 1) /* ways 0-15 */
    #else /* CORE E5500 */
        #define CPCSRCR0_SRAMSZ_64  (0x1 << 1) /* ways 6-7 */
        #define CPCSRCR0_SRAMSZ_128 (0x2 << 1) /* ways 4-7 */
        #define CPCSRCR0_SRAMSZ_256 (0x3 << 1) /* ways 0-7 */
    #endif
    #define CPCSRCR0_SRAMEN     (0x1)

    #define CPCHDBCR0_SPEC_DIS  (0x80000000 >> 4)

    #define CORENET_DCSR_SZ_1G  0x3

    /* T1024/T2080 LAW - Local Access Window (Memory Map) - RM 2.4 */
    #define LAWBAR_BASE(n) (0xC00 + (n * 0x10))
    #define LAWBARH(n)     ((volatile uint32_t*)(CCSRBAR + LAWBAR_BASE(n) + 0x0))
    #define LAWBARL(n)     ((volatile uint32_t*)(CCSRBAR + LAWBAR_BASE(n) + 0x4))
    #define LAWAR(n)       ((volatile uint32_t*)(CCSRBAR + LAWBAR_BASE(n) + 0x8))

    #define LAWAR_ENABLE      (1<<31)
    #define LAWAR_TRGT_ID(id) (id<<20)

    /* T1024/T2080 Global Source/Target ID Assignments - RM Table 2-1 */
    #define LAW_TRGT_PCIE1   0x00
    #define LAW_TRGT_PCIE2   0x01
    #define LAW_TRGT_PCIE3   0x02
    #define LAW_TRGT_DDR_1   0x10 /* Memory Complex 1 */
    #define LAW_TRGT_DDR_2   0x11
    #define LAW_TRGT_BMAN    0x18 /* Buffer Manager (control) */
    #define LAW_TRGT_DCSR    0x1D /* debug facilities */
    #define LAW_TRGT_CORENET 0x1E /* CCSR */
    #define LAW_TRGT_IFC     0x1F /* Integrated Flash Controller */
    #define LAW_TRGT_QMAN    0x3C /* Queue Manager (control) */
    #define LAW_TRGT_PSRAM   0x4A /* 160 KB Platform SRAM */

    /* T1024/T2080 2.4.3 - size is equal to 2^(enum + 1) */
    #define LAW_SIZE_4KB   0x0B
    #define LAW_SIZE_8KB   0x0C
    #define LAW_SIZE_16KB  0x0D
    #define LAW_SIZE_32KB  0x0E
    #define LAW_SIZE_64KB  0x0F
    #define LAW_SIZE_128KB 0x10
    #define LAW_SIZE_256KB 0x11
    #define LAW_SIZE_512KB 0x12
    #define LAW_SIZE_1MB   0x13
    #define LAW_SIZE_2MB   0x14
    #define LAW_SIZE_4MB   0x15
    #define LAW_SIZE_8MB   0x16
    #define LAW_SIZE_16MB  0x17
    #define LAW_SIZE_32MB  0x18
    #define LAW_SIZE_64MB  0x19
    #define LAW_SIZE_128MB 0x1A
    #define LAW_SIZE_256MB 0x1B
    #define LAW_SIZE_512MB 0x1C
    #define LAW_SIZE_1GB   0x1D
    #define LAW_SIZE_2GB   0x1E
    #define LAW_SIZE_4GB   0x1F
    #define LAW_SIZE_8GB   0x20
    #define LAW_SIZE_16GB  0x21
    #define LAW_SIZE_32GB  0x22
    #define LAW_SIZE_64GB  0x23
    #define LAW_SIZE_128GB 0x24
    #define LAW_SIZE_256GB 0x25
    #define LAW_SIZE_512GB 0x26
    #define LAW_SIZE_1TB   0x27
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE (1 << CACHE_LINE_SHIFT)
#endif


/* MMU Assist Registers
 * E6500RM 2.13.10
 * E5500RM 2.16.6
 * E500CORERM 2.12.5
 */
#define MAS0     0x270
#define MAS1     0x271
#define MAS2     0x272
#define MAS3     0x273
#define MAS6     0x276
#define MAS7     0x3B0
#define MAS8     0x155
#define MMUCSR0  0x3F4 /* MMU control and status register 0 */

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


/* L1 Cache */
#define L1CFG0     0x203 /* L1 Cache Configuration Register 0 */
#define L1CSR2     0x25E /* L1 Data Cache Control and Status Register 2 */
#define L1CSR0     0x3F2 /* L1 Data */
#define L1CSR1     0x3F3 /* L1 Instruction */

#define L1CSR_CPE  0x00010000 /* cache parity enable */
#define L1CSR_CLFC 0x00000100 /* cache lock bits flash clear */
#define L1CSR_CFI  0x00000002 /* cache flash invalidate */
#define L1CSR_CE   0x00000001 /* cache enable */

/* L2 Cache */
#if defined(CORE_E6500)
    /* L2 Cache Control - E6500CORERM 2.2.3 Memory-mapped registers (MMRs) */
    #define L2_CLUSTER_BASE(n) (CCSRBAR + 0xC20000 + (n * 0x40000))
    #define L2PID(n)           (0x200 + (n * 0x10)) /* L2 Cache Partitioning ID */
    #define L2PIR(n)           (0x208 + (n * 0x10)) /* L2 Cache Partitioning Allocation */
    #define L2PWR(n)           (0x20C + (n * 0x10)) /* L2 Cache Partitioning Way */

    /* MMRs */
    #define L2CSR0    0x000 /* L2 Cache Control and Status 0 */
    #define L2CSR1    0x004 /* L2 Cache Control and Status 1 */
    #define L2CFG0    0x008 /* L2 Cache Configuration */
#else
    #ifdef CORE_E5500
        /* L2 Cache Control - E5500RM 2.15 L2 Cache Registers */
        #define L2_BASE         (CCSRBAR + 0x20000)
    #else
        /* E500 */
        #define L2_BASE         (CCSRBAR + 0x20000)
        #define L2CTL           0x000 /* 0xFFE20000 - L2 control register */
        #define L2SRBAR0        0x100 /* 0xFFE20100 - L2 SRAM base address register */

        #define L2CTL_EN        (1 << 31) /* L2 enable */
        #define L2CTL_INV       (1 << 30) /* L2 invalidate */
        #define L2CTL_SIZ(n)    (((n) & 0x3) << 28) /* 2=256KB (always) */
        #define L2CTL_L2SRAM(n) (((n) & 0x7) << 16) /* 1=all 256KB, 2=128KB */
    #endif

    /* SPR */
    #define L2CFG0    0x207 /* L2 Cache Configuration Register 0 */
    #define L2CSR0    0x3F9 /* L2 Data Cache Control and Status Register 0 */
    #define L2CSR1    0x3FA /* L2 Data Cache Control and Status Register 1 */
#endif

#define L2CSR0_L2FI  0x00200000 /* L2 Cache Flash Invalidate */
#define L2CSR0_L2FL  0x00000800 /* L2 Cache Flush */
#define L2CSR0_L2LFC 0x00000400 /* L2 Cache Lock Flash Clear */
#define L2CSR0_L2PE  0x40000000 /* L2 Cache Parity/ECC Enable */
#define L2CSR0_L2E   0x80000000 /* L2 Cache Enable */

#define L2CSR0_L2WP  0x1c000000 /* L2 I/D Way Partioning */
#define L2CSR0_L2CM  0x03000000 /* L2 Cache Coherency Mode */
#define L2CSR0_L2FI  0x00200000 /* L2 Cache Flash Invalidate */
#define L2CSR0_L2IO  0x00100000 /* L2 Cache Instruction Only */
#define L2CSR0_L2DO  0x00010000 /* L2 Cache Data Only */
#define L2CSR0_L2REP 0x00003000 /* L2 Line Replacement Algo */


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
#define HID0_TBCLK  (1 << 13) /* select clock: 0=every 8 ccb clocks, 1=rising edge of RTC */
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
#define GIVOR2  (0x1B8)
#define GIVOR3  (0x1B9)
#define GIVOR4  (0x1BA)
#define GIVOR8  (0x1BB)
#define GIVOR13 (0x1BC)
#define GIVOR14 (0x1BD)
#define GIVOR35 (0x1D1)

#define SRR0     0x01A   /* Save/Restore Register 0 */
#define SRR1     0x01B   /* Save/Restore Register 1 */

#define MSR_DS   (1<<4)  /* Book E Data address space */
#define MSR_IS   (1<<5)  /* Book E Instruction address space */
#define MSR_DE   (1<<9)  /* Debug Exception Enable */
#define MSR_ME   (1<<12) /* Machine check enable */
#define MSR_EE   (1<<15) /* External Interrupt enable */
#define MSR_CE   (1<<17) /* Critical interrupt enable */
#define MSR_PR   (1<<14) /* User mode (problem state) */

/* Branch prediction */
#define SPRN_BUCSR    0x3F5      /* Branch Control and Status Register */
#define BUCSR_STAC_EN 0x01000000 /* Segment target addr cache enable */
#define BUCSR_LS_EN   0x00400000 /* Link stack enable */
#define BUCSR_BBFI    0x00000200 /* Branch buffer flash invalidate */
#define BUCSR_BPEN    0x00000001 /* Branch prediction enable */
#define BUCSR_ENABLE (BUCSR_STAC_EN | BUCSR_LS_EN | BUCSR_BBFI | BUCSR_BPEN)

#define SPRN_PID      0x030 /* Process ID */
#define SPRN_PIR      0x11E /* Processor Identification Register */

#define SPRN_TBWL     0x11C /* Time Base Write Lower Register */
#define SPRN_TBWU     0x11D /* Time Base Write Upper Register */

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
#define BOOKE_MAS7(urpn) (urpn)

/* Stringification */
#ifndef WC_STRINGIFY
#define _WC_STRINGIFY_L2(str) #str
#define WC_STRINGIFY(str) _WC_STRINGIFY_L2(str)
#endif

#define mtspr(rn, v) __asm__ __volatile__("mtspr " WC_STRINGIFY(rn) ",%0" : : "r" (v))

#define mfmsr() ({ \
    unsigned int rval; \
    __asm__ __volatile__("mfmsr %0" : "=r" (rval)); rval; \
})
#define mtmsr(v)     __asm__ __volatile__("mtmsr %0" : : "r" (v))


#ifndef __ASSEMBLER__

/* The data barrier / coherency safe functions for reading and writing */
static inline int get8(const volatile unsigned char *addr)
{
    int ret;
    __asm__ __volatile__(
        "sync;\n"
        "lbz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync"
            : "=r" (ret) : "m" (*addr)
    );
    return ret;
}
static inline void set8(volatile unsigned char *addr, int val)
{
    __asm__ __volatile__(
        "stb%U0%X0 %1,%0;\n"
        "eieio"
            : "=m" (*addr) : "r" (val)
    );
}

static inline int get16(const volatile unsigned short *addr)
{
    int ret;
    __asm__ __volatile__(
        "sync;\n"
        "lhz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync"
            : "=r" (ret) : "m" (*addr)
    );
    return ret;
}
static inline void set16(volatile unsigned short *addr, int val)
{
    __asm__ __volatile__(
        "sync;\n"
        "sth%U0%X0 %1,%0"
            : "=m" (*addr) : "r" (val)
    );
}

static inline unsigned int get32(const volatile unsigned int *addr)
{
    unsigned int ret;
    __asm__ __volatile__(
        "sync;\n"
        "lwz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync"
            : "=r" (ret) : "m" (*addr)
    );
    return ret;
}
static inline void set32(volatile unsigned int *addr, unsigned int val)
{
    __asm__ __volatile__(
        "sync;"
        "stw%U0%X0 %1,%0"
            : "=m" (*addr) : "r" (val)
    );
}

/* C version in boot_ppc.c */
extern void set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint32_t rpn,
    uint32_t urpn, uint8_t perms, uint8_t wimge, uint8_t ts, uint8_t tsize,
    uint8_t iprot);
extern void disable_tlb1(uint8_t esel);
extern void flush_cache(uint32_t start_addr, uint32_t size);
extern void set_law(uint8_t idx, uint32_t addr_h, uint32_t addr_l,
    uint32_t trgt_id, uint32_t law_sz, int reset);

/* from hal/nxp_*.c */
extern void uart_init(void);

/* from boot_ppc_start.S */
extern unsigned long long get_ticks(void);
extern void wait_ticks(unsigned long long);
extern unsigned long get_pc(void);
extern void relocate_code(uint32_t *dest, uint32_t *src, uint32_t length);
extern void invalidate_dcache(void);
extern void invalidate_icache(void);
extern void icache_enable(void);
extern void dcache_enable(void);
extern void dcache_disable(void);

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

    #define r25 25
    #define r26 26
    #define r27 27
    #define r28 28
    #define r29 29
    #define r30 30
    #define r31 31
#endif

/* ePAPR 1.1 spin table */
/* For multiple core spin table communication */
/* The spin table must be WING 0b001x (memory-coherence required) */
/* For older PPC compat use dcbf to flush spin table entry */
/* Note: spin-table must be cache-line aligned in memory */
#define EPAPR_MAGIC       (0x45504150) /* Book III-E CPUs */
#define ENTRY_ADDR_UPPER  0
#define ENTRY_ADDR_LOWER  4
#define ENTRY_R3_UPPER    8
#define ENTRY_R3_LOWER    12
#define ENTRY_RESV        16
#define ENTRY_PIR         20

/* not used for ePAPR 1.1 */
#define ENTRY_R6_UPPER    24
#define ENTRY_R6_LOWER    28


#define ENTRY_SIZE        64

#endif /* !_NXP_PPC_H_ */
