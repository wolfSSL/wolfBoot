/* zynq.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#if defined(__QNXNTO__) && !defined(NO_QNX)
    #define USE_QNX
#elif defined(USE_BUILTIN_STARTUP)
    /* we are using Xilinx SDK to build, so use Xilinx QSPI driver */
    #ifndef USE_XQSPIPSU
    #define USE_XQSPIPSU
    #endif
#endif

#include <target.h>
#include "image.h"
#include "printf.h"
#ifndef ARCH_AARCH64
#   error "wolfBoot zynq HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

#ifdef USE_XQSPIPSU
    /* Xilinx BSP Driver */
    #include "xqspipsu.h"
    #define QSPI_DEVICE_ID		XPAR_XQSPIPSU_0_DEVICE_ID
    #define QSPI_CLK_PRESACALE  XQSPIPSU_CLK_PRESCALE_8
#elif defined(USE_QNX)
    /* QNX QSPI driver */
    #include <sys/siginfo.h>
    #include "xzynq_gqspi.h"
#endif

/* QSPI bare-metal driver */
#define CORTEXA53_0_CPU_CLK_FREQ_HZ    1099989014
#define CORTEXA53_0_TIMESTAMP_CLK_FREQ 99998999

/* Generic Quad-SPI */
#define QSPI_BASE          0xFF0F0000UL
#define LQSPI_EN           (*((volatile uint32_t*)(QSPI_BASE + 0x14)))  /* SPI enable: 0: disable the SPI, 1: enable the SPI */
#define GQSPI_CFG          (*((volatile uint32_t*)(QSPI_BASE + 0x100))) /* configuration register. */
#define GQSPI_ISR          (*((volatile uint32_t*)(QSPI_BASE + 0x104))) /* interrupt status register. */
#define GQSPI_IER          (*((volatile uint32_t*)(QSPI_BASE + 0x108))) /* interrupt enable register. */
#define GQSPI_IDR          (*((volatile uint32_t*)(QSPI_BASE + 0x10C))) /* interrupt disable register. */
#define GQSPI_IMR          (*((volatile uint32_t*)(QSPI_BASE + 0x110))) /* interrupt unmask register. */
#define GQSPI_EN           (*((volatile uint32_t*)(QSPI_BASE + 0x114))) /* enable register. */
#define GQSPI_TXD          (*((volatile uint32_t*)(QSPI_BASE + 0x11C))) /* TX data register. Keyhole addresses for the transmit data FIFO. */
#define GQSPI_RXD          (*((volatile uint32_t*)(QSPI_BASE + 0x120))) /* RX data register. */
#define GQSPI_TX_THRESH    (*((volatile uint32_t*)(QSPI_BASE + 0x128))) /* TXFIFO Threshold Level register: (bits 5:0) Defines the level at which the TX_FIFO_NOT_FULL interrupt is generated */
#define GQSPI_RX_THRESH    (*((volatile uint32_t*)(QSPI_BASE + 0x12C))) /* RXFIFO threshold level register: (bits 5:0) Defines the level at which the RX_FIFO_NOT_EMPTY interrupt is generated */
#define GQSPI_GPIO         (*((volatile uint32_t*)(QSPI_BASE + 0x130)))
#define GQSPI_LPBK_DLY_ADJ (*((volatile uint32_t*)(QSPI_BASE + 0x138))) /* adjusting the internal loopback clock delay for read data capturing */
#define GQSPI_GEN_FIFO     (*((volatile uint32_t*)(QSPI_BASE + 0x140))) /* generic FIFO data register. Keyhole addresses for the generic FIFO. */
#define GQSPI_SEL          (*((volatile uint32_t*)(QSPI_BASE + 0x144))) /* select register. */
#define GQSPI_FIFO_CTRL    (*((volatile uint32_t*)(QSPI_BASE + 0x14C))) /* FIFO control register. */
#define GQSPI_GF_THRESH    (*((volatile uint32_t*)(QSPI_BASE + 0x150))) /* generic FIFO threshold level register: (bits 4:0) Defines the level at which the GEN_FIFO_NOT_FULL interrupt is generated */
#define GQSPI_POLL_CFG     (*((volatile uint32_t*)(QSPI_BASE + 0x154))) /* poll configuration register */
#define GQSPI_P_TIMEOUT    (*((volatile uint32_t*)(QSPI_BASE + 0x158))) /* poll timeout register. */
#define GQSPI_XFER_STS     (*((volatile uint32_t*)(QSPI_BASE + 0x15C))) /* transfer status register. */
#define QSPI_DATA_DLY_ADJ  (*((volatile uint32_t*)(QSPI_BASE + 0x1F8))) /* adjusting the internal receive data delay for read data capturing */
#define GQSPI_MOD_ID       (*((volatile uint32_t*)(QSPI_BASE + 0x1FC)))
#define QSPIDMA_DST_STS    (*((volatile uint32_t*)(QSPI_BASE + 0x808)))
#define QSPIDMA_DST_CTRL   (*((volatile uint32_t*)(QSPI_BASE + 0x80C)))
#define QSPIDMA_DST_I_STS  (*((volatile uint32_t*)(QSPI_BASE + 0x814)))
#define QSPIDMA_DST_CTRL2  (*((volatile uint32_t*)(QSPI_BASE + 0x824)))

/* GQSPI Registers */
/* GQSPI_CFG: Configuration registers */
#define GQSPI_CFG_CLK_POL             (1UL << 1) /* Clock polarity outside QSPI word: 0: QSPI clock is quiescent low, 1: QSPI clock is quiescent high */
#define GQSPI_CFG_CLK_PH              (1UL << 2) /* Clock phase: 1: the QSPI clock is inactive outside the word, 0: the QSPI clock is active outside the word */
/* 000: divide by 2,   001: divide by 4,  010: divide by 8,
   011: divide by 16,  100: divide by 32, 101: divide by 64,
   110: divide by 128, 111: divide by 256 */
#define GQSPI_CFG_BAUD_RATE_DIV_MASK  (7UL << 3)
#define GQSPI_CFG_BAUD_RATE_DIV(d)    ((d << 3) & GQSPI_CFG_BAUD_RATE_DIV_MASK)
#define GQSPI_CFG_WP_HOLD             (1UL << 19) /* If set, Holdb and WPn pins are actively driven by the qspi controller in 1-bit and 2-bit modes. */
#define GQSPI_CFG_EN_POLL_TIMEOUT     (1UL << 20) /* Poll Timeout Enable: 0: disable, 1: enable */
#define GQSPI_CFG_ENDIAN              (1UL << 26) /* Endian format transmit data register: 0: little endian, 1: big endian */
#define GQSPI_CFG_START_GEN_FIFO      (1UL << 28) /* Trigger Generic FIFO Command Execution: 0:disable executing requests, 1: enable executing requests */
#define GQSPI_CFG_GEN_FIFO_START_MODE (1UL << 29) /* Start mode of Generic FIFO: 0: Auto Start Mode, 1: Manual Start Mode */
#define GQSPI_CFG_MODE_EN_MASK        (3UL << 30) /* Flash memory interface mode control: 00: IO mode, 10: DMA mode */
#define GQSPI_CFG_MODE_EN(m)          ((m << 30) & GQSPI_CFG_MODE_EN_MASK)
#define GQSPI_CFG_MODE_EN_IO          GQSPI_CFG_MODE_EN(0)
#define GQSPI_CFG_MODE_EN_DMA         GQSPI_CFG_MODE_EN(2)

