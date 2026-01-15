/* versal.h
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
 * AMD Versal ACAP HAL definitions for wolfBoot
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 */

#ifndef _VERSAL_H_
#define _VERSAL_H_

/* Only include C headers when compiling C code, not assembly */
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif /* __ASSEMBLER__ */

/* ============================================================================
 * Exception Level Configuration
 * ============================================================================
 * Versal PLM (Platform Loader Manager) can hand off at EL3, EL2, or EL1
 * depending on configuration. Default is EL2 (hypervisor mode).
 */
#ifndef USE_BUILTIN_STARTUP

#ifndef EL3_SECURE
#define EL3_SECURE     0
#endif
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR 1
#endif
#ifndef EL1_NONSECURE
#define EL1_NONSECURE  1
#endif

#ifndef HYP_GUEST
#define HYP_GUEST      0
#endif

#ifndef FPU_TRAP
#define FPU_TRAP       0
#endif

/* ARM Errata */
#define CONFIG_ARM_ERRATA_855873 1

#endif /* USE_BUILTIN_STARTUP */


/* ============================================================================
 * Memory Map
 * ============================================================================
 * Versal memory map (simplified):
 *   0x0000_0000 - 0x7FFF_FFFF : DDR Low (2GB)
 *   0x8_0000_0000 - ...       : DDR High (extended)
 *   0xF000_0000 - 0xFFFF_FFFF : LPD/FPD Peripherals
 */

/* DDR Memory */
#define VERSAL_DDR_0_BASE       0x00000000UL
#define VERSAL_DDR_0_HIGH       0x7FFFFFFFUL
#define VERSAL_DDR_1_BASE       0x800000000ULL
#define VERSAL_DDR_1_HIGH       0x87FFFFFFFULL

/* DDR defines for MMU table setup (used by boot_aarch64_start.S)
 * These macros enable proper DDR mapping in the page tables.
 * Without these, the MMU tables would have DDR_0_REG=0 and no DDR mapped! */
#define XPAR_PSU_DDR_0_S_AXI_BASEADDR   VERSAL_DDR_0_BASE
#define XPAR_PSU_DDR_0_S_AXI_HIGHADDR   VERSAL_DDR_0_HIGH


/* ============================================================================
 * UART (ARM PL011 UART - UARTPSV)
 * ============================================================================
 * Versal uses ARM PL011 UART (different from ZynqMP Cadence UART!)
 * Based on AMD/Xilinx xuartpsv_hw.h
 */

#define VERSAL_UART0_BASE       0xFF000000UL
#define VERSAL_UART1_BASE       0xFF010000UL

/* Select UART based on DEBUG_UART_NUM */
#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    #define DEBUG_UART_BASE     VERSAL_UART1_BASE
#else
    #define DEBUG_UART_BASE     VERSAL_UART0_BASE
#endif

/* UART Register Offsets (ARM PL011) */
#define UART_DR_OFFSET          0x00    /* Data Register (TX/RX FIFO) */
#define UART_RSR_OFFSET         0x04    /* Receive Status / Error Clear */
#define UART_FR_OFFSET          0x18    /* Flag Register (Status) */
#define UART_ILPR_OFFSET        0x20    /* IrDA Low-Power Counter */
#define UART_IBRD_OFFSET        0x24    /* Integer Baud Rate Divisor */
#define UART_FBRD_OFFSET        0x28    /* Fractional Baud Rate Divisor */
#define UART_LCR_OFFSET         0x2C    /* Line Control Register */
#define UART_CR_OFFSET          0x30    /* Control Register */
#define UART_IFLS_OFFSET        0x34    /* Interrupt FIFO Level Select */
#define UART_IMSC_OFFSET        0x38    /* Interrupt Mask Set/Clear */
#define UART_RIS_OFFSET         0x3C    /* Raw Interrupt Status */
#define UART_MIS_OFFSET         0x40    /* Masked Interrupt Status */
#define UART_ICR_OFFSET         0x44    /* Interrupt Clear Register */
#define UART_DMACR_OFFSET       0x48    /* DMA Control Register */

/* UART Register Access Macros */
#define UART_REG(offset)        (*((volatile uint32_t*)(DEBUG_UART_BASE + (offset))))

