/* nxp_ls1028a.h
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#ifndef NXP_LS1028A_H
#define NXP_LS1028A_H

/* Expose AA64 defines */
#define AA64_TARGET_EL 2                /* Boot to EL2 hypervisor */

#define AA64_CNTFRQ ARMGT_CLK           /* ARM Global Generic Timer */

#define AA64GIC_VERSION 3               /* GIC-500 in v3 mode */
#define AA64GICV3_GICD_BASE  GICD_BASE
#define AA64GICV3_GITS_BASE  GITS_BASE
#define AA64GICV3_GITST_BASE GITST_BASE




/* LS1028A Reference Manual Rev 0 12/2019 */
/* LS1028A Memory Map 0GB - 4GB Table 1 */
#define BOOTROM_BASE    (0x00000000ul)     /* Boot ROM */
#define BOOTROM_SIZE    (64ul*1024)        /* 64kB */
#define CCSR_BASE       (0x01000000ul)     /* CCSR register space */
#define CCSR_SIZE       (240ul*1024*1024)  /* 240MB */
#define OCRAM_BASE      (0x18000000ul)     /* On-Chip RAM */
#define OCRAM_SIZE      (256ul*1024)       /* 256kB */
#define CSSTM_BASE      (0x19000000ul)     /* CoreSight STM */
#define CSSTM_SIZE      (16ul*1024*1024)   /* 16MB */
#define XSPIWIN1_BASE   (0x20000000ul)     /* FlexSPI window 1 */
#define XSPIWIN1_SIZE   (256ul*1024*1024)  /* 256MB */
#define DDRWIN1_BASE    (0x80000000ul)     /* DRAM window 1 */
#define DDRWIN1_SIZE    (2048ul*1024*1024) /* 2GB */

/* LS1028A Memory Map 4GB - 576GB Table 1*/
#define ECAMCFG_BASE    (0x01F0000000ull)  /* ECAM Config space */
#define ECAMREG_BASE    (0x01F0800000ull)  /* ECAM Register space */
#define ECAMRCIE_BASE   (0x01F8000000ull)  /* ECAM RCIE */
#define XSPIWIN2_BASE   (0x0401000000ull)  /* FlexSPI window 2 */
#define XSPIWIN2_SIZE   (0x00F0000000ull)  /* 3840MB */
#define DCSR_BASE       (0x0700000000ull)  /* DCSR */
#define DDRWIN2_BASE    (0x2080000000ull)  /* DDR window 2 */
#define DDRWIN2_SIZE    (0x1F80000000ull)  /* 126GB */
#define DDRWIN3_BASE    (0x6000000000ull)  /* DDR window 3 */
#define DDRWIN3_SIZE    (0x2000000000ull)  /* 126GB */
#define PCIE1_BASE      (0x8000000000ull)  /* PCIE 1 */
#define PCIE1_SIZE      (0x0800000000ull)  /* 32GB */
#define PCIE2_BASE      (0x8800000000ull)  /* PCIE 2 */
#define PCIE2_SIZE      (0x0800000000ull)  /* 32GB */


