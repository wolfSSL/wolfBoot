/* nxp_t1024.c
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
#include <stdint.h>
#include "target.h"
#include "printf.h"
#include "string.h"
#include "hal.h"
#include "nxp_ppc.h"
#include "fdt.h"
#include "pci.h"

/* Tested on T1024E Rev 1.0, e5500 core 2.1, PVR 8024_1021 and SVR 8548_0010 */
/* IFC: CS0 NOR, CS1 MRAM, CS2 APU CPLD, CS3, MPU CPLD */
/* DDR: DDR4 w/ECC (5 chips MT40A256M16GE-083EIT) - SPD on I2C1 at Addr 0x51 */

/* Debugging */
/* #define DEBUG_FLASH */
/* #define DEBUG_ESPI 1 */
/* #define DEBUG_PHY */

#define ENABLE_IFC
#define ENABLE_BUS_CLK_CALC

#ifndef BUILD_LOADER_STAGE1
    /* Tests */
    #if 0
        #define TEST_DDR
        #define TEST_TPM
    #endif

    #define ENABLE_PCIE
    #define ENABLE_CPLD
    #define ENABLE_QE   /* QUICC Engine */
    #define ENABLE_FMAN
    #define ENABLE_PHY
    #define ENABLE_MRAM

    #if defined(WOLFBOOT_TPM) || defined(TEST_TPM)
        #define ENABLE_ESPI /* SPI for TPM */
    #endif
    #define ENABLE_MP   /* multi-core support */

    /* Booting Integrity OS */
    #define RTOS_INTEGRITY_OS
#endif

#define USE_ERRATA_DDRA008378
#define USE_ERRATA_DDRA008109
#define USE_ERRATA_DDRA009663
#define USE_ERRATA_DDRA009942

/* Foward declarations */
#if defined(ENABLE_DDR) && defined(TEST_DDR)
static int test_ddr(void);
#endif
#if defined(ENABLE_ESPI) && defined(TEST_TPM)
static int test_tpm(void);
#endif

static void hal_flash_unlock_sector(uint32_t sector);

#ifdef ENABLE_ESPI
#include "spi_drv.h" /* for transfer flags and chip select */
#endif

/* T1024 */
/* System input clock */
#define SYS_CLK (100000000) /* 100MHz */

/* Boot page translation register - T1024RM 4.5.9 */
#define LCC_BSTRH            ((volatile uint32_t*)(CCSRBAR + 0x20)) /* Boot space translation register high */
#define LCC_BSTRL            ((volatile uint32_t*)(CCSRBAR + 0x24)) /* Boot space translation register low */
#define LCC_BSTAR            ((volatile uint32_t*)(CCSRBAR + 0x28)) /* Boot space translation attribute register */
#define LCC_BSTAR_EN         0x80000000
#define LCC_BSTAR_LAWTRGT(n) ((n) << 20)
#define LCC_BSTAR_LAWSZ(n)   ((n) & 0x3F)

/* DCFG (Device Configuration/Pin Control) T1024RM 7.3 */
#define DCSRBAR_BASE_HIGH 0xF
#define DCSRBAR_BASE      0xF0000000

#define DCFG_BASE      (CCSRBAR + 0xE0000)
#define DCFG_PVR       ((volatile uint32_t*)(DCFG_BASE + 0xA0UL))
#define DCFG_SVR       ((volatile uint32_t*)(DCFG_BASE + 0xA4UL))
#define DCFG_DEVDISR1  ((volatile uint32_t*)(DCFG_BASE + 0x70UL)) /* Device disable register */
#define DCFG_DEVDISR2  ((volatile uint32_t*)(DCFG_BASE + 0x74UL)) /* Device disable register */
#define DCFG_DEVDISR3  ((volatile uint32_t*)(DCFG_BASE + 0x78UL)) /* Device disable register */
#define DCFG_DEVDISR4  ((volatile uint32_t*)(DCFG_BASE + 0x7CUL)) /* Device disable register */
#define DCFG_DEVDISR5  ((volatile uint32_t*)(DCFG_BASE + 0x80UL)) /* Device disable register */
#define DCFG_COREDISR  ((volatile uint32_t*)(DCFG_BASE + 0x94UL)) /* Core Enable/Disable */
#define DCFG_RCWSR(n)  ((volatile uint32_t*)(DCFG_BASE + 0x100UL + ((n) * 4))) /* Reset Control Word Status Register (0-15) */
#define DCFG_BRR       ((volatile uint32_t*)(DCFG_BASE + 0xE4UL))  /* Boot Release Register (DCFG_CCSR_BRR) */
#define DCFG_DCSR      ((volatile uint32_t*)(DCFG_BASE + 0x704UL)) /* Debug configuration and status */

/* RCW */
#define RCWSR4_SRDS1_PRTCL       0xFF800000
#define RCWSR4_SRDS1_PRTCL_SHIFT 23

/* Logical I/O Device Number */
#define DCFG_USB1LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x520))
#define DCFG_USB2LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x524))
#define DCFG_SDMMCLIODNR  ((volatile uint32_t*)(DCFG_BASE + 0x530))
#define DCFG_SATALIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x550))
#define DCFG_DIULIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x570))
#define DCFG_TDMDMALIODNR ((volatile uint32_t*)(DCFG_BASE + 0x574))
#define DCFG_QELIODNR     ((volatile uint32_t*)(DCFG_BASE + 0x578)) /* QUICC Engine Logical I/O Device Number register */
#define DCFG_DMA1LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x580))
#define DCFG_DMA2LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x584))

/* PCI Express LIODN base register */
#define PCIE_MAX_CONTROLLERS 3
#define PCIE_BASE(n)        (CCSRBAR + 0x240000 + ((n-1) * 0x10000))
#define PCIE_CONFIG_ADDR(n) ((volatile uint32_t*)(PCIE_BASE(n) + 0x00)) /* PEXx_PEX_CONFIG_ADDR - configuration address */
#define PCIE_CONFIG_DATA(n) ((volatile uint32_t*)(PCIE_BASE(n) + 0x04)) /* PEXx_PEX_CONFIG_DATA - configuration data */
#define PCIE_LIODN(n)       ((volatile uint32_t*)(PCIE_BASE(n) + 0x40)) /* PEXx_PEX_LBR */
#define PCIE_BLK_REV1(n)    ((volatile uint32_t*)(PCIE_BASE(n) + 0xBF8)) /* PEXx_PEX_IP_BLK_REV1 */
#define PCIE_BLK_REV2(n)    ((volatile uint32_t*)(PCIE_BASE(n) + 0xBFC)) /* PEXx_PEX_IP_BLK_REV1 */

/* PCIe Output Windows (max 5) */
#define PCIE_OTAR(n, w)     ((volatile uint32_t*)(PCIE_BASE(n) + 0xC00 + ((w) * 32)))  /* PEXx_PEXOTARn  - outbound translation address */
#define PCIE_OTEAR(n, w)    ((volatile uint32_t*)(PCIE_BASE(n) + 0xC04 + ((w) * 32)))  /* PEXx_PEXOTEARn - outbound translation extended address */
#define PCIE_OWBAR(n, w)    ((volatile uint32_t*)(PCIE_BASE(n) + 0xC08 + ((w) * 32)))  /* PEXx_PEXOWBARn - outbound window base address */
#define PCIE_OWAR(n, w)     ((volatile uint32_t*)(PCIE_BASE(n) + 0xC10 + ((w) * 32)))  /* PEXx_PEXOWARn  - outbound window attributes */
#define POWAR_EN            0x80000000
#define POWAR_IO_READ       0x00080000
#define POWAR_MEM_READ      0x00040000
#define POWAR_IO_WRITE      0x00008000
#define POWAR_MEM_WRITE     0x00004000

/* PCIe Input Windows (max 4 - seq is 3,2,1,0) */
#define PCIE_ITAR(n, w)     ((volatile uint32_t*)(PCIE_BASE(n) + 0xD80 + ((3-((w) & 0x3)) * 32)))  /* PEXx_PEXITARn  - inbound translation address */
#define PCIE_IWBAR(n, w)    ((volatile uint32_t*)(PCIE_BASE(n) + 0xD88 + ((3-((w) & 0x3)) * 32)))  /* PEXx_PEXIWBARn - inbound window base address */
#define PCIE_IWBEAR(n, w)   ((volatile uint32_t*)(PCIE_BASE(n) + 0xD8C + ((3-((w) & 0x3)) * 32)))  /* PEXx_PEXIWBEARn- inbound window base extended address */
#define PCIE_IWAR(n, w)     ((volatile uint32_t*)(PCIE_BASE(n) + 0xD90 + ((3-((w) & 0x3)) * 32)))  /* PEXx_PEXIWARn  - inbound window attributes */
#define PIWAR_EN            0x80000000
#define PIWAR_DIEN          0x40000000
#define PIWAR_PF            0x20000000
#define PIWAR_TRGT_PCI1     0x00000000
#define PIWAR_TRGT_PCI2     0x00100000
#define PIWAR_TRGT_PCI3     0x00200000
#define PIWAR_TRGT_CCSR     0x00E00000
#define PIWAR_TRGT_LOCAL    0x00F00000
#define PIWAR_READ          0x00040000
#define PIWAR_READ_SNOOP    0x00050000
#define PIWAR_WRITE         0x00004000
#define PIWAR_WRITE_SNOOP   0x00005000

/* Buffer Manager */
#define BMAN_LIODNR  ((volatile uint32_t*)(BMAN_CCSR_BASE + 0xD08))
#define BCSP_ISDR(n) ((volatile uint32_t*)(BMAN_BASE_PHYS + 0x1000E08 + ((n) * 0x1000)))

/* Frame Queue Descriptor (FQD) */
#define FQD_BAR      ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC04))
#define FQD_AR       ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC10))
/* Packed Frame Descriptor Record (PFDR) */
#define PFDR_BARE    ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC20))
#define PFDR_BAR     ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC24))
#define PFDR_AR      ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC30))

/* QMan */
#define QCSP_BARE    ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC80)) /* Base Address (upper) */
#define QCSP_BAR     ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC84)) /* Base Address */
/* QMan Software Portals */
#define QMAN_LIODNR     ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xD08))
#define QCSP_LIO_CFG(n) ((volatile uint32_t*)(QMAN_CCSR_BASE + 0x1000 + ((n) * 0x10)))
#define QCSP_IO_CFG(n)  ((volatile uint32_t*)(QMAN_CCSR_BASE + 0x1004 + ((n) * 0x10)))

#define QCSP_ISDR(n)    ((volatile uint32_t*)(QMAN_BASE_PHYS + 0x1000E08 + ((n) * 0x1000)))

/* SCGG (Supplemental Configuration Unit) T1024RM 6.1 */
#define SCFG_BASE       (CCSRBAR + 0xFC000)
#define SCFG_QEIOCLKCR  ((volatile uint32_t*)(DCFG_BASE + 0x400UL))
#define SCFG_EMIIOCR    ((volatile uint32_t*)(DCFG_BASE + 0x404UL))
#define SCFG_SDHCIOVSEL ((volatile uint32_t*)(DCFG_BASE + 0x408UL))

#define SCFG_QEIOCLKCR_CLK11 0x04000000 /* IO_CLK[11] = GPIO_4[16] */


/* T1024RM: 4.6.5 */
#define CLOCKING_BASE           (CCSRBAR + 0xE1000)
#define CLOCKING_CLKCCSR(n)     ((volatile uint32_t*)(CLOCKING_BASE + 0x000UL + ((n) * 0x20))) /* Core cluster n clock control/status register */
#define CLOCKING_CLKCGHWACSR(n) ((volatile uint32_t*)(CLOCKING_BASE + 0x010UL + ((n) * 0x20))) /* Clock generator n hardware accelerator control/status */
#define CLOCKING_PLLCNGSR(n)    ((volatile uint32_t*)(CLOCKING_BASE + 0x800UL + ((n) * 0x20))) /* PLL cluster n general status register */
#define CLOCKING_CLKPCSR        ((volatile uint32_t*)(CLOCKING_BASE + 0xA00UL)) /* Platform clock domain control/status register */
#define CLOCKING_PLLPGSR        ((volatile uint32_t*)(CLOCKING_BASE + 0xC00UL)) /* Platform PLL general status register */
#define CLOCKING_PLLDGSR        ((volatile uint32_t*)(CLOCKING_BASE + 0xC20UL)) /* DDR PLL general status register */

#define CLKC0CSR_CLKSEL(n)      (((n) >> 27) & 0xF) /* 0000=Cluster PLL1 Output, 0001=Cluster PKK1 divide-by-2 */
#define PLLCGSR_CGF(n)          (((n) >> 1) & 0x3F) /* Reflects the current PLL multiplier configuration. Indicates the frequency for this PLL */

#define RCPM_BASE       (CCSRBAR + 0xE2000)
#define RCPM_PCTBENR    ((volatile uint32_t*)(RCPM_BASE + 0x1A0)) /* Physical Core Time Base Enable Bit 0=Core 0 */
#define RCPM_PCTBCKSELR ((volatile uint32_t*)(RCPM_BASE + 0x1A4)) /* Physical Core Time Base Clock Select 0=Platform Clock/16, 1=RTC */
#define RCPM_TBCLKDIVR  ((volatile uint32_t*)(RCPM_BASE + 0x1A8)) /* Time Base Clock Divider 0=1/16, 1=1/8, 2=1/24, 3=1/32 */

/* MPIC - T1024RM 24.3 */
#define PIC_BASE    (CCSRBAR + 0x40000)
#define PIC_WHOAMI  ((volatile uint32_t*)(PIC_BASE + 0x0090UL)) /* Returns the ID of the processor core reading this register */
#define PIC_GCR     ((volatile uint32_t*)(PIC_BASE + 0x1020UL)) /* Global configuration register (controls PIC operating mode) */
#define PIC_GCR_RST 0x80000000
#define PIC_GCR_M   0x20000000


/* QUICC Engine */
#define QE_MAX_RISC  1
#define QE_MURAM_SIZE (24 * 1024)

/* QE microcode/firmware address */
#ifndef QE_FW_ADDR
#define QE_FW_ADDR   0xEFE00000 /* location in NOR flash */
#endif


#define QE_BASE                (CCSRBAR + 0x140000)
#define QE_IRAM_IADD           ((volatile uint32_t*)(QE_BASE + 0x000UL))
#define QE_IRAM_IDATA          ((volatile uint32_t*)(QE_BASE + 0x004UL))
#define QE_IRAM_IREADY         ((volatile uint32_t*)(QE_BASE + 0x00CUL))

/* QUICC Engine Interrupt Controller */
#define QEIC_CIMR              ((volatile uint32_t*)(QE_BASE + 0x0A0UL))

/* T1024 -> Two UCCs — UCC1, UCC3 supported - CMX UCC1/3 Clock Route Register */
#define QE_CMXUCR1             ((volatile uint32_t*)(QE_BASE + 0xC0000 + 0x410UL))

/* Baud-Rate Generator Configuration Registers */
#define BRG_BRGC(n)            ((volatile uint32_t*)(QE_BASE + 0xC0000 + 0x640UL + ((n-1) * 0x4)))

#define QE_CP                  (QE_BASE + 0x100UL)  /* Configuration register */
#define QE_CP_CECR             ((volatile uint32_t*)(QE_CP + 0x00)) /* command register */
#define QE_CP_CECDR            ((volatile uint32_t*)(QE_CP + 0x08)) /* data register */
#define QE_CP_CERCR            ((volatile uint16_t*)(QE_CP + 0x38)) /* RAM control register */

#define QE_SDMA                (QE_BASE + 0x4000UL) /* Serial DMA */
#define QE_SDMA_SDSR           ((volatile uint32_t*)(QE_SDMA + 0x00))
#define QE_SDMA_SDMR           ((volatile uint32_t*)(QE_SDMA + 0x04))
#define QE_SDMA_SDAQR          ((volatile uint32_t*)(QE_SDMA + 0x38))
#define QE_SDMA_SDAQMR         ((volatile uint32_t*)(QE_SDMA + 0x3C))
#define QE_SDMA_SDEBCR         ((volatile uint32_t*)(QE_SDMA + 0x44))

#define QE_RSP                 (QE_BASE + 0x4100UL) /* Special Registers */
#define QE_RSP_TIBCR(n, i)     ((volatile uint32_t*)(QE_RSP + ((n) * 0x100) + (i)))
#define QE_RSP_ECCR(n)         ((volatile uint32_t*)(QE_RSP + ((n) * 0x100) + 0xF0))

#define QE_MURAM               (QE_BASE + 0x110000UL) /* 24KB */

#define QE_IRAM_IADD_AIE       0x80000000 /* Auto Increment Enable */
#define QE_IRAM_IADD_BADDR     0x00080000 /* Base Address */
#define QE_IRAM_READY          0x80000000

#define QE_CP_CERCR_CIR        0x0800 /* Common instruction RAM */

#define QE_CR_FLG              0x00010000
#define QE_CR_PROTOCOL_SHIFT   6

#define QE_SDMR_GLB_1_MSK      0x80000000
#define QE_SDMR_CEN_SHIFT      13
#define QE_SDEBCR_BA_MASK      0x01FFFFFF

/* QE Commands */
#define QE_RESET               0x80000000


/* T1024RM 10.5.1: Queue Manager (QMan):
 * - QMan block base address: 31_8000h
 * - 512 frame queue (FQ) cache
 * - 2-Kbyte SFDRs
 * - 256 congestion groups
 */
#define QMAN_CCSR_BASE (CCSRBAR + 0x318000)
#define QMAN_BASE_PHYS_HIGH 0xF
#define QMAN_BASE_PHYS      0xF6000000
#define QMAN_NUM_PORTALS    10

/* T1024RM 10.5.2: Buffer Manager (BMan):
 * - BMan block base address: 31_A000h
 * - 64 buffer pools
 */
#define BMAN_CCSR_BASE (CCSRBAR + 0x31A000)
#define BMAN_BASE_PHYS_HIGH 0xF
#define BMAN_BASE_PHYS      0xF4000000
#define BMAN_NUM_POOLS      64

/* T1024RM 10.5.4: Security and Encryption Engine (SEC)
  * - SEC block base address: 30_0000h
  * - 2.5 Gbps SEC processing at 400 MHz
  * - Cryptographic Hardware Accelerators (CHAs) include:
  *   - PKHA
  *   - DESA
  *   - AESA
  *   - MDHA
  *   - RNG4
  *   - AFHA
  */

/* T1024RM 10.5.3: Frame Manager (FMan):
  * - FMan block base address: 40_0000h
  * - Four multirate Ethernet MACs, for configuration options refer to SerDes Protocols
  * - Block base addresses are as follows:
  *   - FM1 mEMAC1: 4E_0000h
  *   - FM1 mEMAC2: 4E_2000h
  *   - FM1 mEMAC3: 4E_4000h
  *   - FM1 mEMAC4: 4E_6000h
  * - mEMAC PortIDs (RX/TX):
  *   - mEMAC1: 08h/28h
  *   - mEMAC2: 09h/29h
  *   - mEMAC3: 0Ah/2Ah
  *   - mEMAC4: 0Bh/2Bh
  * - Supports 1 host command and 3 offline ports:
  *   - Host command: 02h
  *   - Offline port 3: 03h
  *   - Offline port 4: 04h
  *   - Offline port 5: 05h
  * - FM1 Dedicated MDIO1: 4F_C000h
  * - FM1 Dedicated MDIO2: 4F_D000h
  * - One FMan Controller complexes
  * - 192-Kbyte internal FMan memory
  * - 32-Kbyte FMan Controller configuration data
  * - Up to 32 Keygen schemes
  * - Up to 8 Policer profiles
  * - Up to 32 entries in FMan DMA command queue
  * - Up to 64 TNUMs
  * - Up to 1 FMan debug flows
  */

#define FMAN_COUNT 1

#ifndef FMAN_FW_ADDR
#define FMAN_FW_ADDR   0xEFF00000 /* location in NOR flash */
#endif

#define FMAN_BASE              (CCSRBAR + 0x400000)
#define FMAN_MURAM             (FMAN_BASE)
#define FMAN_MURAM_SIZE        (512 * 1024)

/* Hardware Ports (0-63) 4KB each (256KB total) */
#define FMAN_BMI(n)            ((FMAN_BASE + 0x80000) + ((n) * 0x1000))
#define FMAN_BMI_SPLIODN(n, p) ((volatile uint32_t*)(FMAN_BMI(n) + 0x304 + ((((p) - 1) & 0x3F) * 4)))

#define FMAN_QMI(n)            ((FMAN_BASE + 0x80000) + ((n) * 0x1000) + 0x400)

#define FMAN_DMA               (FMAN_BASE + 0xC2000UL) /* FMan DMA */
#define FMAN_DMA_ENTRIES       (32)
#define FMAN_DMA_PORT_LIODN(n) ((volatile uint32_t*)(FMAN_DMA + 0x60 + (((n) & 0x1F) * 4))) /* FMan DMA portID-LIODN #0..31 register */

#define FMAN_FPM               (FMAN_BASE + 0xC3000UL) /* Frame processing manager (FPM) */

