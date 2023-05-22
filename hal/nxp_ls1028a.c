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
#include "printf.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot ls1028a HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif


/* HAL options */
#define ENABLE_DDR

/* LS1028A */
#define CCSRBAR    (0x1000000)
#define SYS_CLK    (400000000) /* Sysclock = 400Mhz set by RCW */
#define FLASH_FREQ (100000000) /* Flash clock = 100Mhz */
#define NOR_BASE   (0x20000000)

/* Trust Zone */
#define TZASC_BASE              (0x1100000)
#define TZASC_BUILD_CONFIG      *((volatile uint32_t*)(TZASC_BASE + 0x0))
#define TZASC_ACTION            *((volatile uint32_t*)(TZASC_BASE + 0x4))
#define TZASC_GATE_KEEPER       *((volatile uint32_t*)(TZASC_BASE + 0x8))
#define TZASC_SPECULATION_CTRL  *((volatile uint32_t*)(TZASC_BASE + 0xC))
#define TZASC_INT_STATUS        *((volatile uint32_t*)(TZASC_BASE + 0x10))
#define TZASC_INT_CLEAR         *((volatile uint32_t*)(TZASC_BASE + 0x14))
#define TZASC_FAIL_ADDRESS_LOW  *((volatile uint32_t*)(TZASC_BASE + 0x20))
#define TZASC_FAIL_ADDRESS_HIGH *((volatile uint32_t*)(TZASC_BASE + 0x24))
#define TZASC_FAIL_CONTROL      *((volatile uint32_t*)(TZASC_BASE + 0x28))
#define TZASC_REGION_BASE_LOW   *((volatile uint32_t*)(TZASC_BASE + 0x100))
#define TZASC_REGION_BASE_HIGH  *((volatile uint32_t*)(TZASC_BASE + 0x104))

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


void hal_flash_init(void);
void switch_el3_to_el2(void);
extern void mmu_enable(void);

#ifdef DEBUG_UART
static void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 400000000
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



/* SPI interface */

#define SPI_BASE(_n) (0x02100000ul + 0x10000*(_n))
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


/*Clear and halt the SPI controller*/
static void spi_close(unsigned int sel)
{
    /* Halt, flush  and set as master */
    SPI_MCR(sel) = SPI_MCR_MASTER_HALT;
}

/*Clear the SPI controller and setup as a running master*/
static void spi_open(unsigned int sel)
{
    spi_close(sel);
    /* Setup CTAR0 */
    SPI_CTAR0(sel) = SPI_CTAR_8_00MODE_8DIV;
    /* Enable as master */
    SPI_MCR(sel) = SPI_MCR_MASTER_RUNNING;
}

static int spi_can_rx(unsigned int sel)
{
    return !!(SPI_SR(sel) & SPI_SR_RXCTR);
}

static unsigned char spi_pop_rx(unsigned int sel)
{
    return SPI_POPR(sel) & 0xFF;
}

static void spi_flush_rx(unsigned int sel)
{
    unsigned char rx_data;
    while (spi_can_rx(sel)) {
        rx_data = spi_pop_rx(sel);
    }
    (void) rx_data;
}

static int spi_can_tx(unsigned int sel)
{
    return !!(SPI_SR(sel) & SPI_SR_TFFF);
}

static void spi_push_tx(unsigned int sel, unsigned int pcs, unsigned char data,
        int last)
{
    SPI_PUSHR(sel) = (last ? SPI_PUSHR_LAST : SPI_PUSHR_CONT)
            | SPI_PUSHR_PCS(pcs) | data;
}

/* Perform a SPI transaction.  Set cont!=0 to not let CS go low after this*/
static void spi_transaction(unsigned int sel, unsigned int pcs,
        const unsigned char *out, unsigned char *in, unsigned int size,
        int cont)
{
    unsigned int tx_count = size;
    unsigned int rx_count = size;
    int last = 0;
    unsigned char rx_data = 0;
    unsigned char tx_data = 0;

    /* XXX No parameter validation */

    /* Flush RX FIFO */
    spi_flush_rx(sel);

    /* Nothing to do? */
    if (!size)
        return;

    while (rx_count > 0) {
        /* Try to push TX data */
        while ((tx_count > 0) && ((rx_count - tx_count) < SPI_FIFO_DEPTH)
                && spi_can_tx(sel)) {
            if (out) {
                tx_data = *out;
                out++;
            }
            tx_count--;
            last = (!cont && !tx_count);
            spi_push_tx(sel, pcs, tx_data, last);
        }

        /* Try to pop RX data */
        while ((rx_count > 0) && spi_can_rx(sel)) {
            rx_data = spi_pop_rx(sel);
            if (in) {
                *in = rx_data;
                in++;
            }
            rx_count--;
        }
    }
}

