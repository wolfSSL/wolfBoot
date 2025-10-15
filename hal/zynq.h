/* zynq.h
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

#ifndef _ZYNQMP_H_
#define _ZYNQMP_H_

#ifndef USE_BUILTIN_STARTUP
/* Macros needs for boot_aarch64_startup.S */

/* By default expect EL2 at startup */
#ifndef EL3_SECURE
#define EL3_SECURE     0
#endif
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR 1
#endif
#ifndef EL1_NONSECURE
#define EL1_NONSECURE  0
#endif

#ifndef HYP_GUEST
/* ZEN Hypervisor guest format support */
#define HYP_GUEST      0
#endif

/* Floating Point Trap Enable */
#ifndef FPU_TRAP
#define FPU_TRAP       0
#endif

/* Errata: 855873: An eviction might overtake a cache clean operation */
#define CONFIG_ARM_ERRATA_855873 1

#define XPAR_PSU_DDR_0_S_AXI_BASEADDR 0x00000000
#define XPAR_PSU_DDR_0_S_AXI_HIGHADDR 0x7FFFFFFF
#define XPAR_PSU_DDR_1_S_AXI_BASEADDR 0x800000000
#define XPAR_PSU_DDR_1_S_AXI_HIGHADDR 0x87FFFFFFF

#endif /* USE_BUILTIN_STARTUP */


/* IOP System-level Control */
#define IOU_SLCR_BASSE             0xFF180000
#define IOU_TAPDLY_BYPASS_ADDR     (IOU_SLCR_BASSE + 0x390)
#define IOU_TAPDLY_BYPASS          (*((volatile uint32_t*)IOU_TAPDLY_BYPASS_ADDR))
#define IOU_TAPDLY_BYPASS_LQSPI_RX (1UL << 2) /* LQSPI Tap Delay Enable on Rx Clock signal. 0: enable. 1: disable (bypass tap delay). */

/* QSPI bare-metal driver */
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
#define GQSPI_DATA_DLY_ADJ (*((volatile uint32_t*)(QSPI_BASE + 0x1F8))) /* adjusting the internal receive data delay for read data capturing */
#define GQSPI_MOD_ID       (*((volatile uint32_t*)(QSPI_BASE + 0x1FC)))
/* DMA Registers */
#define GQSPIDMA_DST       (*((volatile uint32_t*)(QSPI_BASE + 0x800))) /* Destination memory address for DMA stream -> memory data transfer */
#define GQSPIDMA_DST_MSB   (*((volatile uint32_t*)(QSPI_BASE + 0x828))) /* Destination memory address (MSBs) for DMA stream -> memory data transfer */
#define GQSPIDMA_SIZE      (*((volatile uint32_t*)(QSPI_BASE + 0x804))) /* DMA transfer payload for DMA stream -> memory data transfer */
#define GQSPIDMA_STS       (*((volatile uint32_t*)(QSPI_BASE + 0x808))) /* General DST DMA status */
#define GQSPIDMA_CTRL      (*((volatile uint32_t*)(QSPI_BASE + 0x80C))) /* General DST DMA control */
#define GQSPIDMA_ISR       (*((volatile uint32_t*)(QSPI_BASE + 0x814))) /* DST DMA interrupt status register */
#define GQSPIDMA_IER       (*((volatile uint32_t*)(QSPI_BASE + 0x818))) /* DST DMA interrupt enable */
#define GQSPIDMA_IDR       (*((volatile uint32_t*)(QSPI_BASE + 0x81C))) /* DST DMA interrupt disable */
#define GQSPIDMA_IMR       (*((volatile uint32_t*)(QSPI_BASE + 0x820))) /* DST DMA interrupt mask */
#define GQSPIDMA_CTRL2     (*((volatile uint32_t*)(QSPI_BASE + 0x824))) /* General DST DMA control register 2 */