#define UART_DR                 UART_REG(UART_DR_OFFSET)
#define UART_RSR                UART_REG(UART_RSR_OFFSET)
#define UART_FR                 UART_REG(UART_FR_OFFSET)
#define UART_IBRD               UART_REG(UART_IBRD_OFFSET)
#define UART_FBRD               UART_REG(UART_FBRD_OFFSET)
#define UART_LCR                UART_REG(UART_LCR_OFFSET)
#define UART_CR                 UART_REG(UART_CR_OFFSET)
#define UART_IFLS               UART_REG(UART_IFLS_OFFSET)
#define UART_IMSC               UART_REG(UART_IMSC_OFFSET)
#define UART_ICR                UART_REG(UART_ICR_OFFSET)

/* Flag Register (UARTFR) bits - Status */
#define UART_FR_RI              (1UL << 8)  /* Ring indicator */
#define UART_FR_TXFE            (1UL << 7)  /* TX FIFO empty */
#define UART_FR_RXFF            (1UL << 6)  /* RX FIFO full */
#define UART_FR_TXFF            (1UL << 5)  /* TX FIFO full */
#define UART_FR_RXFE            (1UL << 4)  /* RX FIFO empty */
#define UART_FR_BUSY            (1UL << 3)  /* UART busy */
#define UART_FR_DCD             (1UL << 2)  /* Data carrier detect */
#define UART_FR_DSR             (1UL << 1)  /* Data set ready */
#define UART_FR_CTS             (1UL << 0)  /* Clear to send */

/* Control Register (UARTCR) bits */
#define UART_CR_CTSEN           (1UL << 15) /* CTS hardware flow control */
#define UART_CR_RTSEN           (1UL << 14) /* RTS hardware flow control */
#define UART_CR_RTS             (1UL << 11) /* Request to send */
#define UART_CR_DTR             (1UL << 10) /* Data transmit ready */
#define UART_CR_RXE             (1UL << 9)  /* Receive enable */
#define UART_CR_TXE             (1UL << 8)  /* Transmit enable */
#define UART_CR_LBE             (1UL << 7)  /* Loopback enable */
#define UART_CR_UARTEN          (1UL << 0)  /* UART enable */

/* Line Control Register (UARTLCR) bits */
#define UART_LCR_SPS            (1UL << 7)  /* Stick parity select */
#define UART_LCR_WLEN_MASK      (3UL << 5)  /* Word length mask */
#define UART_LCR_WLEN_8         (3UL << 5)  /* 8 data bits */
#define UART_LCR_WLEN_7         (2UL << 5)  /* 7 data bits */
#define UART_LCR_WLEN_6         (1UL << 5)  /* 6 data bits */
#define UART_LCR_WLEN_5         (0UL << 5)  /* 5 data bits */
#define UART_LCR_FEN            (1UL << 4)  /* FIFO enable */
#define UART_LCR_STP2           (1UL << 3)  /* Two stop bits */
#define UART_LCR_EPS            (1UL << 2)  /* Even parity select */
#define UART_LCR_PEN            (1UL << 1)  /* Parity enable */
#define UART_LCR_BRK            (1UL << 0)  /* Send break */

/* Interrupt FIFO Level Select (UARTIFLS) */
#define UART_IFLS_RXIFLSEL_1_8  (0UL << 3)  /* RX FIFO 1/8 full */
#define UART_IFLS_RXIFLSEL_1_4  (1UL << 3)  /* RX FIFO 1/4 full */
#define UART_IFLS_RXIFLSEL_1_2  (2UL << 3)  /* RX FIFO 1/2 full */
#define UART_IFLS_TXIFLSEL_1_8  (0UL << 0)  /* TX FIFO 1/8 full */
#define UART_IFLS_TXIFLSEL_1_4  (1UL << 0)  /* TX FIFO 1/4 full */
#define UART_IFLS_TXIFLSEL_1_2  (2UL << 0)  /* TX FIFO 1/2 full */

/* Interrupt bits (for IMSC, RIS, MIS, ICR) */
#define UART_INT_OE             (1UL << 10) /* Overrun error */
#define UART_INT_BE             (1UL << 9)  /* Break error */
#define UART_INT_PE             (1UL << 8)  /* Parity error */
#define UART_INT_FE             (1UL << 7)  /* Framing error */
#define UART_INT_RT             (1UL << 6)  /* Receive timeout */
#define UART_INT_TX             (1UL << 5)  /* Transmit */
#define UART_INT_RX             (1UL << 4)  /* Receive */
#define UART_INT_ALL            0x7FFU      /* All interrupts */

