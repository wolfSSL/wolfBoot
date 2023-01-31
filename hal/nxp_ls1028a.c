/* ls1028a.c
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

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot ls1028a HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif


/* Fixed addresses */
static const void* kernel_addr  = (void*)0x0140000;
static const void* update_addr  = (void*)0x1140000;
static const void* dts_addr     = (void*)0x00a0000;

/* HAL options */
//#define ENABLE_DDR

/* LS1028A */
#define CCSRBAR    (0x1000000)
#define SYS_CLK    (400000000) /* Sysclock = 400Mhz set by RCW */
#define FLASH_FREQ (100000000) /* Flash clock = 100Mhz */

/* LS1028A PC16552D Dual UART */
#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */


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
#define XSPI_BASE              (0x20C0000)
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

/* XSPI Configuration Bytes */
//#define XSPI_MCR_CFG          0xFFFF80C0 /* MCR Configuraiton bytes */
//#define XSPI_AHBCR_CFG        0x00000028 /* AHB Bus Configuration bytes */
//#define XSPI_DLLACR_CFG       0x00000079 /* DLLA Control Configuration bytes */
//#define XSPI_FLSHA1CR0_CFG    0x0003E800 /* Configure flash size to 256MB */
//#define XSPI_FLSHA1CR1_CFG    0x00000063 /* Reset value, used for CS timing */
//#define XSPI_FLSHA1CR2_CFG    0x00000000 /* Reset value, used for AHB mode confiugration */
//#define XSPI_FLSHCR4_CFG      0x00000000 /* Reset value */

/* XSPI Init */
#define XSPI_MCR0_CFG         0xFFFF8000
#define XSPI_MCR1_CFG         0xFFFFFFFF
#define XSPI_MCR2_CFG         0x200001F7
#define XSPI_AHBCR_CFG        0x00000058
#define XSPI_AHBRXBUFnCR_CFG  0x80000000
#define XSPI_FLSHA1CR0_CFG    0x00200000

/* Flash Size */
#define XSPI_FLSHA1CR0_SIZE   0x40000
#define XSPI_FLSHA2CR0_SIZE   0x40000
#define XSPI_FLSHB1CR0_SIZE   0x40000
#define XSPI_FLSHB2CR0_SIZE   0x40000

/* XSPI Timing */
#define XSPI_FLSHA1CR1_CFG    0x00000063
#define XSPI_FLSHA2CR1_CFG    0x00000063
#define XSPI_FLSHB1CR1_CFG    0x00000063
#define XSPI_FLSHB2CR1_CFG    0x00000063
#define XSPI_FLSHA1CR2_CFG    0x00000900
#define XSPI_FLSHA2CR2_CFG    0x00000900
#define XSPI_FLSHB1CR2_CFG    0x00000900
#define XSPI_FLSHB2CR2_CFG    0x00000900
#define XSPI_IPRXFCR_CFG      0x00000001
#define XSPI_IPTXFCR_CFG      0x00000001
#define XSPI_DLLACR_CFG       0x100
#define XSPI_DLLBCR_CFG       0x100
#define XSPI_AHB_UPDATE       0x20


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
#define FALSH_ERASE_SIZE  (128 * 1024)          /* erase sector size */
#define FLASH_SECTORS     (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */

/* LUT register helper */
#define XSPI_LUT_SEQ(code1, pad1, op1, code0, pad0, op0) \
    (((code1) << 26) | ((pad1) << 24) | ((op1) << 16) |  \
    (code0) << 10 | ((pad0) << 8) | (op0))

/* FlexSPI Look up Table defines */
#define LUT_KEY                 0x5AF05AF0
/* Calculate the number of PAD bits for LUT register*/
#define LUT_PAD(x)              (x - 1)