#define GQSPI_LPBK_DLY_ADJ_USE_LPBK        (1UL << 5)
#define GQSPI_LPBK_DLY_ADJ_DIV0(x)         (((x) & 0x7) << 0)
#define GQSPI_LPBK_DLY_ADJ_DLY1(x)         (((x) & 0x3) << 3)
#define GQSPI_DATA_DLY_ADJ_USE_DATA_DLY    (1UL << 31)
#define GQSPI_DATA_DLY_ADJ_DATA_DLY_ADJ(x) (((x) & 0x7) << 28)

/* GQSPI Registers */
/* GQSPI_CFG: Configuration registers */
/* Clock Phase and Polarity. Only mode 3 and 0 are support (11b or 00b) */
#define GQSPI_CFG_CLK_POL             (1UL << 1) /* Clock polarity: 1: QSPI clock is quiescent high,            0: QSPI clock is quiescent low,                  */
#define GQSPI_CFG_CLK_PH              (1UL << 2) /* Clock phase:    1: QSPI clock is inactive outside the word, 0: QSPI clock is active outside the word */
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
    GQSPI_IXR_TX_FIFO_EMPTY | GQSPI_IXR_GEN_FIFO_NOT_FULL | GQSPI_IXR_GEN_FIFO_FULL | \
    GQSPI_IXR_RX_FIFO_EMPTY)
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

/* GQSPIDMA_CTRL */
#define GQSPIDMA_CTRL_DEF  0x403FFA00UL
#define GQSPIDMA_CTRL2_DEF 0x081BFFF8UL

/* GQSPIDMA_STS */
#define GQSPIDMA_STS_WTC   (7UL << 13)
#define GQSPIDMA_STS_BUSY  (1UL << 0)

/* GQSPIDMA_ISR */
#define GQSPIDMA_ISR_DONE      0x02
#define GQSPIDMA_ISR_ALL_MASK  0xFEU

/* QSPI Configuration (bare-metal only) */
#ifndef GQSPI_CLK_REF
#define GQSPI_CLK_REF          125000000 /* QSPI Reference Clock */
#endif
#ifndef GQSPI_CLK_DIV
#define GQSPI_CLK_DIV          2 /* (QSPI_REF_CLK (125MHZ) / (2 << DIV) = BUS): 0=DIV2, 1=DIV4, 2=DIV8 */
#endif
#define GQSPI_CS_ASSERT_CLOCKS   5 /* CS Setup Time (tCSS) */
#define GQSPI_CS_DEASSERT_CLOCKS 4 /* CS Hold Time */

#define GQSPI_FIFO_WORD_SZ     4
#define QQSPI_DMA_ALIGN        64 /* L1 cache size */
#ifndef GQSPI_DMA_TMPSZ
    /* Use larger of WOLFBOOT_SHA_BLOCK_SIZE or IMAGE_HEADER_SIZE */
    #if defined(WOLFBOOT_SHA_BLOCK_SIZE) && \
            WOLFBOOT_SHA_BLOCK_SIZE > IMAGE_HEADER_SIZE
        #define GQSPI_DMA_TMPSZ WOLFBOOT_SHA_BLOCK_SIZE
    #else
        #define GQSPI_DMA_TMPSZ IMAGE_HEADER_SIZE
    #endif
#endif
#define GQSPI_TIMEOUT_TRIES    100000
#define GQSPIDMA_TIMEOUT_TRIES 100000000
#define QSPI_FLASH_READY_TRIES 1000

/* QSPI Configuration */
#ifndef GQSPI_QSPI_MODE
#define GQSPI_QSPI_MODE        GQSPI_GEN_FIFO_MODE_QSPI
#endif
#ifndef GQPI_USE_DUAL_PARALLEL
#define GQPI_USE_DUAL_PARALLEL 1 /* 0=no stripe, 1=stripe */
#endif
#ifndef GQPI_USE_4BYTE_ADDR
#define GQPI_USE_4BYTE_ADDR    1
#endif
#ifndef GQSPI_DUMMY_READ
#define GQSPI_DUMMY_READ       (8) /* Number of dummy clock cycles for reads */
#endif


