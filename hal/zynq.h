/* zynq.h
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

/* Clocking */
#define CORTEXA53_0_CPU_CLK_FREQ_HZ    1199880127
#define CORTEXA53_0_TIMESTAMP_CLK_FREQ 99990005
#define UART_MASTER_CLOCK              99990005
#define GQSPI_CLK_FREQ_HZ              124987511

/* IOP System-level Control */
#define IOU_SLCR_BASSE             0xFF180000
#define IOU_TAPDLY_BYPASS          (*((volatile uint32_t*)(IOU_SLCR_BASSE + 0x390)))
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

#define GQSPI_LPBK_DLY_ADJ_USE_LPBK       (1UL << 5)
#define GQSPI_LPBK_DLY_ADJ_DIV0(x)        (((x) & 0x7) << 0)
#define GQSPI_LPBK_DLY_ADJ_DLY1(x)        (((x) & 0x3) << 3)
#define GQSPI_DATA_DLY_ADJ_USE_DATA_DLY    (1UL << 31)
#define GQSPI_DATA_DLY_ADJ_DATA_DLY_ADJ(x) (((x) & 0x7) << 28)

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

/* GQSPIDMA_CTRL */
#define GQSPIDMA_CTRL_DEF  0x403FFA00UL
#define GQSPIDMA_CTRL2_DEF 0x081BFFF8UL

/* GQSPIDMA_STS */
#define GQSPIDMA_STS_WTC   0xE000U

/* GQSPIDMA_ISR */
#define GQSPIDMA_ISR_DONE      0x02
#define GQSPIDMA_ISR_ALL_MASK  0xFEU

/* QSPI Configuration (bare-metal only) */
#ifndef GQSPI_CLK_DIV
#define GQSPI_CLK_DIV          2 /* (CLK (300MHz) / (2 << DIV) = BUS): 0=DIV2, 1=DIV4, 2=DIV8 */
#endif
#define GQSPI_CS_ASSERT_CLOCKS 5 /* CS Setup Time (tCSS) - num of clock cycles foes in IMM */
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
 * Micron Serial NOR Flash Memory 64KB Sector Erase MT25QU512ABB
 * Stacked device (two 512Mb (64MB))
 * Dual Parallel so total addressable size is double
 */
#ifndef FLASH_DEVICE_SIZE
    #ifdef ZCU102
        /* 64*2 (dual parallel) = 128MB */
        #define FLASH_DEVICE_SIZE (2 *  64 * 1024 * 1024) /* MT25QU512ABB */
    #else
        /* 128*2 (dual parallel) = 256MB */
        #define FLASH_DEVICE_SIZE (2 * 128 * 1024 * 1024) /* MT25QU01GBBB */
    #endif
#endif
#ifndef FLASH_PAGE_SIZE
    #ifdef ZCU102
        /* MT25QU512ABB - Read FlashID: 20 BB 20 */
        #define FLASH_PAGE_SIZE 256
    #else
        /* MT25QU01GBBB - Read FlashID: 20 BB 21 */
        #define FLASH_PAGE_SIZE 512
    #endif
#endif
#define FLASH_NUM_SECTORS      (FLASH_DEVICE_SIZE/WOLFBOOT_SECTOR_SIZE)


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

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
    #define DEBUG_UART_DIV      6
#endif


#define GICD_BASE       0xF9010000
#define GICC_BASE       0xF9020000


#endif /* _ZYNQMP_H_ */