/* GQSPI_ISR / GQSPI_IER / GQSPI_IDR / GQSPI_IMR: Interrupt registers */
#define GQSPI_IXR_RX_FIFO_EMPTY     (1UL << 11)
#define GQSPI_IXR_GEN_FIFO_FULL     (1UL << 10)
#define GQSPI_IXR_GEN_FIFO_NOT_FULL (1UL <<  9)
#define GQSPI_IXR_TX_FIFO_EMPTY     (1UL <<  8)
#define GQSPI_IXR_GEN_FIFO_EMPTY    (1UL <<  7)
#define GQSPI_IXR_RX_FIFO_FULL      (1UL <<  5)
#define GQSPI_IXR_RX_FIFO_NOT_EMPTY (1UL <<  4)
#define GQSPI_IXR_TX_FIFO_FULL      (1UL <<  3)
#define GQSPI_IXR_TX_FIFO_NOT_FULL  (1UL <<  2)
#define GQSPI_IXR_POLL_TIME_EXPIRE  (1UL <<  1)

#define GQSPI_IXR_ALL_MASK (GQSPI_IXR_POLL_TIME_EXPIRE | GQSPI_IXR_TX_FIFO_NOT_FULL | \
    GQSPI_IXR_TX_FIFO_FULL | GQSPI_IXR_RX_FIFO_NOT_EMPTY | GQSPI_IXR_RX_FIFO_FULL | \
    GQSPI_IXR_GEN_FIFO_EMPTY | GQSPI_IXR_TX_FIFO_EMPTY | GQSPI_IXR_GEN_FIFO_NOT_FULL | \
    GQSPI_IXR_GEN_FIFO_FULL | GQSPI_IXR_RX_FIFO_EMPTY)
#define GQSPI_ISR_WR_TO_CLR_MASK 0x00000002U

/* GQSPI_GEN_FIFO: FIFO data register */
/* bits 0-7: Length in bytes (except when GQSPI_GEN_FIFO_EXP_MASK is set length as 255 chunks) */
#define GQSPI_GEN_FIFO_IMM_MASK    (0xFFUL) /* Immediate Data Field */
#define GQSPI_GEN_FIFO_IMM(imm)    (imm & GQSPI_GEN_FIFO_IMM_MASK)
#define GQSPI_GEN_FIFO_DATA_XFER   (1UL << 8) /* Indicates IMM is size, otherwise byte is sent directly in IMM reg */
#define GQSPI_GEN_FIFO_EXP_MASK    (1UL << 9) /* Length is Exponent (length / 255) */
#define GQSPI_GEN_FIFO_MODE_MASK   (3UL << 10)
#define GQSPI_GEN_FIFO_MODE(m)     ((m << 10) & GQSPI_GEN_FIFO_MODE_MASK)
#define GQSPI_GEN_FIFO_MODE_SPI    GQSPI_GEN_FIFO_MODE(1)
#define GQSPI_GEN_FIFO_MODE_DSPI   GQSPI_GEN_FIFO_MODE(2)
#define GQSPI_GEN_FIFO_MODE_QSPI   GQSPI_GEN_FIFO_MODE(3)
#define GQSPI_GEN_FIFO_CS_MASK     (3UL << 12)
#define GQSPI_GEN_FIFO_CS(c)       ((c << 12) & GQSPI_GEN_FIFO_CS_MASK)
#define GQSPI_GEN_FIFO_CS_LOWER    GQSPI_GEN_FIFO_CS(1)
#define GQSPI_GEN_FIFO_CS_UPPER    GQSPI_GEN_FIFO_CS(2)
#define GQSPI_GEN_FIFO_CS_BOTH     GQSPI_GEN_FIFO_CS(3)
#define GQSPI_GEN_FIFO_BUS_MASK    (3UL << 14)
#define GQSPI_GEN_FIFO_BUS(b)      ((b << 14) & GQSPI_GEN_FIFO_BUS_MASK)
#define GQSPI_GEN_FIFO_BUS_LOW     GQSPI_GEN_FIFO_BUS(1)
#define GQSPI_GEN_FIFO_BUS_UP      GQSPI_GEN_FIFO_BUS(2)
#define GQSPI_GEN_FIFO_BUS_BOTH    GQSPI_GEN_FIFO_BUS(3)
#define GQSPI_GEN_FIFO_TX          (1UL << 16)
#define GQSPI_GEN_FIFO_RX          (1UL << 17)
#define GQSPI_GEN_FIFO_STRIPE      (1UL << 18) /* Stripe data across the lower and upper data buses. */
#define GQSPI_GEN_FIFO_POLL        (1UL << 19)

/* GQSPI_FIFO_CTRL */
#define GQSPI_FIFO_CTRL_RST_GEN_FIFO (1UL << 0)
#define GQSPI_FIFO_CTRL_RST_TX_FIFO  (1UL << 1)
#define GQSPI_FIFO_CTRL_RST_RX_FIFO  (1UL << 2)

/* QSPIDMA_DST_CTRL */
#define QSPIDMA_DST_CTRL_DEF  0x403FFA00UL
#define QSPIDMA_DST_CTRL2_DEF 0x081BFFF8UL

/* QSPIDMA_DST_STS */
#define QSPIDMA_DST_STS_WTC   0xE000U

/* QSPIDMA_DST_I_STS */
#define QSPIDMA_DST_I_STS_ALL_MASK 0xFEU

/* IOP System-level Control */
#define IOU_SLCR_BASSE             0xFF180000
#define IOU_TAPDLY_BYPASS          (*((volatile uint32_t*)(IOU_SLCR_BASSE + 0x390)))
#define IOU_TAPDLY_BYPASS_LQSPI_RX (1UL << 2) /* LQSPI Tap Delay Enable on Rx Clock signal. 0: enable. 1: disable (bypass tap delay). */


