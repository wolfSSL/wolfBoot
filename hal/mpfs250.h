/* mpfs250.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef MPFS250_DEF_INCLUDED
#define MPFS250_DEF_INCLUDED

/* PolarFire SoC MPFS250T board specific configuration */

/* APB/AHB Clock Frequency */
#define MSS_APB_AHB_CLK    150000000

/* Hardware Base Address */
#define SYSREG_BASE 0x20002000

/* Write "0xDEAD" to cause a full MSS reset*/
#define SYSREG_MSS_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x18)))

/* Peripheral Soft Reset Control Register */
#define SYSREG_SOFT_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x88)))
#define SYSREG_SOFT_RESET_CR_ENVM (1U << 0)
#define SYSREG_SOFT_RESET_CR_MMC  (1U << 3)
#define SYSREG_SOFT_RESET_CR_MMUART0 (1U << 5)
#define SYSREG_SOFT_RESET_CR_MMUART1 (1U << 6)
#define SYSREG_SOFT_RESET_CR_MMUART2 (1U << 7)
#define SYSREG_SOFT_RESET_CR_MMUART3 (1U << 8)
#define SYSREG_SOFT_RESET_CR_MMUART4 (1U << 9)
#define SYSREG_SOFT_RESET_CR_SPI0 (1U << 10)
#define SYSREG_SOFT_RESET_CR_SPI1 (1U << 11)
#define SYSREG_SOFT_RESET_CR_QSPI (1U << 19)
#define SYSREG_SOFT_RESET_CR_GPIO0 (1U << 20)
#define SYSREG_SOFT_RESET_CR_GPIO1 (1U << 21)
#define SYSREG_SOFT_RESET_CR_GPIO2 (1U << 22)
#define SYSREG_SOFT_RESET_CR_DDRC (1U << 23)
#define SYSREG_SOFT_RESET_CR_ATHENA (1U << 28) /* Crypto hardware accelerator */


/* TODO: Add support for machine mode wolfBoot */
#if 1
    #define WOLFBOOT_RISCV_SMODE /* supervisor mode */
#else
    #define WOLFBOOT_RISCV_MMODE /* machine mode */
#endif

/* size of each trap frame register */
#define REGBYTES (1 << 3)

/* Machine Information Registers */
#define CSR_MVENDORID    0xF11
#define CSR_MARCHID      0xF12
#define CSR_MIMPID       0xF13
#define CSR_MHARTID      0xF14

/* Initial stack pointer address (stack grows downward from here) */
#ifdef WOLFBOOT_RISCV_SMODE
#define WOLFBOOT_STACK_TOP 0x80200000
#else
#define WOLFBOOT_STACK_TOP 0x80000000
#endif

#ifdef WOLFBOOT_RISCV_SMODE
#define MODE_PREFIX(__suffix)    s##__suffix
#else
#define MODE_PREFIX(__suffix)    m##__suffix
#endif

/* SIE (Interrupt Enable) and SIP (Interrupt Pending) flags */
#define IRQ_U_SOFT   0
#define IRQ_S_SOFT   1
#define IRQ_M_SOFT   3
#define IRQ_U_TIMER  4
#define IRQ_S_TIMER  5
#define IRQ_M_TIMER  7
#define IRQ_U_EXT    8
#define IRQ_S_EXT    9
#define IRQ_M_EXT    11
#define MIE_MSIE     (1 << IRQ_M_SOFT)
#define SIE_SSIE     (1 << IRQ_S_SOFT)
#define SIE_STIE     (1 << IRQ_S_TIMER)
#define SIE_SEIE     (1 << IRQ_S_EXT)

#define MCAUSE64_INT   0x8000000000000000LLU
#define MCAUSE64_CAUSE 0x7FFFFFFFFFFFFFFFLLU



/* UART */
#define MSS_UART0_LO_BASE  0x20000000UL
#define MSS_UART1_LO_BASE  0x20100000UL
#define MSS_UART2_LO_BASE  0x20102000UL
#define MSS_UART3_LO_BASE  0x20104000UL
#define MSS_UART4_LO_BASE  0x20106000UL

#define MSS_UART0_HI_BASE  0x28000000UL
#define MSS_UART1_HI_BASE  0x28100000UL
#define MSS_UART2_HI_BASE  0x28102000UL
#define MSS_UART3_HI_BASE  0x28104000UL
#define MSS_UART4_HI_BASE  0x28106000UL

#define MMUART_RBR(base) *((volatile uint8_t*)((base)) + 0x00) /* Receiver buffer register */
#define MMUART_IER(base) *((volatile uint8_t*)((base)) + 0x04) /* Interrupt enable register */
#define MMUART_IIR(base) *((volatile uint8_t*)((base)) + 0x08) /* Interrupt ID register */
#define MMUART_LCR(base) *((volatile uint8_t*)((base)) + 0x0C) /* Line control register */
#define MMUART_MCR(base) *((volatile uint8_t*)((base)) + 0x10) /* Modem control register */
#define MMUART_LSR(base) *((volatile uint8_t*)((base)) + 0x14) /* Line status register */
#define MMUART_MSR(base) *((volatile uint8_t*)((base)) + 0x18) /* Modem status register */
#define MMUART_SCR(base) *((volatile uint8_t*)((base)) + 0x1C) /* Scratch register */
#define MMUART_IEM(base) *((volatile uint8_t*)((base)) + 0x24) /* Interrupt enable mask */
#define MMUART_IIM(base) *((volatile uint8_t*)((base)) + 0x28) /* multi-mode Interrupt ID register */
#define MMUART_MM0(base) *((volatile uint8_t*)((base)) + 0x30) /* Mode register 0 */
#define MMUART_MM1(base) *((volatile uint8_t*)((base)) + 0x34) /* Mode register 1 */
#define MMUART_MM2(base) *((volatile uint8_t*)((base)) + 0x38) /* Mode register 2 */
#define MMUART_DFR(base) *((volatile uint8_t*)((base)) + 0x3C) /* Data frame register */
#define MMUART_GFR(base) *((volatile uint8_t*)((base)) + 0x44) /* Global filter register */
#define MMUART_TTG(base) *((volatile uint8_t*)((base)) + 0x48) /* TX time guard register */
#define MMUART_RTO(base) *((volatile uint8_t*)((base)) + 0x4C) /* RX timeout register */
#define MMUART_ADR(base) *((volatile uint8_t*)((base)) + 0x50) /* Address register */
#define MMUART_DLR(base) *((volatile uint8_t*)((base)) + 0x80) /* Divisor latch register */
#define MMUART_DMR(base) *((volatile uint8_t*)((base)) + 0x84) /* Divisor mode register */
#define MMUART_THR(base) *((volatile uint8_t*)((base)) + 0x100) /* Transmitter holding register */
#define MMUART_FCR(base) *((volatile uint8_t*)((base)) + 0x104) /* FIFO control register */


/* LCR (Line Control Register) */
#define MSS_UART_DATA_8_BITS        ((uint8_t)0x03)
#define MSS_UART_NO_PARITY          ((uint8_t)0x00)
#define MSS_UART_ONE_STOP_BIT       ((uint8_t)0x00)

/* LSR (Line Status Register) */
#define MSS_UART_THRE               ((uint8_t)0x20)    /* Transmitter holding register empty */
#define MSS_UART_TEMT               ((uint8_t)0x40)    /* Transmit empty */

#define ELIN_MASK            (1U << 3) /* Enable LIN header detection */
#define EIRD_MASK            (1U << 2) /* Enable IrDA modem */
#define EERR_MASK            (1U << 0) /* Enable ERR / NACK during stop time */

#define RXRDY_TXRDYN_EN_MASK (1U << 0) /* Enable TXRDY and RXRDY signals */
#define CLEAR_RX_FIFO_MASK   (1U << 1) /* Clear receiver FIFO */
#define CLEAR_TX_FIFO_MASK   (1U << 2) /* Clear transmitter FIFO */

#define LOOP_MASK            (1U << 4) /* Local loopback */
#define RLOOP_MASK           (1U << 5) /* Remote loopback & Automatic echo*/

#define E_MSB_RX_MASK        (1U << 0) /* MSB / LSB first for receiver */
#define E_MSB_TX_MASK        (1U << 1) /* MSB / LSB first for transmitter */

#define EAFM_MASK            (1U << 1) /* Enable 9-bit address flag mode */
#define ETTG_MASK            (1U << 5) /* Enable transmitter time guard */
#define ERTO_MASK            (1U << 6) /* Enable receiver time-out */
#define ESWM_MASK            (1U << 3) /* Enable single wire half-duplex mode */
#define EFBR_MASK            (1U << 7) /* Enable fractional baud rate mode */

/* Line Control register bit masks */
#define SB_MASK              (1U << 6) /* Set break */
#define DLAB_MASK            (1U << 7) /* Divisor latch access bit */

/* ============================================================================
 * EMMC/SD Card Controller (Cadence SD4HC)
 * Base Address: 0x20008000
 * ============================================================================ */
#define EMMC_SD_BASE 0x20008000UL

/* ----------------------------------------------------------------------------
 * HRS - Host Register Set (Cadence-specific registers)
 * ---------------------------------------------------------------------------- */

/* HRS00 - General Information Register */
#define EMMC_SD_HRS00 *((volatile uint32_t*)(EMMC_SD_BASE + 0x000))
#define EMMC_SD_HRS00_SAV_SHIFT 16       /* 23:16: RO - Number of slots */
#define EMMC_SD_HRS00_SAV_MASK  (0xFFU << 16)
#define EMMC_SD_HRS00_SWR (1U << 0)  /*  0: RW - Software Reset (entire core) */