/* LS1028A CCSR Memory Map Table 2 */
#define DDRC_BASE       (0x01080000ul)     /* DDR Controller */
#define TZASC_BASE      (0x01100000ul)     /* Trust Zone ASC for DDR */
#define CGUCGA_BASE     (0x01300000ul)     /* Clock Generation Unit - CGA */
#define CGUP_BASE       (0x01360000ul)     /* Platform CGU */
#define CGUPTZ_BASE     (0x01368000ul)     /* Secure Platform CGU */
#define CLOCK_BASE      (0x01370000ul)     /* Clocking */
#define CGUD_BASE       (0x01380000ul)     /* DDR CGU */
#define DDRPHY1_BASE    (0x01400000ul)     /* DDR PHY for DDRC 1 */
#define DCFG_BASE       (0x01E00000ul)     /* Privileged Device Config */
#define PMU_BASE        (0x01E30000ul)     /* Power Management Unit */
#define RESET_BASE      (0x01E60000ul)     /* Reset */
#define SFP_BASE        (0x01E80000ul)     /* Security Fuse Processor */
#define SFPTZ_BASE      (0x01E88000ul)     /* TrustZone SFP */
#define SECMON_BASE     (0x01E90000ul)     /* Security Monitor */
#define SERDES_BASE     (0x01EA0000ul)     /* SerDes */
#define SERVP_BASE      (0x01F00000ul)     /* Service Processor */
#define ISC_BASE        (0x01F70000ul)     /* Interrupt Sampling Control */
#define TMU_BASE        (0x01F80000ul)     /* Thermal Monitor Unit */
#define VOLT_BASE       (0x01F90000ul)     /* Voltage Sense */
#define SCFG_BASE       (0x01FC0000ul)     /* Supplemental Configuration */
#define PINC_BASE       (0x01FF0000ul)     /* Pin Control */
#define I2C0_BASE       (0x02000000ul)     /* I2C 1 */
#define I2C_STRIDE      (0x10000ul)        /* For I2C 1-8 */
#define I2C_COUNT       (8)
#define I2C_BASE(_n)    (I2C0_BASE + ((_n) % I2C_COUNT) * I2C_STRIDE)
#define XSPIC_BASE      (0x020C0000ul)     /* FlexSPI */
#define SPI0_BASE       (0x02100000ul)     /* SPI 1 */
#define SPI_STRIDE      (0x10000ul)        /* For SPI 1-3 */
#define SPI_COUNT       (3)
#define SPI_BASE(_n)    (SPI0_BASE + ((_n) % SPI_COUNT) * SPI_STRIDE)
#define ESDHC0_BASE     (0x02140000ul)     /* eSDHC 1 */
#define ESDHC_STRIDE    (0x10000ul)        /* For eSDHC 1-2 */
#define ESDHC_COUNT     (2)
#define ESDHC_BASE(_n)  (ESDHC0_BASE + ((_n) % ESDHC_COUNT) * ESDHC_STRIDE)
#define CANBUS0_BASE    (0x02180000ul)     /* CAN 1 */
#define CANBUS_STRIDE   (0x10000ul)        /* For CAN 1-2 */
#define CANBUS_COUNT    (8)
#define CANBUS_BASE(_n) (CANBUS0_BASE + ((_n) % CANBUS_COUNT) * CANBUS_STRIDE)
#define DUART0_BASE     (0x021C0500ul)     /* DUART 1 */
#define DUART_STRIDE    (0x100ul)          /* For DUART 1-2 */
#define DUART_COUNT     (2)
#define DUART_BASE(_n)  (DUART0_BASE + ((_n) % DUART_COUNT) * DUART_STRIDE)
#define OCTZPC_BASE     (0x02200000ul)     /* OCRAM TZPC */
#define LPUART0_BASE    (0x02260000ul)     /* LPUART 1 */
#define LPUART_STRIDE   (0x10000ul)        /* For LPUART 1-6 */
#define LPUART_COUNT    (6)
#define LPUART_BASE(_n) (LPUART0_BASE + ((_n) % LPUART_COUNT) * LPUART_STRIDE)
#define EDMA_BASE       (0x022C0000ul)     /* eDMA */
#define EDMAMX0_BASE    (0x022D0000ul)     /* eDMA Channel Mux 1 */
#define EDMAMX_STRIDE   (0x10000ul)        /* For EDMAMX 1-2 */
#define EDMAMX_COUNT    (2)
#define EDMAMX_BASE(_n) (EDMAMX0_BASE + ((_n) % EDMAMX_COUNT) * EDMAMX_STRIDE)
#define GPIO0_BASE      (0x02300000ul)     /* GPIO 1 */
#define GPIO_STRIDE     (0x10000ul)        /* For GPIO 1-3 */
#define GPIO_COUNT      (3)
#define GPIO_BASE(_n)   (GPIO0_BASE + ((_n) % GPIO_COUNT) * GPIO_STRIDE)
#define TZWDT_BASE      (0x023C0000ul)     /* Trust Zone Watchdog Timer */
#define GGRT_BASE       (0x023D0000ul)     /* Global Generic Reference Timer */
#define GGRTC_BASE      (0x023E0000ul)     /* GGRT Control */
#define GGRTS_BASE      (0x023F0000ul)     /* GGRT Status */
#define FLEXT0_BASE     (0x02800000ul)     /* Flex Timer 1 */
#define FLEXT_STRIDE    (0x10000ul)        /* For Flex Timer 1-8 */
#define FLEXT_COUNT     (8)
#define FLEXT_BASE(_n)  (FLEXT0_BASE + ((_n) % FLEXT_COUNT) * FLEXT_STRIDE)
#define USB0_BASE       (0x03100000ul)     /* USB 1 */
#define USB_STRIDE      (0x10000ul)        /* For USB 1-2 */
#define USB_COUNT       (2)
#define USB_BASE(_n)    (USB0_BASE + ((_n) % USB_COUNT) * USB_STRIDE)
#define SATA_BASE       (0x03200000ul)     /* SATA */
#define PCIE_COUNT      (2)
#define PCIEPF0_BASE    (0x03400000ul)     /* PCI Express PF0 1 */
#define PCIELT0_BASE    (0x03480000ul)     /* PCIE Controller LUT 1 */
#define PCIEPC0_BASE    (0x034C0000ul)     /* PCIE Controller PF0 Controls 1 */
#define PCIEPF_STRIDE   (0x10000ul)        /* For PCIE PF0 1-2 */
#define PCIELT_STRIDE   (0x10000ul)        /* For PCIE LUT 1-2 */
#define PCIEPC_STRIDE   (0x10000ul)        /* For PCIE PF0 Controls 1-2 */
#define PCIEPF_BASE(_n) (PCIEPF0_BASE + ((_n) % PCIE_COUNT) * PCIEPF_STRIDE)
#define PCIELT_BASE(_n) (PCIELT0_BASE + ((_n) % PCIE_COUNT) * PCIELT_STRIDE)
#define PCIEPC_BASE(_n) (PCIEPC0_BASE + ((_n) % PCIE_COUNT) * PCIEPC_STRIDE)
#define CCI400_BASE     (0x04090000ul)     /* CCI-400 Configuration */
#define MMUGR0_BASE     (0x05000000ul)     /* MMU-500 Global Register Space 0 */
#define MMUGR1_BASE     (0x05010000ul)     /* MMU-500 Global Register Space 1 */
#define MMUGID_BASE     (0x05020000ul)     /* MMU-500 Implementation Defined */
#define MMUPM_BASE      (0x05030000ul)     /* MMU-500 Performance Monitor */
#define MMUSSD_BASE     (0x05050000ul)     /* MMU-500 Security State */
#define MMUTCB0_BASE    (0x05400000ul)     /* MMU Translation Context Bank 0 */
#define MMUTCB_STRIDE   (0x10000ul)        /* For MMUTCB 0-63 */
#define MMUTCB_COUNT    (64)
#define MMUTCB_BASE(_n) (MMUTCB0_BASE + ((_n) % MMUTCB_COUNT) * MMUTCB_STRIDE)
#define GICD_BASE       (0x06000000ul)     /* GIC-500 GICD */
#define GITS_BASE       (0x06020000ul)     /* GIC-500 GITS Control */
#define GITST_BASE      (0x06030000ul)     /* GIC-500 GITS Translation */
#define CPU0RD_BASE     (0x06040000ul)     /* CPU0 control, Locality Perif Int*/
#define CPU0SGI_BASE    (0x06050000ul)     /* CPU0 SW Gen Int, Priv Perif Int */
#define CPU1RD_BASE     (0x06060000ul)     /* CPU1 control, physical LPIs */
#define CPU1SGI_BASE    (0x06070000ul)     /* CPU1 SGIs, PPIs */
#define SEC_BASE        (0x08000000ul)     /* General Purpose SEC Block */
#define QDMACONF_BASE   (0x08380000ul)     /* qDMA Configuration */
#define QDMAMGMT_BASE   (0x08390000ul)     /* qDMA Management */
#define QDMASA_BASE     (0x083A0000ul)     /* qDMA Standalone */
#define CPU0WDT_BASE    (0x0C000000ul)     /* Core 0 WDT */
#define CPU1WDT_BASE    (0x0C010000ul)     /* Core 1 WDT */
#define LCD_BASE        (0x0F080000ul)     /* LCD Controller */
#define GPU_BASE        (0x0F0C0000ul)     /* GPU */
#define SAI0_BASE       (0x0F100000ul)     /* Synchronous Audio Interface 1 */
#define SAI_STRIDE      (0x10000ul)        /* For SAI 1-6 */
#define SAI_COUNT       (6)
#define SAI_BASE(_n)    (SAI0_BASE + ((_n) % SAI_COUNT) * SAI_STRIDE)
#define MMPLL_BASE      (0x0F1F0000ul)     /* Multimedia PLL 1 */
#define EDPPHY_BASE     (0x0F200000ul)     /* Display PHY and PHY Controller */

/* Alternate SMMU names */
#define SMMU_GR0_BASE   MMUGR0_BASE
#define SMMU_GR1_BASE   MMUGR1_BASE
#define SMMU_GID_BASE   MMUGID_BASE
#define SMMU_PM_BASE    MMUPM_BASE
#define SMMU_SSD_BASE   MMUSSD_BASE
#define SMMU_TOP        MMUTCB0_BASE
#define SMMU_GLOBAL_TOP MMUTCB0_BASE
#define SMMU_CB_BASE    MMUTCB0_BASE



/* Clocking */
#define SYSTEM_CLK     (100000000ul)       /* System clock fixed 100MHz */
#define ARMGT_CLK      (SYSTEM_CLK / 4)    /* ARM Generic Timer fixed 1/4 */