/*#define TPM_TEST*/
#ifdef TPM_TEST
void read_tpm_id()
{
    /*Read 4 bytes from offset D40F00.  Assumes 0 wait state on TPM*/
    unsigned char out[8] = { 0x83, 0xD4, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00 };
    unsigned char in[8] = { 0 };
    unsigned char in2[8] = { 0 };
    unsigned int counter;

    /*LS1028A SPI to the MikroBus uses SPI3 (sel is 2) and CS 0*/
    #ifndef SPI_SEL_TPM
    #define SPI_SEL_TPM 2
    #endif

    #ifndef SPI_CS_TPM
    #define SPI_CS_TPM 0
    #endif

    spi_open(SPI_SEL_TPM);

    /* Single transaction */
    spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, out, in, sizeof(out), 0);

    /* Use individual transactions with cont set */
    for (counter = 0; counter < (sizeof(out) - 1); counter++) {
        spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, &out[counter], &in2[counter], 1,
                1);
    }
    spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, &out[counter], &in2[counter], 1, 0);

    spi_close(SPI_SEL_TPM);

    (void) in;
    (void) in2;
}
#endif

/* Exposed functions */
extern void nxp_ls1028a_spi_init(unsigned int sel);
extern int nxp_ls1028a_spi_xfer(unsigned int sel, unsigned int cs,
        const unsigned char *out, unsigned char *in,
        unsigned int size, int cont);
extern void nxp_ls1028a_spi_deinit(unsigned int sel);

void nxp_ls1028a_spi_init(unsigned int sel)
{
    /* TODO Expose more configuration options */
    spi_open(sel);
}

int nxp_ls1028a_spi_xfer(unsigned int sel, unsigned int cs,
        const unsigned char *out, unsigned char *in,
        unsigned int size, int cont)
{
    /*TODO Make spi_transaction actually return errors */
    spi_transaction(sel, cs, out, in, size, cont);
    return 0;
}

void nxp_ls1028a_spi_deinit(unsigned int sel)
{
    spi_close(sel);
}


void hal_delay_us(uint32_t us) {
    uint64_t delay = (uint64_t)SYS_CLK * us / 1000000;
    volatile uint32_t i = 0;
    for (i = 0; i < delay; i++) {
        asm volatile("nop");
    }
}