/* HRS01 - Debounce Setting Register */
#define EMMC_SD_HRS01 *((volatile uint32_t*)(EMMC_SD_BASE + 0x004))
#define EMMC_SD_HRS01_DP_SHIFT 0         /* 23:0: RW - Debounce Period */
#define EMMC_SD_HRS01_DP_MASK  (0xFFFFFFU << 0)

/* HRS02 - Bus Setting Register */
#define EMMC_SD_HRS02 *((volatile uint32_t*)(EMMC_SD_BASE + 0x008))
#define EMMC_SD_HRS02_OTN_SHIFT 16       /* 17:16: RW - Outstanding Transactions Number */
#define EMMC_SD_HRS02_OTN_MASK  (0x3U << 16)
#define EMMC_SD_HRS02_PBL_SHIFT 0        /* 3:0: RW - Programmable Burst Length */
#define EMMC_SD_HRS02_PBL_MASK  (0xFU << 0)

/* HRS03 - AXI ERROR Responses Register */
#define EMMC_SD_HRS03 *((volatile uint32_t*)(EMMC_SD_BASE + 0x00C))
#define EMMC_SD_HRS03_AER_IEBS  (1U << 19) /* 19: Int Enable Bus Status */
#define EMMC_SD_HRS03_AER_IEBD  (1U << 18) /* 18: Int Enable Bus Data */
#define EMMC_SD_HRS03_AER_IERS  (1U << 17) /* 17: Int Enable Resp Status */
#define EMMC_SD_HRS03_AER_IERD  (1U << 16) /* 16: Int Enable Resp Data */
#define EMMC_SD_HRS03_AER_SENBS (1U << 11) /* 11: Status Enable Bus Status */
#define EMMC_SD_HRS03_AER_SENBD (1U << 10) /* 10: Status Enable Bus Data */
#define EMMC_SD_HRS03_AER_SENRS (1U << 9)  /*  9: Status Enable Resp Status */
#define EMMC_SD_HRS03_AER_SENRD (1U << 8)  /*  8: Status Enable Resp Data */

/* HRS04 - PHY Registers Interface (SD/eMMC PHY access) */
#define EMMC_SD_HRS04 *((volatile uint32_t*)(EMMC_SD_BASE + 0x010))
#define EMMC_SD_HRS04_UIS_ACK (1U << 26) /* 26: RO - PHY Acknowledge */
#define EMMC_SD_HRS04_UIS_RD  (1U << 25) /* 25: RW - PHY Read Request */
#define EMMC_SD_HRS04_UIS_WR  (1U << 24) /* 24: RW - PHY Write Request */
#define EMMC_SD_HRS04_UIS_RDATA_SHIFT   16  /* 23:16: RO - PHY Read Data */
#define EMMC_SD_HRS04_UIS_RDATA_MASK    (0xFFU << 16)
#define EMMC_SD_HRS04_UIS_WDATA_SHIFT   8   /* 15:8: RW - PHY Write Data */
#define EMMC_SD_HRS04_UIS_WDATA_MASK    (0xFFU << 8)
#define EMMC_SD_HRS04_UIS_ADDR_SHIFT    0   /* 5:0: RW - PHY Register Address */
#define EMMC_SD_HRS04_UIS_ADDR_MASK     (0x3FU << 0)
/* PHY register addresses */
#define EMMC_SD_PHY_ADDR_HIGH_SPEED      0x00U
#define EMMC_SD_PHY_ADDR_DEFAULT_SPEED   0x01U
#define EMMC_SD_PHY_ADDR_UHSI_SDR12      0x02U
#define EMMC_SD_PHY_ADDR_UHSI_SDR25      0x03U
#define EMMC_SD_PHY_ADDR_UHSI_SDR50      0x04U
#define EMMC_SD_PHY_ADDR_UHSI_DDR50      0x05U
#define EMMC_SD_PHY_ADDR_MMC_LEGACY      0x06U
#define EMMC_SD_PHY_ADDR_MMC_SDR         0x07U
#define EMMC_SD_PHY_ADDR_MMC_DDR         0x08U
#define EMMC_SD_PHY_ADDR_SDCLK           0x0BU
#define EMMC_SD_PHY_ADDR_HS_SDCLK        0x0CU
#define EMMC_SD_PHY_ADDR_DAT_STROBE      0x0DU

/* HRS06 - eMMC Control Register */
#define EMMC_SD_HRS06 *((volatile uint32_t*)(EMMC_SD_BASE + 0x018))
#define EMMC_SD_HRS06_ETR (1U << 15)                            /* 15: RW - eMMC Tune Request */
#define EMMC_SD_HRS06_ETV_SHIFT 8                                   /* eMMC Tune Value */
#define EMMC_SD_HRS06_ETV_MASK  0x3FU
#define EMMC_SD_HRS06_EMM_SHIFT 0        /* 2:0: RW - eMMC Mode Select */
#define EMMC_SD_HRS06_EMM_MASK  (0x7U << 0)
/* eMMC Mode values */
#define EMMC_SD_HRS06_MODE_SD        (0x0U << 0)                    /* SD card mode */
#define EMMC_SD_HRS06_MODE_MMC_SDR   (0x2U << 0)                    /* eMMC SDR mode */
#define EMMC_SD_HRS06_MODE_MMC_DDR   (0x3U << 0)                    /* eMMC DDR mode */
#define EMMC_SD_HRS06_MODE_MMC_HS200 (0x4U << 0)                    /* eMMC HS200 mode */
#define EMMC_SD_HRS06_MODE_MMC_HS400 (0x5U << 0)                    /* eMMC HS400 mode */
#define EMMC_SD_HRS06_MODE_MMC_HS400ES (0x6U << 0)                  /* eMMC HS400 Enhanced Strobe */
#define EMMC_SD_HRS06_MODE_LEGACY    (0x7U << 0)                    /* Legacy mode */

/* HRS07 - IO Delay Information Register */
#define EMMC_SD_HRS07 *((volatile uint32_t*)(EMMC_SD_BASE + 0x01C))
#define EMMC_SD_HRS07_ODELAY_VAL_SHIFT 16  /* 20:16: RW - Output Delay Value */
#define EMMC_SD_HRS07_ODELAY_VAL_MASK  (0x1FU << 16)
#define EMMC_SD_HRS07_IDELAY_VAL_SHIFT 0   /* 4:0: RW - Input Delay Value */
#define EMMC_SD_HRS07_IDELAY_VAL_MASK  (0x1FU << 0)

#define EMMC_SD_HRS30 *((volatile uint32_t*)(EMMC_SD_BASE + 0x078)) /* Host Capability Register */
#define EMMC_SD_HRS30_HS400ESSUP (1U << 1)  /*  1: RO - HS400 Enhanced Strobe Support */
#define EMMC_SD_HRS30_CQSUP      (1U << 0)  /*  0: RO - Command Queuing Support */

#define EMMC_SD_HRS31 *((volatile uint32_t*)(EMMC_SD_BASE + 0x07C)) /* Host Controller Version */
#define EMMC_SD_HRS31_HOSTCTRLVER_SHIFT 16  /* 27:16: RO */
#define EMMC_SD_HRS31_HOSTCTRLVER_MASK  (0xFFFU << 16)
#define EMMC_SD_HRS31_HOSTFIXVER_SHIFT 0   /* 7:0: RO */
#define EMMC_SD_HRS31_HOSTFIXVER_MASK  (0xFFU << 0)

#define EMMC_SD_HRS32 *((volatile uint32_t*)(EMMC_SD_BASE + 0x080)) /* FSM Monitor Register */
#define EMMC_SD_HRS32_LOAD (1U << 31) /* 31: RW */
#define EMMC_SD_HRS32_ADDR_SHIFT 16      /* 30:16: WO */
#define EMMC_SD_HRS32_ADDR_MASK  (0x7FFFU << 16)
#define EMMC_SD_HRS32_DATA_SHIFT 0       /* 15:0: RO */
#define EMMC_SD_HRS32_DATA_MASK  (0xFFFFU << 0)

#define EMMC_SD_HRS33 *((volatile uint32_t*)(EMMC_SD_BASE + 0x084)) /* Tune Status 0 Register */
#define EMMC_SD_HRS33_STAT0_SHIFT 0      /* 31:0: RO */
#define EMMC_SD_HRS33_STAT0_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_HRS34 *((volatile uint32_t*)(EMMC_SD_BASE + 0x088)) /* Tune Status 1 Register */
#define EMMC_SD_HRS34_STAT1_SHIFT 0      /* 7:0: RO */
#define EMMC_SD_HRS34_STAT1_MASK  (0xFFU << 0)

#define EMMC_SD_HRS35 *((volatile uint32_t*)(EMMC_SD_BASE + 0x08C)) /* Tune Debug Register */
#define EMMC_SD_HRS35_TFR (1U << 31) /* 31: RW */
#define EMMC_SD_HRS35_TFV_SHIFT 16       /* 21:16: RW */
#define EMMC_SD_HRS35_TFV_MASK  (0x3FU << 16)
#define EMMC_SD_HRS35_TVAL_SHIFT 0       /* 5:0: RO */
#define EMMC_SD_HRS35_TVAL_MASK  (0x3FU << 0)