/* Platform CLK = SYSTEM_CLK * RCW[SYS_PLL_RAT] which is usually 4
 *
 * Peripherals using Platform CLK:
 * OCRAM, eSDHC, CCI-400, TZC-400, SecMon, SEC, TSN L2Switch, PCIe, GIC-500,
 * ENETC, GPU, RCPM, SATA, qDMA
 *
 * Peripherals using Platform CLK / 2:
 * LPUART, Service Proc, GPIO, SPI, FTM, FlexSPI (APB), DUART, TMU, WDOG, TZPC,
 * SAI, USB, CAN
 *
 * Peripherals using Platform CLK / 4:
 * I2C
 */


#define SYS_CLK     (400000000) /* Sysclock = 400Mhz set by RCW */
#define FLASH_FREQ  (100000000) /* Flash clock = 100Mhz */
#define NOR_BASE    (0x20000000)


/* TZC-400 CoreLink Trust Zone Address Space Controller for DDR RM 32.2*/
#define TZASC_BUILD_CONFIG        *((volatile uint32_t*)(TZASC_BASE + 0x0))
#define TZASC_ACTION              *((volatile uint32_t*)(TZASC_BASE + 0x4))
#define TZASC_GATE_KEEPER         *((volatile uint32_t*)(TZASC_BASE + 0x8))
#define TZASC_SPECULATION_CTRL    *((volatile uint32_t*)(TZASC_BASE + 0xC))
#define TZASC_INT_STATUS          *((volatile uint32_t*)(TZASC_BASE + 0x10))
#define TZASC_INT_CLEAR           *((volatile uint32_t*)(TZASC_BASE + 0x14))
#define TZASC_FAIL_ADDRESS_LOW    *((volatile uint32_t*)(TZASC_BASE + 0x20))
#define TZASC_FAIL_ADDRESS_HIGH   *((volatile uint32_t*)(TZASC_BASE + 0x24))
#define TZASC_FAIL_CONTROL        *((volatile uint32_t*)(TZASC_BASE + 0x28))
#define TZASC_REGION_BASE_LOW_0   *((volatile uint32_t*)(TZASC_BASE + 0x100))
#define TZASC_REGION_BASE_HIGH_0  *((volatile uint32_t*)(TZASC_BASE + 0x104))
#define TZASC_REGION_TOP_LOW_0    *((volatile uint32_t*)(TZASC_BASE + 0x108))
#define TZASC_REGION_TOP_HIGH_0   *((volatile uint32_t*)(TZASC_BASE + 0x10C))
#define TZASC_REGION_ATTRIBUTES_0 *((volatile uint32_t*)(TZASC_BASE + 0x110))
#define TZASC_ACTION_ENABLE_DECERR 0x1                 /* RM 32.4.3 */
#define TZASC_GATE_KEEPER_REQUEST_OPEN 0x1             /* RM 32.4.3 */
#define TZASC_REGION_ATTRIBUTES_ALLOW_SECRW 0xC0000001 /* RM 32.4.15 */

/* TZPC Trust Zone Protection Controller for OCRAM RM 32.6 */
#define TZPC_OCRAM      (0x2200000)
#define TZPCR0SIZE      *((volatile uint32_t*)(TZPC_OCRAM + 0x0))
#define TZDECPROT0_STAT *((volatile uint32_t*)(TZPC_OCRAM + 0x800))
#define TZDECPROT0_SET  *((volatile uint32_t*)(TZPC_OCRAM + 0x804))
#define TZDECPROT0_CLR  *((volatile uint32_t*)(TZPC_OCRAM + 0x808))
#define TZDECPROT1_STAT *((volatile uint32_t*)(TZPC_OCRAM + 0x80C))
#define TZDECPROT1_SET  *((volatile uint32_t*)(TZPC_OCRAM + 0x810))
#define TZDECPROT1_CLR  *((volatile uint32_t*)(TZPC_OCRAM + 0x814))
#define TZPCPERIPHID0   *((volatile uint32_t*)(TZPC_OCRAM + 0xFE0))
#define TZPCPERIPHID1   *((volatile uint32_t*)(TZPC_OCRAM + 0xFE4))
#define TZPCPERIPHID2   *((volatile uint32_t*)(TZPC_OCRAM + 0xFE8))
#define TZPCPERIPHID3   *((volatile uint32_t*)(TZPC_OCRAM + 0xFEC))
#define TZPCPCELLID0    *((volatile uint32_t*)(TZPC_OCRAM + 0xFF0))
#define TZPCPCELLID1    *((volatile uint32_t*)(TZPC_OCRAM + 0xFF4))
#define TZPCPCELLID2    *((volatile uint32_t*)(TZPC_OCRAM + 0xFF8))
#define TZPCPCELLID3    *((volatile uint32_t*)(TZPC_OCRAM + 0xFFC))

#define TZWT_BASE  (0x23C0000)


/* LS1028A PC16552D Dual UART */


#define UART_BASE(n) (0x21C0500 + (n * 100))

#define UART_RBR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* receiver buffer register */
#define UART_THR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* transmitter holding register */
#define UART_IER(n)  *((volatile uint8_t*)(UART_BASE(n) + 1)) /* interrupt enable register */
#define UART_FCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* FIFO control register */
#define UART_IIR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* interrupt ID register */
#define UART_LCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 3)) /* line control register */
#define UART_LSR(n)  *((volatile uint8_t*)(UART_BASE(n) + 5)) /* line status register */
#define UART_SCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 7)) /* scratch register */

/* enabled when UART_LCR_DLAB set */
#define UART_DLB(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* divisor least significant byte register */
#define UART_DMB(n)  *((volatile uint8_t*)(UART_BASE(n) + 1)) /* divisor most significant byte register */

#define UART_FCR_TFR  (0x04) /* Transmitter FIFO reset */
#define UART_FCR_RFR  (0x02) /* Receiver FIFO reset */
#define UART_FCR_FEN  (0x01) /* FIFO enable */
#define UART_LCR_DLAB (0x80) /* Divisor latch access bit */
#define UART_LCR_WLS  (0x03) /* Word length select: 8-bits */
#define UART_LSR_TEMT (0x40) /* Transmitter empty */
#define UART_LSR_THRE (0x20) /* Transmitter holding register empty */