/* Configuration used for bare-metal only */
#define GQSPI_CLK_FREQ_HZ      124987511
#define GQSPI_CLK_DIV          2 /* (CLK / (2 << val) = BUS) - DIV 2 = 37.5 MHz */
#define GQSPI_CS_ASSERT_CLOCKS 5 /* CS Setup Time (tCSS) - num of clock cycles foes in IMM */
#define GQSPI_FIFO_WORD_SZ     4
#define GQSPI_TIMEOUT_TRIES    100000
#define QSPI_FLASH_READY_TRIES 1000


/* QSPI Configuration */
#ifndef GQSPI_QSPI_MODE
#define GQSPI_QSPI_MODE        GQSPI_GEN_FIFO_MODE_QSPI
#endif
#ifndef GQPI_USE_DUAL_PARALLEL
#define GQPI_USE_DUAL_PARALLEL 1 /* stripe */
#endif
#ifndef GQPI_USE_4BYTE_ADDR
#define GQPI_USE_4BYTE_ADDR    1
#endif
#ifndef GQSPI_DUMMY_READ
#define GQSPI_DUMMY_READ       8 /* Number of dummy clock cycles for reads */
#endif



/* Flash Parameters:
 * Micron Serial NOR Flash Memory 64KB Sector Erase MT25QU01GBBB
 * Stacked device (two 512Mb die)
 * Dual Parallel so total addressable size is double
 */
#ifndef FLASH_DEVICE_SIZE
#define FLASH_DEVICE_SIZE      0x10000000
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE        512
#endif
#ifndef FLASH_NUM_PAGES
#define FLASH_NUM_PAGES        0x80000
#endif
#define FLASH_NUM_SECTORS      (FLASH_DEVICE_SIZE/WOLFBOOT_SECTOR_SIZE)


/* Flash Commands */
#define WRITE_ENABLE_CMD       0x06U
#define WRITE_DISABLE_CMD      0x04U
#define READ_ID_CMD            0x9FU
#define MULTI_IO_READ_ID_CMD   0xAFU
#define READ_FSR_CMD           0x70U
#define ENTER_QSPI_MODE_CMD    0x35U
#define EXIT_QSPI_MODE_CMD     0xF5U
#define ENTER_4B_ADDR_MODE_CMD 0xB7U
#define EXIT_4B_ADDR_MODE_CMD  0xE9U

#define FAST_READ_CMD          0x0BU
#define DUAL_READ_CMD          0x3BU
#define QUAD_READ_CMD          0x6BU
#define FAST_READ_4B_CMD       0x0CU
#define DUAL_READ_4B_CMD       0x3CU
#define QUAD_READ_4B_CMD       0x6CU

#define PAGE_PROG_CMD          0x02U
#define DUAL_PROG_CMD          0xA2U
#define QUAD_PROG_CMD          0x22U
#define PAGE_PROG_4B_CMD       0x12U
#define DUAL_PROG_4B_CMD       0x12U
#define QUAD_PROG_4B_CMD       0x34U

#define SEC_ERASE_CMD          0xD8U
#define SEC_4K_ERASE_CMD       0x20U
#define RESET_ENABLE_CMD       0x66U
#define RESET_MEMORY_CMD       0x99U

#define FLASH_READY_MASK       0x80 /* 0=Busy, 1=Ready */


/* Return Codes */
#define GQSPI_CODE_SUCCESS     0
#define GQSPI_CODE_FAILED      -100
#define GQSPI_CODE_TIMEOUT     -101


/* QSPI Slave Device Information */
typedef struct QspiDev {
    uint32_t mode;   /* GQSPI_GEN_FIFO_MODE_SPI, GQSPI_GEN_FIFO_MODE_DSPI or GQSPI_GEN_FIFO_MODE_QSPI */
    uint32_t bus;    /* GQSPI_GEN_FIFO_BUS_LOW, GQSPI_GEN_FIFO_BUS_UP or GQSPI_GEN_FIFO_BUS_BOTH */
    uint32_t cs;     /* GQSPI_GEN_FIFO_CS_LOWER, GQSPI_GEN_FIFO_CS_UPPER */
    uint32_t stripe; /* OFF=0 or ON=GQSPI_GEN_FIFO_STRIPE */
#ifdef USE_XQSPIPSU
    XQspiPsu qspiPsuInst;
#elif defined(USE_QNX)
    xzynq_qspi_t* qnx;
#endif
} QspiDev_t;

static QspiDev_t mDev;

#ifdef TEST_FLASH
static int test_flash(QspiDev_t* dev);
#endif

/* eFUSE support */
#define ZYNQMP_EFUSE_BASE       0xFFCC0000
#define ZYNQMP_EFUSE_STATUS     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x0008)))
#define ZYNQMP_EFUSE_SEC_CTRL   (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x1058)))
#define ZYNQMP_EFUSE_PPK0_0     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10A0)))
#define ZYNQMP_EFUSE_PPK0_1     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10A4)))
#define ZYNQMP_EFUSE_PPK0_2     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10A8)))
#define ZYNQMP_EFUSE_PPK0_3     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10AC)))
#define ZYNQMP_EFUSE_PPK0_4     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10B0)))
#define ZYNQMP_EFUSE_PPK0_5     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10B4)))
#define ZYNQMP_EFUSE_PPK0_6     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10B8)))
#define ZYNQMP_EFUSE_PPK0_7     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10BC)))
#define ZYNQMP_EFUSE_PPK0_8     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10C0)))
#define ZYNQMP_EFUSE_PPK0_9     (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10C4)))
#define ZYNQMP_EFUSE_PPK0_10    (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10C8)))
#define ZYNQMP_EFUSE_PPK0_11    (*((volatile uint32_t*)(ZYNQMP_EFUSE_BASE + 0x10CC)))

/* eFUSE STATUS Registers */
#define ZYNQMP_EFUSE_STATUS_CACHE_DONE    (1UL << 5)
#define ZYNQMP_EFUSE_STATUS_CACHE_LOAD    (1UL << 4)

/* eFUSE SEC_CTRL Registers */
#define ZYNQMP_EFUSE_SEC_CTRL_PPK1_INVLD  (3UL  << 30) /* Revokes PPK1 */
#define ZYNQMP_EFUSE_SEC_CTRL_PPK1_WRLK   (1UL  << 29) /* Locks writing to PPK1 eFuses */
#define ZYNQMP_EFUSE_SEC_CTRL_PPK0_INVLD  (3UL  << 27) /* Revokes PPK0 */
#define ZYNQMP_EFUSE_SEC_CTRL_PPK0_WRLK   (1UL  << 26) /* Locks writing to PPK0 eFuses */
#define ZYNQMP_EFUSE_SEC_CTRL_RSA_EN      (15UL << 11) /* Enables RSA Authentication during boot. All boots must be authenticated */
#define ZYNQMP_EFUSE_SEC_CTRL_SEC_LOCK    (1UL  << 10) /* Disables the reboot into JTAG mode when doing a secure lockdown. */
#define ZYNQMP_EFUSE_SEC_CTRL_JTAG_DIS    (1UL  << 5)  /* Disables the JTAG controller. The only instructions available are BYPASS and IDCODE. */
#define ZYNQMP_EFUSE_SEC_CTRL_ENC_ONLY    (1UL  << 2)  /* Requires all boots to be encrypted using the eFuse key. */
#define ZYNQMP_EFUSE_SEC_CTRL_AES_WRLK    (1UL  << 1)  /* Locks writing to the AES key section of eFuse */
#define ZYNQMP_EFUSE_SEC_CTRL_AES_RDLK    (1UL  << 0)  /* Locks the AES key CRC check function */