#define EMMC_SD_HRS36 *((volatile uint32_t*)(EMMC_SD_BASE + 0x090)) /* Boot Status Register */
#define EMMC_SD_HRS36_BOOT_EDE (1U << 5)  /*  5: RO - Boot Error Data End Bit */
#define EMMC_SD_HRS36_BOOT_EDC (1U << 4)  /*  4: RO - Boot Error Data CRC */
#define EMMC_SD_HRS36_BOOT_EDT (1U << 3)  /*  3: RO - Boot Error Data Timeout */
#define EMMC_SD_HRS36_BOOT_EAI (1U << 2)  /*  2: RO - Boot Error Ack Index */
#define EMMC_SD_HRS36_BOOT_EAT (1U << 1)  /*  1: RO - Boot Error Ack Timeout */
#define EMMC_SD_HRS36_BOOT_ACT (1U << 0)  /*  0: RO - Boot Active */

#define EMMC_SD_HRS37 *((volatile uint32_t*)(EMMC_SD_BASE + 0x094)) /* Read block gap coeff interface mode select */
#define EMMC_SD_HRS37_RGB_COEFF_IFM_SHIFT 0  /* 5:0: RW */
#define EMMC_SD_HRS37_RGB_COEFF_IFM_MASK  (0x3FU << 0)

#define EMMC_SD_HRS38 *((volatile uint32_t*)(EMMC_SD_BASE + 0x098)) /* Read block gap coefficient */
#define EMMC_SD_HRS38_RGB_COEFF_SHIFT 0  /* 3:0: RW */
#define EMMC_SD_HRS38_RGB_COEFF_MASK  (0xFU << 0)

/* CRS - Capability Register Set */
#define EMMC_SD_CRS63 *((volatile uint32_t*)(EMMC_SD_BASE + 0x0FC)) /* Host Controller Version/Slot Interrupt Status */
#define EMMC_SD_CRS63_SVN_SHIFT 16       /* 23:16: RO */
#define EMMC_SD_CRS63_SVN_MASK  (0xFFU << 16)
#define EMMC_SD_CRS63_ISES_SHIFT 0       /* 7:0: RO */
#define EMMC_SD_CRS63_ISES_MASK  (0xFFU << 0)

/* ----------------------------------------------------------------------------
 * SRS - Slot Register Set (SD Host Controller Standard Registers)
 * ---------------------------------------------------------------------------- */

/* SRS00 - SDMA System Address / Argument 2 (for Auto CMD23) */
#define EMMC_SD_SRS00 *((volatile uint32_t*)(EMMC_SD_BASE + 0x200))
#define EMMC_SD_SRS00_SAAR_SHIFT 0       /* 31:0: RW */
#define EMMC_SD_SRS00_SAAR_MASK  (0xFFFFFFFFU << 0)

/* SRS01 - Block Size / Block Count Register */
#define EMMC_SD_SRS01 *((volatile uint32_t*)(EMMC_SD_BASE + 0x204))
#define EMMC_SD_SRS01_BCCT_SHIFT 16      /* 31:16: RW - Block Count for Current Transfer */
#define EMMC_SD_SRS01_BCCT_MASK  (0xFFFFU << 16)
#define EMMC_SD_SRS01_SDMABB_SHIFT 12                               /* SDMA Buffer Boundary */
#define EMMC_SD_SRS01_SDMABB_MASK  0x7U
#define EMMC_SD_SRS01_DMA_BUFF_4KB   (0x0U << 12)                   /* DMA buffer 4KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_8KB   (0x1U << 12)                   /* DMA buffer 8KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_16KB  (0x2U << 12)                   /* DMA buffer 16KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_32KB  (0x3U << 12)                   /* DMA buffer 32KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_64KB  (0x4U << 12)                   /* DMA buffer 64KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_128KB (0x5U << 12)                   /* DMA buffer 128KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_256KB (0x6U << 12)                   /* DMA buffer 256KB boundary */
#define EMMC_SD_SRS01_DMA_BUFF_512KB (0x7U << 12)                   /* DMA buffer 512KB boundary */
#define EMMC_SD_SRS01_TBS_SHIFT 0        /* 11:0: RW - Transfer Block Size */
#define EMMC_SD_SRS01_TBS_MASK  (0xFFFU << 0)

/* SRS02 - Argument 1 Register */
#define EMMC_SD_SRS02 *((volatile uint32_t*)(EMMC_SD_BASE + 0x208))
#define EMMC_SD_SRS02_ARG1_SHIFT 0       /* 31:0: RW */
#define EMMC_SD_SRS02_ARG1_MASK  (0xFFFFFFFFU << 0)

/* SRS03 - Command/Transfer Mode Register */
#define EMMC_SD_SRS03 *((volatile uint32_t*)(EMMC_SD_BASE + 0x20C))
#define EMMC_SD_SRS03_CIDX_SHIFT 24      /* 29:24: RW - Command Index */
#define EMMC_SD_SRS03_CIDX_MASK  (0x3FU << 24)
#define EMMC_SD_SRS03_CT_SHIFT 22        /* 23:22: RW - Command Type */
#define EMMC_SD_SRS03_CT_MASK  (0x3U << 22)
#define EMMC_SD_SRS03_CMD_NORMAL   (0x0U << 22) /* Normal command */
#define EMMC_SD_SRS03_CMD_SUSPEND  (0x1U << 22) /* Suspend command */
#define EMMC_SD_SRS03_CMD_RESUME   (0x2U << 22) /* Resume command */
#define EMMC_SD_SRS03_CMD_ABORT    (0x3U << 22) /* Abort command (CMD12, CMD52) */
#define EMMC_SD_SRS03_DPS   (1U << 21) /* 21: RW - Data Present Select */
#define EMMC_SD_SRS03_CICE  (1U << 20) /* 20: RW - Command Index Check Enable */
#define EMMC_SD_SRS03_CRCCE (1U << 19) /* 19: RW - Command CRC Check Enable */
#define EMMC_SD_SRS03_RTS_SHIFT     16     /* Response Type Select */
#define EMMC_SD_SRS03_RTS_MASK      0x3U
#define EMMC_SD_SRS03_RESP_NONE    (0x0U << 16) /* No response */
#define EMMC_SD_SRS03_RESP_136     (0x1U << 16) /* Response length 136 bits */
#define EMMC_SD_SRS03_RESP_48      (0x2U << 16) /* Response length 48 bits */
#define EMMC_SD_SRS03_RESP_48B     (0x3U << 16) /* Response length 48 bits + busy */
#define EMMC_SD_SRS03_RID  (1U << 8)  /*  8: RW - Response Interrupt Disable */
#define EMMC_SD_SRS03_RECE (1U << 7)  /*  7: RW - Response Error Check Enable */
#define EMMC_SD_SRS03_RECT (1U << 6)  /*  6: RW - Response Check Type (R1/R5) */
#define EMMC_SD_SRS03_MSBS (1U << 5)  /*  5: RW - Multi/Single Block Select */
#define EMMC_SD_SRS03_DTDS (1U << 4)  /*  4: RW - Data Transfer Direction (1=read) */
#define EMMC_SD_SRS03_ACE_SHIFT     2     /* 3:2: RW - Auto CMD Enable */
#define EMMC_SD_SRS03_ACE_MASK      (0x3U << 2)
#define EMMC_SD_SRS03_ACMD_DISABLE (0x0U << 2)  /* Auto CMD disabled */
#define EMMC_SD_SRS03_ACMD12       (0x1U << 2)  /* Auto CMD12 enable */
#define EMMC_SD_SRS03_ACMD23       (0x2U << 2)  /* Auto CMD23 enable */
#define EMMC_SD_SRS03_BCE  (1U << 1)  /*  1: RW - Block Count Enable */
#define EMMC_SD_SRS03_DMAE (1U << 0)  /*  0: RW - DMA Enable */

#define EMMC_SD_SRS04 *((volatile uint32_t*)(EMMC_SD_BASE + 0x210)) /* Response Register 1 */
#define EMMC_SD_SRS04_RESP1_SHIFT 0      /* 31:0: RO */
#define EMMC_SD_SRS04_RESP1_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS05 *((volatile uint32_t*)(EMMC_SD_BASE + 0x214)) /* Response Register 2 */
#define EMMC_SD_SRS05_RESP2_SHIFT 0      /* 31:0: RO */
#define EMMC_SD_SRS05_RESP2_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS06 *((volatile uint32_t*)(EMMC_SD_BASE + 0x218)) /* Response Register 3 */
#define EMMC_SD_SRS06_RESP3_SHIFT 0      /* 31:0: RO */
#define EMMC_SD_SRS06_RESP3_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS07 *((volatile uint32_t*)(EMMC_SD_BASE + 0x21C)) /* Response Register 4 */
#define EMMC_SD_SRS07_RESP4_SHIFT 0      /* 31:0: RO */
#define EMMC_SD_SRS07_RESP4_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS08 *((volatile uint32_t*)(EMMC_SD_BASE + 0x220)) /* Data Buffer */
#define EMMC_SD_SRS08_BDP_SHIFT 0        /* 31:0: RW */
#define EMMC_SD_SRS08_BDP_MASK  (0xFFFFFFFFU << 0)