/* LS1028 XSPI Flex SPI Memory map - RM 18.7.2.1 */
#define XSPI_BASE              (0x20C0000UL)
#define XSPI_MCRn(x)           *((volatile uint32_t*)(XSPI_BASE + (x * 0x4))) /* Module Control Register */
#define XSPI_MCR0              *((volatile uint32_t*)(XSPI_BASE + 0x0)) /* Module Control Register */
#define XSPI_MCR1              *((volatile uint32_t*)(XSPI_BASE + 0x4)) /* Module Control Register */
#define XSPI_MCR2              *((volatile uint32_t*)(XSPI_BASE + 0x8)) /* Module Control Register */
#define XSPI_AHBCR             *((volatile uint32_t*)(XSPI_BASE + 0xC)) /* Bus Control Register */
#define XSPI_INTEN             *((volatile uint32_t*)(XSPI_BASE + 0x10)) /* Interrupt Enable Register */
#define XSPI_INTR              *((volatile uint32_t*)(XSPI_BASE + 0x14)) /* Interrupt Register */
#define XSPI_LUTKEY            *((volatile uint32_t*)(XSPI_BASE + 0x18)) /* LUT Key Register */
#define XSPI_LUTCR             *((volatile uint32_t*)(XSPI_BASE + 0x1C)) /* LUT Control Register */
#define XSPI_AHBRXBUFnCR0(x)   *((volatile uint32_t*)(XSPI_BASE + 0x20 + (x * 0x4))) /* AHB RX Buffer Control Register */
#define XSPI_FLSHA1CR0         *((volatile uint32_t*)(XSPI_BASE + 0x60)) /* Flash A1 Control Register */
#define XSPI_FLSHA2CR0         *((volatile uint32_t*)(XSPI_BASE + 0x64)) /* Flash A2 Control Register */
#define XSPI_FLSHB1CR0         *((volatile uint32_t*)(XSPI_BASE + 0x68)) /* Flash B1 Control Register */
#define XSPI_FLSHB2CR0         *((volatile uint32_t*)(XSPI_BASE + 0x6C)) /* Flash B2 Control Register */
#define XSPI_FLSHA1CR1         *((volatile uint32_t*)(XSPI_BASE + 0x70)) /* Flash A1 Control Register */
#define XSPI_FLSHA2CR1         *((volatile uint32_t*)(XSPI_BASE + 0x74)) /* Flash A2 Control Register */
#define XSPI_FLSHB1CR1         *((volatile uint32_t*)(XSPI_BASE + 0x78)) /* Flash B1 Control Register */
#define XSPI_FLSHB2CR1         *((volatile uint32_t*)(XSPI_BASE + 0x7C)) /* Flash B2 Control Register */
#define XSPI_FLSHA1CR2         *((volatile uint32_t*)(XSPI_BASE + 0x80)) /* Flash A1 Control Register */
#define XSPI_FLSHA2CR2         *((volatile uint32_t*)(XSPI_BASE + 0x84)) /* Flash A2 Control Register */
#define XSPI_FLSHB1CR2         *((volatile uint32_t*)(XSPI_BASE + 0x88)) /* Flash B1 Control Register */
#define XSPI_FLSHB2CR2         *((volatile uint32_t*)(XSPI_BASE + 0x8C)) /* Flash B2 Control Register */
#define XSPI_FLSHCR4           *((volatile uint32_t*)(XSPI_BASE + 0x94)) /* Flash A1 Control Register */
#define XSPI_IPCR0             *((volatile uint32_t*)(XSPI_BASE + 0xA0)) /* IP Control Register 0 */
#define XSPI_IPCR1             *((volatile uint32_t*)(XSPI_BASE + 0xA4)) /* IP Control Register 1 */
#define XSPI_IPCMD             *((volatile uint32_t*)(XSPI_BASE + 0xB0)) /* IP Command Register */
#define XSPI_DLPR              *((volatile uint32_t*)(XSPI_BASE + 0xB4)) /* Data Lean Pattern Register */
#define XSPI_IPRXFCR           *((volatile uint32_t*)(XSPI_BASE + 0xB8)) /* IPC RX FIFO Control Register */
#define XSPI_IPTXFCR           *((volatile uint32_t*)(XSPI_BASE + 0xBC)) /* IPC TX FIFO Control Register */
#define XSPI_DLLACR            *((volatile uint32_t*)(XSPI_BASE + 0xC0)) /* DLLA Control Register */
#define XSPI_DLLBCR            *((volatile uint32_t*)(XSPI_BASE + 0xC4)) /* DLLB Control Register */
#define XSPI_STS0              *((volatile uint32_t*)(XSPI_BASE + 0xE0)) /* Status Register 0 */
#define XSPI_STS1              *((volatile uint32_t*)(XSPI_BASE + 0xE4)) /* Status Register 1 */
#define XSPI_STS2              *((volatile uint32_t*)(XSPI_BASE + 0xE8)) /* Status Register 2 */
#define XSPI_AHBSPNDST         *((volatile uint32_t*)(XSPI_BASE + 0xEC)) /* AHB Suspend Status Register */
#define XSPI_IPRXFSTS          *((volatile uint32_t*)(XSPI_BASE + 0xF0)) /* IPC RX FIFO Status Register */
#define XSPI_IPTXFSTS          *((volatile uint32_t*)(XSPI_BASE + 0xF4)) /* IPC TX FIFO Status Register */
#define XSPI_RFD(x)            *((volatile uint32_t*)(XSPI_BASE + 0x100 + (x * 0x4))) /* RX FIFO Data Register */
#define XSPI_TFD_BASE          (XSPI_BASE + 0x180) /* TX FIFO Data Register Base Address */
#define XSPI_TFD(x)            *((volatile uint32_t*)(XSPI_BASE + 0x180 + (x * 0x4))) /* TX FIFO Data Register */
#define XSPI_LUT(x)            *((volatile uint32_t*)(XSPI_BASE + 0x200 + (x * 0x4))) /* LUT Register */
#define XSPI_SFAR              XSPI_IPCR0 /* Serial Flash Address Register determined by AHB burst address */

/* XSPI register instructions */
#define XSPI_SWRESET()         XSPI_MCRn(0) |= 0x1;           /* XSPI Software Reset */
#define XSPI_ENTER_STOP()      XSPI_MCRn(0) |= 0x1 << 1;      /* XSPI Module Disable */
#define XSPI_EXIT_STOP()       XSPI_MCRn(0) |= 0x0 << 1;      /* XSPI Module Enable */
#define XSPI_LUT_LOCK()        XSPI_LUTCR = 0x1;              /* XSPI LUT Lock */
#define XSPI_LUT_UNLOCK()      XSPI_LUTCR = 0x2;              /* XSPI LUT Unlock */
#define XSPI_ISEQID(x)         (x << 16)                      /* Sequence Index In LUT */
#define XSPI_ISEQNUM(x)        (x << 24)                      /* Number of Sequences to Execute ISEQNUM+1 */
#define XSPI_IPAREN()          (0x1 << 31)                    /* Peripheral Chip Select Enable */
#define XSPI_IDATSZ(x)         (x << 0)                       /* Number of Data Bytes to Send */
#define XSPI_IPCMD_START()     (XSPI_IPCMD = 0x1)             /* Start IP Command */
#define XSPI_IPCMDDONE         (0x1)
#define XSPI_IPRXWA            (0x1 << 5)
#define XSPI_IPRCFCR_FLUSH     (0x1)
#define XSPI_IP_BUF_SIZE       (256)
#define XSPI_IP_WM_SIZE        (8)
#define XSPI_INTR_IPTXWE_MASK  (0x1 << 6)
#define XSPI_SW_RESET          (0x1)


/* XSPI Parameters */
#define XSPI_MAX_BANKS         (8)
#define XSPI_MAX_LUT_ENTRIES   (64)
#define XSPI_FIFO_DEPTH        (32)
#define XSPI_FIFO_SIZE         (XSPI_FIFO_DEPTH * 4)