/* Flash Parameters:
 * Micron Serial NOR Flash Memory 4K Sector Erase MT25QU512ABB
 * ZCU102 uses dual Parallel (stacked device 2*64MB)
 * MT25QU512 - Read FlashID: 20 BB 20 (64MB)
 * MT25QU01G - Read FlashID: 20 BB 21 (128MB)
 * MT25QU02G - Read FlashID: 20 BB 22 (256MB)
 */
#ifndef FLASH_PAGE_SIZE
    #if defined(GQPI_USE_DUAL_PARALLEL) && GQPI_USE_DUAL_PARALLEL == 1
        /* each flash page size is 256 bytes, for dual parallel double it */
        #define FLASH_PAGE_SIZE 512
    #else
        #define FLASH_PAGE_SIZE 256
    #endif
#endif


/* Flash Commands */
#define WRITE_ENABLE_CMD       0x06U
#define READ_SR_CMD            0x05U
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

#define WRITE_EN_MASK          0x02 /* 0=Write Enabled, 1=Disabled Write */
#define FLASH_READY_MASK       0x80 /* 0=Busy, 1=Ready */


/* Return Codes */
#define GQSPI_CODE_SUCCESS     0
#define GQSPI_CODE_FAILED      -100
#define GQSPI_CODE_TIMEOUT     -101



/* eFUSE support */
#define ZYNQMP_EFUSE_BASE       0xFFCC0000
#define ZYNQMP_EFUSE_STATUS     (ZYNQMP_EFUSE_BASE + 0x0008)
#define ZYNQMP_EFUSE_PUF_CHASH  (ZYNQMP_EFUSE_BASE + 0x1050)
#define ZYNQMP_EFUSE_PUF_AUX    (ZYNQMP_EFUSE_BASE + 0x1054)
#define ZYNQMP_EFUSE_SEC_CTRL   (ZYNQMP_EFUSE_BASE + 0x1058)
#define ZYNQMP_EFUSE_PPK0_0     (ZYNQMP_EFUSE_BASE + 0x10A0)
#define ZYNQMP_EFUSE_PPK0_1     (ZYNQMP_EFUSE_BASE + 0x10A4)
#define ZYNQMP_EFUSE_PPK0_2     (ZYNQMP_EFUSE_BASE + 0x10A8)
#define ZYNQMP_EFUSE_PPK0_3     (ZYNQMP_EFUSE_BASE + 0x10AC)
#define ZYNQMP_EFUSE_PPK0_4     (ZYNQMP_EFUSE_BASE + 0x10B0)
#define ZYNQMP_EFUSE_PPK0_5     (ZYNQMP_EFUSE_BASE + 0x10B4)
#define ZYNQMP_EFUSE_PPK0_6     (ZYNQMP_EFUSE_BASE + 0x10B8)
#define ZYNQMP_EFUSE_PPK0_7     (ZYNQMP_EFUSE_BASE + 0x10BC)
#define ZYNQMP_EFUSE_PPK0_8     (ZYNQMP_EFUSE_BASE + 0x10C0)
#define ZYNQMP_EFUSE_PPK0_9     (ZYNQMP_EFUSE_BASE + 0x10C4)
#define ZYNQMP_EFUSE_PPK0_10    (ZYNQMP_EFUSE_BASE + 0x10C8)
#define ZYNQMP_EFUSE_PPK0_11    (ZYNQMP_EFUSE_BASE + 0x10CC)

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


/* UART Support */
#define ZYNQMP_UART0_BASE  0xFF000000
#define ZYNQMP_UART1_BASE  0xFF010000

#define ZYNQMP_UART_CR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x00)))
#define ZYNQMP_UART_MR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x04)))
#define ZYNQMP_UART_IDR         (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x0C))) /* Interrupt Disable Register */
#define ZYNQMP_UART_ISR         (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x14))) /* Interrupt Status Register */
#define ZYNQMP_UART_RXTOUT      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x1C)))
#define ZYNQMP_UART_RXWM        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x20)))
#define ZYNQMP_UART_TXWM        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x44)))
#define ZYNQMP_UART_SR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x2C)))
#define ZYNQMP_UART_FIFO        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x30)))
#define ZYNQMP_UART_BR_GEN      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x18))) /* 2 - 65535: baud_sample */
#define ZYNQMP_UART_BR_DIV      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x34))) /* 4 - 255: Baud rate */