/* Instructions for the LUT register. */
#define LUT_STOP                0x00
#define LUT_CMD                 0x01
#define LUT_ADDR                0x02
#define LUT_CADDR_SDR           0x03
#define LUT_MODE                0x04
#define LUT_MODE2               0x05
#define LUT_MODE4               0x06
#define LUT_MODE8               0x07
#define LUT_NXP_WRITE           0x08
#define LUT_NXP_READ            0x09
#define LUT_LEARN_SDR           0x0A
#define LUT_DATSZ_SDR           0x0B
#define LUT_DUMMY               0x0C
#define LUT_DUMMY_RWDS_SDR      0x0D
#define LUT_JMP_ON_CS           0x1F
#define LUT_CMD_DDR             0x21
#define LUT_ADDR_DDR            0x22
#define LUT_CADDR_DDR           0x23
#define LUT_MODE_DDR            0x24
#define LUT_MODE2_DDR           0x25
#define LUT_MODE4_DDR           0x26
#define LUT_MODE8_DDR           0x27
#define LUT_WRITE_DDR           0x28
#define LUT_READ_DDR            0x29
#define LUT_LEARN_DDR           0x2A
#define LUT_DATSZ_DDR           0x2B
#define LUT_DUMMY_DDR           0x2C
#define LUT_DUMMY_RWDS_DDR      0x2D

/* MT35XU02GCBA1G12 Command definitions */
#define LUT_CMD_WE              0x06 /* Write Enable */
#define LUT_CMD_WD              0x04 /* Write Disable */
#define LUT_CMD_WNVCR           0xB1 /* Write Non-Volatile Configuration Register */
#define LUT_CMD_CLSFR           0x50 /* Clear Status Flag Register */
#define LUT_CMD_WSR             0x01 /* Write Status Register */
#define LUT_CMD_RSR             0x05 /* Read Status Register */
#define LUT_CMD_RID             0x9F /* Read ID */
#define LUT_CMD_PP              0x02 /* Page Program */
#define LUT_CMD_4PP             0x12 /* 4 byte Page Program */
#define LUT_CMD_SE              0xD8 /* Sector Erase */
#define LUT_CMD_SE_4K           0x20 /* 4K Sector Erase */
#define LUT_CMD_SE_32K          0x52 /* 32K Sector Erase */
#define LUT_CMD_4SE             0xDC /* 4 byte Sector Erase */
#define LUT_CMD_CE              0xC4 /* Chip Erase */
#define LUT_CMD_READ            0x03 /* Read */
#define LUT_CMD_4READ           0x13 /* 4 byte Read */
#define LUT_ADDR_3B             0x18 /* 3 byte address */
#define LUT_ADDR_4B             0x20 /* 4 byte address */

/* MT40A1G8SA-075:E --> DDR4: static, 1GB, 1600 MHz (1.6 GT/s) */
#define DDR_ADDRESS            0x80000000
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

#define DDR_CS0_BNDS_VAL       0x0000007F
#define DDR_CS1_BNDS_VAL       0x0
#define DDR_CS2_BNDS_VAL       0x0
#define DDR_CS3_BNDS_VAL       0x0
#define DDR_CS0_CONFIG_VAL     0x80040322
#define DDR_CS1_CONFIG_VAL     0x00000000
#define DDR_CS2_CONFIG_VAL     0x00000000
#define DDR_CS3_CONFIG_VAL     0x00000000
#define DDR_CS_CONFIG_2_VAL    0x00000000

#define DDR_TIMING_CFG_0_VAL   0x91550018
#define DDR_TIMING_CFG_1_VAL   0xBBB48C42
#define DDR_TIMING_CFG_2_VAL   0x0048C111
#define DDR_TIMING_CFG_3_VAL   0x010C1000
#define DDR_TIMING_CFG_4_VAL   0x00000002
#define DDR_TIMING_CFG_5_VAL   0x03401400
#define DDR_TIMING_CFG_6_VAL   0x00000000
#define DDR_TIMING_CFG_7_VAL   0x13300000
#define DDR_TIMING_CFG_8_VAL   0x02115600