#define FMAN_IRAM              (FMAN_BASE + 0xC4000UL) /* FMan Controller Configuration Data */
#define FMAN_IRAM_IADD         ((volatile uint32_t*)(FMAN_IRAM + 0x000UL)) /* Address Register (FMCDADDR) */
#define FMAN_IRAM_IDATA        ((volatile uint32_t*)(FMAN_IRAM + 0x004UL)) /* Register (FMCDDATA) */
#define FMAN_IRAM_IREADY       ((volatile uint32_t*)(FMAN_IRAM + 0x00CUL)) /* Ready Register (FMCDREADY) */

#define FMAN_IRAM_IADD_AIE     0x80000000 /* Auto Increment Enable */
#define FMAN_IRAM_READY        0x80000000

/* mEMAC (Multirate Ethernet Media Access Controller) 1-4 */
#define FMAN_MEMAC_BASE(n)       (FMAN_BASE + 0xE0000UL + (((n-1) & 0x3) * 0x2000))
#define FMAN_MEMAC_CMD_CFG(n)    ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x008))
#define FMAN_MEMAC_MAC_ADDR_0(n) ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x00C))
#define FMAN_MEMAC_MAC_ADDR_1(n) ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x010))
#define FMAN_MEMAC_MAXFRMG(n)    ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x014))
#define FMAN_MEMAC_HTBLE_CTRL(n) ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x02C))
#define FMAN_MEMAC_IEVENT(n)     ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x040))
#define FMAN_MEMAC_IMASK(n)      ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x04C))

#define FMAN_MEMAC_IF_MODE(n)    ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x300))
#define FMAN_MEMAC_IF_STATUS(n)  ((volatile uint32_t*)(FMAN_MEMAC_BASE(n) + 0x304))

/* FMAN_MEMAC_CMD_CFG - Command and configuration register */
#define MEMAC_CMD_CFG_RX_EN       0x00000002 /* MAC RX path enable */
#define MEMAC_CMD_CFG_TX_EN       0x00000001 /* MAC TX path enable */
#define MEMAC_CMD_CFG_NO_LEN_CHK  0x00020000 /* Payload length check disable */

/* FMAN_MEMAC_IF_MODE - Interface Mode Register */
#define IF_MODE_EN_AUTO     0x00008000 /* 1 - Enable automatic speed selection */
#define IF_MODE_SETSP_100M  0x00000000 /* 00 - 100Mbps  RGMII */
#define IF_MODE_SETSP_10M   0x00002000 /* 01 - 10Mbps   RGMII */
#define IF_MODE_SETSP_1000M 0x00004000 /* 10 - 1000Mbps RGMII */
#define IF_MODE_SETSP_MASK  0x00006000 /* setsp mask bits */
#define IF_MODE_XGMII       0x00000000 /* 00- XGMII(10) interface mode */
#define IF_MODE_GMII        0x00000002 /* 10- GMII interface mode */
#define IF_MODE_MASK        0x00000003 /* mask for mode interface mode */
#define IF_MODE_RG          0x00000004 /* 1- RGMII */
#define IF_MODE_RM          0x00000008 /* 1- RGMII */

/* Dedicated MDIO EM1/EM2 Interface for PHY configurion */
#define FMAC_MDIO_BASE(n)      (FMAN_BASE + 0xFC000UL + (((n-1) & 0x1) * 0x1000))
#define FMAN_MDIO_CFG(n)       ((volatile uint32_t*)(FMAC_MDIO_BASE(n) + 0x030))
#define FMAN_MDIO_CTRL(n)      ((volatile uint32_t*)(FMAC_MDIO_BASE(n) + 0x034))
#define FMAN_MDIO_DATA(n)      ((volatile uint32_t*)(FMAC_MDIO_BASE(n) + 0x038))
#define FMAN_MDIO_ADDR(n)      ((volatile uint32_t*)(FMAC_MDIO_BASE(n) + 0x03C))

#define MDIO_STAT_CLKDIV(x)    ((((x)>>1) & 0xFF) << 8) /* valid range 5-511: ratio = (2 * CLKDIV) + 1 */
#define MDIO_STAT_BSY          (1 << 0)
#define MDIO_STAT_RD_ER        (1 << 1)
#define MDIO_STAT_PRE          (1 << 5)
#define MDIO_STAT_EN_C45       (1 << 6) /* Enable Clause 45 support. */
#define MDIO_STAT_HOLD_15_CLK  (7 << 2)
#define MDIO_STAT_NEG          (1 << 23) /* MDIO is driven by master on MDC negative edge */

#define MDIO_CTL_DEV_ADDR(x)   ( (x) & 0x1F)
#define MDIO_CTL_PORT_ADDR(x)  (((x) & 0x1F) << 5)
#define MDIO_CTL_PRE_DIS       (1 << 10)
#define MDIO_CTL_SCAN_EN       (1 << 11)
#define MDIO_CTL_POST_INC      (1 << 14)
#define MDIO_CTL_READ          (1 << 15)

#define MDIO_ADDR(x)           ((x) & 0xFFFF)

#define MDIO_DATA(x)           ((x) & 0xFFFF)
#define MDIO_DATA_BSY          (1UL << 31)



/* T1024 PC16552D Dual UART */
#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */

#define UART_BASE(n) (CCSRBAR + 0x11C500 + (n * 0x1000))

#define UART_RBR(n)  ((volatile uint8_t*)(UART_BASE(n) + 0)) /* receiver buffer register */
#define UART_THR(n)  ((volatile uint8_t*)(UART_BASE(n) + 0)) /* transmitter holding register */
#define UART_IER(n)  ((volatile uint8_t*)(UART_BASE(n) + 1)) /* interrupt enable register */
#define UART_IIR(n)  ((volatile uint8_t*)(UART_BASE(n) + 2)) /* interrupt ID register */
#define UART_FCR(n)  ((volatile uint8_t*)(UART_BASE(n) + 2)) /* FIFO control register */
#define UART_LCR(n)  ((volatile uint8_t*)(UART_BASE(n) + 3)) /* line control register */
#define UART_MCR(n)  ((volatile uint8_t*)(UART_BASE(n) + 4)) /* modem control register */
#define UART_LSR(n)  ((volatile uint8_t*)(UART_BASE(n) + 5)) /* line status register */

/* enabled when UART_LCR_DLAB set */
#define UART_DLB(n)  ((volatile uint8_t*)(UART_BASE(n) + 0)) /* divisor least significant byte register */
#define UART_DMB(n)  ((volatile uint8_t*)(UART_BASE(n) + 1)) /* divisor most significant byte register */

#define UART_FCR_TFR  (0x04) /* Transmitter FIFO reset */
#define UART_FCR_RFR  (0x02) /* Receiver FIFO reset */
#define UART_FCR_FEN  (0x01) /* FIFO enable */
#define UART_LCR_DLAB (0x80) /* Divisor latch access bit */
#define UART_LCR_WLS  (0x03) /* Word length select: 8-bits */
#define UART_LSR_TEMT (0x40) /* Transmitter empty */
#define UART_LSR_THRE (0x20) /* Transmitter holding register empty */


/* T1024 IFC (Integrated Flash Controller) - RM 23.1 */
#define IFC_BASE        (CCSRBAR + 0x00124000)
#define IFC_MAX_BANKS   8

#define IFC_CSPR_EXT(n) ((volatile uint32_t*)(IFC_BASE + 0x000C + (n * 0xC))) /* Extended Base Address */
#define IFC_CSPR(n)     ((volatile uint32_t*)(IFC_BASE + 0x0010 + (n * 0xC))) /* Chip-select Property */
#define IFC_AMASK(n)    ((volatile uint32_t*)(IFC_BASE + 0x00A0 + (n * 0xC)))
#define IFC_CSOR(n)     ((volatile uint32_t*)(IFC_BASE + 0x0130 + (n * 0xC)))
#define IFC_CSOR_EXT(n) ((volatile uint32_t*)(IFC_BASE + 0x0134 + (n * 0xC)))
#define IFC_FTIM0(n)    ((volatile uint32_t*)(IFC_BASE + 0x01C0 + (n * 0x30)))
#define IFC_FTIM1(n)    ((volatile uint32_t*)(IFC_BASE + 0x01C4 + (n * 0x30)))
#define IFC_FTIM2(n)    ((volatile uint32_t*)(IFC_BASE + 0x01C8 + (n * 0x30)))
#define IFC_FTIM3(n)    ((volatile uint32_t*)(IFC_BASE + 0x01CC + (n * 0x30)))

#define IFC_CSPR_PHYS_ADDR(x) (((uint32_t)x) & 0xFFFFFF00) /* Physical base address */
#define IFC_CSPR_PORT_SIZE_8  0x00000080 /* Port Size 8 */
#define IFC_CSPR_PORT_SIZE_16 0x00000100 /* Port Size 16 */
#define IFC_CSPR_WP           0x00000040 /* Write Protect */
#define IFC_CSPR_MSEL_NOR     0x00000000 /* Mode Select - NOR */
#define IFC_CSPR_MSEL_NAND    0x00000002 /* Mode Select - NAND */
#define IFC_CSPR_MSEL_GPCM    0x00000004 /* Mode Select - GPCM (General-purpose chip-select machine) */
#define IFC_CSPR_V            0x00000001 /* Bank Valid */

/* NOR Timings (IFC clocks) */
#define IFC_FTIM0_NOR_TACSE(n) (((n) & 0x0F) << 28) /* After address hold cycle */
#define IFC_FTIM0_NOR_TEADC(n) (((n) & 0x3F) << 16) /* External latch address delay cycles */
#define IFC_FTIM0_NOR_TAVDS(n) (((n) & 0x3F) << 8)  /* Delay between CS assertion */
#define IFC_FTIM0_NOR_TEAHC(n) (((n) & 0x3F) << 0)  /* External latch address hold cycles */
#define IFC_FTIM1_NOR_TACO(n)  (((n) & 0xFF) << 24) /* CS assertion to output enable */
#define IFC_FTIM1_NOR_TRAD(n)  (((n) & 0x3F) << 8)  /* read access delay */
#define IFC_FTIM1_NOR_TSEQ(n)  (((n) & 0x3F) << 0)  /* sequential read access delay */
#define IFC_FTIM2_NOR_TCS(n)   (((n) & 0x0F) << 24) /* Chip-select assertion setup time */
#define IFC_FTIM2_NOR_TCH(n)   (((n) & 0x0F) << 18) /* Chip-select hold time */
#define IFC_FTIM2_NOR_TWPH(n)  (((n) & 0x3F) << 10) /* Chip-select hold time */
#define IFC_FTIM2_NOR_TWP(n)   (((n) & 0xFF) << 0)  /* Write enable pulse width */

/* GPCM Timings (IFC clocks) */
#define IFC_FTIM0_GPCM_TACSE(n) (((n) & 0x0F) << 28) /* After address hold cycle */
#define IFC_FTIM0_GPCM_TEADC(n) (((n) & 0x3F) << 16) /* External latch address delay cycles */
#define IFC_FTIM0_GPCM_TEAHC(n) (((n) & 0x3F) << 0)  /* External latch address hold cycles */
#define IFC_FTIM1_GPCM_TACO(n)  (((n) & 0xFF) << 24) /* CS assertion to output enable */
#define IFC_FTIM1_GPCM_TRAD(n)  (((n) & 0x3F) << 8)  /* read access delay */
#define IFC_FTIM2_GPCM_TCS(n)   (((n) & 0x0F) << 24) /* Chip-select assertion setup time */
#define IFC_FTIM2_GPCM_TCH(n)   (((n) & 0x0F) << 18) /* Chip-select hold time */
#define IFC_FTIM2_GPCM_TWP(n)   (((n) & 0xFF) << 0)  /* Write enable pulse width */

/* IFC AMASK - RM Table 13-3 - Count of MSB minus 1 */
enum ifc_amask_sizes {
    IFC_AMASK_64KB =  0xFFFF0000,
    IFC_AMASK_128KB = 0xFFFE0000,
    IFC_AMASK_256KB = 0xFFFC0000,
    IFC_AMASK_512KB = 0xFFF80000,
    IFC_AMASK_1MB   = 0xFFF00000,
    IFC_AMASK_2MB   = 0xFFE00000,
    IFC_AMASK_4MB   = 0xFFC00000,
    IFC_AMASK_8MB   = 0xFF800000,
    IFC_AMASK_16MB  = 0xFF000000,
    IFC_AMASK_32MB  = 0xFE000000,
    IFC_AMASK_64MB  = 0xFC000000,
    IFC_AMASK_128MB = 0xF8000000,
    IFC_AMASK_256MB = 0xF0000000,
    IFC_AMASK_512MB = 0xE0000000,
    IFC_AMASK_1GB   = 0xC0000000,
    IFC_AMASK_2GB   = 0x80000000,
    IFC_AMASK_4GB   = 0x00000000,
};

/* NOR Flash */
#define FLASH_BANK_SIZE   (64*1024*1024)
#define FLASH_PAGE_SIZE   (1024) /* program buffer */
#define FLASH_SECTOR_SIZE (128*1024)
#define FLASH_SECTORS     (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_CFI_WIDTH   16 /* 8 or 16 */

#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */

/* Intel CFI */
#define FLASH_CMD_CFI                  0x98
#define FLASH_CMD_READ_ID              0x90
#define FLASH_CMD_RESET                0xFF
#define FLASH_CMD_BLOCK_ERASE          0x20
#define FLASH_CMD_ERASE_CONFIRM        0xD0
#define FLASH_CMD_WRITE                0x40
#define FLASH_CMD_PROTECT              0x60
#define FLASH_CMD_SETUP                0x60
#define FLASH_CMD_SET_CR_CONFIRM       0x03
#define FLASH_CMD_PROTECT_SET          0x01
#define FLASH_CMD_PROTECT_CLEAR        0xD0
#define FLASH_CMD_CLEAR_STATUS         0x50
#define FLASH_CMD_READ_STATUS          0x70
#define FLASH_CMD_WRITE_TO_BUFFER      0xE8
#define FLASH_CMD_WRITE_BUFFER_PROG    0xE9
#define FLASH_CMD_WRITE_BUFFER_CONFIRM 0xD0

#define FLASH_STATUS_DONE    0x80
#define FLASH_STATUS_ESS     0x40
#define FLASH_STATUS_ECLBS   0x20
#define FLASH_STATUS_PSLBS   0x10
#define FLASH_STATUS_VPENS   0x08
#define FLASH_STATUS_PSS     0x04
#define FLASH_STATUS_DPS     0x02
#define FLASH_STATUS_R       0x01
#define FLASH_STATUS_PROTECT 0x01

/* AMD CFI */
#define AMD_CMD_RESET                0xF0
#define AMD_CMD_WRITE                0xA0
#define AMD_CMD_ERASE_START          0x80
#define AMD_CMD_ERASE_SECTOR         0x30
#define AMD_CMD_UNLOCK_START         0xAA
#define AMD_CMD_UNLOCK_ACK           0x55
#define AMD_CMD_WRITE_TO_BUFFER      0x25
#define AMD_CMD_WRITE_BUFFER_CONFIRM 0x29
#define AMD_CMD_SET_PPB_ENTRY        0xC0
#define AMD_CMD_SET_PPB_EXIT_BC1     0x90
#define AMD_CMD_SET_PPB_EXIT_BC2     0x00
#define AMD_CMD_PPB_UNLOCK_BC1       0x80
#define AMD_CMD_PPB_UNLOCK_BC2       0x30
#define AMD_CMD_PPB_LOCK_BC1         0xA0
#define AMD_CMD_PPB_LOCK_BC2         0x00

#define AMD_STATUS_TOGGLE            0x40
#define AMD_STATUS_ERROR             0x20

/* Flash unlock addresses */
#if FLASH_CFI_WIDTH == 16
#define FLASH_UNLOCK_ADDR1 0x555
#define FLASH_UNLOCK_ADDR2 0x2AA
#else
#define FLASH_UNLOCK_ADDR1 0xAAA
#define FLASH_UNLOCK_ADDR2 0x555
#endif

/* Flash IO Helpers */
#if FLASH_CFI_WIDTH == 16
#define FLASH_IO8_WRITE(sec, n, val)      *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (((val) << 8) | (val))
#define FLASH_IO16_WRITE(sec, n, val)     *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (val)
#define FLASH_IO8_READ(sec, n)  (uint8_t)(*((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))))
#define FLASH_IO16_READ(sec, n)           *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2)))
#else
#define FLASH_IO8_WRITE(sec, n, val)      *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n))) = (val)
#define FLASH_IO8_READ(sec, n)            *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n)))
#endif



/* DDR4 - 2GB */
/* 1600 MT/s (64-bit, CL=12, ECC on) */

/* SA[0-15]:  0000: Starting address for chip select (bank)n
 * EA[16-31]: 007F: Ending address for chip select (bank)n
 */
#define DDR_CS0_BNDS_VAL       0x0000007F
#define DDR_CS1_BNDS_VAL       0x008000BF
#define DDR_CS2_BNDS_VAL       0x0100013F
#define DDR_CS3_BNDS_VAL       0x0140017F

/* 15=row bits, 10 column bits, 1 bank group bit, 2 logical bank bits, ODT only during writes */
/* CS_EN      [0]: 1   Chip select n enable
 * AP_EN      [8]: 1   Chip select nauto-precharge enable
 * ODT_RD_CFG [9-11]:  ODT for reads configuration
 * ODT_WR_CFG [13-15]: ODT for writes configuration
 * BA_BITS_CS [16-17]: Number of bank bits for SDRAM on chip selectn
 * ROW_BITS_CS[21-23]: Number of row bits for SDRAM on chip selectn
 * BG_BITS_CS [26-27]: Number of bank group bits for SDRAM on chip selectn
 * COL_BITS_CS[29-31]: Number of column bits for SDRAM on chip selectn
 */
#define DDR_CS0_CONFIG_VAL     0x80810312 /* was 0x80010312 */
#define DDR_CS1_CONFIG_VAL     0x00000202
#define DDR_CS2_CONFIG_VAL     0x00000202
#define DDR_CS3_CONFIG_VAL     0x00010202

/* PASR_DEC[0]: Partial array decoding
 * PASR_CFG[5-7]: Partial array self refresh config
 */
#define DDR_CS_CONFIG_2_VAL    0x00000000

/* RWT            [0-1]:      10:  2 clocks: Read-to-write turnaround (tRTW)
 * WRT            [2-3]:      00:  0 clocks: Write-to-read turnaround
 * RRT            [4-5]:      00:  0 clocks: Read-to-read turnaround
 * WWT            [6-7]:      00:  0 clocks: Write-to-write turnaround
 * ACT_PD_EXIT    [8-11]:   0101:  5 clocks: Active powerdown exit timing (tXP)
 * PRE_PD_EXIT    [12-15]:  0100:  4 clocks: Precharge powerdown exit timing (tXP)
 * EXT_PRE_PD_EXIT[16-17]:    01: 16 clocks: Extended precharge powerdown exit timing (tXP)
 * MRS_CYC        [27-31]: 01100: 12 clocks: Mode register set cycle time (tMRD, tMOD)
 */
#define DDR_TIMING_CFG_0_VAL   0x8055000C

/* PRETOACT      [0-3]:    0011:  3 clocks: Precharge-to-activate interval (tRP)
 * ACTTOPRE      [4-7]:    1110: 14 clocks (30 total): Activate to precharge interval (tRAS)
 * ACTTORW       [8-11]:   0010:  2 clocks (18 total): Activate to read/write interval for SDRAM (tRCD)
 * CASLAT        [12-14]:   011:  4 clocks: MCAS_B latency from READ command
 * REFREC        [16-19]:  1100: 12 clocks (240+12+8 total): Refresh recovery time (tRFC)
 * WRREC         [20-23]:  1110: 14 clocks: Last data to precharge minimum interval (tWR)
 * ACTTOACT      [24-27]:  0100:  4 clocks: Activate-to-activate interval (tRRD)
 * WRTORD        [28-31]:  0100:  4 clocks: Last write data pair to read command issue interval (tWTR)
 */
#define DDR_TIMING_CFG_1_VAL   0x3E26CE44 /* was 0x2E268E44 */

/* ADD_LAT       [0-3]:     0000: 0 clocks Additive latency
 * WR_LAT        [9-12]:    1001: 9 clocks Write latency
 * EXT_WR_LAT    [13]:         0: 0 clocks Extended Write Latency (1=16 clocks)
 * RD_TO_PRE     [15-18]:   1000: 8 clocks Read to precharge (tRTP).
 * WR_DATA_DELAY [19-22]:   1000: 1 clock delay Write command to write data strobe timing adjustment.
 * CKE_PLS       [23-25]:    100: 4 clocks Minimum CKE pulse width (tCKE).
 * FOUR_ACT      [26-31]: 011100: 28 Window for four activates (tFAW).
 */
#define DDR_TIMING_CFG_2_VAL   0x0049111C /* tried 0x00491124 */

/* EXT_PRETOACT  [3]:          1: 16 clocks: Extended precharge-to-activate interval (0=0, 1=16 clocks)
 * EXT_ACTTOPRE  [6-7]:       01: 16 clocks: Extended Activate to precharge interval (tRAS)
 * EXT_ACTTORW   [9]:          1: 16 clocks: Extended activate to read/write interval for SDRAM (tRCD) (ACTTORW[5])
 * EXT_REFREC    [10-15]: 001111: 240 Extended refresh recovery time (tRFC).
 * EXT_CASLAT    [18-19]:     01: 8 clocks Extended MCAS_B latency from READ command
 * EXT_ADD_LAT   [21]:         0: 0 clocks Extended Additive Latency
 * EXT_WRREC     [23]:         1: 16 clocks Extended last data to precharge minimum interval (tWR)
 * CNTL_ADJ      [29-31]:    000: MODTn, MCSn_B, and MCKEn will be launched aligned with the other DRAM address and control signals.
 */