/* UART Control Registers */
#define ZYNQMP_UART_CR_TX_DIS      0x00000020  /* TX disable */
#define ZYNQMP_UART_CR_TX_EN       0x00000010  /* TX enabled */
#define ZYNQMP_UART_CR_RX_DIS      0x00000008  /* RX disable */
#define ZYNQMP_UART_CR_RX_EN       0x00000004  /* RX enabled */
#define ZYNQMP_UART_CR_TXRST       0x00000002  /* TX logic reset */
#define ZYNQMP_UART_CR_RXRST       0x00000001  /* RX logic reset */

/* UART ISR Mask 0-13 bits */
#define ZYNQMP_UART_ISR_MASK       0x3FFF

/* UART Mode Registers */
#define ZYNQMP_UART_MR_PARITY_NONE 0x00000020  /* No parity */

/* UART Channel Status Register (read only) */
#define ZYNQMP_UART_SR_TXFULL   0x00000010U /* TX FIFO full */
#define ZYNQMP_UART_SR_TXEMPTY  0x00000008U /* TX FIFO empty */
#define ZYNQMP_UART_SR_RXFULL   0x00000004U /* RX FIFO full */
#define ZYNQMP_UART_SR_RXEMPTY  0x00000002U /* RX FIFO empty */

/* UART Configuration */
#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 0
    #define DEBUG_UART_BASE     ZYNQMP_UART0_BASE
#elif defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    #define DEBUG_UART_BASE     ZYNQMP_UART1_BASE
#endif
#ifndef DEBUG_UART_BASE
    /* default to UART0 */
    #define DEBUG_UART_BASE     ZYNQMP_UART0_BASE
#endif

#ifndef UART_CLK_REF
    #define UART_CLK_REF        100000000
#endif

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
    #define DEBUG_UART_DIV      6
#endif


#define GICD_BASE       0xF9010000
#define GICC_BASE       0xF9020000


/* Clock - Full Power Domain */
#define CRL_APB_BASE  0xFF5E0000UL
#define QSPI_REF_CTRL (*((volatile uint32_t*)(CRL_APB_BASE + 0x68)))

#define QSPI_REF_CTRL_SRCSEL_MASK   0x7
#define QSPI_REF_CTRL_SRCSEL(n)     ((n) & QSPI_REF_CTRL_SRCSEL_MASK)
#define QSPI_REF_CTRL_SRCSEL_IOPLL  QSPI_REF_CTRL_SRCSEL(0)
#define QSPI_REF_CTRL_SRCSEL_RPLL   QSPI_REF_CTRL_SRCSEL(2)
#define QSPI_REF_CTRL_SRCSEL_DPLL   QSPI_REF_CTRL_SRCSEL(3) /* DPLL_CLK_TO_LPD */

#define QSPI_REF_CTRL_DIVISOR0_MASK (0x3F << 8)
#define QSPI_REF_CTRL_DIVISOR0(n)   (((n) << 8) & QSPI_REF_CTRL_DIVISOR0_MASK)

#define QSPI_REF_CTRL_DIVISOR1_MASK (0x3F << 16)
#define QSPI_REF_CTRL_DIVISOR1(n)   (((n) << 16) & QSPI_REF_CTRL_DIVISOR0_MASK)


/* Configuration Security Unit (CSU) */
/* Triple-Dedundant MicroBlaze processor */
/* 128 KB CSU ROM (immutable) */
/* 32 KB CSU RAM (with ECC) */
/* Internal clock source */
/* CSU must be called through PMUFW. */
/* PMUFW must be built with SECURE_ACCESS_VAL=1 */
#define CSU_BASE          0xFFCA0000UL

#define CSU_STATUS        (CSU_BASE + 0x0000U)
#define CSU_STATUS_BOOT_ENC  (1 << 1)
#define CSU_STATUS_BOOT_AUTH (1 << 0)