/* UART Configuration */
#ifndef UART_CLK_REF
    #define UART_CLK_REF        100000000UL  /* 100 MHz reference clock */
#endif

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
#endif

/* ============================================================================
 * PMC_IOU_SLCR - MIO Pin Configuration
 * ============================================================================
 * Required for JTAG boot mode where PLM doesn't run
 */
#define PMC_IOU_SLCR_BASE       0xF1060000UL

/* MIO Pin registers - each MIO pin has its own 4-byte register */
#define PMC_IOU_SLCR_MIO_PIN(n) (*((volatile uint32_t*)(PMC_IOU_SLCR_BASE + 0x0 + ((n) * 4))))

/* MIO Pin register bits */
#define MIO_PIN_L0_SEL_MASK     (0x1UL << 0)   /* Level 0 MUX select */
#define MIO_PIN_L1_SEL_MASK     (0x1UL << 1)   /* Level 1 MUX select */
#define MIO_PIN_L2_SEL_MASK     (0x3UL << 2)   /* Level 2 MUX select */
#define MIO_PIN_L3_SEL_MASK     (0x7UL << 4)   /* Level 3 MUX select */
#define MIO_PIN_TRI_ENABLE      (0x1UL << 8)   /* Tri-state enable (input) */
#define MIO_PIN_PULLUP          (0x1UL << 12)  /* Pull-up enable */
#define MIO_PIN_SCHMITT_ENABLE  (0x1UL << 13)  /* Schmitt trigger enable */
#define MIO_PIN_SLOW_SLEW       (0x0UL << 14)  /* Slow slew rate */
#define MIO_PIN_FAST_SLEW       (0x1UL << 14)  /* Fast slew rate */

/* UART0 default MIO pins on VMK180: MIO 0 (RX), MIO 1 (TX)
 * L3_SEL = 1 selects UART function */
#define MIO_UART0_RX_PIN        0   /* MIO0 = UART0 RX */
#define MIO_UART0_TX_PIN        1   /* MIO1 = UART0 TX */
#define MIO_UART1_RX_PIN        4   /* MIO4 = UART1 RX */
#define MIO_UART1_TX_PIN        5   /* MIO5 = UART1 TX */

/* MIO configuration for UART TX pin: Output, UART function (L3_SEL=1) */
#define MIO_UART_TX_CFG         (0x1UL << 4)  /* L3_SEL = 1 for UART */
/* MIO configuration for UART RX pin: Input, UART function (L3_SEL=1) */
#define MIO_UART_RX_CFG         ((0x1UL << 4) | MIO_PIN_TRI_ENABLE)

/* ============================================================================
 * CRL (Clock Reset LPD) - For UART clock/reset control
 * ============================================================================
 * Register offsets verified from Xilinx lpd_data.cdo
 */
#define VERSAL_CRL_BASE         0xFF5E0000UL

/* UART Reference Clock Control - from lpd_data.cdo line 176 */
#define CRL_UART0_REF_CTRL      (*((volatile uint32_t*)(VERSAL_CRL_BASE + 0x0128)))
#define CRL_UART1_REF_CTRL      (*((volatile uint32_t*)(VERSAL_CRL_BASE + 0x012C)))

/* UART Reset Control - from lpd_data.cdo line 258 */
#define CRL_RST_UART0           (*((volatile uint32_t*)(VERSAL_CRL_BASE + 0x0318)))
#define CRL_RST_UART1           (*((volatile uint32_t*)(VERSAL_CRL_BASE + 0x031C)))
#define CRL_RST_UART0_BIT       (1UL << 0)
#define CRL_RST_UART1_BIT       (1UL << 0)  /* Each UART has its own register */

/* Backward compatibility alias */
#define CRL_RST_UART            CRL_RST_UART0

/* Clock Reference Control bits */
#define CRL_CLK_CLKACT          (1UL << 25)   /* Clock active */
#define CRL_CLK_DIVISOR_MASK    (0x3FFUL << 8) /* Divisor field */


/* ============================================================================
 * System Timer (ARM Generic Timer)
 * ============================================================================
 * Versal uses ARM Generic Timer accessed via system registers
 */

/* Timer frequency (typically configured by PLM) */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ          100000000UL  /* 100 MHz default */
#endif


/* ============================================================================
 * GIC (Generic Interrupt Controller)
 * ============================================================================
 */