/* Fixed addresses */
static const void* kernel_addr  = (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
static const void* update_addr  = (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;

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
  return (void*)NULL;
}

void erratum_err050568()
{
	/* Use IP bus only if systembus PLL is 300MHz (Dont use 300MHz) */
}

/* Application on Serial NOR Flash device 18.6.3 */
void xspi_init()
{
    /* Configure module control register */
    XSPI_MCR0 = XSPI_MCR0_CFG;
    XSPI_MCR1 = XSPI_MCR1_CFG;
    XSPI_MCR2 = XSPI_MCR2_CFG;

    /* Clear RX/TX fifos */
    XSPI_IPRXFCR = XSPI_IPRXFCR_CFG; /* Note: RX/TX Water Mark can be set here default is 64bit */
    XSPI_IPTXFCR = XSPI_IPTXFCR_CFG; /* Increase size to reduce transfer requests */

    /* Configure AHB bus control register (AHBCR) and AHB RX Buffer control register (AHBRXBUFxCR0) */
    XSPI_AHBCR = XSPI_AHBCR_CFG;

    XSPI_AHBRXBUFnCR0(0) = XSPI_AHBRXBUF0CR_CFG;
    XSPI_AHBRXBUFnCR0(1) = XSPI_AHBRXBUF1CR_CFG;
    XSPI_AHBRXBUFnCR0(2) = XSPI_AHBRXBUF2CR_CFG;
    XSPI_AHBRXBUFnCR0(3) = XSPI_AHBRXBUF3CR_CFG;
    XSPI_AHBRXBUFnCR0(4) = XSPI_AHBRXBUF4CR_CFG;
    XSPI_AHBRXBUFnCR0(5) = XSPI_AHBRXBUF5CR_CFG;
    XSPI_AHBRXBUFnCR0(6) = XSPI_AHBRXBUF6CR_CFG;
    XSPI_AHBRXBUFnCR0(7) = XSPI_AHBRXBUF7CR_CFG;

    /* Configure Flash control registers (FLSHxCR0,FLSHxCR1,FLSHxCR2) */
    XSPI_FLSHA1CR0 = XSPI_FLSHA1CR0_CFG;
    XSPI_FLSHA2CR0 = XSPI_FLSHA2CR0_CFG;
    XSPI_FLSHB1CR0 = XSPI_FLSHB1CR0_CFG;
    XSPI_FLSHB2CR0 = XSPI_FLSHB2CR0_CFG;

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
    xspi_lut_unlock();

    xspi_init();

    /* Fast Read */
    XSPI_LUT(LUT_INDEX_READ) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_READ3B);
    XSPI_LUT(1) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, READ_SDR, LUT_PAD_SINGLE, 0x04);
    XSPI_LUT(2) = 0x0;
    XSPI_LUT(3) = 0x0;

    /* Write Enable */
    XSPI_LUT(LUT_INDEX_WRITE_EN) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_WE);
    XSPI_LUT(5) = 0x0;
    XSPI_LUT(6) = 0x0;
    XSPI_LUT(7) = 0x0;

    /* Erase */
    XSPI_LUT(LUT_INDEX_SE) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_SE);
    XSPI_LUT(9) = 0x0;
    XSPI_LUT(10) = 0x0;
    XSPI_LUT(11) = 0x0;

    /* Subsector 4k Erase */
    XSPI_LUT(LUT_INDEX_SSE4K) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_SE_4K);
    XSPI_LUT(13) = 0x0;
    XSPI_LUT(14) = 0x0;
    XSPI_LUT(15) = 0x0;

    /* Page Program */
    XSPI_LUT(LUT_INDEX_PP) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD(1), LUT_OP_PP);
    XSPI_LUT(17) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, WRITE_SDR, LUT_PAD_SINGLE, 0x1);
    XSPI_LUT(18) = 0x0;
    XSPI_LUT(19) = 0x0;

    /* Read Flag Status Regsiter */
    XSPI_LUT(LUT_INDEX_RDSR) = XSPI_LUT_SEQ(READ_SDR, LUT_PAD_SINGLE, LUT_OP_1BYTE, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_RDSR);
    XSPI_LUT(21) = 0x0;
    XSPI_LUT(22) = 0x0;
    XSPI_LUT(23) = 0x0;

    xspi_lut_lock();
}