/* See JTAG IDCODE in TRM */
#define CSU_IDCODE        (CSU_BASE + 0x0040U)
/* 2473_8093h=ZU9EG */
/* 1471_1093h=ZU2CG/EG */

#define CSU_VERSION       (CSU_BASE + 0x0044U)
/* 0: XCZU9EG-ES1,
 * 1: XCZU3EG-ES1, XCZU15EG-ES1,
 * 2: XCZU7EV-ES1, XCZU9EG-ES2, XCZU19EG-ES1,
 * 3: All devices as of October 2017 (Production Level) */
#define CSU_VERSION_MASK  0xF

#define CSU_TAMPER_STATUS (CSU_BASE + 0x5000U)
#define CSU_TAMPER_TRIG   (CSU_BASE + 0x0014U) /* set =1 to trigger tamber event for testing */

/* SSS - Secure Stream Switch */
#define CSU_SSS_CFG           (CSU_BASE + 0x0008U)
#define CSU_SSS_CFG_PCAP_MASK 0x0000000FU
#define CSU_SSS_CFG_PCAP(n)   (((n) << 0) & CSU_SSS_CFG_PCAP_MASK)
#define CSU_SSS_CFG_DMA_MASK  0x000000F0U
#define CSU_SSS_CFG_DMA(n)    (((n) << 4) & CSU_SSS_CFG_DMA_MASK)
#define CSU_SSS_CFG_AES_MASK  0x00000F00U
#define CSU_SSS_CFG_AES(n)    (((n) << 8) & CSU_SSS_CFG_AES_MASK)
#define CSU_SSS_CFG_SHA_MASK  0x0000F000U
#define CSU_SSS_CFG_SHA(n)    (((n) << 12) & CSU_SSS_CFG_SHA_MASK)
/* Data Sources */
#define CSU_SSS_CFG_SRC_NONE  0x0
#define CSU_SSS_CFG_SRC_PCAP  0x3 /* Processor Configuration Access Port */
#define CSU_SSS_CFG_SRC_DMA   0x5
#define CSU_SSS_CFG_SRC_AES   0xA

/* AES-GCM 256-bit */
#define CSU_AES_STATUS    (CSU_BASE + 0x1000U)
#define CSU_AES_KEY_SRC   (CSU_BASE + 0x1004U) /* AES key source selection */
#define CSU_AES_KEY_LOAD  (CSU_BASE + 0x1008U) /* Loads the key selected by AES_KEY_SRC into the AES (self clearing) */
#define CSU_AES_START_MSG (CSU_BASE + 0x100CU) /* Starts the decryption process. The IV must be loaded before the AES will decrypt a payload (self clearing) */
#define CSU_AES_RESET     (CSU_BASE + 0x1010U)
#define CSU_AES_KEY_CLEAR (CSU_BASE + 0x1014U)
#define CSU_AES_CFG       (CSU_BASE + 0x1018U) /* 0=Dec, 1=Enc */
#define CSU_AES_KUP_WR    (CSU_BASE + 0x101CU)
#define CSU_AES_KUP       (CSU_BASE + 0x1020U) /* 32 bytes - through 0x40 */
#define CSU_AES_IV        (CSU_BASE + 0x1040U) /* 16 bytes - through 0x50 */

#define CSU_AES_STATUS_OKR_ZEROED     (1 << 11)
#define CSU_AES_STATUS_BOOT_ZEROED    (1 << 10)
#define CSU_AES_STATUS_KUP_ZEROED     (1 << 9)
#define CSU_AES_STATUS_AES_KEY_ZEROED (1 << 8)
#define CSU_AES_STATUS_KEY_INIT_DONE  (1 << 4)
#define CSU_AES_STATUS_GCM_TAG_PASS   (1 << 3)
#define CSU_AES_STATUS_DONE           (1 << 2)
#define CSU_AES_STATUS_READY          (1 << 1)
#define CSU_AES_STATUS_BUSY           (1 << 0)