/* IPRXFCR Masks*/
#define XSPI_IPRXFCR_RXWMRK_MASK(x)  (x << 2)    /* XSPI RX Watermark */
#define XSPI_IPRXFCR_RXDMAEN_MASK    (0x1 << 1)  /* XSPI RX DMA Enable */
#define XSPI_IPRXFCR_CLRIPRXF_MASK   (0x1 << 0)  /* XSPI Clear RX FIFO */

/* MCR Masks */
#define XSPI_MCR_SWRESET_MASK       (0x1)          /* XSPI Software Reset */
#define XSPI_MCR_MDIS_MASK          (0x1 << 1)     /* XSPI Module Disable */
#define XSPI_MCR_RXCLKSRC_MASK      (0x3 << 4)     /* XSPI RX Clock Source */
#define XSPI_MCR_ARDFEN_MASK        (0x1 << 6)     /* XSPI AHB RX FIFO DMA Enable */
#define XSPI_MCR_ATDFEN_MASK        (0x1 << 7)     /* XSPI AHB TX FIFO DMA Enable */
#define XSPI_MCR_SERCLKDIV_MASK     (0x7 << 8)     /* XSPI Serial Clock Divider */
#define XSPI_MCR_HSEN_MASK          (0x1 << 11)    /* XSPI Half Speed Serial Clock Enable */
#define XSPI_MCR_DOZEEN_MASK        (0x1 << 12)    /* XSPI Doze Enable */
#define XSPI_MCR_COMBINATIONEN_MASK (0x1 << 13)    /* XSPI Combination Mode Enable */
#define XSPI_MCR_SCKFREERUNEN_MASK  (0x1 << 14)    /* XSPI Serial Clock Free Run Enable */
#define XSPI_MCR_LEARNEN_MASK       (0x1 << 15)    /* XSPI Learn Mode Enable */

/* XSPI Init */
/*#define XSPI_MCR0_CFG           0xFFFF80C0*/
#define XSPI_MCR0_CFG           0xFFFF0000
#define XSPI_MCR1_CFG           0xFFFFFFFF
#define XSPI_MCR2_CFG           0x200081F7
#define XSPI_INTEN_CFG          0x00000061
#define XSPI_AHBCR_CFG          0x00000038
#define XSPI_AHBRXBUF0CR_CFG    0x80000100
#define XSPI_AHBRXBUF1CR_CFG    0x80010100
#define XSPI_AHBRXBUF2CR_CFG    0x80020100
#define XSPI_AHBRXBUF3CR_CFG    0x80030100
#define XSPI_AHBRXBUF4CR_CFG    0x80040100
#define XSPI_AHBRXBUF5CR_CFG    0x80050100
#define XSPI_AHBRXBUF6CR_CFG    0x80060100
#define XSPI_AHBRXBUF7CR_CFG    0x80070100

/* Flash Size */
#define XSPI_FLSHA1CR0_CFG      0x80000
#define XSPI_FLSHA2CR0_CFG      0x80000
#define XSPI_FLSHB1CR0_CFG      0x80000
#define XSPI_FLSHB2CR0_CFG      0x80000

/* XSPI Timing */
#define XSPI_FLSHA1CR1_CFG      0x00000063
#define XSPI_FLSHA2CR1_CFG      0x00000063
#define XSPI_FLSHB1CR1_CFG      0x00000063
#define XSPI_FLSHB2CR1_CFG      0x00000063
#define XSPI_FLSHA1CR2_CFG      0x00000C00
#define XSPI_FLSHA2CR2_CFG      0x00000C00
#define XSPI_FLSHB1CR2_CFG      0x00000C00
#define XSPI_FLSHB2CR2_CFG      0x00000C00
#define XSPI_IPRXFCR_CFG        0x00000001
#define XSPI_IPTXFCR_CFG        0x00000001
#define XSPI_DLLACR_CFG         0x100
#define XSPI_DLLBCR_CFG         0x100
#define XSPI_AHB_UPDATE         0x20


/* Initalize LUT for NOR flash
 * MT35XU02GCBA1G12-0SIT ES
 * 256 MB, PBGA24 x1/x8 SPI serial NOR flash memory
 * Supports 166 MHz SDR speed and 200 MHz DDR speed
 * Powers up in x1 mode and can be switched to x8 mode
 * All padding is x1 mode or (1)
 */
/* NOR Flash parameters */
#define FLASH_BANK_SIZE   (256 * 1024 * 1024)   /* 256MB total size */
#define FLASH_PAGE_SIZE   (256)                 /* program size */
#define FLASH_ERASE_SIZE  (128 * 1024)          /* erase sector size */
#define FLASH_SECTOR_CNT  (FLASH_BANK_SIZE / FLASH_ERASE_SIZE)
#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */
#define FLASH_READY_MSK   (0x1 << 0)
#define MASK_32BIT        0xffffffff

/* LUT register helper */
#define XSPI_LUT_SEQ(code1, pad1, op1, code0, pad0, op0) \
    (((code1) << 26) | ((pad1) << 24) | ((op1) << 16) |  \
    (code0) << 10 | ((pad0) << 8) | (op0))

/* FlexSPI Look up Table defines */
#define LUT_KEY                 0x5AF05AF0

/* Calculate the number of PAD bits for LUT register*/
#define LUT_PAD(x)              (x - 1)
#define LUT_PAD_SINGLE          LUT_PAD(1)
#define LUT_PAD_OCTAl           LUT_PAD(4)

#define CMD_SDR                 0x01
#define CMD_DDR                 0x21
#define RADDR_SDR               0x02
#define RADDR_DDR               0x22
#define CADDR_SDR               0x03
#define CADDR_DDR               0x23
#define MODE1_SDR               0x04
#define MODE1_DDR               0x24
#define MODE2_SDR               0x05
#define MODE2_DDR               0x25
#define MODE4_SDR               0x06
#define MODE4_DDR               0x26
#define MODE8_SDR               0x07
#define MODE8_DDR               0x27
#define WRITE_SDR               0x08
#define WRITE_DDR               0x28
#define READ_SDR                0x09
#define READ_DDR                0x29
#define LEARN_SDR               0x0A
#define LEARN_DDR               0x2A
#define DATSZ_SDR               0x0B
#define DATSZ_DDR               0x2B
#define DUMMY_SDR               0x0C
#define DUMMY_DDR               0x2C
#define DUMMY_RWDS_SDR          0x0D
#define DUMMY_RWDS_DDR          0x2D
#define JMP_ON_CS               0x1F
#define STOP                    0