#ifdef DEBUG_UART
/* UART Support for Debugging */
#define ZYNQMP_UART0_BASE  0xFF000000
#define ZYNQMP_UART1_BASE  0xFF010000

#define ZYNQMP_UART_CR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x0)))
#define ZYNQMP_UART_MR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x4)))
#define ZYNQMP_UART_SR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x2C)))
#define ZYNQMP_UART_FIFO        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x30)))
#define ZYNQMP_UART_BR_GEN      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x18))) /* 2 - 65535: baud_sample */
#define ZYNQMP_UART_BR_DIV      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x34))) /* 4 - 255: Baud rate */

/* UART Control Registers */
#define ZYNQMP_UART_CR_TX_EN       0x00000010  /* TX enabled */
#define ZYNQMP_UART_CR_RX_EN       0x00000004  /* RX enabled */
#define ZYNQMP_UART_CR_TXRST       0x00000002  /* TX logic reset */
#define ZYNQMP_UART_CR_RXRST       0x00000001  /* RX logic reset */

/* UART Mode Registers */
#define ZYNQMP_UART_MR_PARITY_NONE 0x00000020  /* No parity */

/* UART Channel Status Register (read only) */
#define ZYNQMP_UART_SR_TXFULL   0x00000010U /* TX FIFO full */
#define ZYNQMP_UART_SR_TXEMPTY  0x00000008U /* TX FIFO empty */
#define ZYNQMP_UART_SR_RXFULL   0x00000004U /* RX FIFO full */
#define ZYNQMP_UART_SR_RXEMPTY  0x00000002U /* RX FIFO empty */

/* UART Configuration */
#define UART_MASTER_CLOCK       100000000
#define DEBUG_UART_BASE         ZYNQMP_UART1_BASE
#define DEBUG_UART_BAUD         115200
#define DEBUG_UART_DIV          4

static void uart_init(void)
{
    /* Enable TX/RX and Reset */
    ZYNQMP_UART_CR = (ZYNQMP_UART_CR_TX_EN | ZYNQMP_UART_CR_RX_EN | 
        ZYNQMP_UART_CR_TXRST | ZYNQMP_UART_CR_RXRST);
    /* 8-bits, no parity */
    ZYNQMP_UART_MR = ZYNQMP_UART_MR_PARITY_NONE;

    /* baud (115200) = master clk / (BR_GEN * (BR_DIV + 1)) */
    ZYNQMP_UART_BR_DIV = DEBUG_UART_DIV;
    ZYNQMP_UART_BR_GEN = UART_MASTER_CLOCK / DEBUG_UART_BAUD / (DEBUG_UART_DIV+1);
}

static void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while (ZYNQMP_UART_SR & ZYNQMP_UART_SR_TXFULL);
        ZYNQMP_UART_SR = (uint32_t)buf[pos++];
    }
    /* Wait till TX Fifo is empty */
    while (!(ZYNQMP_UART_SR & ZYNQMP_UART_SR_TXEMPTY));
}
#endif /* DEBUG_UART */


#ifdef USE_XQSPIPSU
/* Xilinx BSP Driver */

/* Aligned page data buffer for DMA */
#ifdef __ICCARM__
#pragma data_alignment = 32
static uint8_t pageData[FLASH_PAGE_SIZE];
#pragma data_alignment = 4
#else
static uint8_t pageData[FLASH_PAGE_SIZE] __attribute__ ((aligned(32)));;
#endif
static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret;
    XQspiPsu_Msg msgs[4];
    uint32_t msgCnt = 0, busWidth = XQSPIPSU_SELECT_MODE_SPI;
    uint8_t* rxPtr = rxData;

    /* Chip Select */
    if (pDev->cs == GQSPI_GEN_FIFO_CS_BOTH) {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
		    XQSPIPSU_SELECT_FLASH_CS_BOTH, XQSPIPSU_SELECT_FLASH_BUS_BOTH);
    }
    else if (pDev->cs == GQSPI_GEN_FIFO_CS_LOWER) {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
		    XQSPIPSU_SELECT_FLASH_CS_LOWER, XQSPIPSU_SELECT_FLASH_BUS_LOWER);
    }
    else {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
		    XQSPIPSU_SELECT_FLASH_CS_UPPER, XQSPIPSU_SELECT_FLASH_BUS_UPPER);
    }

    /* Transfer Bus Width - only applies to read/write command */
    if (mode == GQSPI_GEN_FIFO_MODE_QSPI)
        busWidth = XQSPIPSU_SELECT_MODE_QUADSPI;
    else if (mode == GQSPI_GEN_FIFO_MODE_DSPI)
        busWidth = XQSPIPSU_SELECT_MODE_DUALSPI;

    /* Command */
    memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
    msgs[msgCnt].TxBfrPtr = (uint8_t*)cmdData;
    msgs[msgCnt].ByteCount = cmdSz;
    msgs[msgCnt].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
	msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_TX;
    msgCnt++;

    /* TX */
    if (txData) {
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].TxBfrPtr = (uint8_t*)txData;
        msgs[msgCnt].ByteCount = txSz;
        msgs[msgCnt].BusWidth = busWidth;
        msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_TX;
        if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE) 
            msgs[msgCnt].Flags |= XQSPIPSU_MSG_FLAG_STRIPE;
        msgCnt++;
    }

    /* Dummy */
    if (dummySz > 0) {
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].ByteCount = dummySz;
        msgs[msgCnt].BusWidth = busWidth;
        msgCnt++;
    }

    /* RX */
    if (rxData) {
        /* If RX pointer is not 32 byte aligned then use temp page data buffer */
        if (((size_t)rxPtr % 32) != 0)
            rxPtr = pageData;
        if (rxSz > (uint32_t)sizeof(pageData))
            rxSz = (uint32_t)sizeof(pageData);
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].RxBfrPtr = rxPtr;
        msgs[msgCnt].ByteCount = rxSz;
        msgs[msgCnt].BusWidth = busWidth;
        msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_RX;
        if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE) 
            msgs[msgCnt].Flags |= XQSPIPSU_MSG_FLAG_STRIPE;
        msgCnt++;
    }

    ret = XQspiPsu_PolledTransfer(&pDev->qspiPsuInst, msgs, msgCnt);
    if (ret < 0) {
        wolfBoot_printf("QSPI Transfer failed! %d\n", ret);
        return GQSPI_CODE_FAILED;
    }

    /* if unaligned read, return results */
    if (rxData && rxPtr == pageData) {
        memcpy(rxData, pageData, rxSz);
    }

    return GQSPI_CODE_SUCCESS;
}