/* SRS09 - Present State Register */
#define EMMC_SD_SRS09 *((volatile uint32_t*)(EMMC_SD_BASE + 0x224))
#define EMMC_SD_SRS09_CMDSL    (1U << 24) /* 24: RO - CMD Line Signal Level */
#define EMMC_SD_SRS09_DATSL1_SHIFT  20   /* 23:20: RO - DAT[3:0] Line Signal Level */
#define EMMC_SD_SRS09_DATSL1_MASK   (0xFU << 20)
#define EMMC_SD_SRS09_DAT3_LVL (1U << 23) /* 23: RO - DAT3 signal level */
#define EMMC_SD_SRS09_DAT2_LVL (1U << 22) /* 22: RO - DAT2 signal level */
#define EMMC_SD_SRS09_DAT1_LVL (1U << 21) /* 21: RO - DAT1 signal level */
#define EMMC_SD_SRS09_DAT0_LVL (1U << 20) /* 20: RO - DAT0 signal level */
#define EMMC_SD_SRS09_DATS_LVL (EMMC_SD_SRS09_DAT0_LVL | EMMC_SD_SRS09_DAT1_LVL | EMMC_SD_SRS09_DAT2_LVL | EMMC_SD_SRS09_DAT3_LVL)
#define EMMC_SD_SRS09_WPSL     (1U << 19) /* 19: RO - Write Protect Switch Level */
#define EMMC_SD_SRS09_CDSL     (1U << 18) /* 18: RO - Card Detect Pin Level */
#define EMMC_SD_SRS09_CSS      (1U << 17) /* 17: RO - Card State Stable */
#define EMMC_SD_SRS09_CI       (1U << 16) /* 16: RO - Card Inserted */
#define EMMC_SD_SRS09_BRE      (1U << 11) /* 11: RO - Buffer Read Enable */
#define EMMC_SD_SRS09_BWE      (1U << 10) /* 10: RO - Buffer Write Enable */
#define EMMC_SD_SRS09_RTA      (1U << 9)  /*  9: RO - Read Transfer Active */
#define EMMC_SD_SRS09_WTA      (1U << 8)  /*  8: RO - Write Transfer Active */
#define EMMC_SD_SRS09_DATSL2_SHIFT  4    /* 7:4: RO - DAT[7:4] Line Signal Level */
#define EMMC_SD_SRS09_DATSL2_MASK   (0xFU << 4)
#define EMMC_SD_SRS09_DLA      (1U << 2)  /*  2: RO - DAT Line Active */
#define EMMC_SD_SRS09_CIDAT    (1U << 1)  /*  1: RO - Command Inhibit (DAT) */
#define EMMC_SD_SRS09_CICMD    (1U << 0)  /*  0: RO - Command Inhibit (CMD) */

/* SRS10 - Host Control 1 Register (Power/Block-Gap/Wake-Up) */
#define EMMC_SD_SRS10 *((volatile uint32_t*)(EMMC_SD_BASE + 0x228))
#define EMMC_SD_SRS10_WORM (1U << 26) /* 26: RW - Wakeup on Card Removal */
#define EMMC_SD_SRS10_WOIS (1U << 25) /* 25: RW - Wakeup on Card Insertion */
#define EMMC_SD_SRS10_WOIQ (1U << 24) /* 24: RW - Wakeup on Card Interrupt */
#define EMMC_SD_SRS10_IBG  (1U << 19) /* 19: RW - Interrupt at Block Gap */
#define EMMC_SD_SRS10_RWC  (1U << 18) /* 18: RW - Read Wait Control */
#define EMMC_SD_SRS10_CREQ (1U << 17) /* 17: RW - Continue Request */
#define EMMC_SD_SRS10_SBGR (1U << 16) /* 16: RW - Stop at Block Gap Request */
#define EMMC_SD_SRS10_BVS2_SHIFT  13     /* 15:13: RW - SD Bus Voltage Select (VDD2) */
#define EMMC_SD_SRS10_BVS2_MASK   (0x7U << 13)
#define EMMC_SD_SRS10_BP2  (1U << 12) /* 12: RW - Bus Power (VDD2) */
#define EMMC_SD_SRS10_BVS_SHIFT   9      /* 11:9: RW - SD Bus Voltage Select */
#define EMMC_SD_SRS10_BVS_MASK    (0x7U << 9)
#define EMMC_SD_SRS10_BVS_3_3V    (0x7U << 9)  /* SD bus voltage 3.3V */
#define EMMC_SD_SRS10_BVS_3_0V    (0x6U << 9)  /* SD bus voltage 3.0V */
#define EMMC_SD_SRS10_BVS_1_8V    (0x5U << 9)  /* SD bus voltage 1.8V */
#define EMMC_SD_SRS10_BVS_CLR     (0x7U << 9)  /* Clear voltage field mask */
#define EMMC_SD_SRS10_BP   (1U << 8)  /*  8: RW - SD Bus Power */
/* Power-on sequences: combine voltage selection with bus power enable */
#define EMMC_SD_SRS10_PWR_3_3V    (EMMC_SD_SRS10_BVS_3_3V | EMMC_SD_SRS10_BP)
#define EMMC_SD_SRS10_PWR_3_0V    (EMMC_SD_SRS10_BVS_3_0V | EMMC_SD_SRS10_BP)
#define EMMC_SD_SRS10_PWR_1_8V    (EMMC_SD_SRS10_BVS_1_8V | EMMC_SD_SRS10_BP)
#define EMMC_SD_SRS10_CDSS (1U << 7)  /*  7: RW - Card Detect Signal Selection */
#define EMMC_SD_SRS10_CDTL (1U << 6)  /*  6: RW - Card Detect Test Level */
#define EMMC_SD_SRS10_EDTW (1U << 5)  /*  5: RW - Extended Data Transfer Width (8-bit) */
#define EMMC_SD_SRS10_DMASEL_SHIFT    3  /* 4:3: RW - DMA Select */
#define EMMC_SD_SRS10_DMASEL_MASK     (0x3U << 3)
#define EMMC_SD_SRS10_DMA_SDMA   (0x0U << 3)  /* SDMA mode */
#define EMMC_SD_SRS10_DMA_ADMA1  (0x1U << 3)  /* ADMA1 mode */
#define EMMC_SD_SRS10_DMA_ADMA2  (0x2U << 3)  /* ADMA2 mode */
#define EMMC_SD_SRS10_HSE  (1U << 2)  /*  2: RW - High Speed Enable */
#define EMMC_SD_SRS10_DTW  (1U << 1)  /*  1: RW - Data Transfer Width (1=4-bit) */
#define EMMC_SD_SRS10_LEDC (1U << 0)  /*  0: RW - LED Control */

/* SRS11 - Host Control 2 Register (Clock/Timeout/Reset) */
#define EMMC_SD_SRS11 *((volatile uint32_t*)(EMMC_SD_BASE + 0x22C))
#define EMMC_SD_SRS11_SRDA (1U << 26) /* 26: RW - Software Reset for DAT Line */
#define EMMC_SD_SRS11_SRCD (1U << 25) /* 25: RW - Software Reset for CMD Line */
#define EMMC_SD_SRS11_SRFA (1U << 24) /* 24: RW - Software Reset for All */
#define EMMC_SD_SRS11_RESET_DAT_CMD (EMMC_SD_SRS11_SRDA | EMMC_SD_SRS11_SRCD) /* Reset DAT+CMD lines */
#define EMMC_SD_SRS11_DTCV_SHIFT    16   /* 19:16: RW - Data Timeout Counter Value */
#define EMMC_SD_SRS11_DTCV_MASK     (0xFU << 16)
#define EMMC_SD_SRS11_SDCFSL_SHIFT  8    /* 15:8: RW - SDCLK Freq Select (lower 8 bits) */
#define EMMC_SD_SRS11_SDCFSL_MASK   (0xFFU << 8)
#define EMMC_SD_SRS11_SDCFSH_SHIFT  6    /*  7:6: RW - SDCLK Freq Select (upper 2 bits) */
#define EMMC_SD_SRS11_SDCFSH_MASK   (0x3U << 6)
#define EMMC_SD_SRS11_CGS  (1U << 5) /*  5: RW - Clock Generator Select */
#define EMMC_SD_SRS11_SDCE (1U << 2) /*  2: RW - SD Clock Enable */
#define EMMC_SD_SRS11_ICS  (1U << 1) /*  1: RO - Internal Clock Stable */
#define EMMC_SD_SRS11_ICE  (1U << 0) /*  0: RW - Internal Clock Enable */