/* MT35XU02GCBA1G12 Operation definitions */
#define LUT_OP_WE              0x06 /* Write Enable */
#define LUT_OP_WD              0x04 /* Write Disable */
#define LUT_OP_WNVCR           0xB1 /* Write Non-Volatile Configuration Register */
#define LUT_OP_CLSFR           0x50 /* Clear Status Flag Register */
#define LUT_OP_WSR             0x01 /* Write Status Register */
#define LUT_OP_RSR             0x05 /* Read Status Register */
#define LUT_OP_RID             0x9F /* Read ID */
#define LUT_OP_PP              0x02 /* Page Program */
#define LUT_OP_PP4B            0x12 /* Page Program */
#define LUT_OP_FPP             0x82 /* Fast Page Program */
#define LUT_OP_SE              0xD8 /* Sector Erase */
#define LUT_OP_SE_4K           0x20 /* 4K Sector Erase */
#define LUT_OP_SE_4K4B         0x21 /* 4K Sector Erase */
#define LUT_OP_SE_32K          0x52 /* 32K Sector Erase */
#define LUT_OP_SE_32K4B        0x5C /* 32K Sector Erase */
#define LUT_OP_4SE             0xDC /* 4 byte Sector Erase */
#define LUT_OP_CE              0xC4 /* Chip Erase */
#define LUT_OP_READ3B          0x03 /* Read */
#define LUT_OP_READ4B          0x13 /* Read */
#define LUT_OP_FAST_READ       0x0B /* Fast Read */
#define LUT_OP_FAST_READ4B     0x0C /* Fast Read */
#define LUT_OP_OCTAL_READ      0x8B /* Octal Read */
#define LUT_OP_ADDR3B          0x18 /* 3 byte address */
#define LUT_OP_ADDR4B          0x20 /* 4 byte address */
#define LUT_OP_RDSR            0x05 /* Read Status Register */
#define LUT_OP_1BYTE           0x01 /* 1 byte */

#define LUT_INDEX_READ         0
#define LUT_INDEX_WRITE_EN     4
#define LUT_INDEX_SE           8
#define LUT_INDEX_SSE4K        12
#define LUT_INDEX_PP           16
#define LUT_INDEX_RDSR         20

/* MT40A1G8SA-075:E --> DDR4: static, 1GB, 1600 MHz (1.6 GT/s) */
#define DDR_ADDRESS            0x80000000
#define DDR_FREQ               1600
#define DDR_SIZE               (2 * 1024 * 1024 * 1024)
#define DDR_N_RANKS            1
#define DDR_RANK_DENS          0x100000000
#define DDR_SDRAM_WIDTH        32
#define DDR_EC_SDRAM_W         0
#define DDR_N_ROW_ADDR         15
#define DDR_N_COL_ADDR         10
#define DDR_N_BANKS            2
#define DDR_EDC_CONFIG         2
#define DDR_BURSTL_MASK        0x0c
#define DDR_TCKMIN_X_PS        750
#define DDR_TCMMAX_PS          1900
#define DDR_CASLAT_X           0x0001FFE00
#define DDR_TAA_PS             13500
#define DDR_TRCD_PS            13500
#define DDR_TRP_PS             13500
#define DDR_TRAS_PS            32000
#define DDR_TRC_PS             45500
#define DDR_TWR_PS             15000
#define DDR_TRFC1_PS           350000
#define DDR_TRFC2_PS           260000
#define DDR_TRFC4_PS           160000
#define DDR_TFAW_PS            21000
#define DDR_TRFC_PS            260000
#define DDR_TRRDS_PS           3000
#define DDR_TRRDL_PS           4900
#define DDR_TCCDL_PS           5000
#define DDR_REF_RATE_PS        7800000

#define DDR_CS0_BNDS_VAL       0x000000FF
#define DDR_CS1_BNDS_VAL       0x00000000
#define DDR_CS2_BNDS_VAL       0x00000000
#define DDR_CS3_BNDS_VAL       0x00000000
#define DDR_CS0_CONFIG_VAL     0x80040422
#define DDR_CS1_CONFIG_VAL     0x00000000
#define DDR_CS2_CONFIG_VAL     0x00000000
#define DDR_CS3_CONFIG_VAL     0x00000000
#define DDR_TIMING_CFG_3_VAL   0x01111000
#define DDR_TIMING_CFG_0_VAL   0x91550018
#define DDR_TIMING_CFG_1_VAL   0xBAB40C42
#define DDR_TIMING_CFG_2_VAL   0x0048C111
#define DDR_SDRAM_CFG_VAL      0xE50C0004
#define DDR_SDRAM_CFG_2_VAL    0x00401110
#define DDR_SDRAM_MODE_VAL     0x03010210
#define DDR_SDRAM_MODE_2_VAL   0x00000000
#define DDR_SDRAM_MD_CNTL_VAL  0x0600041F
#define DDR_SDRAM_INTERVAL_VAL 0x18600618
#define DDR_DATA_INIT_VAL      0xDEADBEEF
#define DDR_SDRAM_CLK_CNTL_VAL 0x02000000
#define DDR_INIT_ADDR_VAL      0x00000000
#define DDR_INIT_EXT_ADDR_VAL  0x00000000
#define DDR_TIMING_CFG_4_VAL   0x00000002
#define DDR_TIMING_CFG_5_VAL   0x03401400
#define DDR_TIMING_CFG_6_VAL   0x00000000
#define DDR_TIMING_CFG_7_VAL   0x23300000
#define DDR_ZQ_CNTL_VAL        0x8A090705
#define DDR_WRLVL_CNTL_VAL     0x8675F605
#define DDR_SR_CNTL_VAL        0x00000000
#define DDR_SDRAM_RCW_1_VAL    0x00000000
#define DDR_SDRAM_RCW_2_VAL    0x00000000
#define DDR_WRLVL_CNTL_2_VAL   0x06070700
#define DDR_WRLVL_CNTL_3_VAL   0x00000008
#define DDR_SDRAM_RCW_3_VAL    0x00000000
#define DDR_SDRAM_RCW_4_VAL    0x00000000
#define DDR_SDRAM_RCW_5_VAL    0x00000000
#define DDR_SDRAM_RCW_6_VAL    0x00000000
#define DDR_SDRAM_MODE_3_VAL   0x00010210
#define DDR_SDRAM_MODE_4_VAL   0x00000000
#define DDR_SDRAM_MODE_5_VAL   0x00010210
#define DDR_SDRAM_MODE_6_VAL   0x00000000
#define DDR_SDRAM_MODE_7_VAL   0x00010210
#define DDR_SDRAM_MODE_8_VAL   0x00000000
#define DDR_SDRAM_MODE_9_VAL   0x00000500
#define DDR_SDRAM_MODE_10_VAL  0x04000000
#define DDR_SDRAM_MODE_11_VAL  0x00000400
#define DDR_SDRAM_MODE_12_VAL  0x04000000
#define DDR_SDRAM_MODE_13_VAL  0x00000400
#define DDR_SDRAM_MODE_14_VAL  0x04000000
#define DDR_SDRAM_MODE_15_VAL  0x00000400
#define DDR_SDRAM_MODE_16_VAL  0x04000000
#define DDR_TIMING_CFG_8_VAL   0x02114600
#define DDR_SDRAM_CFG_3_VAL    0x00000000
#define DDR_DQ_MAP_0_VAL       0x5b65b658
#define DDR_DQ_MAP_1_VAL       0xd96d8000
#define DDR_DQ_MAP_2_VAL       0x00000000
#define DDR_DQ_MAP_3_VAL       0x01600000
#define DDR_DDRDSR_1_VAL       0x00000000
#define DDR_DDRDSR_2_VAL       0x00000000
#define DDR_DDRCDR_1_VAL       0x80040000
#define DDR_DDRCDR_2_VAL       0x0000A181
#define DDR_ERR_INT_EN_VAL     0x00000000
#define DDR_ERR_SBE_VAL        0x00000000