/* Called from boot_aarch64_start.S */
void hal_ddr_init() {
#ifdef ENABLE_DDR 
    uint64_t counter = 0;
    DDR_DDRCDR_1 = DDR_DDRCDR_1_VAL;
    DDR_SDRAM_CLK_CNTL = DDR_SDRAM_CLK_CNTL_VAL;

    /* Setup DDR CS (chip select) bounds */
    DDR_CS_BNDS(0)   = DDR_CS0_BNDS_VAL;
    DDR_CS_BNDS(1)   = DDR_CS1_BNDS_VAL;
    DDR_CS_BNDS(2)   = DDR_CS2_BNDS_VAL;
    DDR_CS_BNDS(3)   = DDR_CS3_BNDS_VAL;

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

    DDR_ZQ_CNTL = DDR_ZQ_CNTL_VAL;
    DDR_DQ_MAP_0 = DDR_DQ_MAP_0_VAL;
    DDR_DQ_MAP_1 = DDR_DQ_MAP_1_VAL;
    DDR_DQ_MAP_2 = DDR_DQ_MAP_2_VAL;
    DDR_DQ_MAP_3 = DDR_DQ_MAP_3_VAL;

    /* DDR SDRAM mode configuration */
    DDR_SDRAM_CFG_3  = DDR_SDRAM_CFG_3_VAL;

    DDR_SDRAM_MODE   = DDR_SDRAM_MODE_VAL;
    DDR_SDRAM_MODE_2 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_3 = DDR_SDRAM_MODE_3_VAL;
    DDR_SDRAM_MODE_4 = DDR_SDRAM_MODE_4_VAL;
    DDR_SDRAM_MODE_5 = DDR_SDRAM_MODE_5_VAL;
    DDR_SDRAM_MODE_6 = DDR_SDRAM_MODE_6_VAL;
    DDR_SDRAM_MODE_7 = DDR_SDRAM_MODE_7_VAL;
    DDR_SDRAM_MODE_8 = DDR_SDRAM_MODE_8_VAL;
    DDR_SDRAM_MODE_9 =  DDR_SDRAM_MODE_9_VAL;
    DDR_SDRAM_MODE_10 = DDR_SDRAM_MODE_10_VAL;
    DDR_SDRAM_MODE_11 = DDR_SDRAM_MODE_11_VAL;
    DDR_SDRAM_MODE_12 = DDR_SDRAM_MODE_12_VAL;
    DDR_SDRAM_MODE_13 = DDR_SDRAM_MODE_13_VAL;
    DDR_SDRAM_MODE_14 = DDR_SDRAM_MODE_14_VAL;
    DDR_SDRAM_MODE_15 = DDR_SDRAM_MODE_15_VAL;
    DDR_SDRAM_MODE_16 = DDR_SDRAM_MODE_16_VAL;
    DDR_SDRAM_MD_CNTL = DDR_SDRAM_MD_CNTL_VAL;

    /* DDR Configuration */
    DDR_SDRAM_INTERVAL = DDR_SDRAM_INTERVAL_VAL;
    DDR_DATA_INIT = DDR_DATA_INIT_VAL;

    DDR_WRLVL_CNTL = DDR_WRLVL_CNTL_VAL;
    DDR_WRLVL_CNTL_2 = DDR_WRLVL_CNTL_2_VAL;
    DDR_WRLVL_CNTL_3 = DDR_WRLVL_CNTL_3_VAL;

    DDR_SR_CNTR = 0;
    DDR_SDRAM_RCW_1 = DDR_SDRAM_RCW_1_VAL;
    DDR_SDRAM_RCW_2 = DDR_SDRAM_RCW_2_VAL;
    DDR_SDRAM_RCW_3 = DDR_SDRAM_RCW_3_VAL; 
    DDR_SDRAM_RCW_4 = DDR_SDRAM_RCW_4_VAL;
    DDR_SDRAM_RCW_5 = DDR_SDRAM_RCW_5_VAL;
    DDR_SDRAM_RCW_6 = DDR_SDRAM_RCW_6_VAL;
    DDR_DDRCDR_2 = DDR_DDRCDR_2_VAL;
    DDR_SDRAM_CFG_2 = DDR_SDRAM_CFG_2_VAL;
    DDR_INIT_ADDR = 0;
    DDR_INIT_EXT_ADDR = 0;
    DDR_ERR_DISABLE = 0;
    DDR_ERR_INT_EN = DDR_ERR_INT_EN_VAL;

    DDR_DDRDSR_1 = DDR_DDRDSR_1_VAL;
    DDR_DDRDSR_2 = DDR_DDRDSR_2_VAL;   
    DDR_ERR_SBE = DDR_ERR_SBE_VAL;

    DDR_CS_CONFIG(0) = DDR_CS0_CONFIG_VAL;
    DDR_CS_CONFIG(1) = DDR_CS1_CONFIG_VAL;
    DDR_CS_CONFIG(2) = DDR_CS2_CONFIG_VAL;
    DDR_CS_CONFIG(3) = DDR_CS3_CONFIG_VAL;

    /* Set values, but do not enable the DDR yet */
    DDR_SDRAM_CFG = (DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN);

    hal_delay_us(500);
    asm volatile("isb");

    /* Enable controller */
    DDR_SDRAM_CFG &= ~(DDR_SDRAM_CFG_BI);
    DDR_SDRAM_CFG |= DDR_SDRAM_CFG_MEM_EN;
    asm volatile("isb");

    /* Wait for data initialization is complete */
    while ((DDR_SDRAM_CFG_2 & DDR_SDRAM_CFG2_D_INIT)) {
        counter++;
    }

    (void)counter;

 #endif
}

void hal_flash_unlock(void) {}
void hal_flash_lock(void) {}
void ext_flash_lock(void) {}
void ext_flash_unlock(void) {}


void xspi_writereg(uint32_t* addr, uint32_t val) {
    *(volatile uint32_t *)(addr) = val;
}

void xspi_write_en(uint32_t addr) {
    XSPI_IPTXFCR = XSPI_IPRCFCR_FLUSH;
    XSPI_IPCR0 = addr;
    XSPI_IPCR1 =  XSPI_ISEQID(LUT_INDEX_WRITE_EN) | 1;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR |= XSPI_IPCMDDONE;
}