/* SRS12 - Error/Normal Interrupt Status Register */
#define EMMC_SD_SRS12 *((volatile uint32_t*)(EMMC_SD_BASE + 0x230))
#define EMMC_SD_SRS12_ERSP   (1U << 27) /* 27: RO - Response Error */
#define EMMC_SD_SRS12_ETUNE  (1U << 26) /* 26: RO - Tuning Error */
#define EMMC_SD_SRS12_EADMA  (1U << 25) /* 25: RO - ADMA Error */
#define EMMC_SD_SRS12_EAC    (1U << 24) /* 24: RO - Auto CMD Error */
#define EMMC_SD_SRS12_ECL    (1U << 23) /* 23: RO - Current Limit Error */
#define EMMC_SD_SRS12_EDEB   (1U << 22) /* 22: RO - Data End Bit Error */
#define EMMC_SD_SRS12_EDCRC  (1U << 21) /* 21: RO - Data CRC Error */
#define EMMC_SD_SRS12_EDT    (1U << 20) /* 20: RO - Data Timeout Error */
#define EMMC_SD_SRS12_ECI    (1U << 19) /* 19: RO - Command Index Error */
#define EMMC_SD_SRS12_ECEB   (1U << 18) /* 18: RO - Command End Bit Error */
#define EMMC_SD_SRS12_ECCRC  (1U << 17) /* 17: RO - Command CRC Error */
#define EMMC_SD_SRS12_ECT    (1U << 16) /* 16: RO - Command Timeout Error */
#define EMMC_SD_SRS12_EINT   (1U << 15) /* 15: RO - Error Interrupt */
#define EMMC_SD_SRS12_CQI    (1U << 14) /* 14: RO - Command Queuing Interrupt */
#define EMMC_SD_SRS12_CINT   (1U << 8)  /*  8: RO - Card Interrupt */
#define EMMC_SD_SRS12_CR     (1U << 7)  /*  7: RW - Card Removal */
#define EMMC_SD_SRS12_CIN    (1U << 6)  /*  6: RW - Card Insertion */
#define EMMC_SD_SRS12_BRR    (1U << 5)  /*  5: RO - Buffer Read Ready */
#define EMMC_SD_SRS12_BWR    (1U << 4)  /*  4: RO - Buffer Write Ready */
#define EMMC_SD_SRS12_DMAINT (1U << 3)  /*  3: RW - DMA Interrupt */
#define EMMC_SD_SRS12_BGE    (1U << 2)  /*  2: RW - Block Gap Event */
#define EMMC_SD_SRS12_TC     (1U << 1)  /*  1: RW - Transfer Complete */
#define EMMC_SD_SRS12_CC     (1U << 0)  /*  0: RW - Command Complete */
/* Interrupt status masks */
#define EMMC_SD_SRS12_NORM_STAT   0x00007FFFU                       /* Normal interrupt status mask */
#define EMMC_SD_SRS12_ERR_STAT    0xFFFF8000U                       /* Error interrupt status mask */
#define EMMC_SD_SRS12_CMD_ERR     (EMMC_SD_SRS12_ECT | EMMC_SD_SRS12_ECCRC | \
                                   EMMC_SD_SRS12_ECEB | EMMC_SD_SRS12_ECI)

#define EMMC_SD_SRS13 *((volatile uint32_t*)(EMMC_SD_BASE + 0x234)) /* Error/Normal Status Enable */
#define EMMC_SD_SRS13_ERSP_SE   (1U << 27) /* 27: RW - Response Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ETUNE_SE  (1U << 26) /* 26: RW - Tuning Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_EADMA_SE  (1U << 25) /* 25: RW - ADMA Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_EAC_SE    (1U << 24) /* 24: RW - Auto CMD Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ECL_SE    (1U << 23) /* 23: RW - Current Limit Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_EDEB_SE   (1U << 22) /* 22: RW - Data End Bit Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_EDCRC_SE  (1U << 21) /* 21: RW - Data CRC Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_EDT_SE    (1U << 20) /* 20: RW - Data Timeout Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ECI_SE    (1U << 19) /* 19: RW - Command Index Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ECEB_SE   (1U << 18) /* 18: RW - Command End Bit Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ECCRC_SE  (1U << 17) /* 17: RW - Command CRC Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_ECT_SE    (1U << 16) /* 16: RW - Command Timeout Error Status Enable (SD mode only) */
#define EMMC_SD_SRS13_CQINT_SE  (1U << 14) /* 14: RW - Command Queuing Status Enable */
#define EMMC_SD_SRS13_RTUNE_SE  (1U << 12) /* 12: RW - Retuning event status enable */
#define EMMC_SD_SRS13_INT_ONC   (1U << 11) /* 11: RW - Interrupt On Line C status enable */
#define EMMC_SD_SRS13_INT_ONB   (1U << 10) /* 10: RW - Interrupt On Line B status enable */
#define EMMC_SD_SRS13_INT_ONA   (1U << 9)  /*  9: RW - Interrupt On Line A status enable */
#define EMMC_SD_SRS13_CINT_SE   (1U << 8)  /*  8: RW - Card Interrupt Status Enable */
#define EMMC_SD_SRS13_CR_SE     (1U << 7)  /*  7: RW - Card Removal Status Enable */
#define EMMC_SD_SRS13_CIN_SE    (1U << 6)  /*  6: RW - Card Insertion Status Enable */
#define EMMC_SD_SRS13_BRR_SE    (1U << 5)  /*  5: RW - Buffer Read Ready Status Enable */
#define EMMC_SD_SRS13_BWR_SE    (1U << 4)  /*  4: RW - Buffer Write Ready Status Enable */
#define EMMC_SD_SRS13_DMAINT_SE (1U << 3)  /*  3: RW - DMA Interrupt Status Enable */
#define EMMC_SD_SRS13_BGE_SE    (1U << 2)  /*  2: RW - Block Gap Event Status Enable */
#define EMMC_SD_SRS13_TC_SE     (1U << 1)  /*  1: RW - Transfer Complete Status Enable */
#define EMMC_SD_SRS13_CC_SE     (1U << 0)  /*  0: RW - Command Complete Status Enable */

#define EMMC_SD_SRS14 *((volatile uint32_t*)(EMMC_SD_BASE + 0x238)) /* Error/Normal Signal Enable */
#define EMMC_SD_SRS14_ERSP_IE   (1U << 27) /* 27: RW - Response Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_EADMA_IE  (1U << 25) /* 25: RW - ADMA Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_EAC_IE    (1U << 24) /* 24: RW - Auto CMD Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_ECL_IE    (1U << 23) /* 23: RW - Current Limit Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_EDEB_IE   (1U << 22) /* 22: RW - Data End Bit Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_EDCRC_IE  (1U << 21) /* 21: RW - Data CRC Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_EDT_IE    (1U << 20) /* 20: RW - Data Timeout Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_ECI_IE    (1U << 19) /* 19: RW - Command Index Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_ECEB_IE   (1U << 18) /* 18: RW - Command End Bit Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_ECCRC_IE  (1U << 17) /* 17: RW - Command CRC Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_ECT_IE    (1U << 16) /* 16: RW - Command Timeout Error Interrupt Enable (SD mode only) */
#define EMMC_SD_SRS14_CQINT_IE  (1U << 14) /* 14: RW - Command Queuing Interrupt Enable */
#define EMMC_SD_SRS14_CINT_IE   (1U << 8)  /*  8: RW - Card Interrupt Interrupt Enable */
#define EMMC_SD_SRS14_CR_IE     (1U << 7)  /*  7: RW - Card Removal Interrupt Enable */
#define EMMC_SD_SRS14_CIN_IE    (1U << 6)  /*  6: RW - Card Insertion Interrupt Enable */
#define EMMC_SD_SRS14_BRR_IE    (1U << 5)  /*  5: RW - Buffer Read Ready Interrupt Enable */
#define EMMC_SD_SRS14_BWR_IE    (1U << 4)  /*  4: RW - Buffer Write Ready Interrupt Enable */
#define EMMC_SD_SRS14_DMAINT_IE (1U << 3)  /*  3: RW - DMA Interrupt Interrupt Enable */
#define EMMC_SD_SRS14_BGE_IE    (1U << 2)  /*  2: RW - Block Gap Event Interrupt Enable */
#define EMMC_SD_SRS14_TC_IE     (1U << 1)  /*  1: RW - Transfer Complete Interrupt Enable */
#define EMMC_SD_SRS14_CC_IE     (1U << 0)  /*  0: RW - Command Complete Interrupt Enable */

/* SRS15 - Host Control #2 / Auto CMD Error Status Register */
#define EMMC_SD_SRS15 *((volatile uint32_t*)(EMMC_SD_BASE + 0x23C))
#define EMMC_SD_SRS15_PVE   (1U << 31) /* 31: RW - Preset Value Enable */
#define EMMC_SD_SRS15_AIE   (1U << 30) /* 30: RW - Async Interrupt Enable */
#define EMMC_SD_SRS15_A64   (1U << 29) /* 29: RW - 64-bit Addressing */
#define EMMC_SD_SRS15_HV4E  (1U << 28) /* 28: RW - Host Version 4 Enable */
#define EMMC_SD_SRS15_UHSII (1U << 24) /* 24: RW - UHS-II Interface Enable */
#define EMMC_SD_SRS15_SCS   (1U << 23) /* 23: RW - Sampling Clock Select */
#define EMMC_SD_SRS15_EXTNG (1U << 22) /* 22: RW - Execute Tuning */
#define EMMC_SD_SRS15_DSS_SHIFT    20   /* 21:20: RW - Driver Strength Select */
#define EMMC_SD_SRS15_DSS_MASK     (0x3U << 20)
#define EMMC_SD_SRS15_DSS_TYPE_B   (0x0U << 20) /* Driver Type B (default) */
#define EMMC_SD_SRS15_DSS_TYPE_A   (0x1U << 20) /* Driver Type A */
#define EMMC_SD_SRS15_DSS_TYPE_C   (0x2U << 20) /* Driver Type C */
#define EMMC_SD_SRS15_DSS_TYPE_D   (0x3U << 20) /* Driver Type D */
#define EMMC_SD_SRS15_V18SE        (1U << 19) /* 19: RW - 1.8V Signaling Enable */
#define EMMC_SD_SRS15_UMS_SHIFT    16   /* 18:16: RW - UHS Mode Select */
#define EMMC_SD_SRS15_UMS_MASK     (0x7U << 16)
#define EMMC_SD_SRS15_UMS_SDR12    (0x0U << 16) /* SDR12 mode */
#define EMMC_SD_SRS15_UMS_SDR25    (0x1U << 16) /* SDR25 mode (High Speed) */
#define EMMC_SD_SRS15_UMS_SDR50    (0x2U << 16) /* SDR50 mode */
#define EMMC_SD_SRS15_UMS_SDR104   (0x3U << 16) /* SDR104 mode */
#define EMMC_SD_SRS15_UMS_DDR50    (0x4U << 16) /* DDR50 mode */
#define EMMC_SD_SRS15_UMS_UHSII    (0x7U << 16) /* UHS-II mode */
/* Auto CMD Error Status bits (lower byte) */
#define EMMC_SD_SRS15_CNIACE (1U << 7)  /*  7: RO - CMD Not Issued by Auto CMD12 */
#define EMMC_SD_SRS15_ACIE   (1U << 4)  /*  4: RO - Auto CMD12 Index Error */
#define EMMC_SD_SRS15_ACEBE  (1U << 3)  /*  3: RO - Auto CMD12 End Bit Error */
#define EMMC_SD_SRS15_ACCE   (1U << 2)  /*  2: RO - Auto CMD12 CRC Error */
#define EMMC_SD_SRS15_ACTE   (1U << 1)  /*  1: RO - Auto CMD12 Timeout Error */
#define EMMC_SD_SRS15_ACNE   (1U << 0)  /*  0: RO - Auto CMD12 Not Executed */