/* 12.4 DDR Memory Map */
#define DDR_BASE           (0x1080000)
#define DDR_PHY_BASE       (0x1400000)

#define DDR_CS_BNDS(n)     *((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   *((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_TIMING_CFG_3   *((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_0   *((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   *((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   *((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_SDRAM_CFG      *((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    *((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_MODE     *((volatile uint32_t*)(DDR_BASE + 0x118)) /* DDR SDRAM mode configuration */
#define DDR_SDRAM_MODE_2   *((volatile uint32_t*)(DDR_BASE + 0x11C)) /* DDR SDRAM mode configuration 2 */
#define DDR_SDRAM_MD_CNTL  *((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_INTERVAL *((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_DATA_INIT      *((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_SDRAM_CLK_CNTL *((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */
#define DDR_INIT_ADDR      *((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  *((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_TIMING_CFG_4   *((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   *((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_TIMING_CFG_6   *((volatile uint32_t*)(DDR_BASE + 0x168)) /* DDR SDRAM timing configuration 6 */
#define DDR_TIMING_CFG_7   *((volatile uint32_t*)(DDR_BASE + 0x16C)) /* DDR SDRAM timing configuration 7 */
#define DDR_ZQ_CNTL        *((volatile uint32_t*)(DDR_BASE + 0x170)) /* DDR ZQ calibration control */
#define DDR_WRLVL_CNTL     *((volatile uint32_t*)(DDR_BASE + 0x174)) /* DDR write leveling control */
#define DDR_SR_CNTR        *((volatile uint32_t*)(DDR_BASE + 0x17C)) /* DDR Self Refresh Counter */
#define DDR_SDRAM_RCW_1    *((volatile uint32_t*)(DDR_BASE + 0x180)) /* DDR Register Control Word 1 */
#define DDR_SDRAM_RCW_2    *((volatile uint32_t*)(DDR_BASE + 0x184)) /* DDR Register Control Word 2 */
#define DDR_WRLVL_CNTL_2   *((volatile uint32_t*)(DDR_BASE + 0x190)) /* DDR write leveling control 2 */
#define DDR_WRLVL_CNTL_3   *((volatile uint32_t*)(DDR_BASE + 0x194)) /* DDR write leveling control 3 */
#define DDR_SDRAM_RCW_3    *((volatile uint32_t*)(DDR_BASE + 0x1A0)) /* DDR Register Control Word 3 */
#define DDR_SDRAM_RCW_4    *((volatile uint32_t*)(DDR_BASE + 0x1A4)) /* DDR Register Control Word 4 */
#define DDR_SDRAM_RCW_5    *((volatile uint32_t*)(DDR_BASE + 0x1A8)) /* DDR Register Control Word 5 */
#define DDR_SDRAM_RCW_6    *((volatile uint32_t*)(DDR_BASE + 0x1AC)) /* DDR Register Control Word 6 */
#define DDR_SDRAM_MODE_3   *((volatile uint32_t*)(DDR_BASE + 0x200)) /* DDR SDRAM mode configuration 3 */
#define DDR_SDRAM_MODE_4   *((volatile uint32_t*)(DDR_BASE + 0x204)) /* DDR SDRAM mode configuration 4 */
#define DDR_SDRAM_MODE_5   *((volatile uint32_t*)(DDR_BASE + 0x208)) /* DDR SDRAM mode configuration 5 */
#define DDR_SDRAM_MODE_6   *((volatile uint32_t*)(DDR_BASE + 0x20C)) /* DDR SDRAM mode configuration 6 */
#define DDR_SDRAM_MODE_7   *((volatile uint32_t*)(DDR_BASE + 0x210)) /* DDR SDRAM mode configuration 7 */
#define DDR_SDRAM_MODE_8   *((volatile uint32_t*)(DDR_BASE + 0x214)) /* DDR SDRAM mode configuration 8 */
#define DDR_SDRAM_MODE_9   *((volatile uint32_t*)(DDR_BASE + 0x220)) /* DDR SDRAM mode configuration 9 */
#define DDR_SDRAM_MODE_10  *((volatile uint32_t*)(DDR_BASE + 0x224)) /* DDR SDRAM mode configuration 10 */
#define DDR_SDRAM_MODE_11  *((volatile uint32_t*)(DDR_BASE + 0x228)) /* DDR SDRAM mode configuration 11 */
#define DDR_SDRAM_MODE_12  *((volatile uint32_t*)(DDR_BASE + 0x22C)) /* DDR SDRAM mode configuration 12 */
#define DDR_SDRAM_MODE_13  *((volatile uint32_t*)(DDR_BASE + 0x230)) /* DDR SDRAM mode configuration 13 */
#define DDR_SDRAM_MODE_14  *((volatile uint32_t*)(DDR_BASE + 0x234)) /* DDR SDRAM mode configuration 14 */
#define DDR_SDRAM_MODE_15  *((volatile uint32_t*)(DDR_BASE + 0x238)) /* DDR SDRAM mode configuration 15 */
#define DDR_SDRAM_MODE_16  *((volatile uint32_t*)(DDR_BASE + 0x23C)) /* DDR SDRAM mode configuration 16 */
#define DDR_TIMING_CFG_8   *((volatile uint32_t*)(DDR_BASE + 0x250)) /* DDR SDRAM timing configuration 8 */
#define DDR_SDRAM_CFG_3    *((volatile uint32_t*)(DDR_BASE + 0x260)) /* DDR SDRAM configuration 3 */
#define DDR_DQ_MAP_0       *((volatile uint32_t*)(DDR_BASE + 0x400)) /* DDR DQ Map 0 */
#define DDR_DQ_MAP_1       *((volatile uint32_t*)(DDR_BASE + 0x404)) /* DDR DQ Map 1 */
#define DDR_DQ_MAP_2       *((volatile uint32_t*)(DDR_BASE + 0x408)) /* DDR DQ Map 2 */
#define DDR_DQ_MAP_3       *((volatile uint32_t*)(DDR_BASE + 0x40C)) /* DDR DQ Map 3 */
#define DDR_DDRDSR_1       *((volatile uint32_t*)(DDR_BASE + 0xB20)) /* DDR Debug Status Register 1 */
#define DDR_DDRDSR_2       *((volatile uint32_t*)(DDR_BASE + 0xB24)) /* DDR Debug Status Register 2 */
#define DDR_DDRCDR_1       *((volatile uint32_t*)(DDR_BASE + 0xB28)) /* DDR Control Driver Register 1 */
#define DDR_DDRCDR_2       *((volatile uint32_t*)(DDR_BASE + 0xB2C)) /* DDR Control Driver Register 2 */
#define DDR_MTCR           *((volatile uint32_t*)(DDR_BASE + 0xD00)) /* Memory Test Control Register */
#define DDR_MTPn(n)        *((volatile uint32_t*)(DDR_BASE + 0xD20 + (n) * 4)) /* Memory Test Pattern Register */
#define DDR_MTP0           *((volatile uint32_t*)(DDR_BASE + 0xD20)) /* Memory Test Pattern Register 0 */
#define DDR_MT_ST_ADDR     *((volatile uint32_t*)(DDR_BASE + 0xD64)) /* Memory Test Start Address */
#define DDR_MT_END_ADDR    *((volatile uint32_t*)(DDR_BASE + 0xD6C)) /* Memory Test End Address */
#define DDR_ERR_DETECT     *((volatile uint32_t*)(DDR_BASE + 0xE40)) /* Memory error detect */
#define DDR_ERR_DISABLE    *((volatile uint32_t*)(DDR_BASE + 0xE44)) /* Memory error disable */
#define DDR_ERR_INT_EN     *((volatile uint32_t*)(DDR_BASE + 0xE48)) /* Memory error interrupt enable */
#define DDR_ERR_SBE        *((volatile uint32_t*)(DDR_BASE + 0xE58)) /* Single-Bit ECC memory error management */

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG_BI       0x00000001
#define DDR_SDRAM_CFG2_D_INIT  0x00000010 /* data initialization in progress */
#define DDR_MEM_TEST_EN        0x80000000 /* Memory test enable */
#define DDR_MEM_TEST_FAIL      0x00000001 /* Memory test fail */
#define TEST_DDR_SIZE          1024 * 5
#define TEST_DDR_OFFSET        0x10000000