#elif defined(USE_QNX)
/* QNX QSPI driver */
static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret;
    qspi_buf cmd_buf;
    qspi_buf tx_buf;
    qspi_buf rx_buf;
    uint32_t flags;

    flags = TRANSFER_FLAG_DEBUG;
    if (mode == GQSPI_GEN_FIFO_MODE_QSPI)
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_QSPI);
    else if (mode == GQSPI_GEN_FIFO_MODE_DSPI)
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_DSPI);
    else
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_SPI);
    if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE)
        flags |= TRANSFER_FLAG_STRIPE;
    if (pDev->cs & GQSPI_GEN_FIFO_CS_LOWER)
        flags |= TRANSFER_FLAG_LOW_DB | TRANSFER_FLAG_CS(TRANSFER_FLAG_CS_LOW);
    if (pDev->cs & GQSPI_GEN_FIFO_CS_UPPER)
        flags |= TRANSFER_FLAG_UP_DB | TRANSFER_FLAG_CS(TRANSFER_FLAG_CS_UP);

    memset(&cmd_buf, 0, sizeof(cmd_buf));
    cmd_buf.offset = (uint8_t*)cmdData;
    cmd_buf.len = cmdSz;

    memset(&tx_buf, 0, sizeof(tx_buf));
    tx_buf.offset = (uint8_t*)txData;
    tx_buf.len = txSz;

    memset(&rx_buf, 0, sizeof(rx_buf));
    rx_buf.offset = rxData;
    rx_buf.len = rxSz;

    /* Send the TX buffer */
    ret = xzynq_qspi_transfer(pDev->qnx, 
        txData ? &tx_buf : NULL, 
        rxData ? &rx_buf : NULL, 
        &cmd_buf, flags);
    if (ret < 0) {
        wolfBoot_printf("QSPI Transfer failed! %d\n", ret);
        return GQSPI_CODE_FAILED;
    }
    return GQSPI_CODE_SUCCESS;
}
#else
/* QSPI bare-metal driver */
static inline int qspi_isr_wait(uint32_t wait_mask, uint32_t wait_val)
{
    uint32_t timeout = 0;
    while ((GQSPI_ISR & wait_mask) == wait_val &&
           ++timeout < GQSPI_TIMEOUT_TRIES);
    if (timeout == GQSPI_TIMEOUT_TRIES) {
        return -1;
    }
    return 0;
}

static int qspi_gen_fifo_write(uint32_t reg_genfifo)
{
    /* wait until the gen FIFO is not full to write */
    if (qspi_isr_wait(GQSPI_IXR_GEN_FIFO_NOT_FULL, 0)) {
        return GQSPI_CODE_TIMEOUT;
    }

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("FifoEntry=%08x\n", reg_genfifo);
#endif
    GQSPI_GEN_FIFO = reg_genfifo;
    return GQSPI_CODE_SUCCESS;
}

static int gspi_fifo_tx(const uint8_t* data, uint32_t sz)
{
    uint32_t tmp32, txSz;
    uint8_t* txData = (uint8_t*)&tmp32;

    while (sz > 0) {
        /* Wait for TX FIFO not full */
        if (qspi_isr_wait(GQSPI_IXR_TX_FIFO_FULL, GQSPI_IXR_TX_FIFO_FULL)) {
            return GQSPI_CODE_TIMEOUT;
        }

        /* Write data */
        txSz = sz;
        if (txSz > GQSPI_FIFO_WORD_SZ)
            txSz = GQSPI_FIFO_WORD_SZ;
        tmp32 = 0;
        memcpy(txData, data, txSz);
        sz -= txSz;
        data += txSz;

    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
        wolfBoot_printf("TXD=%08x\n", tmp32);
    #endif
        GQSPI_TXD = tmp32;
    }
    return GQSPI_CODE_SUCCESS;
}

static int gspi_fifo_rx(uint8_t* data, uint32_t sz, uint32_t discardSz)
{
    uint32_t tmp32, rxSz;
    uint8_t* rxData = (uint8_t*)&tmp32;

    while (sz > 0) {
        /* Wait for RX FIFO not empty */
        if (qspi_isr_wait(GQSPI_IXR_RX_FIFO_NOT_EMPTY, 0)) {
            return GQSPI_CODE_TIMEOUT;
        }

        /* Read data */
        tmp32 = GQSPI_RXD;
    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
        wolfBoot_printf("RXD=%08x\n", tmp32);
        if (discardSz > 0)
        	wolfBoot_printf("Discard %d\n", discardSz);
    #endif
        if (discardSz >= GQSPI_FIFO_WORD_SZ) {
            discardSz -= GQSPI_FIFO_WORD_SZ;
            continue;
        }

        rxSz = sz;
        if (rxSz > GQSPI_FIFO_WORD_SZ)
            rxSz = GQSPI_FIFO_WORD_SZ;
        if (rxSz > discardSz) {
            rxSz -= discardSz;
            sz -= discardSz;
        }
        memcpy(data, rxData + discardSz, rxSz);
        discardSz = 0;

        sz -= rxSz;
        data += rxSz;
    }
    return GQSPI_CODE_SUCCESS;
}

static int qspi_cs(QspiDev_t* pDev, int csAssert)
{
    uint32_t reg_genfifo;

    /* Select slave bus, bank, mode and cs clocks */
    reg_genfifo = (pDev->bus & GQSPI_GEN_FIFO_BUS_MASK);
    reg_genfifo |= GQSPI_GEN_FIFO_MODE_SPI;
    if (csAssert) {
        reg_genfifo |= (pDev->cs & GQSPI_GEN_FIFO_CS_MASK);
    }
    reg_genfifo |= GQSPI_GEN_FIFO_IMM(GQSPI_CS_ASSERT_CLOCKS);
    return qspi_gen_fifo_write(reg_genfifo);
}