#define VERSAL_GIC_BASE         0xF9000000UL
#define VERSAL_GICD_BASE        (VERSAL_GIC_BASE + 0x00000)  /* Distributor */
#define VERSAL_GICC_BASE        (VERSAL_GIC_BASE + 0x40000)  /* CPU Interface */
#define VERSAL_GICH_BASE        (VERSAL_GIC_BASE + 0x60000)  /* Virtual Interface Control */
#define VERSAL_GICV_BASE        (VERSAL_GIC_BASE + 0x80000)  /* Virtual CPU Interface */


/* ============================================================================
 * Clock and Reset (CRL/CRF)
 * ============================================================================
 */
#define VERSAL_CRL_BASE         0xFF5E0000UL  /* Clock and Reset LPD */
#define VERSAL_CRF_BASE         0xFD1A0000UL  /* Clock and Reset FPD */


/* ============================================================================
 * PMC (Platform Management Controller)
 * ============================================================================
 * The PMC is the security controller in Versal (replaces CSU from ZynqMP)
 */
#define VERSAL_PMC_GLOBAL_BASE  0xF1110000UL
#define VERSAL_PMC_TAP_BASE     0xF11A0000UL


/* ============================================================================
 * QSPI (Quad SPI) Flash Controller - GQSPI
 * ============================================================================
 * Versal QSPI is similar to ZynqMP but at different base address.
 * VMK180 uses dual parallel MT25QU01GBBB (128MB each, 256MB total)
 */
#define VERSAL_QSPI_BASE        0xF1030000UL

/* QSPI Enable Register (at base, not +0x100) */
#define QSPI_EN_REG             (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x14)))

/* GQSPI Registers (at offset 0x100 from QSPI base) */
#define GQSPI_CFG               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x100)))
#define GQSPI_ISR               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x104)))
#define GQSPI_IER               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x108)))
#define GQSPI_IDR               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x10C)))
#define GQSPI_IMR               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x110)))
#define GQSPI_EN                (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x114)))
#define GQSPI_TXD               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x11C)))
#define GQSPI_RXD               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x120)))
#define GQSPI_TX_THRESH         (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x128)))
#define GQSPI_RX_THRESH         (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x12C)))
#define GQSPI_GPIO              (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x130)))
#define GQSPI_LPBK_DLY_ADJ      (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x138)))
#define GQSPI_GEN_FIFO          (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x140)))
#define GQSPI_SEL               (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x144)))
#define GQSPI_FIFO_CTRL         (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x14C)))
#define GQSPI_GF_THRESH         (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x150)))
#define GQSPI_POLL_CFG          (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x154)))
#define GQSPI_P_TIMEOUT         (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x158)))
#define GQSPI_XFER_STS          (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x15C)))
#define GQSPI_DATA_DLY_ADJ      (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x1F8)))
#define GQSPI_MOD_ID            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x1FC)))

/* DMA Registers (at offset 0x800 from QSPI base) */
#define GQSPIDMA_DST            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x800)))
#define GQSPIDMA_SIZE           (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x804)))
#define GQSPIDMA_STS            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x808)))
#define GQSPIDMA_CTRL           (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x80C)))
#define GQSPIDMA_ISR            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x814)))
#define GQSPIDMA_IER            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x818)))
#define GQSPIDMA_IDR            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x81C)))
#define GQSPIDMA_IMR            (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x820)))
#define GQSPIDMA_CTRL2          (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x824)))
#define GQSPIDMA_DST_MSB        (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x828)))

/* Tap Delay Bypass Register - Versal specific location */
#define IOU_TAPDLY_BYPASS       (*((volatile uint32_t*)(VERSAL_QSPI_BASE + 0x03C)))
#define IOU_TAPDLY_BYPASS_LQSPI_RX  (1UL << 2)