#define CSU_AES_KEY_SRC_DEVICE_KEY    1 /* Device key is selected and locked by the CSU ROM during boot */
#define CSU_AES_KEY_SRC_KUP           0 /* User provided key source */

#define CSU_AES_KEY_CLEAR_KUP         (1 << 1) /* Zeroize KUP key */
#define CSU_AES_KEY_CLEAR_EXP         (1 << 0) /* Zeroize expanded key */

#define CSU_AES_CFG_DEC 0
#define CSU_AES_CFG_ENC 1


/* PUF */
#define CSU_PUF_CMD     (CSU_BASE + 0x4000U)
#define CSU_PUF_CFG0    (CSU_BASE + 0x4004U)
#define CSU_PUF_CFG1    (CSU_BASE + 0x4008U)
#define CSU_PUF_SHUTTER (CSU_BASE + 0x400CU)
#define CSU_PUF_STATUS  (CSU_BASE + 0x4010U)
#define CSU_PUF_DBG     (CSU_BASE + 0x4014U)
#define CSU_PUF_WORD    (CSU_BASE + 0x4018U)

#define CSU_PUF_CMD_CLEAR        0x6 /* Clear PUF status */
#define CSU_PUF_CMD_STATUS       0x5 /* Read out regeneration status */
#define CSU_PUF_CMD_REGENERATION 0x4 /* Key regeneration */
#define CSU_PUF_CMD_REGISTRATION 0x1 /* Key registration */

#define CSU_PUF_CFG0_INIT        0x2
#define CSU_PUF_CFG1_INIT        0x0C230090U /* 4K */
#define CSU_PUF_SHUTTER_INIT     0x00100005E

#define CSU_PUF_STATUS_OVERFLOW_MASK    (0x3U << 28)     /* Overflow, if bits are not 0. Reduce SHUT[SOPEN] value. */
#define CSU_PUF_STATUS_AUX_MASK         (0xFFFFFFU << 4) /* During provisioning, auxiliary sundrome bits are stored here and must be written to the eFuse or boot image. */
#define CSU_PUF_STATUS_KEY_RDY_MASK     (0x1U << 3)      /* Indicates that the key is ready */
#define CSU_PUF_STATUS_KEY_ZERO_MASK    (0x1U << 1)      /* Indicates that the PUF key has been zeroized */
#define CSU_PUF_STATUS_SYN_WRD_RDY_MASK (0x1U << 0)      /* Indicates a syndrome word is ready in the PUF_WORD register */

/* SHA3 */
#define CSU_SHA_START  (CSU_BASE + 0x2000U)
#define CSU_SHA_RESET  (CSU_BASE + 0x2004U)
#define CSU_SHA_DONE   (CSU_BASE + 0x2008U)
#define CSU_SHA_DIGEST (CSU_BASE + 0x2010U) /* 48 bytes (through 0x40) */
#define CSU_SHA_START  (CSU_BASE + 0x2000U)
#define CSU_SHA_START  (CSU_BASE + 0x2000U)

/* CSU DMA */
/* Addresses and sizes must be word aligned last two bits = 0 */
/* 128 x 32-bit data FIFO for each channel (two channels) */
#define CSUDMA_BASE(ch)     (0xFFC80000UL + (((ch) & 0x1) * 0x800))
#define CSUDMA_ADDR(ch)     (CSUDMA_BASE(ch) + 0x0000U) /* Mem address (lower 32-bits) */
#define CSUDMA_ADDR_MSB(ch) (CSUDMA_BASE(ch) + 0x0028U) /*             (upper 17 bits) */
#define CSUDMA_SIZE(ch)     (CSUDMA_BASE(ch) + 0x0004U) /* DMA transfer payload size */
#define CSUDMA_STS(ch)      (CSUDMA_BASE(ch) + 0x0008U)
#define CSUDMA_CTRL(ch)     (CSUDMA_BASE(ch) + 0x000CU)
#define CSUDMA_CTRL2(ch)    (CSUDMA_BASE(ch) + 0x0024U)
#define CSUDMA_CRC(ch)      (CSUDMA_BASE(ch) + 0x0010U)
#define CSUDMA_ISTS(ch)     (CSUDMA_BASE(ch) + 0x0014U)
#define CSUDMA_IEN(ch)      (CSUDMA_BASE(ch) + 0x0018U)
#define CSUDMA_IDIS(ch)     (CSUDMA_BASE(ch) + 0x001CU)
#define CSUDMA_IMASK(ch)    (CSUDMA_BASE(ch) + 0x0020U)