#define DDR_SDRAM_MODE_VAL     0x03010210
#define DDR_SDRAM_MODE_2_VAL   0x00000000
#define DDR_SDRAM_MODE_3_VAL   0x00001021
#define DDR_SDRAM_MODE_9_VAL   0x00000500
#define DDR_SDRAM_MODE_10_VAL  0x04000000
#define DDR_SDRAM_MODE_11_VAL  0x00000400
#define DDR_SDRAM_MD_CNTL_VAL  0x00000000

#define DDR_SDRAM_CFG_VAL      0xC50C0008
#define DDR_SDRAM_CFG_2_VAL    0x00401100

#define DDR_SDRAM_INTERVAL_VAL 0x18600618
#define DDR_DATA_INIT_VAL      0xDEADBEEF
#define DDR_SDRAM_CLK_CNTL_VAL 0x03000000
#define DDR_ZQ_CNTL_VAL        0x8A090705

#define DDR_WRLVL_CNTL_VAL     0x8675F607
#define DDR_WRLVL_CNTL_2_VAL   0x07090800
#define DDR_WRLVL_CNTL_3_VAL   0x00000000

#define DDR_SDRAM_RCW_1_VAL    0x00000000
#define DDR_SDRAM_RCW_2_VAL    0x00000000

#define DDR_DDRCDR_1_VAL       0x80040000
#define DDR_DDRCDR_2_VAL       0x0000A181

#define DDR_ERR_INT_EN_VAL     0x00000000
#define DDR_ERR_SBE_VAL        0x00000000

/* 12.4 DDR Memory Map */
#define DDR_BASE           (0x1080000)
#define DDR_BASE_PHYS      (0xF00000000ULL | DDR_BASE)

#define DDR_CS_BNDS(n)     *((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   *((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_SDRAM_CFG      *((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    *((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_INTERVAL *((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_INIT_ADDR      *((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  *((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_DATA_INIT      *((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_TIMING_CFG_3   *((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_0   *((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   *((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   *((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_TIMING_CFG_4   *((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   *((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_TIMING_CFG_6   *((volatile uint32_t*)(DDR_BASE + 0x168)) /* DDR SDRAM timing configuration 6 */
#define DDR_TIMING_CFG_7   *((volatile uint32_t*)(DDR_BASE + 0x16C)) /* DDR SDRAM timing configuration 7 */
#define DDR_TIMING_CFG_8   *((volatile uint32_t*)(DDR_BASE + 0x250)) /* DDR SDRAM timing configuration 8 */
#define DDR_ZQ_CNTL        *((volatile uint32_t*)(DDR_BASE + 0x170)) /* DDR ZQ calibration control */
#define DDR_WRLVL_CNTL     *((volatile uint32_t*)(DDR_BASE + 0x174)) /* DDR write leveling control */
#define DDR_WRLVL_CNTL_2   *((volatile uint32_t*)(DDR_BASE + 0x190)) /* DDR write leveling control 2 */
#define DDR_WRLVL_CNTL_3   *((volatile uint32_t*)(DDR_BASE + 0x194)) /* DDR write leveling control 3 */
#define DDR_SR_CNTR        *((volatile uint32_t*)(DDR_BASE + 0x17C)) /* DDR Self Refresh Counter */
#define DDR_SDRAM_RCW_1    *((volatile uint32_t*)(DDR_BASE + 0x180)) /* DDR Register Control Word 1 */
#define DDR_SDRAM_RCW_2    *((volatile uint32_t*)(DDR_BASE + 0x184)) /* DDR Register Control Word 2 */
#define DDR_DDRCDR_1       *((volatile uint32_t*)(DDR_BASE + 0xB28)) /* DDR Control Driver Register 1 */
#define DDR_DDRCDR_2       *((volatile uint32_t*)(DDR_BASE + 0xB2C)) /* DDR Control Driver Register 2 */
#define DDR_DDRDSR_1       *((volatile uint32_t*)(DDR_BASE + 0xB20)) /* DDR Debug Status Register 1 */
#define DDR_DDRDSR_2       *((volatile uint32_t*)(DDR_BASE + 0xB24)) /* DDR Debug Status Register 2 */
#define DDR_ERR_DISABLE    *((volatile uint32_t*)(DDR_BASE + 0xE44)) /* Memory error disable */
#define DDR_ERR_INT_EN     *((volatile uint32_t*)(DDR_BASE + 0xE48)) /* Memory error interrupt enable */
#define DDR_ERR_SBE        *((volatile uint32_t*)(DDR_BASE + 0xE58)) /* Single-Bit ECC memory error management */
#define DDR_SDRAM_MODE     *((volatile uint32_t*)(DDR_BASE + 0x118)) /* DDR SDRAM mode configuration */
#define DDR_SDRAM_MODE_2   *((volatile uint32_t*)(DDR_BASE + 0x11C)) /* DDR SDRAM mode configuration 2 */
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
#define DDR_SDRAM_MD_CNTL  *((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_INTERVAL *((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_SDRAM_CLK_CNTL *((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG2_D_INIT  0x00000010 /* data initialization in progress */