void xspi_read_sr(uint8_t* rxbuf, uint32_t addr, uint32_t len) {
    uint32_t data = 0;

    /* Read IP CR regsiter */
    uint32_t rxfcr = XSPI_IPRXFCR;

    /* Flush RX fifo */
    rxfcr = rxfcr | XSPI_IPRCFCR_FLUSH;
    XSPI_IPTXFCR = rxfcr;

    /* Trigger read SR command */
    XSPI_IPCR0 = addr;
    XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_RDSR) | len;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR |= XSPI_IPCMDDONE;

    data = XSPI_RFD(0);
    memcpy(rxbuf, &data, len);

    XSPI_IPRXFCR = XSPI_IPRCFCR_FLUSH;
    XSPI_INTR = XSPI_IPRXWA;
    XSPI_INTR = XSPI_IPCMDDONE;
}

void xspi_sw_reset() {
    XSPI_SWRESET();
    while (XSPI_MCR0 & XSPI_SW_RESET);
}

void xspi_flash_write(uintptr_t address, const uint8_t *data, uint32_t len) {
    uint32_t size = 0;
    uint32_t tx_data = 0;
    uint32_t size_wm = 0;
    uint32_t loop_cnt = 0;
    uint32_t remaining, rem_size = 0;
    uint32_t i = 0, j = 0;

    while(len) {
        size = len > XSPI_IP_BUF_SIZE ? XSPI_IP_BUF_SIZE : len;

        XSPI_IPCR0 = address;
        loop_cnt = size / XSPI_IP_WM_SIZE; 

        /* Fill TX fifos */
        for(i = 0; i < loop_cnt; i++) {
            /* Wait for TX fifo ready */
            while(!(XSPI_INTR & XSPI_INTR_IPTXWE_MASK));

            for(j = 0; j < XSPI_IP_WM_SIZE; j+=4) {
                memcpy(&tx_data, data++, 4);
                xspi_writereg((uint32_t*)XSPI_TFD_BASE + j, tx_data);
            }

            /* Reset fifo */
            XSPI_INTR = XSPI_INTR_IPTXWE_MASK;
        }

        remaining = size % XSPI_IP_WM_SIZE;

        /* Write remaining data for non aligned data */
        if (remaining) {
            /* Wait for fifo Empty */
            while(!(XSPI_INTR & XSPI_INTR_IPTXWE_MASK));

            for(j = 0; j < remaining; j+=4) {
                tx_data = 0;
                rem_size = (remaining < 4) ? remaining : 4;
                memcpy(&tx_data, data++, rem_size);
                xspi_writereg((uint32_t*)XSPI_TFD_BASE + j, tx_data);
            }

            /* Reset fifo */
            XSPI_INTR = XSPI_INTR_IPTXWE_MASK;
        }

        XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_PP) | size;
        XSPI_IPCMD_START();

        /* Wait command done */
        while (!(XSPI_INTR & XSPI_IPCMDDONE))

        /* Flush fifo, set done flag */
        XSPI_IPTXFCR = XSPI_IPRCFCR_FLUSH;
        XSPI_INTR = XSPI_IPCMDDONE;

        len -= size;
        address += size;
    }
}

void xspi_flash_sec_erase(uintptr_t address) {
    XSPI_IPCR0 = address;
    XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_SE) | FLASH_ERASE_SIZE;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR &= ~XSPI_IPCMDDONE;
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len) {
    xspi_write_en(address);
    xspi_flash_write(address, data, len);
    //xspi_sw_reset();

    return len;
}