#define DDR_TIMING_CFG_3_VAL   0x114F1100 /* was 0x114C1000 */

/* RWT           [0-3]:     0000: 0 clocks: Read-to-write  turnaround for same chip select.
 * WRT           [4-7]:     0000: 0 clocks: Write-to-read  turnaround for same chip select
 * RRT           [8-11]:    0010: 2 clocks: Read-to-read   turnaround for same chip select
 * WWT           [12-15]:   0010: 2 clocks: Write-to-write turnaround for same chip select.
 * EXT_RWT       [16-17]:     00: Extended read-to-write turnaround (tRTW)
 * EXT_WRT       [19]:         0: Extended write-to-read turnaround
 * EXT_RRT       [21]:         0: Extended read-to-read turnaround
 * EXT_WWT       [23]:         0: Extended write-to-write turnaround
 * EXT_REFINT    [27]:         0: Refresh interval (0=0,1=65,536 clocks)
 * DLL_LOCK      [30-31]:     10: 1024 clocks: DDR SDRAM DLL Lock Time (0=200, 1=512, 2=1024 clocks)
 */
#define DDR_TIMING_CFG_4_VAL   0x00220002 /* was 0x00220001 */

/* RODT_ON       [3-7]:     0101: 4 clocks: Read to ODT on (0=CASLAT-WR_LAT, 1=0, 2=1, 12=11 clocks)
 * RODT_OFF      [9-11]:     100: 4 clocks: Read to ODT off (0=4, 1=1, 7=7 clocks)
 * WODT_ON       [15-19]:  00001: 1 clock:  Write to ODT off (1=0, 2=1, 6=5 clocks)
 * WODT_OFF      [21-23]:    100: 4 clocks: Write to ODT off (0=4, 1=1, 7=7 clocks)
 */
#define DDR_TIMING_CFG_5_VAL   0x05401400

#define DDR_TIMING_CFG_6_VAL   0x00000000

/* CKE_RST       [2-3]:      00: 200 clocks: CKE reset time (tXPR) (0=200, 1=256, 2=512, 3=1024 clocks)
 * CKSRE         [4-7]:    0000:  15 clocks: Valid clock after Self Refresh entry (tCKSRE) (0=15, 1=6, )
 * CKSRX         [8-11]:   0000:  15 clocks: Valid clock after Self Refresh exit (tCKSRX)
 * PAR_LAT       [12-15]:  0000:   0 clocks
 * CS_TO_CMD     [24-27]:  0000:   0 clocks: Chip select to command latency
 */
#define DDR_TIMING_CFG_7_VAL   0x00000000 /* tried 0x00050000 */

/* RWT_BG         [0-3]:    0000: Read-to-write turnaround for same chip select and same bank group
 * WRT_BG         [4-7]:    0011: Write-to-read turnaround for same chip select and same bank group
 * RRT_BG         [8-11]:   0001: Read-to-read turnaround for same chip select and same bank group
 * WWT_BG         [12-15]:  0001: Write-to-write turnaround for same chip select and same bank group
 * ACTTOACT_BG    [16-19]:  0101: Activate-to-activate interval for the same bank group(tRRD_L).
 * WRTORD_BG      [20-23]:  1000: Last write data pair to read command issue interval for the same bank group(tWTR_L)
 * PRE_ALL_REC    [27-31]: 00000: Precharge all-to-activate interval
 */
#define DDR_TIMING_CFG_8_VAL   0x03115800

/* MR1 | MR0
 * MR0 0x0215
 * | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2   | 1 | 0 |
 * [   WR/RTP    ]       [   CL3-1   ][BT][CL0][   BL  ]
 * Burst Length (BL) = 01: 00 (fixed 8), 01 (on the fly), 02 (fixed 4)
 * Burst Type (BT) = 0 (nibble sequential), 1 (interleave)
 * CAS Latency (CL):
 *   00000 (9 clocks)
 *   00001 (10 clocks)
 *   00010 (11 clocks)
 *   00011 (12 clocks) (original)
 *   00100 (13 clocks)
 *   00101 (14 clocks)
 *   00110 (15 clocks)
 *   00111 (16 clocks)
 *   10111 (32 clocks)
 * WRITE recovery (WR)/READ-to-PRECHARGE(RTP):
 *   0000 (10/5 clocks)
 *   0001 (12/6 clocks) (original)
 *   0010 (14/7 clocks)
 *   0011 (16/8 clocks)
 *   0100 (18/9 clocks)
 *   0101 (20/10 clocks)
 *   0110 (24/12 clocks)
 *   0111 (22/11 clocks)
 *   1000 (26/13 clocks)
 *   1001 (28/14 clocks)
 *
 * MR1 0x0101
 * | 11 | 10 | 9 | 8 | 7   | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 * [TDQS][  RTT_NOM  ][Wlev]       [   AL  ][ ODI ][DLL]
 * DLL=1, RTT_NOM=001 (RZQ/4 60ohm), ODI=00 (RZQ/7 34ohm)
 */
#define DDR_SDRAM_MODE_VAL     0x01010215

/* MR2 | MR3
 * MR2
 * | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2   | 1 | 0 |
 * [    RTT_WR   ]           [    CWL    ]
 *
 * MR3
 * | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2   | 1 | 0 |
 *      [ WR_CMD_LAT]
 */
#define DDR_SDRAM_MODE_2_VAL   0x00000000

/* Not applicable (reuse other MRs) */
#define DDR_SDRAM_MODE_3_8_VAL 0x00000000

/* MR4 | MR5
 * MR4: 0x0100
 * MR5: 0x013F: RTT_PARK=000 disabled, CA Parity Latency=010 (5 clocks) */
#define DDR_SDRAM_MODE_9_VAL   0x00000500

/* MR6 | MR7:
 * MR7: CCD_L 010=6 clocks, VREF Range 1 */
#define DDR_SDRAM_MODE_10_VAL  0x04000000

#define DDR_SDRAM_MD_CNTL_VAL  0x03001000

#define DDR_SDRAM_CFG_VAL      0xE5200000 /* DDR4 w/ECC */

/* ODT_CFG [9-10]:     10: Assert ODT to internal IOs only
 * NUM_PR  [16-19]: 00001: 1 refresh
 * OBC_CFG [25]:        1: On-the-fly Burst Chop mode will be used
 * D_INIT  [27]:        1: The memory controller will initialize memory once it is enabled
 */
#define DDR_SDRAM_CFG_2_VAL    0x00401050

/* REF_MODE[22-23]: Refresh Mode */
#define DDR_SDRAM_CFG_3_VAL    0x00000000

/* REFINT  [0-15]:   6240: Refresh interval  12480=0x30C0
 * BSTOPRE [18-31]:  1560: Precharge interval
 */
#define DDR_SDRAM_INTERVAL_VAL 0x18600000 /* was 0x18600618 */

#define DDR_DATA_INIT_VAL      0xDEADBEEF

/* CLK_ADJUST[5-9]: applied cycle after address/command
 * 00000 = aligned
 * 00001 = 1/16
 * 00100 = 1/4
 * 00110 = 3/8
 * 01001 = 9/16
 * 01000 = 1/2 (configured)
 * 01010 = 5/8
 * 10000 = 1
 */
#define DDR_SDRAM_CLK_CNTL_VAL 0x02000000 /* was 0x02400000 */

/* ZQ_EN */
#define DDR_ZQ_CNTL_VAL        0x8A090705

/* WRLVL_EN   [0]:        1: Write Leveling Enable
 * WRLVL_MRD  [5-7]:    110 0x6:  64 clocks
 * WRLVL_ODTEN[9-11]:   111 0x7: 128 clocks ODT delay after margining mode is programmed (tWL_ODTEN).
 * WRLVL_DQSEN[13-15]:  101 0x5:  32 clocks DQS/DQS_B delay after margining mode is programmed (tWL_DQSEN).
 * WRLVL_SMPL [16-19]: 1111 0xF:  15 clocks Write leveling sample time
 * WRLVL_WLR  [21-23]:  110 0x6:  64 clocks Write leveling repetition time.
 * WRLVL_START[27-31]: 1000 0x8: 3/4 clocks Write leveling start time for DQS[0].
 */
#define DDR_WRLVL_CNTL_VAL     0x8675F606
/* WRLVL_START_1  [3-7]: 3/4 Write leveling start time for DQS[1]
 * WRLVL_START_2[11-15]: 7/8 Write leveling start time for DQS[2]
 * WRLVL_START_3[19-23]: 7/8 Write leveling start time for DQS[3]
 * WRLVL_START_4[27-31]: 9/8 Write leveling start time for DQS[4]
 */
#define DDR_WRLVL_CNTL_2_VAL   0x06070709
/* WRLVL_START_5  [3-7]: 9/8 Write leveling start time for DQS[5]
 * WRLVL_START_6[11-15]: 9/8 Write leveling start time for DQS[6]
 * WRLVL_START_7[19-23]: 9/8 Write leveling start time for DQS[7]
 * WRLVL_START_8[27-31]: 1   Write leveling start time for DQS[8]
 */
#define DDR_WRLVL_CNTL_3_VAL   0x09090908

#define DDR_SDRAM_RCW_1_VAL    0x00000000
#define DDR_SDRAM_RCW_2_VAL    0x00000000


/* DHC_EN[0]=1, ODT[12-13]=120 Ohms, VREF_OVRD 37% */
#define DDR_DDRCDR_1_VAL       0x80000000 /* was 0x80080000 */
#define DDR_DDRCDR_2_VAL       0x00000000

#define DDR_ERR_INT_EN_VAL     0x0000001D
#define DDR_ERR_SBE_VAL        0x00000000


/* 12.4 DDR Memory Map */
#define DDR_BASE           (CCSRBAR + 0x8000)

#define DDR_CS_BNDS(n)     ((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   ((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_CS_CONFIG_2(n) ((volatile uint32_t*)(DDR_BASE + 0x0C0 + (n * 4))) /* Chip select n configuration 2 */
#define DDR_SDRAM_CFG      ((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    ((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_CFG_3    ((volatile uint32_t*)(DDR_BASE + 0x260)) /* DDR SDRAM control configuration 3 */
#define DDR_SDRAM_INTERVAL ((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_INIT_ADDR      ((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  ((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_DATA_INIT      ((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_TIMING_CFG_0   ((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   ((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   ((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_TIMING_CFG_3   ((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_4   ((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   ((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_TIMING_CFG_6   ((volatile uint32_t*)(DDR_BASE + 0x168)) /* DDR SDRAM timing configuration 6 */
#define DDR_TIMING_CFG_7   ((volatile uint32_t*)(DDR_BASE + 0x16C)) /* DDR SDRAM timing configuration 7 */
#define DDR_TIMING_CFG_8   ((volatile uint32_t*)(DDR_BASE + 0x250)) /* DDR SDRAM timing configuration 8 */
#define DDR_ZQ_CNTL        ((volatile uint32_t*)(DDR_BASE + 0x170)) /* DDR ZQ calibration control */
#define DDR_WRLVL_CNTL     ((volatile uint32_t*)(DDR_BASE + 0x174)) /* DDR write leveling control */
#define DDR_WRLVL_CNTL_2   ((volatile uint32_t*)(DDR_BASE + 0x190)) /* DDR write leveling control 2 */
#define DDR_WRLVL_CNTL_3   ((volatile uint32_t*)(DDR_BASE + 0x194)) /* DDR write leveling control 3 */
#define DDR_SR_CNTR        ((volatile uint32_t*)(DDR_BASE + 0x17C)) /* DDR Self Refresh Counter */
#define DDR_SDRAM_RCW_1    ((volatile uint32_t*)(DDR_BASE + 0x180)) /* DDR Register Control Word 1 */
#define DDR_SDRAM_RCW_2    ((volatile uint32_t*)(DDR_BASE + 0x184)) /* DDR Register Control Word 2 */
#define DDR_SDRAM_RCW_3    ((volatile uint32_t*)(DDR_BASE + 0x1A0)) /* DDR Register Control Word 3 */
#define DDR_SDRAM_RCW_4    ((volatile uint32_t*)(DDR_BASE + 0x1A4)) /* DDR Register Control Word 4 */
#define DDR_SDRAM_RCW_5    ((volatile uint32_t*)(DDR_BASE + 0x1A8)) /* DDR Register Control Word 5 */
#define DDR_SDRAM_RCW_6    ((volatile uint32_t*)(DDR_BASE + 0x1AC)) /* DDR Register Control Word 6 */
#define DDR_DDRCDR_1       ((volatile uint32_t*)(DDR_BASE + 0xB28)) /* DDR Control Driver Register 1 */
#define DDR_DDRCDR_2       ((volatile uint32_t*)(DDR_BASE + 0xB2C)) /* DDR Control Driver Register 2 */
#define DDR_DDRDSR_1       ((volatile uint32_t*)(DDR_BASE + 0xB20)) /* DDR Debug Status Register 1 */
#define DDR_DDRDSR_2       ((volatile uint32_t*)(DDR_BASE + 0xB24)) /* DDR Debug Status Register 2 */
#define DDR_ERR_DISABLE    ((volatile uint32_t*)(DDR_BASE + 0xE44)) /* Memory error disable */
#define DDR_ERR_INT_EN     ((volatile uint32_t*)(DDR_BASE + 0xE48)) /* Memory error interrupt enable */
#define DDR_ERR_SBE        ((volatile uint32_t*)(DDR_BASE + 0xE58)) /* Single-Bit ECC memory error management */
#define DDR_SDRAM_MODE     ((volatile uint32_t*)(DDR_BASE + 0x118)) /* DDR SDRAM mode configuration */
#define DDR_SDRAM_MODE_2   ((volatile uint32_t*)(DDR_BASE + 0x11C)) /* DDR SDRAM mode configuration 2 */
#define DDR_SDRAM_MODE_3   ((volatile uint32_t*)(DDR_BASE + 0x200)) /* DDR SDRAM mode configuration 3 */
#define DDR_SDRAM_MODE_4   ((volatile uint32_t*)(DDR_BASE + 0x204)) /* DDR SDRAM mode configuration 4 */
#define DDR_SDRAM_MODE_5   ((volatile uint32_t*)(DDR_BASE + 0x208)) /* DDR SDRAM mode configuration 5 */
#define DDR_SDRAM_MODE_6   ((volatile uint32_t*)(DDR_BASE + 0x20C)) /* DDR SDRAM mode configuration 6 */
#define DDR_SDRAM_MODE_7   ((volatile uint32_t*)(DDR_BASE + 0x210)) /* DDR SDRAM mode configuration 7 */
#define DDR_SDRAM_MODE_8   ((volatile uint32_t*)(DDR_BASE + 0x214)) /* DDR SDRAM mode configuration 8 */
#define DDR_SDRAM_MODE_9   ((volatile uint32_t*)(DDR_BASE + 0x220)) /* DDR SDRAM mode configuration 9 */
#define DDR_SDRAM_MODE_10  ((volatile uint32_t*)(DDR_BASE + 0x224)) /* DDR SDRAM mode configuration 10 */
#define DDR_SDRAM_MD_CNTL  ((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_CLK_CNTL ((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */

#define DDR_DEBUG_9        ((volatile uint32_t*)(DDR_BASE + 0xF20))
#define DDR_DEBUG_10       ((volatile uint32_t*)(DDR_BASE + 0xF24))
#define DDR_DEBUG_11       ((volatile uint32_t*)(DDR_BASE + 0xF28))
#define DDR_DEBUG_12       ((volatile uint32_t*)(DDR_BASE + 0xF2C))
#define DDR_DEBUG_13       ((volatile uint32_t*)(DDR_BASE + 0xF30))
#define DDR_DEBUG_14       ((volatile uint32_t*)(DDR_BASE + 0xF34))
#define DDR_DEBUG_19       ((volatile uint32_t*)(DDR_BASE + 0xF48))
#define DDR_DEBUG_29       ((volatile uint32_t*)(DDR_BASE + 0xF70))

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG_ECC_EN   0x20000000
#define DDR_SDRAM_CFG_32_BE    0x00080000
#define DDR_SDRAM_CFG_2_D_INIT 0x00000010 /* data initialization in progress */
#define DDR_SDRAM_CFG_HSE      0x00000008
#define DDR_SDRAM_CFG_BI       0x00000001 /* Bypass initialization */
#define DDR_SDRAM_CFG_SDRAM_TYPE_MASK 0x07000000
#define DDR_SDRAM_CFG_SDRAM_TYPE(n) (((n) & 0x7) << 24)
#define DDR_SDRAM_TYPE_DDR4    5
#define DDR_SDRAM_INTERVAL_BSTOPRE 0x3FFF


/* CPLD (APU) */
#define CPLD_BASE               0xFFDF0000
#define CPLD_BASE_PHYS_HIGH     0xFULL
/* CPLD (MPU) */
#define CPLD_MPU_BASE           0xFFCF0000
#define CPLD_MPU_BASE_PHYS_HIGH 0xFULL

#define BOARD_ID_L_ADDR     0x0002
#define BOARD_ID_H_ADDR     0x0004
#define PLD_VER_ADDR        0x0006
#define POWER_STATUS_ADDRR  0x0400
#define MPU_INT_STATUS_ADDR 0x0402
#define MPU_INT_ENABLE_ADDR 0x0404
#define MPU_CONTROL_ADDR    0x0430
#define MPU_RESET_ADDR      0x0432
#define PCI_STATUS_ADDR     0x0434
#define HS_CSR_ADDR         0x1040
#define CPCI_GA_ADDRS       0x1042
#define CPCI_INTX_ADDR      0x1044

#define CPLD_LBMAP_MASK       0x3F
#define CPLD_BANK_SEL_MASK    0x07
#define CPLD_BANK_OVERRIDE    0x40
#define CPLD_LBMAP_ALTBANK    0x44 /* BANK OR | BANK 4 */
#define CPLD_LBMAP_DFLTBANK   0x40 /* BANK OR | BANK 0 */
#define CPLD_LBMAP_RESET      0xFF
#define CPLD_LBMAP_SHIFT      0x03
#define CPLD_BOOT_SEL         0x80

#define CPLD_PCIE_SGMII_MUX   0x80
#define CPLD_OVERRIDE_BOOT_EN 0x01
#define CPLD_OVERRIDE_MUX_EN  0x02 /* PCIE/2.5G-SGMII mux override enable */

#define CPLD_DATA(n) ((volatile uint16_t*)(CPLD_BASE + (n)))
#define CPLD_READ(reg)         get16(CPLD_DATA(reg))
#define CPLD_WRITE(reg, value) set16(CPLD_DATA(reg), value)

/* MRAM */
#define MRAM_BASE               0xFF800000
#define MRAM_BASE_PHYS_HIGH     0xFULL

/* eSDHC */
#define ESDHC_BASE            (CCSRBAR + 0x114000)



/* eSPI */
#define ESPI_MAX_CS_NUM      4
#define ESPI_MAX_RX_LEN      (1 << 16)
#define ESPI_FIFO_WORD       4

#define ESPI_BASE            (CCSRBAR + 0x110000)
#define ESPI_SPMODE          ((volatile uint32_t*)(ESPI_BASE + 0x00)) /* controls eSPI general operation mode */
#define ESPI_SPIE            ((volatile uint32_t*)(ESPI_BASE + 0x04)) /* controls interrupts and report events */
#define ESPI_SPIM            ((volatile uint32_t*)(ESPI_BASE + 0x08)) /* enables/masks interrupts */
#define ESPI_SPCOM           ((volatile uint32_t*)(ESPI_BASE + 0x0C)) /* command frame information */
#define ESPI_SPITF           ((volatile uint32_t*)(ESPI_BASE + 0x10)) /* transmit FIFO access register (32-bit) */
#define ESPI_SPIRF           ((volatile uint32_t*)(ESPI_BASE + 0x14)) /* read-only receive data register (32-bit) */
#define ESPI_SPITF8          ((volatile uint8_t*)( ESPI_BASE + 0x10)) /* transmit FIFO access register (8-bit) */
#define ESPI_SPIRF8          ((volatile uint8_t*)( ESPI_BASE + 0x14)) /* read-only receive data register (8-bit) */
#define ESPI_SPCSMODE(x)     ((volatile uint32_t*)(ESPI_BASE + 0x20 + ((cs) * 4))) /* controls master operation with chip select 0-3 */

#define ESPI_SPMODE_EN       (0x80000000) /* Enable eSPI */
#define ESPI_SPMODE_TXTHR(x) ((x) << 8)   /* Tx FIFO threshold (1-32) */
#define ESPI_SPMODE_RXTHR(x) ((x) << 0)   /* Rx FIFO threshold (0-31) */

#define ESPI_SPCOM_CS(x)     ((x) << 30)       /* Chip select-chip select for which transaction is destined */
#define ESPI_SPCOM_RXSKIP(x) ((x) << 16)       /* Number of characters skipped for reception from frame start */
#define ESPI_SPCOM_TRANLEN(x) (((x) - 1) << 0) /* Transaction length */

