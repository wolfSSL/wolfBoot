/* nxp_t2080.h
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
 *
 * Board: NAII 68PPC2
 * NXP T2080E Rev 1.1, e6500 core 2.0, PVR 8040_0120 and SVR 8538_0011
 */

#ifndef NXP_T2080_H
#define NXP_T2080_H

#include "nxp_ppc.h"

/* Uncomment to enable verbose DDR debugging output */
/* #define DEBUG_DDR */

/* T2080 System Clock */
#define SYS_CLK (600000000) /* 100MHz PLL with 6:1 = 600 MHz */

/* ---- UART (PC16552D Dual UART) ---- */
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


/* ---- IFC (Integrated Flash Controller) - T2080RM 13.3 ---- */
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

#define IFC_CSPR_PHYS_ADDR(x) (((uint32_t)x) & 0xFFFF0000) /* Physical base address */
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


/* ---- NOR Flash ---- */
#define FLASH_BANK_SIZE   (128*1024*1024)
#define FLASH_PAGE_SIZE   (512) /* program buffer (256 bytes per chip x 2 chips) */
#define FLASH_SECTOR_SIZE (128*1024)
#define FLASH_SECTORS     (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_CFI_WIDTH   16 /* 8 or 16 */

#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */


/* ---- CPLD ---- */
#if 0
    #define ENABLE_CPLD
#endif
#define CPLD_BASE               0xFFDF0000
#define CPLD_BASE_PHYS_HIGH     0xFULL

#define CPLD_SPARE              0x00
#define CPLD_SATA_MUX_SEL       0x02
#define CPLD_BANK_SEL           0x04
#define CPLD_FW_REV             0x06
#define CPLD_TTL_RW             0x08
#define CPLD_TTL_LPBK           0x0A
#define CPLD_TTL_DATA           0x0C
#define CPLD_PROC_STATUS        0x0E /* write 1 to enable proc reset function, reset default value is 0 */
#define CPLD_FPGA_RDY           0x10 /* read only when reg read 0x0DB1 then fpga is ready */
#define CPLD_PCIE_SW_RESET      0x12 /* write 1 to reset the PCIe switch */
#define CPLD_WR_TTL_INT_EN      0x14
#define CPLD_WR_TTL_INT_DIR     0x16
#define CPLD_INT_STAT           0x18
#define CPLD_WR_TEMP_ALM_OVRD   0x1A /* write 0 to enable temp shutdown. reset default value is 1 */
#define CPLD_PWR_DWN_CMD        0x1C
#define CPLD_TEMP_ALM_INT_STAT  0x1E
#define CPLD_WR_TEMP_ALM_INT_EN 0x20

#define CPLD_FLASH_BANK_0       0x00
#define CPLD_FLASH_BANK_1       0x01

#define CPLD_DATA(n) ((volatile uint8_t*)(CPLD_BASE + n))


/* ---- SATA ---- */
#define SATA_ENBL ((volatile uint32_t*)0xB1003F4C) /* also saw 0xB4003F4C */


/* ---- Boot Page Translation - T2080RM 4.4.9 ---- */
#define LCC_BSTRH            ((volatile uint32_t*)(CCSRBAR + 0x20)) /* Boot space translation register high */
#define LCC_BSTRL            ((volatile uint32_t*)(CCSRBAR + 0x24)) /* Boot space translation register low */
#define LCC_BSTAR            ((volatile uint32_t*)(CCSRBAR + 0x28)) /* Boot space translation attribute register */
#define LCC_BSTAR_EN         0x80000000
#define LCC_BSTAR_LAWTRGT(n) ((n) << 20)
#define LCC_BSTAR_LAWSZ(n)   ((n) & 0x3F)

/* ---- DCFG (Device Configuration) - T2080RM 6.3 ---- */
#define DCFG_BASE       (CCSRBAR + 0xE0000)
#define DCFG_DCSR       ((volatile uint32_t*)(DCFG_BASE + 0x704)) /* Debug Configuration and Status */
#define DCFG_DEVDISR1   ((volatile uint32_t*)(DCFG_BASE + 0x070)) /* Device Disable Control 1 */
#define DCFG_DEVDISR2   ((volatile uint32_t*)(DCFG_BASE + 0x074)) /* Device Disable Control 2 */
#define DCFG_DEVDISR3   ((volatile uint32_t*)(DCFG_BASE + 0x078)) /* Device Disable Control 3 */
#define DCFG_DEVDISR4   ((volatile uint32_t*)(DCFG_BASE + 0x07C)) /* Device Disable Control 4 */
#define DCFG_DEVDISR5   ((volatile uint32_t*)(DCFG_BASE + 0x080)) /* Device Disable Control 5 */
#define DCFG_BRR        ((volatile uint32_t*)(DCFG_BASE + 0xE4))  /* Boot Release Register */