static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret = GQSPI_CODE_SUCCESS;
    uint32_t reg_genfifo, xferSz;

    GQSPI_EN = 1; /* Enable device */
    qspi_cs(pDev, 1); /* Select slave */

    /* Setup bus slave selection */
    reg_genfifo = ((pDev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
                   (pDev->cs & GQSPI_GEN_FIFO_CS_MASK) |
                    GQSPI_GEN_FIFO_MODE_SPI);

    /* Cmd Data */
    xferSz = cmdSz;
    while (ret == GQSPI_CODE_SUCCESS && cmdData && xferSz > 0) {
       /* Enable TX and send command inline */
       reg_genfifo |= GQSPI_GEN_FIFO_TX;
       reg_genfifo &= ~(GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_IMM_MASK);
       reg_genfifo |= GQSPI_GEN_FIFO_IMM(*cmdData); /* IMM is data */

       /* Submit general FIFO operation */
       ret = qspi_gen_fifo_write(reg_genfifo);
       if (ret != GQSPI_CODE_SUCCESS)
           break;

       /* offset size and buffer */
       xferSz--;
       cmdData++;
    }

    /* Set desired data mode and stripe */
    reg_genfifo |= (mode & GQSPI_GEN_FIFO_MODE_MASK);
    reg_genfifo |= (pDev->stripe & GQSPI_GEN_FIFO_STRIPE);

    /* TX Data */
    while (ret == GQSPI_CODE_SUCCESS && txData && txSz > 0) {
        xferSz = txSz;

        /* Enable TX */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_IMM_MASK |
                         GQSPI_GEN_FIFO_EXP_MASK);
        reg_genfifo |= (GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_DATA_XFER);

        if (xferSz > GQSPI_GEN_FIFO_IMM_MASK) {
            /* Use exponent mode */
            xferSz = 256; /* 2 ^ 8 = 256 */
            reg_genfifo |= GQSPI_GEN_FIFO_EXP_MASK;
            reg_genfifo |= GQSPI_GEN_FIFO_IMM(8); /* IMM is exponent */
        }
        else {
            reg_genfifo |= GQSPI_GEN_FIFO_IMM(xferSz); /* IMM is length */
        }

        /* Submit general FIFO operation */
        ret = qspi_gen_fifo_write(reg_genfifo);

        /* Fill FIFO */
        ret = gspi_fifo_tx(txData, xferSz);
        if (ret != GQSPI_CODE_SUCCESS)
            break;

        /* offset size and buffer */
        txSz -= xferSz;
        txData += xferSz;
    }

    /* Dummy operations */
    if (ret == GQSPI_CODE_SUCCESS && dummySz) {
        /* Send dummy clocks (Disable TX & RX) */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_RX |
                         GQSPI_GEN_FIFO_IMM_MASK | GQSPI_GEN_FIFO_EXP_MASK);
        /* IMM is number of dummy clock cycles */
        reg_genfifo |= GQSPI_GEN_FIFO_IMM(dummySz);
        ret = qspi_gen_fifo_write(reg_genfifo); /* Submit FIFO Dummy Op */

        if (rxSz > 0) {
            /* Convert dummy bits to bytes */
            dummySz = (dummySz + 7) / 8;
            /* Adjust rxSz for dummy bytes */
            rxSz += dummySz;
            /* round up by FIFO Word Size */
            rxSz = (((rxSz + GQSPI_FIFO_WORD_SZ - 1) / GQSPI_FIFO_WORD_SZ) *
                    GQSPI_FIFO_WORD_SZ);
        }
    }

    /* RX Data */
    while (ret == GQSPI_CODE_SUCCESS && rxData && rxSz > 0) {
        xferSz = rxSz;

        /* Enable RX */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_IMM_MASK |
                         GQSPI_GEN_FIFO_EXP_MASK);
        reg_genfifo |= (GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_DATA_XFER);

        if (xferSz > GQSPI_GEN_FIFO_IMM_MASK) {
            /* Use exponent mode */
            xferSz = 256; /* 2 ^ 8 = 256 */
            reg_genfifo |= GQSPI_GEN_FIFO_EXP_MASK;
            reg_genfifo |= GQSPI_GEN_FIFO_IMM(8); /* IMM is exponent */
        }
        else {
            reg_genfifo |= GQSPI_GEN_FIFO_IMM(xferSz); /* IMM is length */
        }

        /* Submit general FIFO operation */
        ret = qspi_gen_fifo_write(reg_genfifo);
        if (ret != GQSPI_CODE_SUCCESS)
            break;

        /* Read FIFO */
        ret = gspi_fifo_rx(rxData, xferSz, dummySz);

        /* offset size and buffer */
        rxSz -= xferSz;
        rxData += (xferSz - dummySz);
        dummySz = 0; /* only first RX */
    }

    qspi_cs(pDev, 0); /* Deselect Slave */
    GQSPI_EN = 0; /* Disable Device */

    return ret;
}

#endif

static int qspi_flash_read_id(QspiDev_t* dev, uint8_t* id, uint32_t idSz)
{
    int ret;
    uint8_t cmd[20]; /* size multiple of uint32_t */
    cmd[0] = MULTI_IO_READ_ID_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, cmd, sizeof(cmd), 0, 
        GQSPI_GEN_FIFO_MODE_SPI);

    wolfBoot_printf("Read FlashID %s: Ret %d, %02x %02x %02x\n",
        (dev->cs & GQSPI_GEN_FIFO_CS_LOWER) ? "Lower" : "Upper",
        ret, cmd[0],  cmd[1],  cmd[2]);

    if (ret == GQSPI_CODE_SUCCESS && id) {
        if (idSz > sizeof(cmd))
            idSz = sizeof(cmd);
        memcpy(id, cmd, idSz);
    }
    return ret;
}

static int qspi_write_enable(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = WRITE_DISABLE_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, NULL, 0, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
    wolfBoot_printf("Write Enable: Ret %d\n", ret);
    return ret;
}
static int qspi_write_disable(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = WRITE_DISABLE_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, NULL, 0, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
    wolfBoot_printf("Write Disable: Ret %d\n", ret);
    return ret;
}

static int qspi_flash_status(QspiDev_t* dev, uint8_t* status)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = READ_FSR_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, cmd, 2, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
    wolfBoot_printf("Flash Status: Ret %d Cmd %02x %02x\n", ret, cmd[0], cmd[1]);
    if (ret == GQSPI_CODE_SUCCESS && status) {
        if (dev->stripe) {
            cmd[0] &= cmd[1];
        }
        *status = cmd[0];
    }
    return ret;
}

static int qspi_wait_ready(QspiDev_t* dev)
{
    int ret;
    uint32_t timeout;
    uint8_t status = 0;

    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_flash_status(dev, &status);
        if (ret == GQSPI_CODE_SUCCESS && (status & FLASH_READY_MASK)) {
            return ret;
        }
    }

    wolfBoot_printf("Flash Ready Timeout!\n");

    return GQSPI_CODE_TIMEOUT;
}