#define ESPI_SPIE_TXE        (1 << 15) /* transmit empty */
#define ESPI_SPIE_DON        (1 << 14) /* Last character was transmitted */
#define ESPI_SPIE_RXT        (1 << 13) /* Rx FIFO has more than RXTHR bytes */
#define ESPI_SPIE_RNE        (1 << 9)  /* receive not empty */
#define ESPI_SPIE_TNF        (1 << 8)  /* transmit not full */
#define ESPI_SPIE_RXCNT(n)   (((n) >> 24) & 0x3F) /* The current number of full Rx FIFO bytes */

#define ESPI_CSMODE_CI       0x80000000 /* Inactive high */
#define ESPI_CSMODE_CP       0x40000000 /* Begin edge clock */
#define ESPI_CSMODE_REV      0x20000000 /* MSB first */
#define ESPI_CSMODE_DIV16    0x10000000 /* divide system clock by 16 */
#define ESPI_CSMODE_PM(x)    (((x) & 0xF) << 24) /* presale modulus select */
#define ESPI_CSMODE_POL      0x00100000  /* asserted low */
#define ESPI_CSMODE_LEN(x)   ((((x) - 1) & 0xF) << 16) /* Character length in bits per character */
#define ESPI_CSMODE_CSBEF(x) (((x) & 0xF) << 12) /* CS assertion time in bits before frame start */
#define ESPI_CSMODE_CSAFT(x) (((x) & 0xF) << 8)  /* CS assertion time in bits after frame end */
#define ESPI_CSMODE_CSCG(x)  (((x) & 0xF) << 3)  /* Clock gaps between transmitted frames according to this size */

/* generic share NXP QorIQ driver code */
#include "nxp_ppc.c"


#ifdef ENABLE_BUS_CLK_CALC
static uint32_t hal_get_core_clk(void)
{
    /* compute core clock (system input * ratio) */
    uint32_t core_clk;
    uint32_t core_ratio = get32(CLOCKING_PLLCNGSR(0)); /* see CGA_PLL1_RAT in RCW */
    /* shift by 1 and mask */
    core_ratio = ((core_ratio >> 1) & 0x3F);
    core_clk = SYS_CLK * core_ratio;
    return core_clk;
}
static uint32_t hal_get_plat_clk(void)
{
    /* compute core clock (system input * ratio) */
    uint32_t plat_clk;
    uint32_t plat_ratio = get32(CLOCKING_PLLPGSR); /* see SYS_PLL_RAT in RCW */
    /* shift by 1 and mask */
    plat_ratio = ((plat_ratio >> 1) & 0x1F);
    plat_clk = SYS_CLK * plat_ratio;
    return plat_clk;
}
static uint32_t hal_get_bus_clk(void)
{
    /* compute bus clock (platform clock / 2) */
    uint32_t bus_clk = hal_get_plat_clk() / 2;
    return bus_clk;
}
#else
#define hal_get_core_clk() (uint32_t)(SYS_CLK * 14)
#define hal_get_plat_clk() (uint32_t)(SYS_CLK * 4)
#define hal_get_bus_clk()  (uint32_t)(hal_get_plat_clk() / 2)
#endif

#define TIMEBASE_CLK_DIV 16
#define TIMEBASE_HZ (hal_get_plat_clk() / TIMEBASE_CLK_DIV)
#define DELAY_US  (TIMEBASE_HZ / 1000000)
static void udelay(uint32_t delay_us)
{
    wait_ticks(delay_us * DELAY_US);
}

static void law_init(void)
{
#ifndef BUILD_LOADER_STAGE1
    /* Buffer Manager (BMan) (control) - 32MB */
    set_law(3, BMAN_BASE_PHYS_HIGH, BMAN_BASE_PHYS, LAW_TRGT_BMAN, LAW_SIZE_32MB, 1);
    set_tlb(1, 5, BMAN_BASE_PHYS,
                  BMAN_BASE_PHYS, BMAN_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_16M, 1);
    set_tlb(1, 6, BMAN_BASE_PHYS + 0x01000000,
                  BMAN_BASE_PHYS + 0x01000000, BMAN_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_16M, 1);

    /* QMAN - 32MB */
    set_law(4, QMAN_BASE_PHYS_HIGH, QMAN_BASE_PHYS, LAW_TRGT_QMAN, LAW_SIZE_32MB, 1);
    set_tlb(1, 7, QMAN_BASE_PHYS,
                  QMAN_BASE_PHYS, QMAN_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR), 0, 0, BOOKE_PAGESZ_16M, 1);
    set_tlb(1, 8, QMAN_BASE_PHYS + 0x01000000,
                  QMAN_BASE_PHYS + 0x01000000, QMAN_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_16M, 1);

    /* DCSR - 4MB */
    set_law(5, DCSRBAR_BASE_HIGH, DCSRBAR_BASE, LAW_TRGT_DCSR, LAW_SIZE_4MB, 1);
    set_tlb(1, 9, DCSRBAR_BASE,
                  DCSRBAR_BASE, DCSRBAR_BASE_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_4M, 1);
#endif
}


/* ---- eSPI Driver ---- */
#ifdef ENABLE_ESPI
void hal_espi_init(uint32_t cs, uint32_t clock_hz, uint32_t mode)
{
    uint32_t spibrg = hal_get_bus_clk() / 2, pm, csmode;

    /* Enable eSPI with TX threadshold 4 and RX threshold 3 */
    set32(ESPI_SPMODE, (ESPI_SPMODE_EN | ESPI_SPMODE_TXTHR(4) |
        ESPI_SPMODE_RXTHR(3)));

    set32(ESPI_SPIE, 0xffffffff); /* Clear all eSPI events */
    set32(ESPI_SPIM, 0x00000000); /* Mask all eSPI interrupts */

    csmode = (ESPI_CSMODE_REV | ESPI_CSMODE_POL | ESPI_CSMODE_LEN(8) |
        ESPI_CSMODE_CSBEF(0) | ESPI_CSMODE_CSAFT(0) | ESPI_CSMODE_CSCG(1));

    /* calculate clock divisor */
    if (spibrg / clock_hz > 16) {
        csmode |= ESPI_CSMODE_DIV16;
        pm = (spibrg / (clock_hz * 16));
    }
    else {
        pm = (spibrg / (clock_hz));
    }
    if (pm > 0)
        pm--;

    csmode |= ESPI_CSMODE_PM(pm);

    if (mode & 1)
        csmode |= ESPI_CSMODE_CP;
    if (mode & 2)
        csmode |= ESPI_CSMODE_CI;

    /* configure CS */
    set32(ESPI_SPCSMODE(cs), csmode);
}

int hal_espi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz,
    int flags)
{
    uint32_t mosi, miso, xfer, event;

#ifdef DEBUG_ESPI
    wolfBoot_printf("CS %d, Sz %d, Flags %x\n", cs, sz, flags);
#endif

    if (sz > 0) {
        /* assert CS - use max length and control CS with mode enable toggle */
        set32(ESPI_SPCOM, ESPI_SPCOM_CS(cs) | ESPI_SPCOM_TRANLEN(0x10000));
        set32(ESPI_SPIE, 0xffffffff); /* Clear all eSPI events */
    }
    while (sz > 0) {
        xfer = ESPI_FIFO_WORD;
        if (xfer > sz)
            xfer = sz;

        /* Transfer 4 or 1 */
        if (xfer == ESPI_FIFO_WORD) {
            set32(ESPI_SPITF, *((uint32_t*)tx));
        }
        else {
            xfer = 1;
            set8(ESPI_SPITF8, *((uint8_t*)tx));
        }

        /* wait till TX fifo is empty or done */
        while (1) {
            event = get32(ESPI_SPIE);
            if (event & (ESPI_SPIE_TXE | ESPI_SPIE_DON)) {
                /* clear events */
                set32(ESPI_SPIE, (ESPI_SPIE_TXE | ESPI_SPIE_DON));
                break;
            }
        }

        /* wait till RX has enough data */
        while (1) {
            event = get32(ESPI_SPIE);
            if ((event & ESPI_SPIE_RNE) == 0)
                continue;
        #if defined(DEBUG_ESPI) && DEBUG_ESPI > 1
            wolfBoot_printf("event %x\n", event);
        #endif
            if (ESPI_SPIE_RXCNT(event) >= xfer)
                break;
        }
        if (xfer == ESPI_FIFO_WORD) {
            *((uint32_t*)rx) = get32(ESPI_SPIRF);
        }
        else {
            *((uint8_t*)rx) = get8(ESPI_SPIRF8);
        }

#ifdef DEBUG_ESPI
        wolfBoot_printf("MOSI %x, MISO %x\n",
            *((uint32_t*)tx), *((uint32_t*)rx));
#endif
        tx += xfer;
        rx += xfer;
        sz -= xfer;
    }

    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        /* toggle ESPI_SPMODE_EN - to deassert CS */
        set32(ESPI_SPMODE, get32(ESPI_SPMODE) & ~ESPI_SPMODE_EN);
        set32(ESPI_SPMODE, get32(ESPI_SPMODE) | ESPI_SPMODE_EN);
    }

    return 0;
}
void hal_espi_deinit(void)
{
    /* do nothing */
}
#endif /* ENABLE_ESPI */

/* ---- DUART Driver ---- */
#ifdef DEBUG_UART
void uart_init(void)
{
    /* calc divisor for UART
     * baud rate = CCSRBAR frequency ÷ (16 x [UDMB||UDLB])
     */
    /* compute UART divisor - round up */
    uint32_t div = (hal_get_bus_clk() + (16/2 * BAUD_RATE)) / (16 * BAUD_RATE);

    while (!(get8(UART_LSR(UART_SEL)) & UART_LSR_TEMT))
       ;

    /* set ier, fcr, mcr */
    set8(UART_IER(UART_SEL), 0);
    set8(UART_FCR(UART_SEL), (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN));

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    set8(UART_LCR(UART_SEL), (UART_LCR_DLAB | UART_LCR_WLS));
    /* set divisor */
    set8(UART_DLB(UART_SEL), (div & 0xff));
    set8(UART_DMB(UART_SEL), ((div>>8) & 0xff));
    /* disable rate access (DLAB=0) */
    set8(UART_LCR(UART_SEL), (UART_LCR_WLS));
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
            set8(UART_THR(UART_SEL), '\r');
        }
        while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
        set8(UART_THR(UART_SEL), c);
    }
}
#endif /* DEBUG_UART */

/* ---- IFC Driver ---- */
#if defined(ENABLE_IFC) && !defined(BUILD_LOADER_STAGE1)
static int hal_flash_getid(void)
{
    uint8_t manfid[4];

    hal_flash_unlock_sector(0);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, FLASH_CMD_READ_ID);
    udelay(1000);

    manfid[0] = FLASH_IO8_READ(0, 0);  /* Manufacture Code */
    manfid[1] = FLASH_IO8_READ(0, 1);  /* Device Code 1 */
    manfid[2] = FLASH_IO8_READ(0, 14); /* Device Code 2 */
    manfid[3] = FLASH_IO8_READ(0, 15); /* Device Code 3 */

    /* Exit read info */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    udelay(1);

    wolfBoot_printf("Flash: Mfg 0x%x, Device Code 0x%x/0x%x/0x%x\n",
        manfid[0], manfid[1], manfid[2], manfid[3]);

    return 0;
}
#endif /* ENABLE_IFC && !BUILD_LOADER_STAGE1 */

static void hal_flash_init(void)
{
#ifdef ENABLE_IFC
    /* IFC - NOR Flash */
    /* LAW is already set in boot_ppc_start.S:flash_law */

    /* NOR IFC Flash Timing Parameters */
    set32(IFC_FTIM0(0), (IFC_FTIM0_NOR_TACSE(4) |
                         IFC_FTIM0_NOR_TEADC(5) |
                         IFC_FTIM0_NOR_TEAHC(5)));
    set32(IFC_FTIM1(0), (IFC_FTIM1_NOR_TACO(53) |
                         IFC_FTIM1_NOR_TRAD(26) |
                         IFC_FTIM1_NOR_TSEQ(19)));
    set32(IFC_FTIM2(0), (IFC_FTIM2_NOR_TCS(4) |
                         IFC_FTIM2_NOR_TCH(4) |
                         IFC_FTIM2_NOR_TWPH(14) |
                         IFC_FTIM2_NOR_TWP(28)));
    set32(IFC_FTIM3(0), 0);
    /* NOR IFC Definitions (CS0) */
    set32(IFC_CSPR_EXT(0), FLASH_BASE_PHYS_HIGH);
    set32(IFC_CSPR(0), (IFC_CSPR_PHYS_ADDR(FLASH_BASE_ADDR) |
                    #if FLASH_CFI_WIDTH == 16
                        IFC_CSPR_PORT_SIZE_16 |
                    #else
                        IFC_CSPR_PORT_SIZE_8 |
                    #endif
                        IFC_CSPR_MSEL_NOR |
                        IFC_CSPR_V));
    set32(IFC_AMASK(0), IFC_AMASK_64MB);
    set32(IFC_CSOR(0),  0x0000000C); /* TRHZ (80 clocks for read enable high) */

    #ifndef BUILD_LOADER_STAGE1
    hal_flash_getid();
    #endif
#endif /* ENABLE_IFC */
}

static void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint32_t reg;

    /* Map LAW for DDR */
    set_law(15, 0, DDR_ADDRESS, LAW_TRGT_DDR_1, LAW_SIZE_2GB, 0);

    /* If DDR is already enabled then just return */
    if ((get32(DDR_SDRAM_CFG) & DDR_SDRAM_CFG_MEM_EN)) {
        return;
    }

    /* Set early for clock / pin */
    set32(DDR_SDRAM_CLK_CNTL, DDR_SDRAM_CLK_CNTL_VAL);

    /* Setup DDR CS (chip select) bounds */
    set32(DDR_CS_BNDS(0), DDR_CS0_BNDS_VAL);
    set32(DDR_CS_CONFIG(0), DDR_CS0_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(0), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(1), DDR_CS1_BNDS_VAL);
    set32(DDR_CS_CONFIG(1), DDR_CS1_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(1), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(2), DDR_CS2_BNDS_VAL);
    set32(DDR_CS_CONFIG(2), DDR_CS2_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(2), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(3), DDR_CS3_BNDS_VAL);
    set32(DDR_CS_CONFIG(3), DDR_CS3_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(3), DDR_CS_CONFIG_2_VAL);

    /* DDR SDRAM timing configuration */
    set32(DDR_TIMING_CFG_3, DDR_TIMING_CFG_3_VAL);
    set32(DDR_TIMING_CFG_0, DDR_TIMING_CFG_0_VAL);
    set32(DDR_TIMING_CFG_1, DDR_TIMING_CFG_1_VAL);
    set32(DDR_TIMING_CFG_2, DDR_TIMING_CFG_2_VAL);
    set32(DDR_TIMING_CFG_4, DDR_TIMING_CFG_4_VAL);
    set32(DDR_TIMING_CFG_5, DDR_TIMING_CFG_5_VAL);
    set32(DDR_TIMING_CFG_6, DDR_TIMING_CFG_6_VAL);
    set32(DDR_TIMING_CFG_7, DDR_TIMING_CFG_7_VAL);
    set32(DDR_TIMING_CFG_8, DDR_TIMING_CFG_8_VAL);

    set32(DDR_ZQ_CNTL, DDR_ZQ_CNTL_VAL);
    set32(DDR_SDRAM_CFG_3, DDR_SDRAM_CFG_3_VAL);

    /* DDR SDRAM mode configuration */
    set32(DDR_SDRAM_MODE,   DDR_SDRAM_MODE_VAL);
    set32(DDR_SDRAM_MODE_2, DDR_SDRAM_MODE_2_VAL);
    set32(DDR_SDRAM_MODE_3, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_4, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_5, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_6, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_7, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_8, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_9, DDR_SDRAM_MODE_9_VAL);
    set32(DDR_SDRAM_MODE_10, DDR_SDRAM_MODE_10_VAL);
    set32(DDR_SDRAM_MD_CNTL, DDR_SDRAM_MD_CNTL_VAL);

    /* DDR Configuration */
#ifdef USE_ERRATA_DDRA009663
    /* Errata A-009663 - DRAM VRef training (do not set precharge interval till after enable) */
    set32(DDR_SDRAM_INTERVAL, DDR_SDRAM_INTERVAL_VAL & ~DDR_SDRAM_INTERVAL_BSTOPRE);
#else
    set32(DDR_SDRAM_INTERVAL, DDR_SDRAM_INTERVAL_VAL);
#endif
    set32(DDR_DATA_INIT, DDR_DATA_INIT_VAL);
    set32(DDR_WRLVL_CNTL, DDR_WRLVL_CNTL_VAL);
    set32(DDR_WRLVL_CNTL_2, DDR_WRLVL_CNTL_2_VAL);
    set32(DDR_WRLVL_CNTL_3, DDR_WRLVL_CNTL_3_VAL);
    set32(DDR_SR_CNTR, 0);
    set32(DDR_SDRAM_RCW_1, 0);
    set32(DDR_SDRAM_RCW_2, 0);
    set32(DDR_SDRAM_RCW_3, 0);
    set32(DDR_SDRAM_RCW_4, 0);
    set32(DDR_SDRAM_RCW_5, 0);
    set32(DDR_SDRAM_RCW_6, 0);
    set32(DDR_DDRCDR_1, DDR_DDRCDR_1_VAL);
    set32(DDR_SDRAM_CFG_2, (DDR_SDRAM_CFG_2_VAL | DDR_SDRAM_CFG_2_D_INIT));
    set32(DDR_INIT_ADDR, 0);
    set32(DDR_INIT_EXT_ADDR, 0);
    set32(DDR_DDRCDR_2, DDR_DDRCDR_2_VAL);
    set32(DDR_ERR_DISABLE, 0);
    set32(DDR_ERR_INT_EN, DDR_ERR_INT_EN_VAL);
    set32(DDR_ERR_SBE, DDR_ERR_SBE_VAL);

    /* Set values, but do not enable the DDR yet */
    set32(DDR_SDRAM_CFG, DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN);
    __asm__ __volatile__("sync;isync");

    /* busy wait for ~500us */
    udelay(500);
    __asm__ __volatile__("sync;isync");

    /* Enable controller */
    reg = get32(DDR_SDRAM_CFG) & ~DDR_SDRAM_CFG_BI;
    set32(DDR_SDRAM_CFG, reg | DDR_SDRAM_CFG_MEM_EN);
    __asm__ __volatile__("sync;isync");

#ifdef USE_ERRATA_DDRA008378
    /* Errata A-008378: training in DDR4 mode */
    /* write to DEBUG_29[8:11] a value of 4'b1001 before controller is enabled */
    reg = get32(DDR_DEBUG_29);
    reg |= (0x9 << 20);
    set32(DDR_DEBUG_29, reg);
#endif
#ifdef USE_ERRATA_DDRA008109
    /* Errata A-008109: Memory controller could fail to complete initialization */
    reg = get32(DDR_SDRAM_CFG_2);
    reg |= 0x800; /* set DDR_SLOW */
    set32(DDR_SDRAM_CFG_2, reg);
    reg = get32(DDR_DEBUG_19);
    reg |= 0x2;
    set32(DDR_DEBUG_19, reg);
    set32(DDR_DEBUG_29, 0x30000000);
#endif
#ifdef USE_ERRATA_DDRA009942
    /* Errata A-009942: DDR controller can train to non-optimal setting */
    reg = get32(DDR_DEBUG_29);
    reg &= ~0xFF0FFF00;
    reg |=  0x0070006F; /* CPO calculated */
    set32(DDR_DEBUG_29, reg);
#endif

    /* Wait for data initialization to complete */
    while (get32(DDR_SDRAM_CFG_2) & DDR_SDRAM_CFG_2_D_INIT) {
        /* busy wait loop - throttle polling */
        udelay(10000);
    }

#ifdef USE_ERRATA_DDRA009663
    /* Errata A-009663 - Write real precharge interval */
    set32(DDR_SDRAM_INTERVAL, DDR_SDRAM_INTERVAL_VAL);
#endif
#endif
}


void hal_early_init(void)
{
    /* enable timebase on core 0 */
    set32(RCPM_PCTBENR, (1 << 0));

    /* invalidate the CPC before DDR gets enabled */
    set32((volatile uint32_t*)(CPC_BASE + CPCCSR0),
        (CPCCSR0_CPCFI | CPCCSR0_CPCLFC));
    while (get32((volatile uint32_t*)(CPC_BASE + CPCCSR0)) &
        (CPCCSR0_CPCFI | CPCCSR0_CPCLFC));

    /* set DCSRCR space = 1G */
    set32(DCFG_DCSR, (get32(DCFG_DCSR) | CORENET_DCSR_SZ_1G));
    get32(DCFG_DCSR); /* read again */

    /* disable devices */
    set32(DCFG_DEVDISR1,
        ((1 << 19) | /* Disable USB1 */
         (1 << 18) | /* Disable USB2 */
         (1 << 15) | /* SATA1 */
         (1 << 2)    /* DIU (LCD) */
    ));

    set32(DCFG_DEVDISR3,
        (1 << 30) /* Disable PEX2 (PCIe2) */
    );

    hal_ddr_init();
}