/* ---- RCPM (Run Control and Power Management) - T2080RM 6.4 ---- */
#define RCPM_BASE       (CCSRBAR + 0xE2000)
#define RCPM_PCTBENR    ((volatile uint32_t*)(RCPM_BASE + 0x1A0)) /* Physical Core Timebase Enable */

/* ---- Clocking - T2080RM 5.3 ---- */
#define CLOCKING_BASE           (CCSRBAR + 0xE1000)
#define CLOCKING_CLKCCSR(n)     ((volatile uint32_t*)(CLOCKING_BASE + 0x000UL + ((n) * 0x20)))
#define CLOCKING_PLLCNGSR(n)    ((volatile uint32_t*)(CLOCKING_BASE + 0x800UL + ((n) * 0x20))) /* PLL cluster n general status */
#define CLOCKING_PLLPGSR        ((volatile uint32_t*)(CLOCKING_BASE + 0xC00UL)) /* Platform PLL general status */

/* ---- MPIC - T2080RM 24.3 ---- */
#define PIC_BASE    (CCSRBAR + 0x40000)
#define PIC_WHOAMI  ((volatile uint32_t*)(PIC_BASE + 0x0090UL))


/* ---- DDR (T2080RM 12.4) ---- */
/* NAII 68PPC2 - 8GB discrete DDR3 IM8G08D3EBDG-15E */
/* 1333.333 MT/s data rate 8 GiB (DDR3, 64-bit, CL=9, ECC on) */
#define DDR_N_RANKS     2
#define DDR_RANK_DENS   0x100000000
#define DDR_SDRAM_WIDTH 64
#define DDR_EC_SDRAM_W  8
#define DDR_N_ROW_ADDR  16
#define DDR_N_COL_ADDR  10
#define DDR_N_BANKS     8
#define DDR_EDC_CONFIG  2
#define DDR_BURSTL_MASK 0x0c
#define DDR_TCKMIN_X_PS 1500
#define DDR_TCMMAX_PS   3000
#define DDR_CASLAT_X    0x000007E0
#define DDR_TAA_PS      13500
#define DDR_TRCD_PS     13500
#define DDR_TRP_PS      13500
#define DDR_TRAS_PS     36000
#define DDR_TRC_PS      49500
#define DDR_TFAW_PS     30000
#define DDR_TWR_PS      15000
#define DDR_TRFC_PS     260000
#define DDR_TRRD_PS     6000
#define DDR_TWTR_PS     7500
#define DDR_TRTP_PS     7500
#define DDR_REF_RATE_PS 7800000

/* DDR values from working U-Boot on NAII 68PPC2 board */
#define DDR_CS0_BNDS_VAL       0x000000FF
#define DDR_CS1_BNDS_VAL       0x010001FF
#define DDR_CS2_BNDS_VAL       0x0300033F
#define DDR_CS3_BNDS_VAL       0x0340037F
#define DDR_CS0_CONFIG_VAL     0x80044402
#define DDR_CS1_CONFIG_VAL     0x80044402
#define DDR_CS2_CONFIG_VAL     0x00000202
#define DDR_CS3_CONFIG_VAL     0x00040202
#define DDR_CS_CONFIG_2_VAL    0x00000000

#define DDR_TIMING_CFG_0_VAL   0xFF530004
#define DDR_TIMING_CFG_1_VAL   0x98906345
#define DDR_TIMING_CFG_2_VAL   0x0040A114
#define DDR_TIMING_CFG_3_VAL   0x010A1100
#define DDR_TIMING_CFG_4_VAL   0x00000001
#define DDR_TIMING_CFG_5_VAL   0x04402400

#define DDR_SDRAM_MODE_VAL     0x00441C70
#define DDR_SDRAM_MODE_2_VAL   0x00980000
#define DDR_SDRAM_MODE_3_8_VAL 0x00000000
#define DDR_SDRAM_MD_CNTL_VAL  0x00000000

#define DDR_SDRAM_CFG_VAL      0xE7040000
#define DDR_SDRAM_CFG_2_VAL    0x00401000

#define DDR_SDRAM_INTERVAL_VAL 0x0C300100
#define DDR_DATA_INIT_VAL      0xDEADBEEF
#define DDR_SDRAM_CLK_CNTL_VAL 0x02400000
#define DDR_ZQ_CNTL_VAL        0x89080600

/* Write leveling - CRITICAL: board-specific values from U-Boot */
#define DDR_WRLVL_CNTL_VAL     0x8675F604
#define DDR_WRLVL_CNTL_2_VAL   0x05060607
#define DDR_WRLVL_CNTL_3_VAL   0x080A0A0B

#define DDR_SDRAM_RCW_1_VAL    0x00000000
#define DDR_SDRAM_RCW_2_VAL    0x00000000