/* GQSPI_CFG: Configuration register bits */
#define GQSPI_CFG_CLK_POL           (1UL << 1)
#define GQSPI_CFG_CLK_PH            (1UL << 2)
#define GQSPI_CFG_BAUD_RATE_DIV_MASK (7UL << 3)
#define GQSPI_CFG_BAUD_RATE_DIV(d)  (((d) << 3) & GQSPI_CFG_BAUD_RATE_DIV_MASK)
#define GQSPI_CFG_WP_HOLD           (1UL << 19)
#define GQSPI_CFG_EN_POLL_TIMEOUT   (1UL << 20)
#define GQSPI_CFG_ENDIAN            (1UL << 26)
#define GQSPI_CFG_START_GEN_FIFO    (1UL << 28)
#define GQSPI_CFG_GEN_FIFO_START_MODE (1UL << 29)
#define GQSPI_CFG_MODE_EN_MASK      (3UL << 30)
#define GQSPI_CFG_MODE_EN_IO        (0UL << 30)
#define GQSPI_CFG_MODE_EN_DMA       (2UL << 30)

/* GQSPI_ISR/IER/IDR: Interrupt bits */
#define GQSPI_IXR_POLL_TIME_EXPIRE  (1UL << 1)
#define GQSPI_IXR_TX_FIFO_NOT_FULL  (1UL << 2)
#define GQSPI_IXR_TX_FIFO_FULL      (1UL << 3)
#define GQSPI_IXR_RX_FIFO_NOT_EMPTY (1UL << 4)
#define GQSPI_IXR_RX_FIFO_FULL      (1UL << 5)
#define GQSPI_IXR_GEN_FIFO_EMPTY    (1UL << 7)
#define GQSPI_IXR_TX_FIFO_EMPTY     (1UL << 8)
#define GQSPI_IXR_GEN_FIFO_NOT_FULL (1UL << 9)
#define GQSPI_IXR_GEN_FIFO_FULL     (1UL << 10)
#define GQSPI_IXR_RX_FIFO_EMPTY     (1UL << 11)
#define GQSPI_IXR_ALL_MASK          0x0FBEU
#define GQSPI_ISR_WR_TO_CLR_MASK    0x02U

/* GenFIFO Entry bits */
#define GQSPI_GEN_FIFO_IMM_MASK     0xFFU
#define GQSPI_GEN_FIFO_IMM(x)       ((x) & GQSPI_GEN_FIFO_IMM_MASK)
#define GQSPI_GEN_FIFO_DATA_XFER    (1UL << 8)
#define GQSPI_GEN_FIFO_EXP          (1UL << 9)
#define GQSPI_GEN_FIFO_MODE_SPI     (1UL << 10)
#define GQSPI_GEN_FIFO_MODE_DSPI    (2UL << 10)
#define GQSPI_GEN_FIFO_MODE_QSPI    (3UL << 10)
#define GQSPI_GEN_FIFO_MODE_MASK    (3UL << 10)
#define GQSPI_GEN_FIFO_CS_LOWER     (1UL << 12)
#define GQSPI_GEN_FIFO_CS_UPPER     (1UL << 13)
#define GQSPI_GEN_FIFO_CS_MASK      (3UL << 12)
#define GQSPI_GEN_FIFO_CS_BOTH      (3UL << 12)
#define GQSPI_GEN_FIFO_BUS_LOW      (1UL << 14)
#define GQSPI_GEN_FIFO_BUS_UP       (1UL << 15)
#define GQSPI_GEN_FIFO_BUS_BOTH     (3UL << 14)
#define GQSPI_GEN_FIFO_BUS_MASK     (3UL << 14)
#define GQSPI_GEN_FIFO_TX           (1UL << 16)
#define GQSPI_GEN_FIFO_RX           (1UL << 17)
#define GQSPI_GEN_FIFO_STRIPE       (1UL << 18)
#define GQSPI_GEN_FIFO_POLL         (1UL << 19)

/* DMA Control bits */
#define GQSPIDMA_CTRL_DEF           0x403FFA00UL
#define GQSPIDMA_CTRL2_DEF          0x081BFFF8UL
#define GQSPIDMA_CTRL_ENDIANNESS    (1UL << 23)

/* DMA Status bits */
#define GQSPIDMA_STS_BUSY           (1UL << 0)
#define GQSPIDMA_STS_WTC            (7UL << 13)

/* DMA Interrupt bits */
#define GQSPIDMA_ISR_DONE           (1UL << 1)
#define GQSPIDMA_ISR_ALL_MASK       0xFEU

/* FIFO Control bits */
#define GQSPI_FIFO_CTRL_RST_GEN     (1UL << 0)
#define GQSPI_FIFO_CTRL_RST_TX      (1UL << 1)
#define GQSPI_FIFO_CTRL_RST_RX      (1UL << 2)

/* QSPI Select */
#define GQSPI_SEL_GQSPI             (1UL << 0)