#define CSUDMA_SIZE_LAST_WORD     (1 << 0)

#define CSUDMA_STS_DONE_CNT       (0x07 << 13)
#define CSUDMA_STS_SRC_FIFO_LEVEL (0xFF << 5)
#define CSUDMA_STS_RD_OUTSTANDING (0x0F << 1)
#define CSUDMA_STS_BUSY           (0x01 << 0)

#define CSUDMA_CTRL_FIFO_THRESH_MASK (0xF << 2)
#define CSUDMA_CTRL_FIFO_THRESH(n)   (((n) << 2) & CSUDMA_CTRL_FIFO_THRESH_MASK)
#define CSUDMA_CTRL_TIMEOUT_VAL_MASK (0xFFF << 10)
#define CSUDMA_CTRL_TIMEOUT_VAL(n)   (((n) << 10) & CSUDMA_CTRL_TIMEOUT_VAL_MASK)
#define CSUDMA_CTRL_ENDIANNESS       (1 << 23)
#define CSUDMA_CTRL_AXI_BRST_TYPE    (1 << 22)

#define CSUDMA_CTRL2_ARCACHE_MASK       (0x7 << 24)
#define CSUDMA_CTRL2_ARCACHE(n)         (((n) << 24) & CSUDMA_CTRL2_ARCACHE_MASK)
#define CSUDMA_CTRL2_ROUTE_BIT          (1 << 23)
#define CSUDMA_CTRL2_TIMEOUT_EN         (1 << 22)
#define CSUDMA_CTRL2_TIMEOUT_PRE_MASK   (0xFFF << 4)
#define CSUDMA_CTRL2_TIMEOUT_PRE(n)     (((n) << 4) & CSUDMA_CTRL2_TIMEOUT_PRE_MASK)
#define CSUDMA_CTRL2_MAX_OUTS_CMDS_MASK (0xF << 0)
#define CSUDMA_CTRL2_MAX_OUTS_CMDS(n)   (((n) << 0) & CSUDMA_CTRL2_MAX_OUTS_CMDS_MASK)

#define CSUDMA_ISR_INVALID_APB  (1 << 6)
#define CSUDMA_ISR_THRESH_HIT   (1 << 5)
#define CSUDMA_ISR_TIMEOUT_MEM  (1 << 4)
#define CSUDMA_ISR_TIMEOUT_STRM (1 << 3)
#define CSUDMA_ISR_AXI_RDERR    (1 << 2)
#define CSUDMA_ISR_DONE         (1 << 1)
#define CSUDMA_ISR_MEM_DONE     (1 << 0)

/* CSU DMA Channels */
#define CSUDMA_CH_SRC 0
#define CSUDMA_CH_DST 1

/* CSU JTAG */
#define CSU_JTAG_CHAIN_CFG    (CSU_BASE + 0x0030U)
#define CSU_JTAG_CHAIN_STATUS (CSU_BASE + 0x0034U)
#define CSU_JTAG_SEC          (CSU_BASE + 0x0038U)
#define CSU_JTAG_DAP_CFG      (CSU_BASE + 0x003CU)

#define CSU_PCAP_PROG         (CSU_BASE + 0x3000U)

/* Clock and Reset Control */
#define CRL_APB_BASE 0xFF5E0000UL
#define CRL_APB_DBG_LPD_CTRL (CRL_APB_BASE + 0x00B0U)
#define CRL_APB_RST_LPD_DBG  (CRL_APB_BASE + 0x0240U)



#endif /* _ZYNQMP_H_ */