#ifdef ENABLE_PCIE

/* PCI IO read/write functions */
/* Intel PCI addr/data mappings for compatibility with our PCI driver */
#define PCI_CONFIG_ADDR_PORT 0xcf8
#define PCI_CONFIG_DATA_PORT 0xcfc
static int pcie_bus = 0;
/* See T1024RM 27.12.1.2.3 Byte order for configuration transactions */
void io_write32(uint16_t port, uint32_t value)
{
    if (port == PCI_CONFIG_ADDR_PORT) {
        //wolfBoot_printf("WRITE32 Addr %x\n", value);
        set32(PCIE_CONFIG_ADDR(pcie_bus), value);
    }
    else if (port == PCI_CONFIG_DATA_PORT) {
        //wolfBoot_printf("WRITE32 Data %x\n", value);
    #ifdef BIG_ENDIAN_ORDER
        value = __builtin_bswap32(value);
    #endif
        set32(PCIE_CONFIG_DATA(pcie_bus), value);
    }
}
uint32_t io_read32(uint16_t port)
{
    uint32_t value = 0;
    if (port == PCI_CONFIG_ADDR_PORT) {
        value = get32(PCIE_CONFIG_ADDR(pcie_bus));
        //wolfBoot_printf("READ32 Addr %x\n", value);
    }
    else if (port == PCI_CONFIG_DATA_PORT) {
        value = get32(PCIE_CONFIG_DATA(pcie_bus));
    #ifdef BIG_ENDIAN_ORDER
        value = __builtin_bswap32(value);
    #endif
        //wolfBoot_printf("READ32 Data %x\n", value);
    }
    return value;
}

#define CONFIG_PCIE_MEM_BUS              0xE0000000
#define CONFIG_PCIE_IO_BASE              0x2000
#define CONFIG_PCIE_MEM_LENGTH          (0x10000000)
#define CONFIG_PCIE_MEM_PREFETCH_LENGTH (0x100000)

#define CONFIG_PCIE1_MEM_PHYS_HIGH      0xCULL
#define CONFIG_PCIE1_MEM_PHYS           0x00000000
#define CONFIG_PCIE1_MEM_VIRT           0x80000000
#define CONFIG_PCIE1_IO_PHYS_HIGH       0xFULL
#define CONFIG_PCIE1_IO_PHYS            0xF8000000
#define CONFIG_PCIE1_IO_VIRT            CONFIG_PCIE1_IO_PHYS

#define CONFIG_PCIE2_MEM_PHYS_HIGH      0xCULL
#define CONFIG_PCIE2_MEM_PHYS           0x10000000
#define CONFIG_PCIE2_MEM_VIRT           0x90000000
#define CONFIG_PCIE2_IO_PHYS_HIGH       0xFULL
#define CONFIG_PCIE2_IO_PHYS            0xF8010000
#define CONFIG_PCIE2_IO_VIRT            CONFIG_PCIE2_IO_PHYS

#define CONFIG_PCIE3_MEM_PHYS_HIGH      0xCULL
#define CONFIG_PCIE3_MEM_PHYS           0x20000000
#define CONFIG_PCIE3_MEM_VIRT           0xA0000000
#define CONFIG_PCIE3_IO_PHYS_HIGH       0xFULL
#define CONFIG_PCIE3_IO_PHYS            0xF8020000
#define CONFIG_PCIE3_IO_VIRT            CONFIG_PCIE3_IO_PHYS

static int hal_pcie_init(void)
{
    int ret;
    int law_idx = 8;
    int tlb_idx = 14; /* next available TLB (after DDR) */
    struct pci_enum_info enum_info;
    uint64_t mem_phys_h, io_phys_h;
    uint32_t mem_phys, io_phys;
    uint32_t mem_virt, io_virt;
    uint32_t rcw4, srds_prtcl_s1;
    uint16_t cpld_pci;

    /* Configure Lane B */
    cpld_pci = CPLD_READ(PCI_STATUS_ADDR);
    rcw4 = get32(DCFG_RCWSR(4));
    srds_prtcl_s1 = (rcw4 & RCWSR4_SRDS1_PRTCL) >> RCWSR4_SRDS1_PRTCL_SHIFT;
    wolfBoot_printf("CPLD PCI 0x%x, RCW4 0x%x, SRDS1_PRTCL 0x%x\n",
        cpld_pci, rcw4, srds_prtcl_s1);
    if (srds_prtcl_s1 == 0x95) {
        /* Route Lane B to PCIE */
        CPLD_WRITE(PCI_STATUS_ADDR, cpld_pci & ~CPLD_PCIE_SGMII_MUX);
        wolfBoot_printf("Route Lane B->PCIE\n");
    }
    else {
        /* Route Lane B to SGMII */
        CPLD_WRITE(PCI_STATUS_ADDR, cpld_pci | CPLD_PCIE_SGMII_MUX);
        wolfBoot_printf("Route Lane B->SGMII\n");
    }
    cpld_pci = CPLD_READ(PCI_STATUS_ADDR);
    wolfBoot_printf("CPLD PCI 0x%x\n", cpld_pci);

    for (pcie_bus=1; pcie_bus<=PCIE_MAX_CONTROLLERS; pcie_bus++) {
        /* Check device disable register */
        if (get32(DCFG_DEVDISR3) & (1 << (32-pcie_bus))) {
            wolfBoot_printf("PCIe %d: Disabled\n", pcie_bus);
            continue;
        }

        /* Read block revision */
        wolfBoot_printf("PCIe %d: Base 0x%x, Rev 0x%x\n",
            pcie_bus, PCIE_BASE(pcie_bus),
            get32(PCIE_BLK_REV1(pcie_bus)));

        /* Setup PCIe memory regions */
        if (pcie_bus == 1) {
            mem_virt = CONFIG_PCIE1_MEM_VIRT;
            io_virt = CONFIG_PCIE1_IO_VIRT;
            mem_phys_h = CONFIG_PCIE1_MEM_PHYS_HIGH;
            mem_phys = CONFIG_PCIE1_MEM_PHYS;
            io_phys_h = CONFIG_PCIE1_IO_PHYS_HIGH;
            io_phys = CONFIG_PCIE1_IO_PHYS;
        }
        else if (pcie_bus == 2) {
            mem_virt = CONFIG_PCIE2_MEM_VIRT;
            io_virt = CONFIG_PCIE2_IO_VIRT;
            mem_phys_h = CONFIG_PCIE2_MEM_PHYS_HIGH;
            mem_phys = CONFIG_PCIE2_MEM_PHYS;
            io_phys_h = CONFIG_PCIE2_IO_PHYS_HIGH;
            io_phys = CONFIG_PCIE2_IO_PHYS;
        }
        else if (pcie_bus == 3) {
            mem_virt = CONFIG_PCIE3_MEM_VIRT;
            io_virt = CONFIG_PCIE3_IO_VIRT;
            mem_phys_h = CONFIG_PCIE3_MEM_PHYS_HIGH;
            mem_phys = CONFIG_PCIE3_MEM_PHYS;
            io_phys_h = CONFIG_PCIE3_IO_PHYS_HIGH;
            io_phys = CONFIG_PCIE3_IO_PHYS;
        }

        /* LAW_TRGT_PCIE1 = 0, LAW_TRGT_PCIE2 = 1, LAW_TRGT_PCIE1 = 2 */
        set_law(law_idx++, mem_phys_h, mem_phys, pcie_bus-1, LAW_SIZE_256MB, 1);
        set_law(law_idx++, io_phys_h, io_phys,  pcie_bus-1, LAW_SIZE_64KB, 1);

        /* Map TLB for PCIe */
        set_tlb(1, tlb_idx++, mem_virt,
            mem_phys, mem_phys_h,
            (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), 0,
            BOOKE_PAGESZ_256M, 1);
        set_tlb(1, tlb_idx++, io_virt,
            io_phys, io_phys_h,
            (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), 0,
            BOOKE_PAGESZ_64K, 1);

        /* PCI I/O Base */
        memset(&enum_info, 0, sizeof(enum_info));
        enum_info.curr_bus_number = 0;
        enum_info.mem = CONFIG_PCIE_MEM_BUS;
        enum_info.mem_limit = enum_info.mem + (CONFIG_PCIE_MEM_LENGTH - 1);
        enum_info.mem_pf = (enum_info.mem + CONFIG_PCIE_MEM_PREFETCH_LENGTH);
        enum_info.mem_pf_limit = enum_info.mem_pf +
            (CONFIG_PCIE_MEM_PREFETCH_LENGTH - 1);
        enum_info.io = CONFIG_PCIE_IO_BASE;

        /* Setup PCIe Output Windows */
        /* See T1024RM: 27.12.1.5 PCI Express outbound ATMUs */
        set32( PCIE_OTAR(pcie_bus, 0), 0x0);
        set32(PCIE_OTEAR(pcie_bus, 0), 0x0);
        set32( PCIE_OWAR(pcie_bus, 0), (POWAR_EN | POWAR_MEM_READ |
            POWAR_MEM_WRITE | LAW_SIZE_1TB));

        /* Outbound Memory */
        set32( PCIE_OTAR(pcie_bus, 1), (CONFIG_PCIE_MEM_BUS >> 12));
        set32(PCIE_OTEAR(pcie_bus, 1), 0x0);
        set32(PCIE_OWBAR(pcie_bus, 1), ((mem_phys_h << 32 | mem_phys) >> 12));
        set32( PCIE_OWAR(pcie_bus, 1), (POWAR_EN | POWAR_MEM_READ |
            POWAR_MEM_WRITE | LAW_SIZE_256MB));

        /* Outbound IO */
        set32( PCIE_OTAR(pcie_bus, 2), 0x0);
        set32(PCIE_OTEAR(pcie_bus, 2), 0x0);
        set32(PCIE_OWBAR(pcie_bus, 2), ((io_phys_h << 32) | io_phys) >> 12);
        set32( PCIE_OWAR(pcie_bus, 2), (POWAR_EN | POWAR_IO_READ |
            POWAR_IO_WRITE | LAW_SIZE_64KB));

        /* Disabled */
        set32( PCIE_OTAR(pcie_bus, 3), 0x0);
        set32(PCIE_OTEAR(pcie_bus, 3), 0x0);
        set32(PCIE_OWBAR(pcie_bus, 3), 0x0);
        set32( PCIE_OWAR(pcie_bus, 3), 0x0);

        /* Setup PCIe Input Windows */
        /* See T1024RM: 27.12.1.6 PCI Express inbound ATMUs */
        /* CCSRBAR */
        set32(  PCIE_ITAR(pcie_bus, 0), (CCSRBAR_PHYS >> 12));
        set32(  PCIE_IWAR(pcie_bus, 0), (PIWAR_EN | PIWAR_TRGT_CCSR |
            PIWAR_READ | PIWAR_WRITE | LAW_SIZE_16MB));

        /* Map DDR to PCIe */
        set32(  PCIE_ITAR(pcie_bus, 1), (DDR_ADDRESS >> 12));
        set32( PCIE_IWBAR(pcie_bus, 1), (DDR_ADDRESS >> 12));
        set32(PCIE_IWBEAR(pcie_bus, 1), 0x0);
        set32(  PCIE_IWAR(pcie_bus, 1), (PIWAR_EN | PIWAR_PF |
            PIWAR_TRGT_LOCAL | PIWAR_READ_SNOOP | PIWAR_WRITE_SNOOP | LAW_SIZE_2GB));

        /* Map DDR High (64GB) to PCIe */
        set32(  PCIE_ITAR(pcie_bus, 2), (DDR_ADDRESS >> 12));
        set32( PCIE_IWBAR(pcie_bus, 2), ((64ull*1024*1024*1024) >> 12));
        set32(PCIE_IWBEAR(pcie_bus, 2), 0x0);
        set32(  PCIE_IWAR(pcie_bus, 2), (PIWAR_EN | PIWAR_PF |
            PIWAR_TRGT_LOCAL | PIWAR_READ_SNOOP | PIWAR_WRITE_SNOOP | LAW_SIZE_2GB));

        /* Disabled */
        set32(  PCIE_ITAR(pcie_bus, 3), 0x0);
        set32( PCIE_IWBAR(pcie_bus, 3), 0x0);
        set32(PCIE_IWBEAR(pcie_bus, 3), 0x0);
        set32(  PCIE_IWAR(pcie_bus, 3), (PIWAR_PF | PIWAR_TRGT_LOCAL |
            PIWAR_READ | PIWAR_WRITE | LAW_SIZE_1TB));

        #define PCI_LTSSM    0x404  /* PCIe Link Training, Status State Machine */
        #define PCI_LTSSM_L0 0x16   /* L0 state */

        /* TODO: Check if link is active. Read config PCI_LTSSM */
    #if 0
        link = pci_config_read16(0, 0, 0, PCI_LTSSM);
        enabled = (link >= PCI_LTSSM_L0);
    #endif
    }

    /* Only enumerate PCIe 3 */
    pcie_bus = 3;
    ret = pci_enum_bus(0, &enum_info);
    if (ret != 0) {
        wolfBoot_printf("PCIe %d: Enum failed %d\n", pcie_bus, ret);
    }

    return ret;
}
#endif

#if defined(ENABLE_CPLD) || defined(ENABLE_MRAM)
static void hal_ifc_init(uint8_t ifc, uint32_t base, uint32_t base_high,
    uint32_t port_sz, uint32_t amask)
{
    /* CPLD IFC Timing Parameters */
    set32(IFC_FTIM0(ifc), (IFC_FTIM0_GPCM_TACSE(14UL) |
                           IFC_FTIM0_GPCM_TEADC(14UL) |
                           IFC_FTIM0_GPCM_TEAHC(14UL)));
    set32(IFC_FTIM1(ifc), (IFC_FTIM1_GPCM_TACO(14UL) |
                           IFC_FTIM1_GPCM_TRAD(31UL)));
    set32(IFC_FTIM2(ifc), (IFC_FTIM2_GPCM_TCS(14UL) |
                           IFC_FTIM2_GPCM_TCH(8UL) |
                           IFC_FTIM2_GPCM_TWP(31UL)));
    set32(IFC_FTIM3(ifc), 0);

    /* CPLD IFC Definitions (CS2) */
    set32(IFC_CSPR_EXT(ifc), base_high);
    set32(IFC_CSPR(ifc),     (IFC_CSPR_PHYS_ADDR(base) |
                              port_sz |
                              IFC_CSPR_MSEL_GPCM |
                              IFC_CSPR_V));
    set32(IFC_AMASK(ifc), amask);
    set32(IFC_CSOR(ifc), 0);
}
#endif

#ifdef ENABLE_MRAM
static void hal_mram_init(void)
{
    /* MRAM IFC Timing Parameters */
    hal_ifc_init(1, MRAM_BASE, MRAM_BASE_PHYS_HIGH,
        IFC_CSPR_PORT_SIZE_8, IFC_AMASK_1MB);

    /* MRAM IFC 1 - LAW 7, TLB 1.4 */
    set_law(7, MRAM_BASE_PHYS_HIGH, MRAM_BASE, LAW_TRGT_IFC, LAW_SIZE_1MB, 1);
    set_tlb(1, 4, MRAM_BASE,
                  MRAM_BASE, MRAM_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR),
        (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_1M, 1);
}
#endif

#if defined(ENABLE_CPLD) && defined(DEBUG)
void hal_cpld_dump(void)
{
    wolfBoot_printf("\n--------------------\n");
    wolfBoot_printf("CPLD Dump\n");
    wolfBoot_printf("BOARD_ID_L_Addr     = 0x%04x\n", CPLD_READ(BOARD_ID_L_ADDR));
    wolfBoot_printf("BOARD_ID_H_Addr     = 0x%04x\n", CPLD_READ(BOARD_ID_H_ADDR));
    wolfBoot_printf("PLD_VER_Addr        = 0x%04x\n", CPLD_READ(PLD_VER_ADDR));
    wolfBoot_printf("Power_Status_Addrr  = 0x%04x\n", CPLD_READ(POWER_STATUS_ADDRR));
    wolfBoot_printf("MPU_Int_Status_Addr = 0x%04x\n", CPLD_READ(MPU_INT_STATUS_ADDR));
    wolfBoot_printf("MPU_Int_Enable_Addr = 0x%04x\n", CPLD_READ(MPU_INT_ENABLE_ADDR));
    wolfBoot_printf("MPU_Control_Addr    = 0x%04x\n", CPLD_READ(MPU_CONTROL_ADDR));
    wolfBoot_printf("MPU_Reset_Addr      = 0x%04x\n", CPLD_READ(MPU_RESET_ADDR));
    wolfBoot_printf("PCI_Status_Addr     = 0x%04x\n", CPLD_READ(PCI_STATUS_ADDR));
    wolfBoot_printf("HS_CSR_Addr         = 0x%04x\n", CPLD_READ(HS_CSR_ADDR));
    wolfBoot_printf("CPCI_GA_Addr        = 0x%04x\n", CPLD_READ(CPCI_GA_ADDRS));
    wolfBoot_printf("CPCI_INTx_Addr      = 0x%04x\n", CPLD_READ(CPCI_INTX_ADDR));
    wolfBoot_printf("\n--------------------\n");
}
#endif

static void hal_cpld_init(void)
{
#ifdef ENABLE_CPLD
    uint32_t reg;

    /* CPLD (APU) IFC 2 - LAW 2, TLB 1.11 */
    hal_ifc_init(2, CPLD_BASE, CPLD_BASE_PHYS_HIGH,
        IFC_CSPR_PORT_SIZE_16, IFC_AMASK_64KB);
    set_law(2, CPLD_BASE_PHYS_HIGH, CPLD_BASE, LAW_TRGT_IFC, LAW_SIZE_64KB, 1);
    set_tlb(1, 11, CPLD_BASE,
                   CPLD_BASE, CPLD_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR),
        (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_256K, 1);

    /* CPLD (MPU) IFC 3 - LAW 6, TLB 1.10 */
    hal_ifc_init(3, CPLD_MPU_BASE, CPLD_MPU_BASE_PHYS_HIGH,
        IFC_CSPR_PORT_SIZE_16, IFC_AMASK_64KB);
    set_law(6, CPLD_MPU_BASE_PHYS_HIGH, CPLD_MPU_BASE, LAW_TRGT_IFC,
        LAW_SIZE_64KB, 1);
    set_tlb(1, 10, CPLD_MPU_BASE,
                   CPLD_MPU_BASE, CPLD_MPU_BASE_PHYS_HIGH,
        (MAS3_SX | MAS3_SW | MAS3_SR),
        (MAS2_I | MAS2_G), 0, BOOKE_PAGESZ_256K, 1);

    reg  = CPLD_READ(BOARD_ID_L_ADDR) << 16;
    reg |= CPLD_READ(BOARD_ID_H_ADDR);
    wolfBoot_printf("CPLD BOARD_ID: 0x%x\n", reg);
    reg = CPLD_READ(PLD_VER_ADDR);
    wolfBoot_printf("CPLD PLD_VER: 0x%x\n", reg);

#ifdef DEBUG
    hal_cpld_dump();
#endif
#endif /* ENABLE_CPLD */
}


/* QE Microcode Loading */
#if defined(ENABLE_QE) || defined(ENABLE_FMAN)

/* Structure packing */
#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
    defined(__GNUC__)
    #define QE_PACKED __attribute__ ((packed))
#else
    #define QE_PACKED
#endif

/* QE based on work from Shlomi Gridish and Dave Liu at Freescale/NXP */
struct qe_header {
    uint32_t length;      /* Length of the entire structure, in bytes */
    uint8_t  magic[3];    /* Set to { 'Q', 'E', 'F' } */
    uint8_t  version;     /* Version of this layout. First ver is '1' */
} QE_PACKED;

struct qe_soc {
    uint16_t model;       /* The SOC model  */
    uint8_t  major;       /* The SOC revision major */
    uint8_t  minor;       /* The SOC revision minor */
} QE_PACKED;

struct qe_microcode {
    uint8_t  id[32];      /* Null-terminated identifier */
    uint32_t traps[16];   /* Trap addresses, 0 == ignore */
    uint32_t eccr;        /* The value for the ECCR register */
    uint32_t iram_offset; /* Offset into I-RAM for the code */
    uint32_t count;       /* Number of 32-bit words of the code */
    uint32_t code_offset; /* Offset of the actual microcode */
    uint8_t  major;       /* The microcode version major */
    uint8_t  minor;       /* The microcode version minor */
    uint8_t  revision;    /* The microcode version revision */
    uint8_t  padding;     /* Reserved, for alignment */
    uint8_t  reserved[4]; /* Reserved, for future expansion */
} QE_PACKED;

struct qe_firmware {
    struct qe_header    header;
    uint8_t             id[62];         /* Null-terminated identifier string */
    uint8_t             split;          /* 0 = shared I-RAM, 1 = split I-RAM */
    uint8_t             count;          /* Number of microcode[] structures */
    struct qe_soc       soc;
    uint8_t             padding[4];     /* Reserved, for alignment */
    uint64_t            extended_modes; /* Extended modes */
    uint32_t            vtraps[8];      /* Virtual trap addresses */
    uint8_t             reserved[4];    /* Reserved, for future expansion */
    struct qe_microcode microcode[1];
    /* All microcode binaries should be located here */
    /* CRC32 should be located here, after the microcode binaries */
} QE_PACKED;