/* SRS16 - Capabilities Register #1 */
#define EMMC_SD_SRS16 *((volatile uint32_t*)(EMMC_SD_BASE + 0x240))
#define EMMC_SD_SRS16_SLT_SHIFT     30   /* 31:30: RO - Slot Type */
#define EMMC_SD_SRS16_SLT_MASK      (0x3U << 30)
#define EMMC_SD_SRS16_AIS    (1U << 29) /* 29: RO - Async Interrupt Support */
#define EMMC_SD_SRS16_A64S   (1U << 28) /* 28: RO - 64-bit System Bus Support */
#define EMMC_SD_SRS16_VS18   (1U << 26) /* 26: RO - Voltage Support 1.8V */
#define EMMC_SD_SRS16_VS30   (1U << 25) /* 25: RO - Voltage Support 3.0V */
#define EMMC_SD_SRS16_VS33   (1U << 24) /* 24: RO - Voltage Support 3.3V */
#define EMMC_SD_SRS16_SRS    (1U << 23) /* 23: RO - Suspend/Resume Support */
#define EMMC_SD_SRS16_DMAS   (1U << 22) /* 22: RO - SDMA Support */
#define EMMC_SD_SRS16_HSS    (1U << 21) /* 21: RO - High Speed Support */
#define EMMC_SD_SRS16_ADMA1S (1U << 20) /* 20: RO - ADMA1 Support */
#define EMMC_SD_SRS16_ADMA2S (1U << 19) /* 19: RO - ADMA2 Support */
#define EMMC_SD_SRS16_EDS8   (1U << 18) /* 18: RO - 8-bit Data Support */
#define EMMC_SD_SRS16_MBL_SHIFT     16   /* 17:16: RO - Max Block Length */
#define EMMC_SD_SRS16_MBL_MASK      (0x3U << 16)
#define EMMC_SD_SRS16_MBL_512      (0x0U << 16) /* Max 512 bytes */
#define EMMC_SD_SRS16_MBL_1024     (0x1U << 16) /* Max 1024 bytes */
#define EMMC_SD_SRS16_MBL_2048     (0x2U << 16) /* Max 2048 bytes */
#define EMMC_SD_SRS16_BCSDCLK_SHIFT 8    /* 15:8: RO - Base Clock for SD Clock (in MHz) */
#define EMMC_SD_SRS16_BCSDCLK_MASK  (0xFFU << 8)
#define EMMC_SD_SRS16_TCU    (1U << 7)  /*  7: RO - Timeout Clock Unit (1=MHz) */
#define EMMC_SD_SRS16_TCF_SHIFT     0    /* 5:0: RO - Timeout Clock Frequency */
#define EMMC_SD_SRS16_TCF_MASK      (0x3FU << 0)

/* SRS17 - Capabilities Register #2 */
#define EMMC_SD_SRS17 *((volatile uint32_t*)(EMMC_SD_BASE + 0x244))
#define EMMC_SD_SRS17_VDD2S  (1U << 28) /* 28: RO - VDD2 Support */
#define EMMC_SD_SRS17_CLKMPR_SHIFT  16   /* 23:16: RO - Clock Multiplier */
#define EMMC_SD_SRS17_CLKMPR_MASK   (0xFFU << 16)
#define EMMC_SD_SRS17_RTNGM_SHIFT   14   /* 15:14: RO - Re-Tuning Modes */
#define EMMC_SD_SRS17_RTNGM_MASK    (0x3U << 14)
#define EMMC_SD_SRS17_RTNGM_MODE1  (0x0U << 14) /* Re-tuning mode 1 */
#define EMMC_SD_SRS17_RTNGM_MODE2  (0x1U << 14) /* Re-tuning mode 2 */
#define EMMC_SD_SRS17_RTNGM_MODE3  (0x2U << 14) /* Re-tuning mode 3 */
#define EMMC_SD_SRS17_TSDR50 (1U << 13) /* 13: RO - Tuning for SDR50 */
#define EMMC_SD_SRS17_TCRT_SHIFT    8    /* 11:8: RO - Timer Count for Re-Tuning */
#define EMMC_SD_SRS17_TCRT_MASK     (0xFU << 8)
#define EMMC_SD_SRS17_UHSII  (1U << 8)  /*  8: RO - UHS-II Support */
#define EMMC_SD_SRS17_DRVD   (1U << 6)  /*  6: RO - Driver Type D Support */
#define EMMC_SD_SRS17_DRVC   (1U << 5)  /*  5: RO - Driver Type C Support */
#define EMMC_SD_SRS17_DRVA   (1U << 4)  /*  4: RO - Driver Type A Support */
#define EMMC_SD_SRS17_DDR50  (1U << 2)  /*  2: RO - DDR50 Support */
#define EMMC_SD_SRS17_SDR104 (1U << 1)  /*  1: RO - SDR104 Support */
#define EMMC_SD_SRS17_SDR50  (1U << 0)  /*  0: RO - SDR50 Support */

#define EMMC_SD_SRS18 *((volatile uint32_t*)(EMMC_SD_BASE + 0x248)) /* Capabilities #3 */
#define EMMC_SD_SRS18_MC18_SHIFT 16      /* 23:16: RO - Maximym current for 1.8v */
#define EMMC_SD_SRS18_MC18_MASK  (0xFFU << 16)
#define EMMC_SD_SRS18_MC30_SHIFT 8       /* 15:8: RO - Maximym current for 3.0v */
#define EMMC_SD_SRS18_MC30_MASK  (0xFFU << 8)
#define EMMC_SD_SRS18_MC33_SHIFT 0       /* 7:0: RO - Maximym current for 3.3v */
#define EMMC_SD_SRS18_MC33_MASK  (0xFFU << 0)

#define EMMC_SD_SRS19 *((volatile uint32_t*)(EMMC_SD_BASE + 0x24C)) /* Capabilities #4 */
#define EMMC_SD_SRS19_MC18V2_SHIFT 0     /* 7:0: RO */
#define EMMC_SD_SRS19_MC18V2_MASK  (0xFFU << 0)

#define EMMC_SD_SRS20 *((volatile uint32_t*)(EMMC_SD_BASE + 0x250)) /* Force Event */
#define EMMC_SD_SRS20_ERESP_FE (1U << 27) /* 27: WO - Force Response Error */
#define EMMC_SD_SRS20_ETUNE_FE (1U << 26) /* 26: WO - Force Tuning Error */
#define EMMC_SD_SRS20_EADMA_FE (1U << 25) /* 25: WO - Force ADMA Error */
#define EMMC_SD_SRS20_EAC_FE   (1U << 24) /* 24: WO - Force Auto CMD Error */
#define EMMC_SD_SRS20_ECL_FE   (1U << 23) /* 23: WO - Force Current Limit Error */
#define EMMC_SD_SRS20_EDEB_FE  (1U << 22) /* 22: WO - Force Data End Bit Error */
#define EMMC_SD_SRS20_EDCRC_FE (1U << 21) /* 21: WO - Force Data CRC Error */
#define EMMC_SD_SRS20_EDT_FE   (1U << 20) /* 20: WO - Force Data Timeout Error */
#define EMMC_SD_SRS20_ECI_FE   (1U << 19) /* 19: WO - Force Command Index Error */
#define EMMC_SD_SRS20_ECEB_FE  (1U << 18) /* 18: WO - Force Command End Bit Error */
#define EMMC_SD_SRS20_ECCRC_FE (1U << 17) /* 17: WO - Force Command CRC Error */
#define EMMC_SD_SRS20_ECT_FE   (1U << 16) /* 16: WO - Force Command Timeout Error */

#define EMMC_SD_SRS21 *((volatile uint32_t*)(EMMC_SD_BASE + 0x254)) /* ADMA Error Status */
#define EMMC_SD_SRS21_EADMAL (1U << 2)  /*  2: RO - ADMA Length Mismatch Error */
#define EMMC_SD_SRS21_EADMAS_SHIFT  0    /* 1:0: RO */
#define EMMC_SD_SRS21_EADMAS_MASK   (0x3U << 0)