#if GQPI_USE_4BYTE_ADDR == 1
static int qspi_enter_4byte_addr(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = ENTER_4B_ADDR_MODE_CMD;
    ret = qspi_write_enable(&mDev);
    if (ret == GQSPI_CODE_SUCCESS) {
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
        wolfBoot_printf("Enter 4-byte address mode: Ret %d\n", ret);
        if (ret == GQSPI_CODE_SUCCESS) {
            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        }
        qspi_write_disable(&mDev);
    }
    return ret;
}
static int qspi_exit_4byte_addr(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = EXIT_4B_ADDR_MODE_CMD;
    ret = qspi_write_enable(&mDev);
    if (ret == GQSPI_CODE_SUCCESS) {
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
        wolfBoot_printf("Exit 4-byte address mode: Ret %d\n", ret);
        if (ret == GQSPI_CODE_SUCCESS) {
            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        }
        qspi_write_disable(&mDev);
    }
    return ret;
}
#endif

/* QSPI functions */
void qspi_init(uint32_t cpu_clock, uint32_t flash_freq)
{
    int ret;
    uint32_t reg_cfg;
    uint8_t id_low[4];
#if GQPI_USE_DUAL_PARALLEL == 1
    uint8_t id_hi[4];
#endif
    uint32_t timeout;
#ifdef USE_XQSPIPSU
    XQspiPsu_Config *QspiConfig;
#endif

    (void)cpu_clock;
    (void)flash_freq;

    memset(&mDev, 0, sizeof(mDev));

#ifdef USE_XQSPIPSU
    /* Xilinx BSP Driver */
    QspiConfig = XQspiPsu_LookupConfig(QSPI_DEVICE_ID);
    if (QspiConfig == NULL) {
        wolfBoot_printf("QSPI config lookup failed\n");
        return;
    }
    ret = XQspiPsu_CfgInitialize(&mDev.qspiPsuInst, QspiConfig, QspiConfig->BaseAddress);
    if (ret != 0) {
        wolfBoot_printf("QSPI config init failed\n");
        return;
    }
    XQspiPsu_SetOptions(&mDev.qspiPsuInst, XQSPIPSU_MANUAL_START_OPTION);
    XQspiPsu_SetClkPrescaler(&mDev.qspiPsuInst, QSPI_CLK_PRESACALE);

#elif defined(USE_QNX)
    /* QNX QSPI driver */
    mDev.qnx = xzynq_qspi_open();
    if (mDev.qnx == NULL) {
        wolfBoot_printf("QSPI failed to open\n");
        return;
    }
#else
    /* QSPI bare-metal driver */

    /* Disable Linear Mode in case FSBL enabled it */
    LQSPI_EN = 0;

    /* Select Generic Quad-SPI */
    GQSPI_SEL = 1;

    /* Clear and disable interrupts */
    reg_cfg = GQSPI_ISR;
    GQSPI_ISR |= GQSPI_ISR_WR_TO_CLR_MASK; /* Clear poll timeout counter interrupt */
    QSPIDMA_DST_I_STS = QSPIDMA_DST_I_STS; /* clear all active interrupts */
    QSPIDMA_DST_STS |= QSPIDMA_DST_STS_WTC; /* mark outstanding DMA's done */
    GQSPI_IDR = GQSPI_IXR_ALL_MASK; /* disable interrupts */
    QSPIDMA_DST_I_STS = QSPIDMA_DST_I_STS_ALL_MASK; /* disable interrupts */
    /* Reset FIFOs */
    if (GQSPI_ISR & GQSPI_IXR_RX_FIFO_EMPTY) {
        GQSPI_FIFO_CTRL |= (GQSPI_FIFO_CTRL_RST_TX_FIFO | GQSPI_FIFO_CTRL_RST_RX_FIFO);
    }
    if (reg_cfg & GQSPI_IXR_RX_FIFO_EMPTY) {
        GQSPI_FIFO_CTRL |= GQSPI_FIFO_CTRL_RST_RX_FIFO;
    }

    GQSPI_EN = 0; /* Disable device */

    /* Initialize clock divisor, write protect hold and start mode */
    reg_cfg  = GQSPI_CFG_MODE_EN_IO; /* Use I/O Transfer Mode */
    reg_cfg |= GQSPI_CFG_BAUD_RATE_DIV(GQSPI_CLK_DIV); /* Clock Divider */
    reg_cfg |= GQSPI_CFG_WP_HOLD; /* Use WP Hold */
    reg_cfg |= GQSPI_CFG_START_GEN_FIFO; /* Start GFIFO command execution */
    reg_cfg &= ~(GQSPI_CFG_CLK_POL | GQSPI_CFG_CLK_PH); /* Use POL=0,PH=0 */
    GQSPI_CFG = reg_cfg;

    /* use tap delay bypass < 40MHz SPI clock */
    IOU_TAPDLY_BYPASS |= IOU_TAPDLY_BYPASS_LQSPI_RX;
    GQSPI_LPBK_DLY_ADJ = 0;
    QSPI_DATA_DLY_ADJ = 0;

    /* Initialize hardware parameters for Threshold and Interrupts */
    GQSPI_TX_THRESH = 1;
    GQSPI_RX_THRESH = 1;
    GQSPI_GF_THRESH = 16;

    /* Reset DMA */
    QSPIDMA_DST_CTRL = QSPIDMA_DST_CTRL_DEF;
    QSPIDMA_DST_CTRL2 = QSPIDMA_DST_CTRL2_DEF;

    /* Interrupts unmask and enable */
    GQSPI_IMR = GQSPI_IXR_ALL_MASK;
    GQSPI_IER = GQSPI_IXR_ALL_MASK;

    GQSPI_EN = 1; /* Enable Device */
#endif /* USE_QNX */
    (void)reg_cfg;

    /* ------ Flash Read ID (retry) ------ */
    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        /* Slave Select - lower chip */
        mDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
        mDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        mDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        ret = qspi_flash_read_id(&mDev, id_low, sizeof(id_low));
        if (ret != GQSPI_CODE_SUCCESS) {
            continue;
        }

    #if GQPI_USE_DUAL_PARALLEL == 1
        /* Slave Select - upper chip */
        mDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
        mDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        mDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_flash_read_id(&mDev, id_hi, sizeof(id_hi));
        if (ret != GQSPI_CODE_SUCCESS) {
            continue;
        }

        /* ID's for upper and lower must match */
        if ((id_hi[0] == 0 || id_hi[0] == 0xFF) ||
            (id_hi[0] != id_low[0] &&
            id_hi[1] != id_low[1] &&
            id_hi[2] != id_low[2]))
        {
            wolfBoot_printf("Flash ID error!\n");
            continue;
        }
    #endif
        break; /* success */
    }

    /* Slave Select */
    mDev.mode = GQSPI_QSPI_MODE;