int hal_flash_erase(uint32_t address, int len) {
    uint32_t num_sectors = 0;
    uint32_t i = 0;
    uint8_t status[4] = {0, 0, 0, 0};

    num_sectors = len / FLASH_ERASE_SIZE;
    num_sectors += (len % FLASH_ERASE_SIZE) ? 1 : 0;

    for (i = 0; i < num_sectors; i++) {
        xspi_write_en(address + i * FLASH_ERASE_SIZE);
        xspi_flash_sec_erase(address + i * FLASH_ERASE_SIZE);

        while (!(status[0] & FLASH_READY_MSK))  {
            xspi_read_sr(status, 0, 1);
        }
    }

    xspi_sw_reset();

    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len) {
    xspi_write_en(address);
    xspi_flash_write(address, data, len);
    //xspi_sw_reset();

    return len;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len) {
    address = address & MASK_32BIT;
    memcpy(data, (void*)address, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len) {
    uint32_t num_sectors = 0;
    uint32_t i = 0;
    uint8_t status[4] = {0, 0, 0, 0};

    num_sectors = len / FLASH_ERASE_SIZE;
    num_sectors += (len % FLASH_ERASE_SIZE) ? 1 : 0;

    for (i = 0; i < num_sectors; i++) {
        xspi_write_en(address + i * FLASH_ERASE_SIZE);
        xspi_flash_sec_erase(address + i * FLASH_ERASE_SIZE);

        while (!(status[0] & FLASH_READY_MSK))  {
            xspi_read_sr(status, 0, 1);
        }
    }

    xspi_sw_reset();

    return len;
}

void hal_prepare_boot(void) {
    //switch_el3_to_el2();
}

static int test_hw_ddr(void) {
    int status = 0; 
    uint64_t counter = 0;
    
    DDR_MTPn(0) = 0xffffffff;
    DDR_MTPn(1) = 0x00000001;
    DDR_MTPn(2) = 0x00000002;
    DDR_MTPn(3) = 0x00000003;
    DDR_MTPn(4) = 0x00000004;
    DDR_MTPn(5) = 0x00000005;
    DDR_MTPn(6) = 0xcccccccc;
    DDR_MTPn(7) = 0xbbbbbbbb;
    DDR_MTPn(8) = 0xaaaaaaaa;
    DDR_MTPn(9) = 0xffffffff;

    DDR_MTCR = DDR_MEM_TEST_EN;

    while (DDR_MTCR & DDR_MEM_TEST_EN)
        counter++;

    if (DDR_ERR_SBE & 0xffff || DDR_ERR_DETECT) {
        status = -1;
        wolfBoot_printf("DDR ECC error\n");
    }      
    if (DDR_MTCR & DDR_MEM_TEST_FAIL) {
        status = -1;
        wolfBoot_printf("DDR self-test failed\n");
    } else {
        status = 0;
        wolfBoot_printf("DDR self-test passed\n");
    }

    return status;
}

static int test_ddr(void)
{
    int ret = 0;
    int i;
    uint32_t *ptr = (uint32_t*)(DDR_ADDRESS + TEST_DDR_OFFSET);
    uint32_t tmp[TEST_DDR_SIZE/4];

    memset(tmp, 0, sizeof(tmp));

    /* test write to DDR */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        ptr[i] = (uint32_t)i;
    }

    /* test read from DDR */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        tmp[i] = ptr[i];
    }

    /* compare results */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        if (tmp[i] != (uint32_t)i) {
            ret = -1;
            break;
        }
    }

    return ret;
}

#define TEST_ADDRESS 0x20012000 

static int test_flash(void) {
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];

    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -i;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}

/* Function to set MMU MAIR memory attributes base on index */
void set_memory_attribute(uint32_t attr_idx, uint64_t mair_value) {
    uint64_t mair = 0;

    asm volatile("mrs %0, mair_el3" : "=r"(mair));
    mair &= ~(0xffUL << (attr_idx * 8));
    mair |= (mair_value << (attr_idx * 8));
    asm volatile("msr mair_el3, %0" : : "r"(mair));
}

void hal_init_tzpc() {
    TZDECPROT0_SET = 0xff; //0x86;
    TZDECPROT1_SET = 0xff; //0x00;
    TZPCR0SIZE = 0x00; //0x200;
}

void hal_init(void) {
#ifdef DEBUG_UART
    uint32_t fw;

    uart_init();
    wolfBoot_printf("wolfBoot Init\n");
#endif

    //hal_init_tzpc();

    hal_flash_init();
    wolfBoot_printf("Flash init done\n");
    //test_flash();

#ifdef TPM_TEST
    read_tpm_id();
    wolfBoot_printf("TPM test done\n");

#endif

    hal_ddr_init();
    wolfBoot_printf("DDR init done\n");
    test_hw_ddr();
#if 0
    if(test_ddr() == -1) {
        wolfBoot_printf("DDR R/W test failed\n");
    } else {
        wolfBoot_printf("DDR R/W test passed\n");
    }
#endif
    mmu_enable();
    wolfBoot_printf("MMU init done\n");
}