#define EMMC_SD_SRS22 *((volatile uint32_t*)(EMMC_SD_BASE + 0x258)) /* ADMA System Address 1 */
#define EMMC_SD_SRS22_DMASA1_SHIFT 0     /* 31:0: RW */
#define EMMC_SD_SRS22_DMASA1_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS23 *((volatile uint32_t*)(EMMC_SD_BASE + 0x25C)) /* ADMA System Address 2 */
#define EMMC_SD_SRS23_DMASA2_SHIFT 0     /* 31:0: RW */
#define EMMC_SD_SRS23_DMASA2_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_SRS24 *((volatile uint32_t*)(EMMC_SD_BASE + 0x260)) /* Preset Value (Default Speed) */
#define EMMC_SD_SRS25 *((volatile uint32_t*)(EMMC_SD_BASE + 0x264)) /* Preset Value (High Speed/SDR12) */
#define EMMC_SD_SRS26 *((volatile uint32_t*)(EMMC_SD_BASE + 0x268)) /* Preset Value (SDR25/SDR50) */
#define EMMC_SD_SRS27 *((volatile uint32_t*)(EMMC_SD_BASE + 0x26C)) /* Preset Value (SDR104/DDR50) */
#define EMMC_SD_SRS29 *((volatile uint32_t*)(EMMC_SD_BASE + 0x274)) /* Preset Value for UHS-II */

/* ----------------------------------------------------------------------------
 * CQRS - Command Queuing Register Set (eMMC 5.1 Command Queuing)
 * ---------------------------------------------------------------------------- */

/* CQRS00 - Command Queuing Version Register */
#define EMMC_SD_CQRS00 *((volatile uint32_t*)(EMMC_SD_BASE + 0x400))
#define EMMC_SD_CQRS00_CQVN1_SHIFT 8     /* 11:8: RO - CQ Version (major) */
#define EMMC_SD_CQRS00_CQVN1_MASK  (0xFU << 8)
#define EMMC_SD_CQRS00_CQVN2_SHIFT 4     /* 7:4: RO - CQ Version (minor) */
#define EMMC_SD_CQRS00_CQVN2_MASK  (0xFU << 4)
#define EMMC_SD_CQRS00_CQVN3_SHIFT 0     /* 3:0: RO - CQ Version (suffix) */
#define EMMC_SD_CQRS00_CQVN3_MASK  (0xFU << 0)

/* CQRS01 - Command Queuing Capabilities Register */
#define EMMC_SD_CQRS01 *((volatile uint32_t*)(EMMC_SD_BASE + 0x404))
#define EMMC_SD_CQRS01_ITCFMUL_SHIFT 12  /* 15:12: RO - Internal Timer Clock Freq Multiplier */
#define EMMC_SD_CQRS01_ITCFMUL_MASK  (0xFU << 12)
#define EMMC_SD_CQRS01_ITCFVAL_SHIFT 0   /* 9:0: RO - Internal Timer Clock Freq Value */
#define EMMC_SD_CQRS01_ITCFVAL_MASK  (0x3FFU << 0)

/* CQRS02 - Command Queuing Configuration Register */
#define EMMC_SD_CQRS02 *((volatile uint32_t*)(EMMC_SD_BASE + 0x408))
#define EMMC_SD_CQRS02_CQDCE    (1U << 12) /* 12: RW - Direct Command (DCMD) Enable */
#define EMMC_SD_CQRS02_CQTDS    (1U << 8)  /*  8: RW - Task Desc Size (0=64bit, 1=128bit) */
#define EMMC_SD_CQRS02_CQTDS_64    (0x0U << 8) /* Task descriptor 64 bits */
#define EMMC_SD_CQRS02_CQTDS_128   (0x1U << 8) /* Task descriptor 128 bits */
#define EMMC_SD_CQRS02_CQE      (1U << 0)  /*  0: RW - Command Queuing Enable */

/* CQRS03 - Command Queuing Control Register */
#define EMMC_SD_CQRS03 *((volatile uint32_t*)(EMMC_SD_BASE + 0x40C))
#define EMMC_SD_CQRS03_CQHLT (1U << 0)  /*  0: RW - Halt CQ Engine */

/* CQRS04 - Command Queuing Interrupt Status Register */
#define EMMC_SD_CQRS04 *((volatile uint32_t*)(EMMC_SD_BASE + 0x410))
#define EMMC_SD_CQRS04_CQTCL (1U << 3)  /*  3: RW - Task Cleared Interrupt */
#define EMMC_SD_CQRS04_CQRED (1U << 2)  /*  2: RW - Response Error Detected */
#define EMMC_SD_CQRS04_CQTCC (1U << 1)  /*  1: RW - Task Complete Interrupt */
#define EMMC_SD_CQRS04_CQHAC (1U << 0)  /*  0: RW - Halt Complete Interrupt */

/* CQRS05 - Command Queuing Interrupt Status Enable Register */
#define EMMC_SD_CQRS05 *((volatile uint32_t*)(EMMC_SD_BASE + 0x414))
#define EMMC_SD_CQRS05_CQTCLST (1U << 3)  /*  3: RW - Task Cleared Status Enable */
#define EMMC_SD_CQRS05_CQREDST (1U << 2)  /*  2: RW - Response Error Status Enable */
#define EMMC_SD_CQRS05_CQTCCST (1U << 1)  /*  1: RW - Task Complete Status Enable */
#define EMMC_SD_CQRS05_CQHACST (1U << 0)  /*  0: RW - Halt Complete Status Enable */

/* CQRS06 - Command Queuing Interrupt Signal Enable Register */
#define EMMC_SD_CQRS06 *((volatile uint32_t*)(EMMC_SD_BASE + 0x418))
#define EMMC_SD_CQRS06_CQTCLSI (1U << 3)  /*  3: RW - Task Cleared Signal Enable */
#define EMMC_SD_CQRS06_CQREDSI (1U << 2)  /*  2: RW - Response Error Signal Enable */
#define EMMC_SD_CQRS06_CQTCCSI (1U << 1)  /*  1: RW - Task Complete Signal Enable */
#define EMMC_SD_CQRS06_CQHACSI (1U << 0)  /*  0: RW - Halt Complete Signal Enable */

/* CQRS07 - Interrupt Coalescing Register */
#define EMMC_SD_CQRS07 *((volatile uint32_t*)(EMMC_SD_BASE + 0x41C))
#define EMMC_SD_CQRS07_CQICED     (1U << 31) /* 31: RW - Int Coalescing Enable/Disable */
#define EMMC_SD_CQRS07_CQICSB     (1U << 20) /* 20: RO - Int Coalescing Status Bit */
#define EMMC_SD_CQRS07_CQICCTR    (1U << 16) /* 16: WO - Counter and Timer Reset */
#define EMMC_SD_CQRS07_CQICCTHWEN (1U << 15) /* 15: WO - Counter Threshold Write Enable */
#define EMMC_SD_CQRS07_CQICCTH_SHIFT    8   /* 12:8: RW - Int Coalescing Counter Threshold */
#define EMMC_SD_CQRS07_CQICCTH_MASK     (0x1FU << 8)
#define EMMC_SD_CQRS07_CQICTOVALEN (1U << 7) /*  7: WO - Timeout Value Write Enable */
#define EMMC_SD_CQRS07_CQICTOVAL_SHIFT  0   /* 6:0: RW - Int Coalescing Timeout Value */
#define EMMC_SD_CQRS07_CQICTOVAL_MASK   (0x7FU << 0)

#define EMMC_SD_CQRS08 *((volatile uint32_t*)(EMMC_SD_BASE + 0x420)) /* Task Descriptor List Base Address */
#define EMMC_SD_CQRS08_CQTDLBA_SHIFT 0   /* 31:0: RW */
#define EMMC_SD_CQRS08_CQTDLBA_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_CQRS09 *((volatile uint32_t*)(EMMC_SD_BASE + 0x424)) /* Task Descriptor List Base Address Upper */
#define EMMC_SD_CQRS09_CQTDLBAU_SHIFT 0  /* 31:0: RW */
#define EMMC_SD_CQRS09_CQTDLBAU_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_CQRS10 *((volatile uint32_t*)(EMMC_SD_BASE + 0x428)) /* Command Queuing Task Doorbell */
#define EMMC_SD_CQRS11 *((volatile uint32_t*)(EMMC_SD_BASE + 0x42C)) /* Task Complete Notification */
#define EMMC_SD_CQRS12 *((volatile uint32_t*)(EMMC_SD_BASE + 0x430)) /* Device Queue Status */
#define EMMC_SD_CQRS12_CQDQS_SHIFT 0     /* 31:0: RO */
#define EMMC_SD_CQRS12_CQDQS_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_CQRS13 *((volatile uint32_t*)(EMMC_SD_BASE + 0x434)) /* Device Pending Tasks */
#define EMMC_SD_CQRS14 *((volatile uint32_t*)(EMMC_SD_BASE + 0x438)) /* Task Clear */

#define EMMC_SD_CQRS16 *((volatile uint32_t*)(EMMC_SD_BASE + 0x440)) /* Send Status Configuration 1 */
#define EMMC_SD_CQRS16_CQSSCBC_SHIFT 16  /* 19:16: RW */
#define EMMC_SD_CQRS16_CQSSCBC_MASK  (0xFU << 16)
#define EMMC_SD_CQRS16_CQSSCIT_SHIFT 0   /* 15:0: RW */
#define EMMC_SD_CQRS16_CQSSCIT_MASK  (0xFFFFU << 0)