/* Checks for valid QE firmware */
static int qe_check_firmware(const struct qe_firmware *firmware, const char* t)
{
    unsigned int i;
#ifdef ENABLE_QE_CRC32
    uint32_t crc;
#endif
    size_t calc_size = sizeof(struct qe_firmware);
    size_t length;
    const struct qe_header *hdr;

    hdr = &firmware->header;
    length = hdr->length;

    /* Check the magic */
    if ((hdr->magic[0] != 'Q') || (hdr->magic[1] != 'E') ||
        (hdr->magic[2] != 'F')) {
        wolfBoot_printf("%s: firmware header invalid!\n", t);
        return -1;
    }

    /* Check the version */
    if (hdr->version != 1) {
        wolfBoot_printf("%s: version %d unsupported!\n", t, hdr->version);
        return -1;
    }

    /* Validate some of the fields */
    if ((firmware->count < 1) || (firmware->count > QE_MAX_RISC)) {
        wolfBoot_printf("%s: count %d invalid!\n", t, firmware->count);
        return -1;
    }

    /* Validate the length and check if there's a CRC */
    calc_size += (firmware->count - 1) * sizeof(struct qe_microcode);
    for (i = 0; i < firmware->count; i++) {
        /* For situations where the second RISC uses the same microcode
         * as the first, the 'code_offset' and 'count' fields will be
         * zero, so it's okay to add those. */
        calc_size += sizeof(uint32_t) * firmware->microcode[i].count;
    }

    /* Validate the length */
    if (length != calc_size + sizeof(uint32_t)) {
        wolfBoot_printf("%s: length %d invalid!\n", t, length);
        return -1;
    }

#ifdef ENABLE_QE_CRC32
    /* Validate the CRC */
    crc = *(uint32_t *)((void *)firmware + calc_size);
    if (crc != (crc32(-1, (const void *) firmware, calc_size) ^ -1)) {
        wolfBoot_printf("%s: firmware CRC is invalid\n", t);
        return -1;
    }
#endif

    wolfBoot_printf("%s: Firmware: Length %d, Count %d\n",
        t, length, firmware->count);

    return 0;
}
#endif /* ENABLE_QE || ENABLE_FMAN */


struct liodn_id_table {
    const char* compat;
    uint32_t id;
    void* reg_offset;
};
#define SET_LIODN(fdtcomp, liodn, reg) \
    {.compat = fdtcomp, .id = liodn, .reg_offset = (void*)reg}

static const struct liodn_id_table liodn_tbl[] = {
    SET_LIODN("fsl-usb2-mph", 553, DCFG_USB1LIODNR),
    SET_LIODN("fsl-usb2-dr", 554, DCFG_USB2LIODNR),
    SET_LIODN("fsl,esdhc", 552, DCFG_SDMMCLIODNR),
    SET_LIODN("fsl,pq-sata-v2", 555, DCFG_SATALIODNR),
    SET_LIODN("fsl,tdm1.0", 560, DCFG_TDMDMALIODNR),
    SET_LIODN("fsl,qe", 559, DCFG_QELIODNR),
    SET_LIODN("fsl,elo3-dma", 147, DCFG_DMA1LIODNR),
    SET_LIODN("fsl,elo3-dma", 227, DCFG_DMA2LIODNR),
    SET_LIODN("fsl,fman-port-1g-rx", 0x425, NULL),
    SET_LIODN("fsl,fman-port-1g-rx", 0x426, NULL),
    SET_LIODN("fsl,fman-port-1g-rx", 0x427, NULL),
    SET_LIODN("fsl,fman-port-1g-rx", 0x428, NULL),
    SET_LIODN("fsl,qman", 62, QMAN_LIODNR),
    SET_LIODN("fsl,bman", 63, BMAN_LIODNR),
    SET_LIODN("fsl,qoriq-pcie", 148, PCIE_LIODN(1)),
    SET_LIODN("fsl,qoriq-pcie", 228, PCIE_LIODN(2)),
    SET_LIODN("fsl,qoriq-pcie", 308, PCIE_LIODN(3)),
};

/* Logical I/O Device Number */
void hal_liodn_init(void)
{
    int i;
    for (i=0; i<(int)(sizeof(liodn_tbl)/sizeof(struct liodn_id_table)); i++) {
        if (liodn_tbl[i].reg_offset != NULL) {
            wolfBoot_printf("LIODN %s: %p=%d\n",
                liodn_tbl[i].compat, liodn_tbl[i].reg_offset, liodn_tbl[i].id);
            set32(liodn_tbl[i].reg_offset, liodn_tbl[i].id);
        }
    }
}

/* ---- QUICC Engine Driver ---- */
#ifdef ENABLE_QE

struct qportal_info {
    uint16_t   dliodn;    /* DQRR LIODN */
    uint16_t   fliodn;    /* frame data LIODN */
    uint16_t   liodn_offset;
    uint8_t    sdest;
};

#define SET_QP_INFO(dqrr, fdata, off, dest) \
    { .dliodn = dqrr, .fliodn = fdata, .liodn_offset = off, .sdest = dest }

static const struct qportal_info qp_info[QMAN_NUM_PORTALS] = {
    /* dqrr liodn, frame data liodn, liodn off, sdest */
    SET_QP_INFO(1, 27, 1, 0),
    SET_QP_INFO(2, 28, 1, 0),
    SET_QP_INFO(3, 29, 1, 1),
    SET_QP_INFO(4, 30, 1, 1),
    SET_QP_INFO(5, 31, 1, 2),
    SET_QP_INFO(6, 32, 1, 2),
    SET_QP_INFO(7, 33, 1, 3),
    SET_QP_INFO(8, 34, 1, 3),
    SET_QP_INFO(9, 35, 1, 0),
    SET_QP_INFO(10, 36, 1, 0)
};

static void qe_upload_microcode(const struct qe_firmware *firmware,
    const struct qe_microcode *ucode)
{
    const uint32_t *code = (uint32_t*)((uint8_t *)firmware + ucode->code_offset);
    unsigned int i;

    wolfBoot_printf("QE: uploading '%s' version %u.%u.%u\n",
        ucode->id, ucode->major, ucode->minor, ucode->revision);

    /* Use auto-increment */
    set32(QE_IRAM_IADD, ucode->iram_offset |
        QE_IRAM_IADD_AIE | QE_IRAM_IADD_BADDR);

    /* Copy 32-bits at a time to iRAM */
    for (i = 0; i < ucode->count; i++) {
        set32(QE_IRAM_IDATA, code[i]);
    }
}

/* Upload a microcode to the I-RAM at a specific address */
static int qe_upload_firmware(const struct qe_firmware *firmware)
{
    unsigned int i, j;

    /* Use common instruction RAM if not split (default is split) */
    if (!firmware->split) {
        set16(QE_CP_CERCR, get16(QE_CP_CERCR) | QE_CP_CERCR_CIR);
    }

    /* Loop through each microcode. */
    for (i = 0; i < firmware->count; i++) {
        const struct qe_microcode *ucode = &firmware->microcode[i];
        uint32_t trapCount = 0;

        /* Upload a microcode if it's present */
        if (ucode->code_offset) {
            qe_upload_microcode(firmware, ucode);
        }

        /* Program the traps for this processor (max 16) */
        for (j = 0; j < 16; j++) {
            uint32_t trap = ucode->traps[j];
            if (trap) {
                trapCount++;
                set32(QE_RSP_TIBCR(i, j), trap);
            }
        }

        /* Enable traps */
        set32(QE_RSP_ECCR(i), ucode->eccr);
        wolfBoot_printf("QE: Traps %d\n", trapCount);
    }

    return 0;
}

static void qe_issue_cmd(uint32_t cmd, uint32_t sbc, uint8_t mcn,
    uint32_t cmd_data)
{
    set32(QE_CP_CECDR, cmd_data);
    set32(QE_CP_CECR,
        sbc |       /* sub block code */
        QE_CR_FLG | /* flag: set by software, cleared by hardware */
        ((uint32_t)mcn << QE_CR_PROTOCOL_SHIFT) | /* MCC/QMC channel number */
        cmd         /* opcode (reset sets 0x8000_0000) */
    );

    /* Wait for the command semaphore flag to clear */
    while (get32(QE_CP_CECR) & QE_CR_FLG);
}

static int hal_qe_init(void)
{
    int ret, i;
    uint32_t sdma_base;
    const struct qe_firmware* fw = (const struct qe_firmware*)QE_FW_ADDR;

    /* setup QE clk */
    set32(SCFG_QEIOCLKCR, get32(SCFG_QEIOCLKCR) | SCFG_QEIOCLKCR_CLK11);

    ret = qe_check_firmware(fw, "QE");
    if (ret == 0) {
        /* Upload microcode to IRAM */
        ret = qe_upload_firmware(fw);
    }
    if (ret == 0) {
        /* enable the microcode in IRAM */
        set32(QE_IRAM_IREADY, QE_IRAM_READY);

        /* Serial DMA */
        /* All of DMA transaction in bus 1 */
        set32(QE_SDMA_SDAQR, 0);
        set32(QE_SDMA_SDAQMR, 0);

        /* Allocate 2KB temporary buffer for sdma */
        sdma_base = 0; /* offset in QE_MURAM */
        set32(QE_SDMA_SDEBCR, sdma_base & QE_SDEBCR_BA_MASK);

        /* Clear sdma status */
        set32(QE_SDMA_SDSR, 0x03000000);

        /* Enable global mode on bus 1, and 2KB buffer size */
        set32(QE_SDMA_SDMR, QE_SDMR_GLB_1_MSK | (0x3 << QE_SDMR_CEN_SHIFT));

        /* Reset QUICC Engine */
        qe_issue_cmd(QE_RESET, 0, 0, 0);
    }

    /* Configure QMan software portal base address (QCSP) */
    set32(QCSP_BARE, QMAN_BASE_PHYS_HIGH);
    set32(QCSP_BAR,  QMAN_BASE_PHYS);

    /* Configure Frame Queue Descriptor (FQD) */
    set32(FQD_BAR, 0);
    set32(FQD_AR, 0);

    /* Packed Frame Descriptor Record (PFDR) */
    set32(PFDR_BARE, 0);
    set32(PFDR_BAR, 0);
    set32(PFDR_AR, 0);

    /* Inhibit BMan/QMan portals by default */
    for (i=0; i<QMAN_NUM_PORTALS; i++) {
        set32(QCSP_ISDR(i), 0x3FFFFF);
        set32(BCSP_ISDR(i), 0x7);
    }

    /* Setup LIODN */
    for (i=0; i<(int)(sizeof(qp_info)/sizeof(struct qportal_info)); i++) {
        set32(QCSP_LIO_CFG(i),
            (qp_info[i].liodn_offset << 16) | qp_info[i].dliodn);
        set32(QCSP_IO_CFG(i),
                   (qp_info[i].sdest << 16) | qp_info[i].fliodn);
    }

    /* Setup QUICC Engine UCC 1/3 Clock Route */
    set32(QE_CMXUCR1, 0);

    /* Set baud rate configuration */
    set32(BRG_BRGC(1), 0);

    /* Disable all QUICC Engine interrupts */
    set32(QEIC_CIMR, 0);

    return ret;
}
#endif /* ENABLE_QUICC */

#ifdef ENABLE_FMAN
static void fman_upload_microcode(const struct qe_firmware *firmware,
    const struct qe_microcode *ucode)
{
    const uint32_t *code = (uint32_t*)((uint8_t *)firmware + ucode->code_offset);
    unsigned int i;

    wolfBoot_printf("FMAN: uploading '%s' version %u.%u.%u\n",
        ucode->id, ucode->major, ucode->minor, ucode->revision);

    /* Use auto-increment */
    set32(FMAN_IRAM_IADD, FMAN_IRAM_IADD_AIE);

    /* Copy 32-bits at a time to iRAM */
    for (i = 0; i < ucode->count; i++) {
        set32(FMAN_IRAM_IDATA, code[i]);
    }

    /* Verify write is done */
    set32(FMAN_IRAM_IADD, 0);
    while (get32(FMAN_IRAM_IDATA) != code[0]);

    /* Enable microcode */
    set32(FMAN_IRAM_IREADY, FMAN_IRAM_READY);
}

/* Upload a microcode to the I-RAM at a specific address */
static int fman_upload_firmware(const struct qe_firmware *firmware)
{
    unsigned int i;

    /* Loop through each microcode. */
    for (i = 0; i < firmware->count; i++) {
        const struct qe_microcode *ucode = &firmware->microcode[i];

        /* Upload a microcode if it's present */
        if (ucode->code_offset) {
            fman_upload_microcode(firmware, ucode);
        }
    }

    return 0;
}

/* ----------- PHY ----------- */
#ifdef ENABLE_PHY
/* TI DP83867 */
/* PHY CTRL bits */
#define DP83867_PHYCR_FIFO_DEPTH_3_B_NIB 0x00
#define DP83867_PHYCR_FIFO_DEPTH_4_B_NIB 0x01
#define DP83867_PHYCR_FIFO_DEPTH_6_B_NIB 0x02
#define DP83867_PHYCR_FIFO_DEPTH_8_B_NIB 0x03

/* RGMIIDCTL internal delay for rx and tx */
#define DP83867_RGMIIDCTL_250_PS  0x0
#define DP83867_RGMIIDCTL_500_PS  0x1
#define DP83867_RGMIIDCTL_750_PS  0x2
#define DP83867_RGMIIDCTL_1_NS    0x3
#define DP83867_RGMIIDCTL_1_25_NS 0x4
#define DP83867_RGMIIDCTL_1_50_NS 0x5
#define DP83867_RGMIIDCTL_1_75_NS 0x6
#define DP83867_RGMIIDCTL_2_00_NS 0x7
#define DP83867_RGMIIDCTL_2_25_NS 0x8
#define DP83867_RGMIIDCTL_2_50_NS 0x9
#define DP83867_RGMIIDCTL_2_75_NS 0xA
#define DP83867_RGMIIDCTL_3_00_NS 0xB
#define DP83867_RGMIIDCTL_3_25_NS 0xC
#define DP83867_RGMIIDCTL_3_50_NS 0xD
#define DP83867_RGMIIDCTL_3_75_NS 0xE
#define DP83867_RGMIIDCTL_4_00_NS 0xF

#define DP83867_DEVADDR        0x1F

#define MII_DP83867_PHYCTRL 0x10
#define MII_DP83867_MICR    0x12
#define MII_DP83867_CFG2    0x14
#define MII_DP83867_BISCR   0x16
#define DP83867_CTRL        0x1f

/* Extended Registers */
#define DP83867_RGMIICTL    0x0032
#define DP83867_RGMIIDCTL   0x0086
#define DP83867_IO_MUX_CFG  0x0170

#define DP83867_SW_RESET    (1 << 15)
#define DP83867_SW_RESTART  (1 << 14)

/* MICR Interrupt bits */
#define MII_DP83867_MICR_AN_ERR_INT_EN          (1 << 15)
#define MII_DP83867_MICR_SPEED_CHNG_INT_EN      (1 << 14)
#define MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN   (1 << 13)
#define MII_DP83867_MICR_PAGE_RXD_INT_EN        (1 << 12)
#define MII_DP83867_MICR_AUTONEG_COMP_INT_EN    (1 << 11)
#define MII_DP83867_MICR_LINK_STS_CHNG_INT_EN   (1 << 10)
#define MII_DP83867_MICR_FALSE_CARRIER_INT_EN   (1 << 8)
#define MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN (1 << 4)
#define MII_DP83867_MICR_WOL_INT_EN             (1 << 3)
#define MII_DP83867_MICR_XGMII_ERR_INT_EN       (1 << 2)
#define MII_DP83867_MICR_POL_CHNG_INT_EN        (1 << 1)
#define MII_DP83867_MICR_JABBER_INT_EN          (1 << 0)

/* RGMIICTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_EN        (1 << 1)
#define DP83867_RGMII_RX_CLK_DELAY_EN        (1 << 0)

/* PHY CTRL bits */
#define DP83867_PHYCR_FIFO_DEPTH_SHIFT     14
#define DP83867_MDI_CROSSOVER              5
#define DP83867_MDI_CROSSOVER_AUTO         2
#define DP83867_MDI_CROSSOVER_MDIX         2
#define DP83867_PHYCTRL_SGMIIEN            0x0800
#define DP83867_PHYCTRL_RXFIFO_SHIFT       12
#define DP83867_PHYCTRL_TXFIFO_SHIFT       14

/* RGMIIDCTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_SHIFT    4

/* CFG2 bits */
#define MII_DP83867_CFG2_SPEEDOPT_10EN      0x0040
#define MII_DP83867_CFG2_SGMII_AUTONEGEN    0x0080
#define MII_DP83867_CFG2_SPEEDOPT_ENH       0x0100
#define MII_DP83867_CFG2_SPEEDOPT_CNT       0x0800
#define MII_DP83867_CFG2_SPEEDOPT_INTLOW    0x2000
#define MII_DP83867_CFG2_MASK               0x003F

#define MII_MMD_CTRL    0x0D /* MMD Access Control Register */
#define MII_MMD_DATA    0x0E /* MMD Access Data Register */

/* MMD Access Control register fields */
#define MII_MMD_CTRL_DEVAD_MASK  0x1F /* Mask MMD DEVAD*/
#define MII_MMD_CTRL_ADDR        0x0000 /* Address */
#define MII_MMD_CTRL_NOINCR      0x4000 /* no post increment */
#define MII_MMD_CTRL_INCR_RDWT   0x8000 /* post increment on reads & writes */
#define MII_MMD_CTRL_INCR_ON_WT  0xC000 /* post increment on writes only */

/* User setting - can be taken from DTS */
#define DEFAULT_RX_ID_DELAY    DP83867_RGMIIDCTL_2_25_NS
#define DEFAULT_TX_ID_DELAY    DP83867_RGMIIDCTL_2_75_NS
#define DEFAULT_FIFO_DEPTH     DP83867_PHYCR_FIFO_DEPTH_4_B_NIB

/* IO_MUX_CFG bits */
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_CTRL   0x1F

#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MAX    0x0
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN    0x1F

/* Generic MII registers */
#define MII_BMCR          0x00      /* Basic mode control register */
#define MII_BMSR          0x01      /* Basic mode status register  */
#define MII_PHYIDR1       0x02      /* PHYS ID 1                   */
#define MII_PHYIDR2       0x03      /* PHYS ID 2                   */

/* Basic mode control register */
#define BMCR_SPEED1000    0x0040    /* MSB of Speed (1000)          */
#define BMCR_CTST         0x0080    /* Collision test               */
#define BMCR_FULLDPLX     0x0100    /* Full duplex                  */
#define BMCR_ANRESTART    0x0200    /* Auto negotiation restart     */
#define BMCR_ISOLATE      0x0400    /* Disconnect DP83840 from MII  */
#define BMCR_PDOWN        0x0800    /* Powerdown the DP83840        */
#define BMCR_ANENABLE     0x1000    /* Enable auto negotiation      */
#define BMCR_SPEED100     0x2000    /* Select 100Mbps               */
#define BMCR_LOOPBACK     0x4000    /* TXD loopback bits            */
#define BMCR_RESET        0x8000    /* Reset the DP83840            */

/* Basic mode status register */
#define BMSR_ERCAP        0x0001    /* Ext-reg capability           */
#define BMSR_JCD          0x0002    /* Jabber detected              */
#define BMSR_LSTATUS      0x0004    /* Link status                  */
#define BMSR_ANEGCAPABLE  0x0008    /* Able to do auto-negotiation  */
#define BMSR_RFAULT       0x0010    /* Remote fault detected        */
#define BMSR_ANEGCOMPLETE 0x0020    /* Auto-negotiation complete    */
#define BMSR_RESV         0x00c0    /* Unused...                    */
#define BMSR_ESTATEN      0x0100    /* Extended Status in R15       */
#define BMSR_100HALF2     0x0200    /* Can do 100BASE-T2 HDX        */
#define BMSR_100FULL2     0x0400    /* Can do 100BASE-T2 FDX        */
#define BMSR_10HALF       0x0800    /* Can do 10mbps, half-duplex   */
#define BMSR_10FULL       0x1000    /* Can do 10mbps, full-duplex   */
#define BMSR_100HALF      0x2000    /* Can do 100mbps, half-duplex  */
#define BMSR_100FULL      0x4000    /* Can do 100mbps, full-duplex  */
#define BMSR_100BASE4     0x8000    /* Can do 100mbps, 4k packets   */


enum phy_interface {
    PHY_INTERFACE_MODE_NONE,
    PHY_INTERFACE_MODE_MII,
    PHY_INTERFACE_MODE_GMII,
    PHY_INTERFACE_MODE_SGMII,
    PHY_INTERFACE_MODE_SGMII_2500,
    PHY_INTERFACE_MODE_QSGMII,
    PHY_INTERFACE_MODE_TBI,
    PHY_INTERFACE_MODE_RMII,
    PHY_INTERFACE_MODE_RGMII,
    PHY_INTERFACE_MODE_RGMII_ID,
    PHY_INTERFACE_MODE_RGMII_RXID,
    PHY_INTERFACE_MODE_RGMII_TXID,
    PHY_INTERFACE_MODE_RTBI,
    PHY_INTERFACE_MODE_XGMII,
};