void hal_flash_init(void);

#ifdef DEBUG_UART
static void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 300000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);

    while (!(UART_LSR(UART_SEL) & UART_LSR_TEMT));

    /* set ier, fcr, mcr */
    UART_IER(UART_SEL) = 0;
    UART_FCR(UART_SEL) = (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN);

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    UART_LCR(UART_SEL) = (UART_LCR_DLAB | UART_LCR_WLS);
    /* set divisor */
    UART_DLB(UART_SEL) = (div & 0xff);
    UART_DMB(UART_SEL) = ((div>>8) & 0xff);
    /* disable rate access (DLAB=0) */
    UART_LCR(UART_SEL) = (UART_LCR_WLS);
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while (!(UART_LSR(UART_SEL) & UART_LSR_THRE));
        UART_THR(UART_SEL) = buf[pos++];
    }
}
#endif /* DEBUG_UART */

void* hal_get_primary_address(void)
{
    return (void*)kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)update_addr;
}

void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
}

void* hal_get_dts_update_address(void)
{
  return (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
}

void hal_delay_us(uint32_t us) {
    us = SYS_CLK * us / 1000000;
    uint32_t i = 0;
    for (i = 0; i < us; i++) {
        asm volatile("nop");
    }
}

void erratum_err050568()
{
	/* Use IP bus only if systembus PLL is 300MHz */
}

/* Application on Serial NOR Flash device 18.6.3 */
void xspi_init()
{
    /* Enable controller clocks (AHB clock/IP Bus clock/Serial root clock) in System level. */
    /* Power FelxSPI SRAM */
    /* Release FlexSPI from reset using PRSTCLR */

    /* Set MCR0[MDIS] to 0x1 (Make sure controller is configured in module stop mode) */
    XSPI_ENTER_STOP();
    while ((XSPI_MCR0 & XSPI_MCR_MDIS_MASK) == 0);

    /* Configure module control register */
    XSPI_MCR0 = XSPI_MCR0_CFG;
    XSPI_MCR1 = XSPI_MCR1_CFG;
    XSPI_MCR2 = XSPI_MCR2_CFG;

    /* Configure AHB bus control register (AHBCR) and AHB RX Buffer control register (AHBRXBUFxCR0) */
    XSPI_AHBCR = XSPI_AHBCR_CFG;
    XSPI_AHBRXBUFnCR0(0) = XSPI_AHBRXBUFnCR_CFG;
    XSPI_AHBRXBUFnCR0(1) = XSPI_AHBRXBUFnCR_CFG;
    XSPI_AHBRXBUFnCR0(2) = XSPI_AHBRXBUFnCR_CFG;

    /* Configure Flash control registers (FLSHxCR0,FLSHxCR1,FLSHxCR2) */
    XSPI_FLSHA1CR0 = XSPI_FLSHA1CR0_CFG;
    XSPI_FLSHA1CR0 = XSPI_FLSHA1CR0_SIZE;
    XSPI_FLSHA2CR0 = XSPI_FLSHA2CR0_SIZE;
    XSPI_FLSHB1CR0 = XSPI_FLSHB1CR0_SIZE;
    XSPI_FLSHB2CR0 = XSPI_FLSHB2CR0_SIZE;
    XSPI_FLSHA1CR1 = XSPI_FLSHA1CR1_CFG;
    XSPI_FLSHA2CR1 = XSPI_FLSHA2CR1_CFG;
    XSPI_FLSHB1CR1 = XSPI_FLSHB1CR1_CFG;
    XSPI_FLSHB2CR1 = XSPI_FLSHB2CR1_CFG;
    XSPI_FLSHA1CR2 = XSPI_FLSHA1CR2_CFG;
    XSPI_FLSHA2CR2 = XSPI_FLSHA2CR2_CFG;
    XSPI_FLSHB1CR2 = XSPI_FLSHB1CR2_CFG;
    XSPI_FLSHB2CR2 = XSPI_FLSHB2CR2_CFG;

    /* Configure DLL control register (DLLxCR) according to sample clock source selection */
    XSPI_DLLACR = XSPI_DLLACR_CFG;
    XSPI_DLLBCR = XSPI_DLLBCR_CFG;

    /* set MCR0[MDIS] to 0x0 (Exit module stop mode) */
    XSPI_EXIT_STOP();
}

void xspi_lut_lock(void)
{
    XSPI_LUTKEY = LUT_KEY;
    XSPI_LUT_LOCK()
}

void xspi_lut_unlock(void)
{
    XSPI_LUTKEY = LUT_KEY;
    XSPI_LUT_UNLOCK()
}

void hal_flash_init()
{

    /* Init base XSPI module */
    xspi_init();

    xspi_lut_unlock();

    /* LUT0 - Read Status 1 byte */
    XSPI_LUT(0) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_RSR, LUT_NXP_READ, LUT_PAD(1), 0x1);
    XSPI_LUT(1) = 0x0;
    XSPI_LUT(2) = 0x0;
    XSPI_LUT(3) = 0x0;

    /* LUT1 - Write Enable */
    XSPI_LUT(4) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_WE, LUT_STOP, LUT_PAD(1), 0);
    XSPI_LUT(5) = 0x0;
    XSPI_LUT(6) = 0x0;
    XSPI_LUT(7) = 0x0;

    /* LUT2 - Page Program */
    XSPI_LUT(8) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_PP, LUT_ADDR, LUT_PAD(1), LUT_ADDR_3B);
    XSPI_LUT(9) = XSPI_LUT_SEQ(LUT_NXP_WRITE, LUT_PAD(1), 0x1, LUT_STOP, LUT_PAD(1), 0);
    XSPI_LUT(10) = 0x0;
    XSPI_LUT(11) = 0x0;

    /* LUT3 - Read */
    XSPI_LUT(12) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_READ, LUT_ADDR, LUT_PAD(1), LUT_ADDR_3B);
    XSPI_LUT(13) = XSPI_LUT_SEQ(LUT_NXP_READ, LUT_PAD(1), 0x1, LUT_STOP, LUT_PAD(1), 0);
    XSPI_LUT(14) = 0x0;
    XSPI_LUT(15) = 0x0;

    /* LUT4 - Sector Erase */
    XSPI_LUT(16) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_SE, LUT_ADDR, LUT_PAD(1), LUT_ADDR_3B);
    XSPI_LUT(17) = 0x0;
    XSPI_LUT(18) = 0x0;
    XSPI_LUT(19) = 0x0;

    /* LUT5 - Chip Erase */
    XSPI_LUT(20) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_CE, LUT_ADDR, LUT_PAD(1), LUT_ADDR_3B);
    XSPI_LUT(21) = 0x0;
    XSPI_LUT(22) = 0x0;
    XSPI_LUT(23) = 0x0;

    /* LUT6 - Read ID */
    XSPI_LUT(24) = XSPI_LUT_SEQ(LUT_CMD, LUT_PAD(1), LUT_CMD_RID, LUT_STOP, LUT_PAD(1), 0);
    XSPI_LUT(25) = 0x0;
    XSPI_LUT(26) = 0x0;
    XSPI_LUT(27) = 0x0;

    xspi_lut_lock();

    XSPI_AHBCR &= ~XSPI_AHB_UPDATE;
}