#if GQPI_USE_DUAL_PARALLEL == 1
    mDev.bus = GQSPI_GEN_FIFO_BUS_BOTH;
    mDev.cs = GQSPI_GEN_FIFO_CS_BOTH;
    mDev.stripe = GQSPI_GEN_FIFO_STRIPE;
#endif

#if GQPI_USE_4BYTE_ADDR == 1
    /* Enter 4-byte address mode */
    ret = qspi_enter_4byte_addr(&mDev);
    if (ret != GQSPI_CODE_SUCCESS)
        return;
#endif

#ifdef TEST_FLASH
    test_flash(&mDev);
#endif
}


void zynq_init(uint32_t cpu_clock)
{
    qspi_init(cpu_clock, 0);
}

void zynq_exit(void)
{
    int ret;

#if GQPI_USE_4BYTE_ADDR == 1
    /* Exit 4-byte address mode */
    ret = qspi_exit_4byte_addr(&mDev);
    if (ret != GQSPI_CODE_SUCCESS)
        return;
#endif

#ifdef USE_QNX
    if (mDev.qnx) {
        xzynq_qspi_close(mDev.qnx);
        mDev.qnx = NULL;
    }
#endif

    (void)ret;
}


/* public HAL functions */
void hal_init(void)
{
    uint32_t cpu_freq = 0;

#ifdef DEBUG_ZYNQ
    const char* bootMsg = "\nwolfBoot Secure Boot\n";
#ifdef DEBUG_UART    
    uart_init();
    uart_write(bootMsg, strlen(bootMsg));
#endif
    wolfBoot_printf(bootMsg);
#endif /* DEBUG_ZYNQ */

    /* This is only allowed for EL-3 */
    //asm volatile("msr cntfrq_el0, %0" : : "r" (cpu_freq) : "memory");

    zynq_init(cpu_freq);
}

void hal_prepare_boot(void)
{
    zynq_exit();
}

/* Flash functions must be relocated to RAM for execution */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    return 0;
}

/* Xilinx Write uses SPI mode and Page Program 0x02 */
/* Issues using write with QSPI mode */
int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int ret = 0;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t xferSz, page, pages, idx = 0;
    uintptr_t addr;

    /* write by page */
    pages = ((len + (FLASH_PAGE_SIZE-1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages; page++) {
        ret = qspi_write_enable(&mDev);
        if (ret == GQSPI_CODE_SUCCESS) {
            xferSz = len;
            if (xferSz > FLASH_PAGE_SIZE)
                xferSz = FLASH_PAGE_SIZE;

            addr = address + (page * FLASH_PAGE_SIZE);
            if (mDev.stripe) {
                /* For dual parallel the address divide by 2 */
                addr /= 2;
            }

            /* ------ Write Flash (page at a time) ------ */
            memset(cmd, 0, sizeof(cmd));
            cmd[idx++] = PAGE_PROG_CMD;
        #if GQPI_USE_4BYTE_ADDR == 1
            cmd[idx++] = ((addr >> 24) & 0xFF);
        #endif
            cmd[idx++] = ((addr >> 16) & 0xFF);
            cmd[idx++] = ((addr >> 8)  & 0xFF);
            cmd[idx++] = ((addr >> 0)  & 0xFF);
            ret = qspi_transfer(&mDev, cmd, idx,
                (const uint8_t*)(data + (page * FLASH_PAGE_SIZE)),
                xferSz, NULL, 0, 0, GQSPI_GEN_FIFO_MODE_SPI);
            wolfBoot_printf("Flash Page %d Write: Ret %d\n", page, ret);
            if (ret != GQSPI_CODE_SUCCESS)
                break;

            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
            if (ret != GQSPI_CODE_SUCCESS) {
                break;
            }
            qspi_write_disable(&mDev);
        }
    }

    return ret;
}

#if GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_QSPI && GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD QUAD_READ_4B_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_DSPI && GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD DUAL_READ_4B_CMD
#elif GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD FAST_READ_4B_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_QSPI
#define FLASH_READ_CMD QUAD_READ_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_DSPI
#define FLASH_READ_CMD DUAL_READ_CMD
#else
#define FLASH_READ_CMD FAST_READ_CMD
#endif

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int ret;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t idx = 0;

    if (mDev.stripe) {
        /* For dual parallel the address divide by 2 */
        address /= 2;
    }

    /* ------ Read Flash ------ */
    memset(cmd, 0, sizeof(cmd));
    cmd[idx++] = FLASH_READ_CMD;
#if GQPI_USE_4BYTE_ADDR == 1
    cmd[idx++] = ((address >> 24) & 0xFF);
#endif
    cmd[idx++] = ((address >> 16) & 0xFF);
    cmd[idx++] = ((address >> 8)  & 0xFF);
    cmd[idx++] = ((address >> 0)  & 0xFF);
    ret = qspi_transfer(&mDev, cmd, idx, NULL, 0, data, len, GQSPI_DUMMY_READ,
        mDev.mode);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Read: Ret %d\r\n", ret);
#endif

    return ret;
}

/* Issues a sector erase based on flash address */
/* Assumes len is not > sector size */
int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    int ret;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t idx = 0;

    if (mDev.stripe) {
        /* For dual parallel the address divide by 2 */
        address /= 2;
    }

    ret = qspi_write_enable(&mDev);
    if (ret == GQSPI_CODE_SUCCESS) {
        /* ------ Erase Flash ------ */
        memset(cmd, 0, sizeof(cmd));
        cmd[idx++] = SEC_ERASE_CMD;
    #if GQPI_USE_4BYTE_ADDR == 1
        cmd[idx++] = ((address >> 24) & 0xFF);
    #endif
        cmd[idx++] = ((address >> 16) & 0xFF);
        cmd[idx++] = ((address >> 8)  & 0xFF);
        cmd[idx++] = ((address >> 0)  & 0xFF);
        ret = qspi_transfer(&mDev, cmd, idx, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
        wolfBoot_printf("Flash Erase: Ret %d\n", ret);
        if (ret == GQSPI_CODE_SUCCESS) {
            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        }
        qspi_write_disable(&mDev);
    }

    return ret;
}

void RAMFUNCTION ext_flash_lock(void)
{

}

void RAMFUNCTION ext_flash_unlock(void)
{

}

#ifdef TEST_FLASH
#define TEST_ADDRESS 0x2800000 /* 40MB */
static int test_flash(QspiDev_t* dev)
{
    int ret;
    uint32_t i;

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        if (pageData[i] != (i & 0xff)) {
        	wolfBoot_printf("Check Data @ %d failed\n", i);
            return GQSPI_CODE_FAILED;
        }
    }
    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_FLASH */