#define EMMC_SD_CQRS17 *((volatile uint32_t*)(EMMC_SD_BASE + 0x444)) /* Send Status Configuration 2 */
#define EMMC_SD_CQRS17_CQSQSR_SHIFT 0    /* 15:0: RW */
#define EMMC_SD_CQRS17_CQSQSR_MASK  (0xFFFFU << 0)

#define EMMC_SD_CQRS18 *((volatile uint32_t*)(EMMC_SD_BASE + 0x448)) /* Command Response for Direct-Command */
#define EMMC_SD_CQRS18_CQDCLR_SHIFT 0    /* 31:0: RO */
#define EMMC_SD_CQRS18_CQDCLR_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_CQRS20 *((volatile uint32_t*)(EMMC_SD_BASE + 0x450)) /* Response Mode Error Mask */
#define EMMC_SD_CQRS20_CQRMEM_SHIFT 0    /* 31:0: RW */
#define EMMC_SD_CQRS20_CQRMEM_MASK  (0xFFFFFFFFU << 0)

#define EMMC_SD_CQRS21 *((volatile uint32_t*)(EMMC_SD_BASE + 0x454)) /* Task Error Information */
#define EMMC_SD_CQRS21_CQDTEFV (1U << 31) /* 31: RO */
#define EMMC_SD_CQRS21_CQDTETID_SHIFT 24 /* 28:24: RO */
#define EMMC_SD_CQRS21_CQDTETID_MASK  (0x1FU << 24)
#define EMMC_SD_CQRS21_CQDTECI_SHIFT 16  /* 21:16: RO */
#define EMMC_SD_CQRS21_CQDTECI_MASK  (0x3FU << 16)
#define EMMC_SD_CQRS21_CQRMEFV (1U << 15) /* 15: RO */
#define EMMC_SD_CQRS21_CQRMETID_SHIFT 8  /* 12:8: RO */
#define EMMC_SD_CQRS21_CQRMETID_MASK  (0x1FU << 8)
#define EMMC_SD_CQRS21_CQRMECI_SHIFT 0   /* 5:0: RO */
#define EMMC_SD_CQRS21_CQRMECI_MASK  (0x3FU << 0)

#define EMMC_SD_CQRS22 *((volatile uint32_t*)(EMMC_SD_BASE + 0x458)) /* Command Response Index */
#define EMMC_SD_CQRS22_CQLCRI_SHIFT 0    /* 5:0: RO */
#define EMMC_SD_CQRS22_CQLCRI_MASK  (0x3FU << 0)

#define EMMC_SD_CQRS23 *((volatile uint32_t*)(EMMC_SD_BASE + 0x45C)) /* Command Response Argument */
#define EMMC_SD_CQRS23_CQLCRA_SHIFT 0    /* 31:0: RO */
#define EMMC_SD_CQRS23_CQLCRA_MASK  (0xFFFFFFFFU << 0)

/* ----------------------------------------------------------------------------
 * EMMC/SD - Common Constants and Helper Macros
 * ---------------------------------------------------------------------------- */

/* Clock Frequencies (in KHz) */
#define EMMC_SD_CLK_400KHZ   400U       /* Initial identification clock */
#define EMMC_SD_CLK_25MHZ    25000U     /* Default Speed / SDR12 */
#define EMMC_SD_CLK_50MHZ    50000U     /* High Speed / SDR25 */
#define EMMC_SD_CLK_100MHZ   100000U    /* SDR50 */
#define EMMC_SD_CLK_200MHZ   200000U    /* SDR104 / HS200 / HS400 */

/* Block Size */
#define EMMC_SD_BLOCK_SIZE   512U       /* Standard block size */

/* SD Interface Conditions */
#define IF_COND_27V_33V        (1U << 8)

/* SD OCR register bits definitions */
#define SDCARD_REG_OCR_2_7_2_8 (1U << 15)
#define SDCARD_REG_OCR_2_8_2_9 (1U << 16)
#define SDCARD_REG_OCR_2_9_3_0 (1U << 17)
#define SDCARD_REG_OCR_3_0_3_1 (1U << 18)
#define SDCARD_REG_OCR_3_1_3_2 (1U << 19)
#define SDCARD_REG_OCR_3_2_3_3 (1U << 20)
#define SDCARD_REG_OCR_3_3_3_4 (1U << 21)
#define SDCARD_REG_OCR_3_4_3_5 (1U << 22)
#define SDCARD_REG_OCR_3_5_3_6 (1U << 23)
#define SDCARD_REG_OCR_S18RA   (1U << 24) /* Switching to 1.8V request/accept bit */
#define SDCARD_REG_OCR_XPC     (1U << 28) /* eXtended Power Control (XPC) bit */
#define SDCARD_REG_OCR_CCS     (1U << 30) /* Card Capacity Status bit */
#define SDCARD_REG_OCR_READY   (1U << 31) /* OCR ready bit */

#define SDCARD_ACMD41_HCS      (1U << 30) /* High Capacity Support bit */

/* Common MMC/SD Commands (for use with SRS03) */
#define MMC_CMD0_GO_IDLE          0    /* Reset card to idle state */
#define MMC_CMD1_SEND_OP_COND     1    /* MMC: Send operating conditions */
#define MMC_CMD2_ALL_SEND_CID     2    /* Get card identification */
#define MMC_CMD3_SET_REL_ADDR     3    /* MMC: Set relative address */
#define SD_CMD3_SEND_REL_ADDR     3    /* SD: Get relative address */
#define MMC_CMD_4_SET_DSR         4
#define SD_CMD_6_SWITCH_FUNC      6    /* SD: Switch function */
#define MMC_CMD7_SELECT_CARD      7    /* Select/deselect card */
#define MMC_CMD8_SEND_EXT_CSD     8    /* MMC: Get EXT_CSD */
#define SD_CMD8_SEND_IF_COND      8    /* SD: Send interface condition */
#define MMC_CMD9_SEND_CSD         9    /* Get card-specific data */
#define SD_CMD11_VOLAGE_SWITCH    11   /* R1 Rsp        */
#define MMC_CMD12_STOP_TRANS      12   /* Stop transmission */
#define MMC_CMD13_SEND_STATUS     13   /* Get card status */
#define MMC_CMD_15_GOTO_INACT_ST  15
#define SD_CMD_16                 16   /* R1 Rsp        */
#define MMC_CMD17_READ_SINGLE     17   /* Read single block */
#define MMC_CMD18_READ_MULTIPLE   18   /* Read multiple blocks */
#define MMC_CMD24_WRITE_SINGLE    24   /* Write single block */
#define MMC_CMD25_WRITE_MULTIPLE  25   /* Write multiple blocks */

#define SD_CMD55_APP_CMD          55   /* Prefix for ACMD */
#define SD_ACMD6_SET_BUS_WIDTH    6    /* SD: Set bus width */
#define SD_ACMD41_SEND_OP_COND    41   /* SD: Send operating conditions */
#define SD_ACMD51_SEND_SCR        51   /* SD: Get SCR register */

/* Debouncing time for card detect */
#define EMMC_SD_DEBOUNCE_TIME     0x300000U

/* Timeout values */
#define EMMC_SD_DATA_TIMEOUT_US   500000U  /* 500ms data timeout */
#define EMMC_SD_CMD_TIMEOUT_MS    3000U    /* 3s command timeout */

#define WOLFBOOT_CARDTYPE_SD   1
#define WOLFBOOT_CARDTYPE_EMMC 2

#define WOLFBOOT_CARDTYPE WOLFBOOT_CARDTYPE_SD

#define MAX_CURRENT_MA 150 /* mA */

#define SD_RCA_SHIFT 16

#define SCR_REG_DATA_SIZE 8

/* Switch Function Command Arguments */
#define SDCARD_SWITCH_FUNC_MODE_SWITCH       (0x1u << 31) /* Set function mode */
#define SDCARD_SWITCH_FUNC_MODE_CHECK        (0x0u << 31) /* Check mode */
/* group 1 - function 1 */
#define SDCARD_SWITCH_ACCESS_MODE_SDR12      0x0U /* Card access mode - SDR12 default */
#define SDCARD_SWITCH_ACCESS_MODE_SDR25      0x1U /* Card access mode - SDR25 high speed */
#define SDCARD_SWITCH_ACCESS_MODE_SDR50      0x2U /* Card access mode - SDR50 */
#define SDCARD_SWITCH_ACCESS_MODE_SDR104     0x3U /* Card access mode - SDR104 */
#define SDCARD_SWITCH_ACCESS_MODE_DDR50      0x4U /* Card access mode - DDR50 */

#define SDCARD_SWITCH_DRIVER_STRENGTH_TYPE_B 0x0U /* Card driver strength - Type B default */
#define SDCARD_SWITCH_DRIVER_STRENGTH_TYPE_A 0x1U /* Card driver strength - Type A */
#define SDCARD_SWITCH_DRIVER_STRENGTH_TYPE_C 0x2U /* Card driver strength - Type C */
#define SDCARD_SWITCH_DRIVER_STRENGTH_TYPE_D 0x3U /* Card driver strength - Type D */


/* Crypto Engine: Athena F5200 TeraFire Crypto Processor (1x), 200 MHz */
#define ATHENA_BASE (SYSREG_BASE + 0x125000)


#endif /* MPFS250_DEF_INCLUDED */