static int hal_get_mac_addr(int phy_addr, uint8_t* mac_addr)
{
    int ret = 0;
    phy_addr--; /* convert to zero based */
    if (phy_addr < 0 || phy_addr > 3) {
        return -1;
    }
#ifdef RTOS_INTEGRITY_OS
    /* Integrity OS Ethernet Configuration in Flash */
    #ifndef ETHERNET_CONFIG_ADDR
    #define ETHERNET_CONFIG_ADDR 0xED0E0000
    #endif
    /* Mac Addr offsets for each port */
    static const uint32_t etherAdd[4] = {
        ETHERNET_CONFIG_ADDR + 408,
        ETHERNET_CONFIG_ADDR + 372,
        ETHERNET_CONFIG_ADDR + 336,
        ETHERNET_CONFIG_ADDR + 300
    };
    memcpy(mac_addr, (void*)etherAdd[phy_addr], 6);
#else
    #ifndef DEFAULT_MAC_ADDR
    #define DEFAULT_MAC_ADDR {0xDC, 0xA7, 0xD9, 0x00, 0x06, 0xF4}
    #endif
    static const uint8_t defaultMacAddr[6] = DEFAULT_MAC_ADDR;
    memcpy(mac_addr, defaultMacAddr, sizeof(defaultMacAddr));
    mac_addr[5] += phy_addr; /* increment by port number */
#endif
    return ret;
}


struct phy_device {
    uint8_t phyaddr;
    enum phy_interface interface;
    uint8_t mac_addr[6];
};
static struct phy_device phydevs[5];

#define MDIO_PRTAD_NONE (-1)
#define MDIO_DEVAD_NONE (-1)

/* Use EMI1 */
#define MDIO_PHY_EMI  1

/* IEEE 802.3: Clause 45 (XFI/1000Base-KX) and Clause 22 (SGMII, QSGMII) */
static int hal_phy_write(struct phy_device *phydev, int dev_addr, int regnum,
    uint16_t value)
{
    uint32_t reg, clause = 45;

#ifdef DEBUG_PHY
    wolfBoot_printf("EM%d MDIO%d Write: Dev %d, Reg %d, Val 0x%x\n",
        MDIO_PHY_EMI, phydev->phyaddr, dev_addr, regnum, value);
#endif

    reg = get32(FMAN_MDIO_CFG(MDIO_PHY_EMI));
    if (dev_addr == MDIO_DEVAD_NONE) {
        clause = 22;
        dev_addr = regnum;
        set32(FMAN_MDIO_CFG(MDIO_PHY_EMI), reg &= ~MDIO_STAT_EN_C45);
    }
    else {
        set32(FMAN_MDIO_CFG(MDIO_PHY_EMI), reg |= MDIO_STAT_EN_C45);
    }
    /* Wait till bus is available */
    while (get32(FMAN_MDIO_CFG(MDIO_PHY_EMI)) & MDIO_STAT_BSY);

    /* Set the port and dev addresses */
    reg = MDIO_CTL_PORT_ADDR(phydev->phyaddr) | MDIO_CTL_DEV_ADDR(dev_addr);
    set32(FMAN_MDIO_CTRL(MDIO_PHY_EMI), reg);

    /* Set register address */
    if (clause == 45) {
        set32(FMAN_MDIO_ADDR(MDIO_PHY_EMI), MDIO_ADDR(regnum));
    }

    /* Wait till bus is available */
    while (get32(FMAN_MDIO_CFG(MDIO_PHY_EMI)) & MDIO_STAT_BSY);

    /* Write value */
    set32(FMAN_MDIO_DATA(MDIO_PHY_EMI), MDIO_DATA(value));

    /* Wait till write is complete */
    while (get32(FMAN_MDIO_DATA(MDIO_PHY_EMI)) & MDIO_DATA_BSY);

    return 0;
}

static int hal_phy_read(struct phy_device *phydev, int dev_addr, int regnum)
{
    uint32_t reg, clause = 45, mdio_dev_addr = dev_addr;

    reg = get32(FMAN_MDIO_CFG(MDIO_PHY_EMI));
    if (dev_addr == MDIO_DEVAD_NONE) {
        clause = 22;
        mdio_dev_addr = regnum;
        set32(FMAN_MDIO_CFG(MDIO_PHY_EMI), reg &= ~MDIO_STAT_EN_C45);
    }
    else {
        set32(FMAN_MDIO_CFG(MDIO_PHY_EMI), reg |= MDIO_STAT_EN_C45);
    }
    /* Wait till bus is available */
    while (get32(FMAN_MDIO_CFG(MDIO_PHY_EMI)) & MDIO_STAT_BSY);

    /* Set the port and dev addresses */
    reg = MDIO_CTL_PORT_ADDR(phydev->phyaddr) | MDIO_CTL_DEV_ADDR(mdio_dev_addr);
    set32(FMAN_MDIO_CTRL(MDIO_PHY_EMI), reg);

    /* Set register address */
    if (clause == 45) {
        set32(FMAN_MDIO_ADDR(MDIO_PHY_EMI), MDIO_ADDR(regnum));
    }

    /* Wait till bus is available */
    while (get32(FMAN_MDIO_CFG(MDIO_PHY_EMI)) & MDIO_STAT_BSY);

    /* Start read */
    reg |= MDIO_CTL_READ;
    set32(FMAN_MDIO_CTRL(MDIO_PHY_EMI), reg);

    /* Wait till read is complete */
    while (get32(FMAN_MDIO_DATA(MDIO_PHY_EMI)) & MDIO_DATA_BSY);

    /* Check for error */
    reg = get32(FMAN_MDIO_CFG(MDIO_PHY_EMI));
    if (reg & MDIO_STAT_RD_ER) {
        return 0xFFFF; /* Failure */
    }

    /* Get data */
    reg = get32(FMAN_MDIO_DATA(MDIO_PHY_EMI));

#ifdef DEBUG_PHY
    wolfBoot_printf("EM%d MDIO%d Read: Dev %d, Reg %d, Val 0x%x\n",
        MDIO_PHY_EMI, phydev->phyaddr, dev_addr, regnum, MDIO_DATA(reg));
#endif

    return MDIO_DATA(reg);
}

static inline int phy_interface_is_rgmii(struct phy_device *phydev)
{
    return (phydev->interface >= PHY_INTERFACE_MODE_RGMII &&
            phydev->interface <= PHY_INTERFACE_MODE_RGMII_TXID);
}

static inline int phy_interface_is_sgmii(struct phy_device *phydev)
{
    return (phydev->interface >= PHY_INTERFACE_MODE_SGMII &&
            phydev->interface <= PHY_INTERFACE_MODE_QSGMII);
}

int hal_phy_read_indirect(struct phy_device *phydev,
    int port_addr, int dev_addr)
{
    uint16_t value = -1;

    /* Write the desired MMD Devad */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_CTRL, DP83867_DEVADDR);

    /* Write the desired MMD register address */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_DATA, port_addr);

    /* Select the Function : DATA with no post increment */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_CTRL,
        (DP83867_DEVADDR | MII_MMD_CTRL_NOINCR));

    /* Read the content of the MMD's selected register */
    value = hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_MMD_DATA);

#ifdef DEBUG_PHY
    wolfBoot_printf("PHY Ind Read: port_addr=%d, dev_addr=%d, value=0x%x\n",
        port_addr, dev_addr, value);
#endif
    return value;
}

void hal_phy_write_indirect(struct phy_device *phydev,
    int port_addr, int dev_addr, uint16_t value)
{
    /* Write the desired MMD Devad */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_CTRL, dev_addr);

    /* Write the desired MMD register address */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_DATA, port_addr);

    /* Select the Function : DATA with no post increment */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_CTRL,
        (dev_addr | MII_MMD_CTRL_NOINCR));

    /* Write the data into MMD's selected register */
    hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_DATA, value);

#ifdef DEBUG_PHY
    wolfBoot_printf("PHY Ind Write: port_addr=%d, dev_addr=%d, value=0x%x\n",
        port_addr, dev_addr, value);
#endif
}

static const char* hal_phy_interface_str(enum phy_interface interface)
{
    switch (interface) {
        case PHY_INTERFACE_MODE_RGMII:
            return "RGMII";
        case PHY_INTERFACE_MODE_XGMII:
            return "XGMII";
        case PHY_INTERFACE_MODE_SGMII:
            return "SGMII";
        default:
            break;
    }
    return "Unknown";
}

#define PHY_TIDP83867_PHYIDR 0x2000A231
static const char* hal_phy_vendor_str(uint32_t id)
{
    switch (id) {
        case PHY_TIDP83867_PHYIDR:
            return "TI DP83867";
        default:
            break;
    }
    return "Unknown";
}

/* Support for TI DP83867IS */
static int hal_phy_init(struct phy_device *phydev)
{
    int ret;
    uint32_t val, val2;

    /* Set MAC address */
    /* Example MAC 0x12345678ABCD is:
     * MAC_ADDR0 of 0x78563412
     * MAC_ADDR1 of 0x0000CDAB */
    ret = hal_get_mac_addr(phydev->phyaddr, phydev->mac_addr);

    wolfBoot_printf("PHY %d: %s, Mac %x:%x:%x:%x:%x:%x\n",
        phydev->phyaddr, hal_phy_interface_str(phydev->interface),
        phydev->mac_addr[0], phydev->mac_addr[1],
        phydev->mac_addr[2], phydev->mac_addr[3],
        phydev->mac_addr[4], phydev->mac_addr[5]);

    if (ret != 0) {
        return ret;
    }

    /* Mask all interrupt */
    set32(FMAN_MEMAC_IMASK(phydev->phyaddr), 0x00000000);

    /* Clear all events */
    set32(FMAN_MEMAC_IEVENT(phydev->phyaddr), 0xFFFFFFFF);

    /* Set maximum RX length */
    set32(FMAN_MEMAC_MAXFRMG(phydev->phyaddr), 0x800);

    /* Disable multi-cast */
    set32(FMAN_MEMAC_HTBLE_CTRL(phydev->phyaddr), 0);

    /* Setup mEMAC */
    val = (MEMAC_CMD_CFG_RX_EN | MEMAC_CMD_CFG_TX_EN | MEMAC_CMD_CFG_NO_LEN_CHK);
    set32(FMAN_MEMAC_CMD_CFG(phydev->phyaddr), val);

    /* Set MAC Addresss */
    val =  ((phydev->mac_addr[3] << 24) | (phydev->mac_addr[2] << 16) | \
            (phydev->mac_addr[1] << 8)  |  phydev->mac_addr[0]);
    val2 = ((phydev->mac_addr[5] << 8)  |  phydev->mac_addr[4]);
    set32(FMAN_MEMAC_MAC_ADDR_0(phydev->phyaddr), val);
    set32(FMAN_MEMAC_MAC_ADDR_1(phydev->phyaddr), val2);

    /* Set interface mode */
    val =  get32(FMAN_MEMAC_IF_MODE(phydev->phyaddr));
    switch (phydev->interface) {
        case PHY_INTERFACE_MODE_GMII:
            val &= ~IF_MODE_MASK;
            val |= IF_MODE_GMII;
            break;
        case PHY_INTERFACE_MODE_RGMII:
            val |= (IF_MODE_GMII | IF_MODE_RG);
            break;
        case PHY_INTERFACE_MODE_RMII:
            val |= (IF_MODE_GMII | IF_MODE_RM);
            break;
        case PHY_INTERFACE_MODE_SGMII:
        case PHY_INTERFACE_MODE_QSGMII:
            val &= ~IF_MODE_MASK;
            val |= IF_MODE_GMII;
            break;
        case PHY_INTERFACE_MODE_XGMII:
            val &= ~IF_MODE_MASK;
            val |= IF_MODE_XGMII;
            break;
        default:
            break;
    }
    /* Enable automatic speed selection */
    val |= IF_MODE_EN_AUTO;
    set32(FMAN_MEMAC_IF_MODE(phydev->phyaddr), val);

    /* Set clock div = 258 and neg = 1 */
    set32(FMAN_MDIO_CFG(MDIO_PHY_EMI), (MDIO_STAT_CLKDIV(258) | MDIO_STAT_NEG));

    /* Read the PHY ID's */
    val =  (uint16_t)hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_PHYIDR1);
    val <<= 16;
    val |= (uint16_t)hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_PHYIDR2);
    wolfBoot_printf("PHY %d: %s (OUI %x, Mdl %x, Rev %x)\n",
        phydev->phyaddr, hal_phy_vendor_str(val),
        (val >> 10), ((val >> 4) & 0x3F), (val & 0xF)
    );

    /* Reset the PHY */
    val = hal_phy_read(phydev, MDIO_DEVAD_NONE, DP83867_CTRL);
    val |= DP83867_SW_RESTART;
    hal_phy_write(phydev, MDIO_DEVAD_NONE, DP83867_CTRL, val);
    val = hal_phy_read(phydev, MDIO_DEVAD_NONE, DP83867_CTRL);
#ifdef DEBUG_PHY
    wolfBoot_printf("DP83867_CTRL=0x%x\n", val);
#endif

    if (phy_interface_is_rgmii(phydev)) {
        val = ((DP83867_MDI_CROSSOVER_AUTO << DP83867_MDI_CROSSOVER) |
               (DP83867_PHYCR_FIFO_DEPTH_4_B_NIB << DP83867_PHYCR_FIFO_DEPTH_SHIFT));
        hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_DP83867_PHYCTRL, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_DP83867_PHYCTRL);
        wolfBoot_printf("MII_DP83867_PHYCTRL=0x%x\n", val);
    #endif
    }
    else if (phy_interface_is_sgmii(phydev)) {
        val = (BMCR_ANENABLE | BMCR_FULLDPLX | BMCR_SPEED1000);
        hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_BMCR, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);
        wolfBoot_printf("MII_BMCR=0x%x\n", val);
    #endif

        val = hal_phy_read(phydev, phydev->phyaddr, MII_DP83867_CFG2);
        val &= MII_DP83867_CFG2_MASK;
        val |= (MII_DP83867_CFG2_SPEEDOPT_10EN |
                MII_DP83867_CFG2_SGMII_AUTONEGEN |
                MII_DP83867_CFG2_SPEEDOPT_ENH |
                MII_DP83867_CFG2_SPEEDOPT_CNT |
                MII_DP83867_CFG2_SPEEDOPT_INTLOW);
        hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_DP83867_CFG2, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_DP83867_CFG2);
        wolfBoot_printf("MII_DP83867_CFG2=0x%x\n", val);
    #endif
        hal_phy_write_indirect(phydev, DP83867_RGMIICTL, DP83867_DEVADDR, 0x0);
        val = (DP83867_PHYCTRL_SGMIIEN |
              (DP83867_MDI_CROSSOVER_MDIX << DP83867_MDI_CROSSOVER) |
              (DP83867_PHYCR_FIFO_DEPTH_4_B_NIB << DP83867_PHYCTRL_RXFIFO_SHIFT) |
              (DP83867_PHYCR_FIFO_DEPTH_4_B_NIB << DP83867_PHYCTRL_TXFIFO_SHIFT));
        hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_DP83867_PHYCTRL, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read(phydev, MDIO_DEVAD_NONE, MII_DP83867_PHYCTRL);
        wolfBoot_printf("MII_DP83867_PHYCTRL=0x%x\n", val);
    #endif
        hal_phy_write(phydev, MDIO_DEVAD_NONE, MII_DP83867_BISCR, 0x0);
    }

    if (ret == 0 && phy_interface_is_rgmii(phydev)) {
        val = hal_phy_read_indirect(phydev, DP83867_RGMIICTL, MDIO_DEVAD_NONE);
        if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
            val |= (DP83867_RGMII_TX_CLK_DELAY_EN |
                    DP83867_RGMII_RX_CLK_DELAY_EN);
        }
        else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
            val |= DP83867_RGMII_TX_CLK_DELAY_EN;
        }
        else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
            val |= DP83867_RGMII_RX_CLK_DELAY_EN;
        }
        hal_phy_write_indirect(phydev, DP83867_RGMIICTL, DP83867_DEVADDR, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read_indirect(phydev, DP83867_RGMIICTL, MDIO_DEVAD_NONE);
        wolfBoot_printf("DP83867_RGMIICTL=0x%x\n", val);
    #endif

        val = (DP83867_RGMIIDCTL_1_75_NS |
              (DP83867_RGMIIDCTL_1_75_NS << DP83867_RGMII_TX_CLK_DELAY_SHIFT));
        hal_phy_write_indirect(phydev, DP83867_RGMIIDCTL, DP83867_DEVADDR, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read_indirect(phydev, DP83867_RGMIIDCTL, MDIO_DEVAD_NONE);
        wolfBoot_printf("RGMIIDCTL delay=0x%x\n", val);
    #endif

#if DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN >= 0
    #ifdef DEBUG_PHY
        wolfBoot_printf("Impedance Match 0x%x\n", DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN);
        val = hal_phy_read_indirect(phydev, DP83867_IO_MUX_CFG, MDIO_DEVAD_NONE);
        wolfBoot_printf("IOMUX (before)=0x%x\n", val);
    #endif

        /* CLK_O_SEL=Channel D transmit clock, IO_IMPEDANCE_CTRL=0x1F (max) */
        val = 0x0B1F;
        hal_phy_write_indirect(phydev, DP83867_IO_MUX_CFG, DP83867_DEVADDR, val);
    #ifdef DEBUG_PHY
        val = hal_phy_read_indirect(phydev, DP83867_IO_MUX_CFG, MDIO_DEVAD_NONE);
        wolfBoot_printf("IOMUX (after)=%x\n", val);
    #endif
#endif
    }
    return ret;
}


#ifndef RGMII_PHY1_ADDR
#define RGMII_PHY1_ADDR        0x4
#endif
#ifndef RGMII_PHY2_ADDR
#define RGMII_PHY2_ADDR        0x3
#endif
#ifndef SGMII_PHY2_ADDR
#define SGMII_PHY2_ADDR        0x2
#endif
#ifndef SGMII_PHY1_ADDR
#define SGMII_PHY1_ADDR        0x1
#endif
#ifndef SGMII_AQR_PHY_ADDR
#define SGMII_AQR_PHY_ADDR     0x2
#endif
#ifndef FM1_10GEC1_PHY_ADDR
#define FM1_10GEC1_PHY_ADDR    0x1
#endif

#define FM1_DTSEC1 0
#define FM1_DTSEC2 1
#define FM1_DTSEC3 2
#define FM1_DTSEC4 3
#define FM1_10GEC1 4


static int hal_ethernet_init(void)
{
    int ret, i;
    uint32_t reg;

    memset(phydevs, 0, sizeof(phydevs));

    /* Set the on-board RGMII PHY addresses */
    phydevs[FM1_DTSEC4].interface = PHY_INTERFACE_MODE_RGMII;
    phydevs[FM1_DTSEC4].phyaddr = RGMII_PHY1_ADDR;
    phydevs[FM1_DTSEC3].interface = PHY_INTERFACE_MODE_RGMII;
    phydevs[FM1_DTSEC3].phyaddr = RGMII_PHY2_ADDR;

    /* SRDS_PRTCL_S1 Bits 128-183 - SerDes protocol select - SerDes 1 */
    /* See T1024RM - 30.1.1.1.2 SerDes Protocols
     * Figure 30-1 Supported SerDes Options  */
    reg = get32(DCFG_RCWSR(4));
    reg = (reg & RCWSR4_SRDS1_PRTCL) >> RCWSR4_SRDS1_PRTCL_SHIFT;
    if (reg == 0x95) {
        /* 0x095: A=XFI1 10G Aquantia AQR105 PHY, B=PCIe3, C=PCIe2, D=PCIe1 */
        phydevs[FM1_10GEC1].interface = PHY_INTERFACE_MODE_XGMII;
        phydevs[FM1_10GEC1].phyaddr = FM1_10GEC1_PHY_ADDR;
    }
    else {
        /* 0x05B: A=PCIe1,  B=PCIe3, C=SGMII2, D=SGMII1 */
        /* 0x119: A=Aurora, B=PCIe3, C=SGMII2, D=PCIe1 */
        phydevs[FM1_DTSEC1].interface = PHY_INTERFACE_MODE_SGMII;
        phydevs[FM1_DTSEC1].phyaddr = SGMII_PHY1_ADDR;
        phydevs[FM1_DTSEC2].interface = PHY_INTERFACE_MODE_SGMII;
        phydevs[FM1_DTSEC2].phyaddr = SGMII_PHY2_ADDR;
    }

    /* Init PHY for each device */
    for (i=0; i<(int)(sizeof(phydevs)/sizeof(struct phy_device)); i++) {
        if (phydevs[i].phyaddr != 0) {
            ret = hal_phy_init(&phydevs[i]);
            if (ret != 0) {
                wolfBoot_printf("PHY %d: Failed! %d\n", phydevs[i].phyaddr, ret);
            }
        }
    }

    return 0;
}
#endif /* ENABLE_PHY */

#define FMAN_DMA_LIODN 973