/* Called from boot_aarch64_start.S */
void hal_ddr_init() {
#ifdef ENABLE_DDR
    /* Setup DDR CS (chip select) bounds */
    DDR_CS_BNDS(0)   = DDR_CS0_BNDS_VAL;
    DDR_CS_CONFIG(0) = DDR_CS0_CONFIG_VAL;
    DDR_CS_BNDS(1)   = DDR_CS1_BNDS_VAL;
    DDR_CS_CONFIG(1) = DDR_CS1_CONFIG_VAL;
    DDR_CS_BNDS(2)   = DDR_CS2_BNDS_VAL;
    DDR_CS_CONFIG(2) = DDR_CS2_CONFIG_VAL;
    DDR_CS_BNDS(3)   = DDR_CS3_BNDS_VAL;
    DDR_CS_CONFIG(3) = DDR_CS3_CONFIG_VAL;

    /* DDR SDRAM timing configuration */
    DDR_TIMING_CFG_0 = DDR_TIMING_CFG_0_VAL;
    DDR_TIMING_CFG_1 = DDR_TIMING_CFG_1_VAL;
    DDR_TIMING_CFG_2 = DDR_TIMING_CFG_2_VAL;
    DDR_TIMING_CFG_3 = DDR_TIMING_CFG_3_VAL;
    DDR_TIMING_CFG_4 = DDR_TIMING_CFG_4_VAL;
    DDR_TIMING_CFG_5 = DDR_TIMING_CFG_5_VAL;
    DDR_TIMING_CFG_6 = DDR_TIMING_CFG_6_VAL;
    DDR_TIMING_CFG_7 = DDR_TIMING_CFG_7_VAL;
    DDR_TIMING_CFG_8 = DDR_TIMING_CFG_8_VAL;

    /* DDR SDRAM mode configuration */
    DDR_SDRAM_MODE   = DDR_SDRAM_MODE_VAL;
    DDR_SDRAM_MODE_2 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_3 = DDR_SDRAM_MODE_3_VAL;
    DDR_SDRAM_MODE_4 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_5 = DDR_SDRAM_MODE_3_VAL;
    DDR_SDRAM_MODE_6 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_7 = DDR_SDRAM_MODE_3_VAL;
    DDR_SDRAM_MODE_8 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_9 =  DDR_SDRAM_MODE_9_VAL;
    DDR_SDRAM_MODE_10 = DDR_SDRAM_MODE_10_VAL;
    DDR_SDRAM_MODE_11 = DDR_SDRAM_MODE_11_VAL;
    DDR_SDRAM_MODE_12 = DDR_SDRAM_MODE_10_VAL;
    DDR_SDRAM_MODE_13 = DDR_SDRAM_MODE_11_VAL;
    DDR_SDRAM_MODE_14 = DDR_SDRAM_MODE_10_VAL;
    DDR_SDRAM_MODE_15 = DDR_SDRAM_MODE_11_VAL;

    DDR_SDRAM_MD_CNTL = DDR_SDRAM_MD_CNTL_VAL;

    /* DDR Configuration */
    DDR_SDRAM_INTERVAL = DDR_SDRAM_INTERVAL_VAL;
    DDR_SDRAM_CLK_CNTL = DDR_SDRAM_CLK_CNTL_VAL;
    DDR_DATA_INIT = DDR_DATA_INIT_VAL;
    DDR_ZQ_CNTL = DDR_ZQ_CNTL_VAL;
    DDR_WRLVL_CNTL = DDR_WRLVL_CNTL_VAL;
    DDR_WRLVL_CNTL_2 = DDR_WRLVL_CNTL_2_VAL;
    DDR_WRLVL_CNTL_3 = DDR_WRLVL_CNTL_3_VAL;
    DDR_SR_CNTR = 0;
    DDR_SDRAM_RCW_1 = DDR_SDRAM_RCW_1_VAL;
    DDR_SDRAM_RCW_2 = DDR_SDRAM_RCW_2_VAL;


    DDR_DDRCDR_1 = DDR_DDRCDR_1_VAL;
    DDR_DDRCDR_2 = DDR_DDRCDR_2_VAL;

    DDR_SDRAM_CFG_2 = DDR_SDRAM_CFG_2_VAL;
    DDR_INIT_ADDR = 0;
    DDR_INIT_EXT_ADDR = 0;
    DDR_ERR_DISABLE = 0;
    DDR_ERR_INT_EN = DDR_ERR_INT_EN_VAL;
    DDR_ERR_SBE = DDR_ERR_SBE_VAL;

    /* Set values, but do not enable the DDR yet */
    DDR_SDRAM_CFG = (DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN);

    hal_delay_us(500);
    asm volatile("isb");

    /* Enable controller */
    DDR_SDRAM_CFG |= DDR_SDRAM_CFG_MEM_EN;
    asm volatile("isb");

    /* Wait for data initialization is complete */
    while ((DDR_SDRAM_CFG_2 & DDR_SDRAM_CFG2_D_INIT));

 #endif
}