#define DDR_DDRCDR_1_VAL       0x80040000
#define DDR_DDRCDR_2_VAL       0x00000001

#define DDR_ERR_INT_EN_VAL     0x0000001D
#define DDR_ERR_SBE_VAL        0x00010000


/* 12.4 DDR Memory Map */
#define DDR_BASE           (CCSRBAR + 0x8000)

#define DDR_CS_BNDS(n)     ((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   ((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_CS_CONFIG_2(n) ((volatile uint32_t*)(DDR_BASE + 0x0C0 + (n * 4))) /* Chip select n configuration 2 */
#define DDR_TIMING_CFG_3   ((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_0   ((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   ((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   ((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_SDRAM_CFG      ((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    ((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_MODE     ((volatile uint32_t*)(DDR_BASE + 0x118)) /* DDR SDRAM mode configuration */
#define DDR_SDRAM_MODE_2   ((volatile uint32_t*)(DDR_BASE + 0x11C)) /* DDR SDRAM mode configuration 2 */
#define DDR_SDRAM_MD_CNTL  ((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_INTERVAL ((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_DATA_INIT      ((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_SDRAM_CLK_CNTL ((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */
#define DDR_INIT_ADDR      ((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  ((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_TIMING_CFG_4   ((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   ((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_TIMING_CFG_6   ((volatile uint32_t*)(DDR_BASE + 0x168)) /* DDR SDRAM timing configuration 6 */
#define DDR_ZQ_CNTL        ((volatile uint32_t*)(DDR_BASE + 0x170)) /* DDR ZQ calibration control */
#define DDR_WRLVL_CNTL     ((volatile uint32_t*)(DDR_BASE + 0x174)) /* DDR write leveling control */
#define DDR_SR_CNTR        ((volatile uint32_t*)(DDR_BASE + 0x17C)) /* DDR Self Refresh Counter */
#define DDR_SDRAM_RCW_1    ((volatile uint32_t*)(DDR_BASE + 0x180)) /* DDR Register Control Word 1 */
#define DDR_SDRAM_RCW_2    ((volatile uint32_t*)(DDR_BASE + 0x184)) /* DDR Register Control Word 2 */
#define DDR_WRLVL_CNTL_2   ((volatile uint32_t*)(DDR_BASE + 0x190)) /* DDR write leveling control 2 */
#define DDR_WRLVL_CNTL_3   ((volatile uint32_t*)(DDR_BASE + 0x194)) /* DDR write leveling control 3 */
#define DDR_SDRAM_MODE_3   ((volatile uint32_t*)(DDR_BASE + 0x200)) /* DDR SDRAM mode configuration 3 */
#define DDR_SDRAM_MODE_4   ((volatile uint32_t*)(DDR_BASE + 0x204)) /* DDR SDRAM mode configuration 4 */
#define DDR_SDRAM_MODE_5   ((volatile uint32_t*)(DDR_BASE + 0x208)) /* DDR SDRAM mode configuration 5 */
#define DDR_SDRAM_MODE_6   ((volatile uint32_t*)(DDR_BASE + 0x20C)) /* DDR SDRAM mode configuration 6 */
#define DDR_SDRAM_MODE_7   ((volatile uint32_t*)(DDR_BASE + 0x210)) /* DDR SDRAM mode configuration 7 */
#define DDR_SDRAM_MODE_8   ((volatile uint32_t*)(DDR_BASE + 0x214)) /* DDR SDRAM mode configuration 8 */
#define DDR_DDRCDR_1       ((volatile uint32_t*)(DDR_BASE + 0xB28)) /* DDR Control Driver Register 1 */
#define DDR_DDRCDR_2       ((volatile uint32_t*)(DDR_BASE + 0xB2C)) /* DDR Control Driver Register 2 */
#define DDR_DDRDSR_1       ((volatile uint32_t*)(DDR_BASE + 0xB20)) /* DDR Debug Status Register 1 */
#define DDR_DDRDSR_2       ((volatile uint32_t*)(DDR_BASE + 0xB24)) /* DDR Debug Status Register 2 */
#define DDR_ERR_DETECT     ((volatile uint32_t*)(DDR_BASE + 0xE40)) /* Memory error detect */
#define DDR_ERR_DISABLE    ((volatile uint32_t*)(DDR_BASE + 0xE44)) /* Memory error disable */
#define DDR_ERR_INT_EN     ((volatile uint32_t*)(DDR_BASE + 0xE48)) /* Memory error interrupt enable */
#define DDR_ERR_SBE        ((volatile uint32_t*)(DDR_BASE + 0xE58)) /* Single-Bit ECC memory error management */

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG_BI       0x00000001 /* Bypass initialization */
#define DDR_SDRAM_CFG_2_D_INIT 0x00000010 /* data initialization in progress */

#endif /* NXP_T2080_H */