/* Flash Commands */
#define FLASH_CMD_READ_ID           0x9F
#define FLASH_CMD_READ_STATUS       0x05
#define FLASH_CMD_READ_FLAG_STATUS  0x70
#define FLASH_CMD_WRITE_ENABLE      0x06
#define FLASH_CMD_WRITE_DISABLE     0x04
#define FLASH_CMD_READ              0x03
#define FLASH_CMD_FAST_READ         0x0B
#define FLASH_CMD_QUAD_READ         0x6B
#define FLASH_CMD_READ_4B           0x13
#define FLASH_CMD_FAST_READ_4B      0x0C
#define FLASH_CMD_QUAD_READ_4B      0x6C
#define FLASH_CMD_PAGE_PROG         0x02
#define FLASH_CMD_PAGE_PROG_4B      0x12
#define FLASH_CMD_SECTOR_ERASE      0xD8
#define FLASH_CMD_SECTOR_ERASE_4B   0xDC
#define FLASH_CMD_ENTER_4B_MODE     0xB7
#define FLASH_CMD_EXIT_4B_MODE      0xE9

/* Flash Status Register bits */
#define FLASH_SR_WIP                (1UL << 0)  /* Write In Progress */
#define FLASH_SR_WEL                (1UL << 1)  /* Write Enable Latch */
#define FLASH_FSR_READY             (1UL << 7)  /* Flag Status Ready */

/* Flash Configuration for MT25QU01GBBB */
#define FLASH_JEDEC_MICRON          0x20
#define FLASH_JEDEC_MT25QU01G       0x20BB21
#define FLASH_PAGE_SIZE             256
#define FLASH_SECTOR_SIZE           0x10000  /* 64KB */
#define FLASH_DEVICE_SIZE           0x8000000 /* 128MB per chip */

/* QSPI Configuration (bare-metal driver) */
#ifndef GQSPI_CLK_REF
    #define GQSPI_CLK_REF           300000000  /* 300 MHz */
#endif
#ifndef GQSPI_CLK_DIV
    #define GQSPI_CLK_DIV           1  /* Divide by 4 (300MHz / 4 = 75MHz) */
#endif
#define GQSPI_CS_ASSERT_CLOCKS      5  /* CS Setup Time (tCSS) */
#define GQSPI_CS_DEASSERT_CLOCKS    4  /* CS Hold Time */
#define GQSPI_FIFO_WORD_SZ          4
#define GQSPI_DMA_ALIGN             64 /* L1 cache size */
#ifndef GQSPI_DMA_TMPSZ
    #define GQSPI_DMA_TMPSZ         4096
#endif
#define GQSPI_TIMEOUT_TRIES         100000
#define GQSPIDMA_TIMEOUT_TRIES      100000000
#define GQSPI_FLASH_READY_TRIES     1000000  /* Erase can take seconds */

/* QSPI Mode Configuration */
#ifndef GQSPI_QSPI_MODE
    #define GQSPI_QSPI_MODE         GQSPI_GEN_FIFO_MODE_QSPI  /* 4-bit data */
#endif
#ifndef GQPI_USE_DUAL_PARALLEL
    #define GQPI_USE_DUAL_PARALLEL  1  /* 0=single, 1=dual parallel (striped) */
#endif
#ifndef GQPI_USE_4BYTE_ADDR
    #define GQPI_USE_4BYTE_ADDR     1  /* 0=3-byte addr, 1=4-byte addr */
#endif
#ifndef GQSPI_DUMMY_READ
    #define GQSPI_DUMMY_READ        8  /* Dummy clocks for Fast/Quad Read */
#endif

#define XALIGNED(x) __attribute__((aligned(x)))


/* ============================================================================
 * SD/eMMC Controller (SDHCI)
 * ============================================================================
 * Versal has 2 SD/eMMC controllers
 */
#define VERSAL_SD0_BASE         0xF1040000UL
#define VERSAL_SD1_BASE         0xF1050000UL


/* ============================================================================
 * Helper Functions (C code only)
 * ============================================================================
 */
#ifndef __ASSEMBLER__

/* Get current exception level */
static inline unsigned int current_el(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (el));
    return (unsigned int)((el >> 2) & 0x3);
}

/* Memory barrier */
static inline void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

#endif /* __ASSEMBLER__ */

#endif /* _VERSAL_H_ */