void hal_init(void)
{
#ifdef DEBUG_UART
    uint32_t fw;

    uart_init();
    uart_write("wolfBoot Init\n", 14);

#endif

    uint32_t* p = (uint32_t*)0x80001000;
    *p = 0x12345678;

    //hal_flash_init();

#ifdef ENABLE_CPLD
    CPLD_DATA(CPLD_PROC_STATUS) = 1; /* Enable proc reset */
    CPLD_DATA(CPLD_WR_TEMP_ALM_OVRD) = 0; /* Enable temp alarm */

#ifdef DEBUG_UART
    fw = CPLD_DATA(CPLD_FW_REV);
    wolfBoot_printf("CPLD FW Rev: 0x%x\n", fw);
#endif
#endif /* ENABLE_CPLD */

}

/* NOR flash write */
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t bytes_written = 0, i, dat_sz;
    uint32_t *p = (uint32_t*)data;
    uint64_t fifo_addr = 0;
    uint32_t* fifo_p = NULL;

    while(bytes_written < (uint32_t)len) {
        /* Set XSPI start address */
        XSPI_SFAR = address;

        /* only 128 bytes can be written per IP CMD 18.5.9 */
        dat_sz = len > XSPI_FIFO_SIZE ? XSPI_FIFO_SIZE : len - bytes_written;

        XSPI_IPCR1 |= XSPI_ISEQID(0x4);
        XSPI_IPCR1 |= XSPI_ISEQNUM(0x4);
        XSPI_IPCR1 |= dat_sz;

        /* Fill XSPI TX FIFO */
        for (i = 0; i < dat_sz; i++) {
            if (bytes_written >= (uint32_t)len) {
                break;
            }

            fifo_addr = (XSPI_TFD(0) + (i * 0x4));
            fifo_p = (uint32_t*)fifo_addr;
            *fifo_p = *p++;

            bytes_written += 4;
        }

        address += bytes_written;

        /* Set XSPI Start command */
        XSPI_IPCMD_START();

        /* Wait for TX complete */
        while (!(XSPI_INTR & XSPI_IPCMDDONE));
    }

    return bytes_written;
}

/* NOR Flash Erase */
int hal_flash_erase(uint32_t address, int len)
{
    uint32_t i = 0;
    uint32_t num_sector = (len / FLASH_PAGE_SIZE) +
        ((len % FLASH_PAGE_SIZE) ? 1 : 0);

    for(i = 0; i < num_sector; i++) {
        XSPI_SFAR = address;

        XSPI_IPCR1 = XSPI_ISEQID(16);
        XSPI_IPCR1 |= XSPI_ISEQNUM(4);

        /* Set XSPI Start command */
        XSPI_IPCMD_START();

        /* Wait for complete */
        while (!(XSPI_INTR & XSPI_IPCMDDONE));

        address += FLASH_PAGE_SIZE;
    }

    return 0;
}

void hal_flash_unlock(void){}
void hal_flash_lock(void){}
void hal_prepare_boot(void){}