/* MMU Access permission and shareability
   Device mem encoding 0b0000dd00
   dd = 00, 01, 10, 11
   Nomral Mem encoding 0bxxxxiiii
   xxxx = 00RW, 0100, 01RW, 10RW, 11RW, where RW = Outer Read/Write policy
   iiii = 00RW, 0100, 01RW, 10RW, 11RW, where RW = Inner Read/Write policy
   R or W is 0 for No alloc, 1 for alloc
*/
 #define ATTR_SH_IS               (0x3 << 8) /* Inner Shareable */
 #define ATTR_SH_OS               (0x2 << 8) /* Outer Shareable */
 #define ATTR_UXN                 (0x1 << 54) /* EL0 cannot execute */
 #define ATTR_PXN                 (0x1 << 53) /* EL1 cannot execute */
 #define ATTR_AF                  (0x1 << 10) /* Access Flag */
 #define ATTR_AP_RW_PL1           (0x1 << 6) /* EL1 Read-Write */
 #define ATTR_AP_RW_PL0           (0x0 << 6) /* EL0 Read-Write */
 #define ATTR_AP_RO_PL1           (0x5 << 6) /* EL1 Read-Only */
 #define ATTR_AP_RO_PL0           (0x4 << 6) /* EL0 Read-Only */
 #define ATTR_NS                  (0x1 << 5) /* Non-secure */
 #define ATTR_AP_RW               (ATTR_AP_RW_PL1 | ATTR_AP_RW_PL0)

 /* Memory attribute MAIR reg cfg */
 #define ATTR_IDX_NORMAL_MEM      0
 #define MAIR_ATTR_NORMAL_MEM     0xFF /* Normal, Write-Back, Read-Write-Allocate */
 #define ATTR_IDX_DEVICE_MEM      1
 #define MAIR_ATTR_DEVICE_MEM     0x04 /* Device-nGnRnE */

 #define ATTRIBUTE_DEVICE        (ATTR_IDX_DEVICE_MEM << 2) | ATTR_AP_RW | ATTR_SH_IS
 #define ATTRIBUTE_NORMAL_MEM    (ATTR_IDX_NORMAL_MEM << 2) | ATTR_AP_RW | ATTR_SH_IS

/* SPI interface */
#define SPI_MCR(_n)    *((volatile unsigned int*)(SPI_BASE(_n)+0x000))
#define SPI_TCR(_n)    *((volatile unsigned int*)(SPI_BASE(_n)+0x008))
#define SPI_CTAR0(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x00C))
#define SPI_CTAR1(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x010))
#define SPI_SR(_n)     *((volatile unsigned int*)(SPI_BASE(_n)+0x02C))
#define SPI_RSER(_n)   *((volatile unsigned int*)(SPI_BASE(_n)+0x030))
#define SPI_PUSHR(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x034))
#define SPI_POPR(_n)   *((volatile unsigned int*)(SPI_BASE(_n)+0x038))
#define SPI_TXFR0(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x03C))
#define SPI_TXFR1(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x040))
#define SPI_TXFR2(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x044))
#define SPI_TXFR3(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x048))
#define SPI_RXFR0(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x07C))
#define SPI_RXFR1(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x080))
#define SPI_RXFR2(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x084))
#define SPI_RXFR3(_n)  *((volatile unsigned int*)(SPI_BASE(_n)+0x088))
#define SPI_CTARE0(_n) *((volatile unsigned int*)(SPI_BASE(_n)+0x11C))
#define SPI_CTARE1(_n) *((volatile unsigned int*)(SPI_BASE(_n)+0x120))
#define SPI_SREX(_n)   *((volatile unsigned int*)(SPI_BASE(_n)+0x13C))

/* Configuration is a simple, 8-bit frame that is expected to be
 * accessed with single byte transactions.
 */

/* MCR config */
/* Master, no frz, inactive CS high, flush FIFO, halt */
#define SPI_MCR_MASTER_HALT    0x80010301ul
/* Master, no frz, inactive CS high, running */
#define SPI_MCR_MASTER_RUNNING 0x80010000ul

/* CTAR config*/
/* no double baud, 8-bit frame, mode 00, MSB, default delays
 * PBR=2, BR=32 for total divisor 64
 * 200MHz platform clock yields 3.125MHz SPI clk */
#define SPI_CTAR_8_00MODE_64DIV 0x38000005

/* no double baud, 8-bit frame, mode 00, MSB, default delays
 * PBR=2, BR=4 for total divisor 8
 * 200MHz platform clock yields 25MHz SPI clk */
#define SPI_CTAR_8_00MODE_8DIV 0x38000001

/* SPI has TX/RX FIFO with limited depth, overwrite on overflow */
#define SPI_FIFO_DEPTH 4

/* CMD/DATA FIFO entry*/
/* no keep pcs asserted, use CTAR0, not EOQ, no clear TC, no parity*/
#define SPI_PUSHR_LAST 0x00000000
/* keep pcs asserted, use CTAR0, not EOQ, no clear TC, no parity*/
#define SPI_PUSHR_CONT 0x80000000
/* CS selection (0-3).  Not all SPI interfaces have 4 CS's */
#define SPI_PUSHR_PCS_SHIFT (16)
#define SPI_PUSHR_PCS(_n) ((1ul<<(_n)) << SPI_PUSHR_PCS_SHIFT)

/* Status register bits*/
#define SPI_SR_TCF (1ul << 31)
#define SPI_SR_TFFF (1ul << 25)
#define SPI_SR_RXCTR (0xFul << 4)

#endif /* !NXP_LS1028A_H */