static int hal_fman_init(void)
{
    int ret, i;
    const struct qe_firmware* fw = (const struct qe_firmware*)FMAN_FW_ADDR;

    /* Upload microcode to IRAM */
    ret = qe_check_firmware(fw, "FMAN");
    if (ret == 0) {
        ret = fman_upload_firmware(fw);
    }
    if (ret == 0) {
        /* Setup FMAN LIDON */
        set32(FMAN_BMI_SPLIODN(0, 0+8), 88); /* RX_10G_TYPE2 */
        set32(FMAN_BMI_SPLIODN(0, 1+8), 89); /* RX_1G */
        set32(FMAN_BMI_SPLIODN(0, 2+8), 90); /* RX_1G */
        set32(FMAN_BMI_SPLIODN(0, 3+8), 91); /* RX_1G */

        /* Setup FMAN DMA LIODN - use same base for all */
        for (i=0; i<FMAN_DMA_ENTRIES; i++) {
            set32(FMAN_DMA_PORT_LIODN(i),
                ((FMAN_DMA_LIODN << 16) | FMAN_DMA_LIODN));
        }
    }

#ifdef ENABLE_PHY
    hal_ethernet_init();
#endif

    return ret;
}
#endif /* ENABLE_FMAN */


/* SMP Multi-Processor Driver */
#ifdef ENABLE_MP

/* from boot_ppc_core.S */
extern uint32_t _secondary_start_page;
extern uint32_t _second_half_boot_page;
extern uint32_t _spin_table;
extern uint32_t _spin_table_addr;
extern uint32_t _bootpg_addr;

/* Startup additional cores with spin table and synchronize the timebase */
static void hal_mp_up(uint32_t bootpg)
{
    uint32_t all_cores, active_cores, whoami;
    int timeout = 50, i;

    whoami = get32(PIC_WHOAMI); /* Get current running core number */
    all_cores = ((1 << CPU_NUMCORES) - 1); /* mask of all cores */
    active_cores = (1 << whoami); /* current running cores */

    wolfBoot_printf("MP: Starting core 2 (boot page %p, spin table %p)\n",
        bootpg, (uint32_t)&_spin_table);

    /* Set the boot page translation register */
    set32(LCC_BSTRH, 0);
    set32(LCC_BSTRL, bootpg);
    set32(LCC_BSTAR, (LCC_BSTAR_EN |
                      LCC_BSTAR_LAWTRGT(LAW_TRGT_DDR_1) |
                      LAW_SIZE_4KB));
    (void)get32(LCC_BSTAR); /* read back to sync */

    /* Enable time base on current core only */
    set32(RCPM_PCTBENR, (1 << whoami));

    /* Release the CPU core(s) */
    set32(DCFG_BRR, all_cores);
    __asm__ __volatile__("sync; isync; msync");

    /* wait for other core(s) to start */
    while (timeout) {
        for (i = 0; i < CPU_NUMCORES; i++) {
            uint32_t* entry = (uint32_t*)(
                  (uint8_t*)&_spin_table + (i * ENTRY_SIZE) + ENTRY_ADDR_LOWER);
            if (*entry) {
                active_cores |= (1 << i);
            }
        }
        if ((active_cores & all_cores) == all_cores) {
            break;
        }

        udelay(100);
        timeout--;
    }

    if (timeout == 0) {
        wolfBoot_printf("MP: Timeout enabling additional cores!\n");
    }

    /* Disable all timebases */
    set32(RCPM_PCTBENR, 0);

    /* Reset our timebase */
    mtspr(SPRN_TBWU, 0);
    mtspr(SPRN_TBWL, 0);

    /* Enable timebase for all cores */
    set32(RCPM_PCTBENR, all_cores);
}

static void hal_mp_init(void)
{
    uint32_t *fixup = (uint32_t*)&_secondary_start_page;
    uint32_t bootpg;
    int i_tlb = 0; /* always 0 */
    size_t i;
    const volatile uint32_t *s;
    volatile uint32_t *d;

    /* Assign virtual boot page at end of DDR (should be 0x7FFFF000) */
    bootpg = DDR_ADDRESS + DDR_SIZE - BOOT_ROM_SIZE;

    /* Store the boot page address for use by additional CPU cores */
    _bootpg_addr = (uint32_t)&_second_half_boot_page;

    /* Store location of spin table for other cores */
    _spin_table_addr = (uint32_t)&_spin_table;

    /* Flush bootpg before copying to invalidate any stale cache lines */
    flush_cache(bootpg, BOOT_ROM_SIZE);

    /* Map reset page to bootpg so we can copy code there */
    disable_tlb1(i_tlb);
    set_tlb(1, i_tlb, BOOT_ROM_ADDR, bootpg, 0, /* tlb, epn, rpn, urpn */
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), /* perms, wimge */
        0, BOOKE_PAGESZ_4K, 1); /* ts, esel, tsize, iprot */

    /* copy startup code to virtually mapped boot address */
    /* do not use memcpy due to compiler array bounds report (not valid) */
    s = (const uint32_t*)fixup;
    d = (uint32_t*)BOOT_ROM_ADDR;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* start core and wait for it to be enabled */
    hal_mp_up(bootpg);
}
#endif /* ENABLE_MP */



void hal_init(void)
{
    law_init();

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif

    hal_liodn_init();
    hal_flash_init();
    hal_cpld_init();
#ifdef ENABLE_MRAM
    hal_mram_init();
#endif
#ifdef ENABLE_PCIE
    if (hal_pcie_init() != 0) {
        wolfBoot_printf("PCIe: init failed!\n");
    }
#endif

#ifdef ENABLE_QE
    if (hal_qe_init() != 0) {
        wolfBoot_printf("QE: Engine init failed!\n");
    }
#endif
#ifdef ENABLE_FMAN
    if (hal_fman_init() != 0) {
        wolfBoot_printf("FMAN: init failed!\n");
    }
#endif
#ifdef ENABLE_MP
    hal_mp_init();
#endif

    /* Hardware Tests */
#if defined(ENABLE_DDR) && defined(TEST_DDR)
    if (test_ddr() != 0) {
        wolfBoot_printf("DDR Test Failed!\n");
    }
#endif

#if defined(ENABLE_ESPI) && defined(TEST_TPM)
    if (test_tpm() != 0) {
        wolfBoot_printf("TPM Test Failed!\n");
    }
#endif
}

/* wait for toggle to stop and status mask to be met within microsecond timeout */
static int hal_flash_status_wait(uint32_t sector, uint16_t mask, uint32_t timeout_us)
{
    int ret = 0;
    uint32_t timeout = 0;
    uint16_t read1, read2;

    do {
        /* detection of completion happens when reading status bits DQ6 and DQ2 stop toggling (0x44) */
        /* Only the */
        read1 = FLASH_IO8_READ(sector, 0);
        if ((read1 & AMD_STATUS_TOGGLE) == 0)
            read1 = FLASH_IO8_READ(sector, 0);
        read2 = FLASH_IO8_READ(sector, 0);
        if ((read2 & AMD_STATUS_TOGGLE) == 0)
            read2 = FLASH_IO8_READ(sector, 0);
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Wait toggle %x -> %x\n", read1, read2);
    #endif
        if (read1 == read2 && ((read1 & mask) == mask))
            break;
        udelay(1);
    } while (timeout++ < timeout_us);
    if (timeout >= timeout_us) {
        ret = -1; /* timeout */
    }
#ifdef DEBUG_FLASH
    wolfBoot_printf("Wait done (%d tries): %x -> %x\n",
        timeout, read1, read2);
#endif
    return ret;
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t i, pos, sector, offset, xfer, nwords;

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

#ifdef DEBUG_FLASH
    wolfBoot_printf("Flash Write: Ptr %p -> Addr 0x%x (len %d)\n",
        data, address, len);
#endif

    pos = 0;
    while (len > 0) {
        /* dertermine sector address */
        sector = (address / FLASH_SECTOR_SIZE);
        offset = address - (sector * FLASH_SECTOR_SIZE);
        offset /= (FLASH_CFI_WIDTH/8);
        xfer = len;
        if (xfer > FLASH_PAGE_SIZE)
            xfer = FLASH_PAGE_SIZE;
        nwords = xfer / (FLASH_CFI_WIDTH/8);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Write: Sector %d, Offset %d, Len %d, Pos %d\n",
            sector, offset, xfer, pos);
    #endif

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, offset, AMD_CMD_WRITE_TO_BUFFER);
    #if FLASH_CFI_WIDTH == 16
        FLASH_IO16_WRITE(sector, offset, (nwords-1));
    #else
        FLASH_IO8_WRITE(sector, offset, (nwords-1));
    #endif

        for (i=0; i<nwords; i++) {
            const uint8_t* ptr = &data[pos];
        #if FLASH_CFI_WIDTH == 16
            FLASH_IO16_WRITE(sector, i, *((const uint16_t*)ptr));
        #else
            FLASH_IO8_WRITE(sector, i, *ptr);
        #endif
            pos += (FLASH_CFI_WIDTH/8);
        }
        FLASH_IO8_WRITE(sector, offset, AMD_CMD_WRITE_BUFFER_CONFIRM);
        /* Typical 410us */

        /* poll for program completion - max 200ms */
        hal_flash_status_wait(sector, 0x44, 200*1000);

        address += xfer;
        len -= xfer;
    }
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    uint32_t sector;

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

    while (len > 0) {
        /* dertermine sector address */
        sector = (address / FLASH_SECTOR_SIZE);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Erase: Sector %d, Addr 0x%x, Len %d\n",
            sector, address, len);
    #endif

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_ERASE_START);
        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, 0, AMD_CMD_ERASE_SECTOR);
        /* block erase timeout = 50us - for additional sectors */
        /* Typical is 200ms (max 1100ms) */

        /* poll for erase completion - max 1.1 sec */
        hal_flash_status_wait(sector, 0x4C, 1100*1000);

        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }
    return 0;
}

static void hal_flash_unlock_sector(uint32_t sector)
{
    /* Unlock sequence */
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
}

void hal_flash_unlock(void)
{
    hal_flash_unlock_sector(0);
}

void hal_flash_lock(void)
{

}

void hal_prepare_boot(void)
{

}

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}

int hal_dts_fixup(void* dts_addr)
{
#ifndef BUILD_LOADER_STAGE1
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    int off, i;
    uint32_t *reg;
    const char* prev_compat;

    /* verify the FTD is valid */
    off = fdt_check_header(dts_addr);
    if (off != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", off);
        return off;
    }

    /* display FTD information */
    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* expand total size */
    fdt->totalsize += 2048; /* expand by 2KB */
    wolfBoot_printf("FDT: Expanded (2KB) to %d bytes\n", fdt->totalsize);

    /* fixup the memory region - single bank */
    off = fdt_find_devtype(fdt, -1, "memory");
    if (off != -FDT_ERR_NOTFOUND) {
        /* build addr/size as 64-bit */
        uint8_t ranges[sizeof(uint64_t) * 2], *p = ranges;
        *(uint64_t*)p = cpu_to_fdt64(DDR_ADDRESS);
        p += sizeof(uint64_t);
        *(uint64_t*)p = cpu_to_fdt64(DDR_SIZE);
        p += sizeof(uint64_t);
        wolfBoot_printf("FDT: Set memory, start=0x%x, size=0x%x\n",
            DDR_ADDRESS, (uint32_t)DDR_SIZE);
        fdt_setprop(fdt, off, "reg", ranges, (int)(p - ranges));
    }

    /* fixup CPU status and, release address and enable method */
    off = fdt_find_devtype(fdt, -1, "cpu");
    while (off != -FDT_ERR_NOTFOUND) {
        int core;
        uint64_t core_spin_table;

        reg = (uint32_t*)fdt_getprop(fdt, off, "reg", NULL);
        if (reg == NULL)
            break;
        core = (int)fdt32_to_cpu(*reg);
        if (core >= CPU_NUMCORES) {
            break; /* invalid core index */
        }

        /* calculate location of spin table for core */
        core_spin_table = (uint64_t)((uintptr_t)(
                  (uint8_t*)&_spin_table + (core * ENTRY_SIZE)));

        fdt_fixup_str(fdt, off, "cpu", "status", (core == 0) ? "okay" : "disabled");
        fdt_fixup_val64(fdt, off, "cpu", "cpu-release-addr", core_spin_table);
        fdt_fixup_str(fdt, off, "cpu", "enable-method", "spin-table");
        fdt_fixup_val(fdt, off, "cpu", "timebase-frequency", TIMEBASE_HZ);
        fdt_fixup_val(fdt, off, "cpu", "clock-frequency", hal_get_core_clk());
        fdt_fixup_val(fdt, off, "cpu", "bus-frequency", hal_get_plat_clk());

        off = fdt_find_devtype(fdt, off, "cpu");
    }

    /* fixup the soc clock */
    off = fdt_find_devtype(fdt, -1, "soc");
    if (off != -FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "soc", "bus-frequency", hal_get_plat_clk());
    }

    /* fixup the serial clocks */
    off = fdt_find_devtype(fdt, -1, "serial");
    while (off != -FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "serial", "clock-frequency", hal_get_bus_clk());
        off = fdt_find_devtype(fdt, off, "serial");
    }

    /* fixup the QE bridge and bus blocks */
    off = fdt_find_devtype(fdt, -1, "qe");
    if (off != -FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "qe", "clock-frequency", hal_get_bus_clk());
        fdt_fixup_val(fdt, off, "qe", "bus-frequency", hal_get_bus_clk());
        fdt_fixup_val(fdt, off, "qe", "brg-frequency", hal_get_bus_clk()/2);
    }

    /* fixup the LIODN */
    prev_compat = NULL;
    for (i=0; i<(int)(sizeof(liodn_tbl)/sizeof(struct liodn_id_table)); i++) {
        if (prev_compat == NULL || strcmp(prev_compat, liodn_tbl[i].compat) != 0) {
            off = -1;
        }
        off = fdt_node_offset_by_compatible(fdt, off, liodn_tbl[i].compat);
        if (off >= 0) {
            fdt_fixup_val(fdt, off, liodn_tbl[i].compat, "fsl,liodn",
                liodn_tbl[i].id);
        }
        prev_compat = liodn_tbl[i].compat;
    }

    /* fixup the QMAN portals */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,qman-portal");
    while (off != -FDT_ERR_NOTFOUND) {
        uint32_t liodns[2];
        int childoff;

        reg = (uint32_t*)fdt_getprop(fdt, off, "cell-index", NULL);
        if (reg == NULL)
            break;
        i = (int)fdt32_to_cpu(*reg);
        if (i >= QMAN_NUM_PORTALS) {
            break; /* invalid index */
        }
        liodns[0] = qp_info[i].dliodn;
        liodns[1] = qp_info[i].fliodn;

        wolfBoot_printf("FDT: Set %s@%d (%d), %s=%d,%d\n",
            "qman-portal", i, off, "fsl,liodn", liodns[0], liodns[1]);
        fdt_setprop(fdt, off, "fsl,liodn", liodns, sizeof(liodns));

        /* Add fman@0 node and fsl,liodon = FMAN_DMA_LIODN + index */
        childoff = fdt_add_subnode(fdt, off, "fman@0");
        if (childoff > 0) {
            liodns[0] = FMAN_DMA_LIODN + i + 1;
            wolfBoot_printf("FDT: Set %s@%d/%s (%d), %s=%d\n",
                "qman-portal", i, "fman@0", childoff, "fsl,liodn", liodns[0]);
            fdt_setprop(fdt, childoff, "fsl,liodn", liodns, sizeof(liodns[0]));
            off = childoff;
        }

        off = fdt_node_offset_by_compatible(fdt, off, "fsl,qman-portal");
    }

    /* fixup the fman clock */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,fman");
    if (off != !FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "fman@", "clock-frequency", hal_get_bus_clk());
    }

    /* Ethernet Devices */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,fman-memac");
    while (off != -FDT_ERR_NOTFOUND) {
        reg = (uint32_t*)fdt_getprop(fdt, off, "cell-index", NULL);
        if (reg == NULL)
            break;
        i = (int)fdt32_to_cpu(*reg);

        wolfBoot_printf("FDT: Ethernet%d: Offset %d\n", i, off);

        /* Set Ethernet MAC addresses */
        wolfBoot_printf("FDT: Set %s@%d (%d), %s=%x:%x:%x:%x:%x:%x\n",
                "ethernet", i, off, "local-mac-address",
                phydevs[i].mac_addr[0], phydevs[i].mac_addr[1],
                phydevs[i].mac_addr[2], phydevs[i].mac_addr[3],
                phydevs[i].mac_addr[4], phydevs[i].mac_addr[5]);
        fdt_setprop(fdt, off, "local-mac-address",
            phydevs[i].mac_addr, sizeof(phydevs[i].mac_addr));

        off = fdt_node_offset_by_compatible(fdt, off, "fsl,fman-memac");
    }

    /* PCIe Ranges */
    for (i=1; i<=PCIE_MAX_CONTROLLERS; i++) {
        uint32_t dma_ranges[] = {
        /* TYPE         BUS START         PHYS              SIZE */
           (FDT_PCI_MEM32),
                        0x00, 0xff000000, 0x0f, 0xfe000000, 0x00, 0x01000000,
           (FDT_PCI_PREFETCH | FDT_PCI_MEM32),
                        0x00, 0x00,       0x00, 0x00,       0x00, 0x80000000,
           (FDT_PCI_PREFETCH | FDT_PCI_MEM32),
                        0x10, 0x00,       0x00, 0x00,       0x00, 0x80000000
        };
        uint32_t bus_range[2], base;
        bus_range[0] = 0;
        bus_range[1] = i-1;

        /* find offset for pci controlller base register */
        off = fdt_node_offset_by_compatible(fdt, -1, "fsl,qoriq-pcie");
        while (off != -FDT_ERR_NOTFOUND) {
            reg = (uint32_t*)fdt_getprop(fdt, off, "reg", NULL);
            if (reg == NULL)
                break;
            reg++; /* skip first part of 64-bit */
            base = fdt32_to_cpu(*reg);
            if (base == (uint32_t)PCIE_BASE(i)) {
                break;
            }
            off = fdt_node_offset_by_compatible(fdt, off, "fsl,qoriq-pcie");
        }
        if (off == -FDT_ERR_NOTFOUND)
            break;

        /* Set ranges or disable if not enabled */
        wolfBoot_printf("FDT: pcie%d@%x, Offset %d\n", i, base, off);
        if (get32(DCFG_DEVDISR3) & (1 << (32-i))) {
            /* If device disabled, remove node */
            wolfBoot_printf("FDT: PCI%d Disabled, removing\n", i);
            off = fdt_del_node(fdt, off);
        }
        else {
            /* Set "dma-ranges" */
            wolfBoot_printf("FDT: Set %s@%d (%d), %s\n",
                "pcie", i, off, "dma-ranges");
            fdt_setprop(fdt, off, "dma-ranges", dma_ranges, sizeof(dma_ranges));
            /* Set "bus-range" */
            wolfBoot_printf("FDT: Set %s@%d (%d), %s\n",
                "pcie", i, off, "bus-ranges");
            fdt_setprop(fdt, off, "bus-range", bus_range, sizeof(bus_range));
        }
    }

    /* fix SDHC */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,esdhc");
    if (off != !FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "sdhc@", "clock-frequency", hal_get_bus_clk());
        fdt_fixup_str(fdt, off, "cpu", "status", "okay");
    }

#endif /* !BUILD_LOADER_STAGE1 */
    (void)dts_addr;
    return 0;
}
#endif /* MMU */


#if defined(ENABLE_DDR) && defined(TEST_DDR)

#ifndef TEST_DDR_OFFSET
#define TEST_DDR_OFFSET     (2 * 1024 * 1024)
#endif
#ifndef TEST_DDR_TOTAL_SIZE
#define TEST_DDR_TOTAL_SIZE (2 * 1024)
#endif
#ifndef TEST_DDR_CHUNK_SIZE
#define TEST_DDR_CHUNK_SIZE 1024
#endif

static int test_ddr(void)
{
    int ret = 0;
    int i;
    uint32_t *ptr = (uint32_t*)(DDR_ADDRESS + TEST_DDR_OFFSET);
    uint32_t tmp[TEST_DDR_CHUNK_SIZE/4];
    uint32_t total = 0;

    while (total < TEST_DDR_TOTAL_SIZE) {
        /* test write to DDR */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            ptr[i] = (uint32_t)i;
        }

        /* test read from DDR */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            tmp[i] = ptr[i];
        }

        /* compare results */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            if (tmp[i] != (uint32_t)i) {
                ret = -1;
                break;
            }
        }
        total += TEST_DDR_CHUNK_SIZE;
        ptr += TEST_DDR_CHUNK_SIZE;
    }

    return ret;
}
#endif /* ENABLE_DDR && TEST_DDR */

#if defined(ENABLE_ESPI) && defined(TEST_TPM)
int test_tpm(void)
{
    /* Read 4 bytes at TIS address D40F00. Assumes 0 wait state on TPM */
    uint8_t tx[8] = {0x83, 0xD4, 0x0F, 0x00,
                     0x00, 0x00, 0x00, 0x00};
    uint8_t rx[8] = {0};

    hal_espi_init(SPI_CS_TPM, 2000000, 0);
    hal_espi_xfer(SPI_CS_TPM, tx, rx, (uint32_t)sizeof(rx), 0);

    wolfBoot_printf("RX: 0x%x\n", *((uint32_t*)&rx[4]));
    return rx[4] != 0xFF ? 0 : -1;
}
#endif
