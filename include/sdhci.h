/* sdhci.h
 *
 * Cadence SD Host Controller Interface (SDHCI) Driver
 * Generic implementation supporting SD cards and eMMC.
 *
 * Compile with DISK_SDCARD=1 or DISK_EMMC=1
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

#ifndef SDHCI_H
#define SDHCI_H

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

#include <stdint.h>

/* ============================================================================
 * Configuration (override in target .config or platform header)
 * ============================================================================ */

/* Block size */
#ifndef SDHCI_BLOCK_SIZE
#define SDHCI_BLOCK_SIZE        512
#endif

/* DMA threshold - minimum transfer size to use DMA mode (default: 512KB) */
#ifndef SDHCI_DMA_THRESHOLD
#define SDHCI_DMA_THRESHOLD     (512U * 1024U)
#endif

/* Disk test block address (platform should override) */
#ifndef DISK_TEST_BLOCK_ADDR
#define DISK_TEST_BLOCK_ADDR    149504  /* ~76MB offset */
#endif

/* Auto-select DMA buffer boundary based on threshold */
#if (SDHCI_DMA_THRESHOLD > (256U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_512KB
    #if (SDHCI_DMA_THRESHOLD != (512U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 512KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (128U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_256KB
    #if (SDHCI_DMA_THRESHOLD != (256U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 256KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (64U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_128KB
    #if (SDHCI_DMA_THRESHOLD != (128U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 128KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (32U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_64KB
    #if (SDHCI_DMA_THRESHOLD != (64U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 64KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (16U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_32KB
    #if (SDHCI_DMA_THRESHOLD != (32U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 32KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (8U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_16KB
    #if (SDHCI_DMA_THRESHOLD != (16U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 16KB"
    #endif
#elif (SDHCI_DMA_THRESHOLD > (4U * 1024U))
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_8KB
    #if (SDHCI_DMA_THRESHOLD != (8U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 8KB"
    #endif
#else
    #define SDHCI_DMA_BUFF_BOUNDARY   SDHCI_SRS01_DMA_BUFF_4KB
    #if (SDHCI_DMA_THRESHOLD != (4U * 1024U))
        #warning "SDHCI_DMA_THRESHOLD rounded up to 4KB (minimum)"
    #endif
#endif

/* Timeouts */
#ifndef SDHCI_INIT_TIMEOUT_US
#define SDHCI_INIT_TIMEOUT_US   500000      /* 500ms for initialization */
#endif
#ifndef SDHCI_DATA_TIMEOUT_US
#define SDHCI_DATA_TIMEOUT_US   3000000     /* 3000ms for data operations */
#endif

/* Clock frequencies (kHz) */
#ifndef SDHCI_CLK_400KHZ
#define SDHCI_CLK_400KHZ        400
#endif
#ifndef SDHCI_CLK_25MHZ
#define SDHCI_CLK_25MHZ         25000
#endif
#ifndef SDHCI_CLK_50MHZ
#define SDHCI_CLK_50MHZ         50000
#endif

/* ============================================================================
 * Cadence SDHCI Register Offsets (SD4HC Standard)
 * ============================================================================ */

/* Host Register Set (HRS) */
#define SDHCI_HRS00             0x000   /* General information */
#define SDHCI_HRS01             0x004   /* Debounce setting */
#define SDHCI_HRS02             0x008   /* Bus setting */
#define SDHCI_HRS04             0x010   /* PHY access port */
#define SDHCI_HRS06             0x018   /* eMMC control */

/* Slot Register Set (SRS) - SD Host Controller standard registers */
#define SDHCI_SRS00             0x200   /* SDMA System Address / Argument 2 */
#define SDHCI_SRS01             0x204   /* Block Size / Block Count */
#define SDHCI_SRS02             0x208   /* Argument 1 */
#define SDHCI_SRS03             0x20C   /* Command / Transfer Mode */
#define SDHCI_SRS04             0x210   /* Response 0 */
#define SDHCI_SRS05             0x214   /* Response 1 */
#define SDHCI_SRS06             0x218   /* Response 2 */
#define SDHCI_SRS07             0x21C   /* Response 3 */
#define SDHCI_SRS08             0x220   /* Data Port */
#define SDHCI_SRS09             0x224   /* Present State */
#define SDHCI_SRS10             0x228   /* Host Control 1 / Power / Block Gap / Wakeup */
#define SDHCI_SRS11             0x22C   /* Clock Control / Timeout / Software Reset */
#define SDHCI_SRS12             0x230   /* Normal Interrupt Status */
#define SDHCI_SRS13             0x234   /* Normal Interrupt Status Enable */
#define SDHCI_SRS14             0x238   /* Normal Interrupt Signal Enable */
#define SDHCI_SRS15             0x23C   /* Auto CMD Error Status / Host Control 2 */
#define SDHCI_SRS16             0x240   /* Capabilities 1 */
#define SDHCI_SRS17             0x244   /* Capabilities 2 */
#define SDHCI_SRS18             0x248   /* Maximum Current */
#define SDHCI_SRS22             0x258   /* ADMA2/SDMA Address (low) */
#define SDHCI_SRS23             0x25C   /* ADMA2/SDMA Address (high) */

/* ============================================================================
 * Register Bit Definitions
 * ============================================================================ */

/* HRS00 - General Information Register */
#define SDHCI_HRS00_SWR         (1U << 0)   /* Software reset */

/* HRS01 - Debounce Setting Register */
#define SDHCI_HRS01_DP_SHIFT    16
#define SDHCI_HRS01_DP_MASK     (0xFFFFU << 16)

/* HRS04 - PHY Access Port */
#define SDHCI_HRS04_UIS_ACK     (1U << 26)
#define SDHCI_HRS04_UIS_WR      (1U << 24)
#define SDHCI_HRS04_UIS_ADDR_MASK   0x3F
#define SDHCI_HRS04_UIS_WDATA_SHIFT 8

/* HRS06 - eMMC Control Register */
#define SDHCI_HRS06_EMM_MASK    0x07
#define SDHCI_HRS06_MODE_SD     0x00    /* SD mode */
#define SDHCI_HRS06_MODE_LEGACY 0x02    /* eMMC legacy mode */

/* SRS01 - Block Size / Block Count Register */
#define SDHCI_SRS01_BCCT_SHIFT  16      /* Block count shift */
#define SDHCI_SRS01_DMA_BUFF_4KB    (0x0U << 12)
#define SDHCI_SRS01_DMA_BUFF_8KB    (0x1U << 12)
#define SDHCI_SRS01_DMA_BUFF_16KB   (0x2U << 12)
#define SDHCI_SRS01_DMA_BUFF_32KB   (0x3U << 12)
#define SDHCI_SRS01_DMA_BUFF_64KB   (0x4U << 12)
#define SDHCI_SRS01_DMA_BUFF_128KB  (0x5U << 12)
#define SDHCI_SRS01_DMA_BUFF_256KB  (0x6U << 12)
#define SDHCI_SRS01_DMA_BUFF_512KB  (0x7U << 12)

/* SRS03 - Command / Transfer Mode Register */
#define SDHCI_SRS03_CIDX_SHIFT  24
#define SDHCI_SRS03_CIDX_MASK   (0x3FU << 24)
#define SDHCI_SRS03_CT_SHIFT    22
#define SDHCI_SRS03_CT_MASK     (0x03U << 22)
#define SDHCI_SRS03_CMD_NORMAL  0x00
#define SDHCI_SRS03_CMD_SUSPEND 0x01
#define SDHCI_SRS03_CMD_RESUME  0x02
#define SDHCI_SRS03_CMD_ABORT   0x03
#define SDHCI_SRS03_DPS         (1U << 21)  /* Data present */
#define SDHCI_SRS03_CICE        (1U << 20)  /* Command index check enable */
#define SDHCI_SRS03_CRCCE       (1U << 19)  /* Command CRC check enable */
#define SDHCI_SRS03_RID         (1U << 17)  /* Response interrupt disable */
#define SDHCI_SRS03_RECE        (1U << 16)  /* Response error check enable */
#define SDHCI_SRS03_RESP_NONE   (0x0U << 16)
#define SDHCI_SRS03_RESP_136    (0x1U << 16)
#define SDHCI_SRS03_RESP_48     (0x2U << 16)
#define SDHCI_SRS03_RESP_48B    (0x3U << 16)
#define SDHCI_SRS03_MSBS        (1U << 5)   /* Multi/single block select */
#define SDHCI_SRS03_DTDS        (1U << 4)   /* Data transfer direction (1=read) */
#define SDHCI_SRS03_BCE         (1U << 1)   /* Block count enable */
#define SDHCI_SRS03_DMAE        (1U << 0)   /* DMA enable */

/* SRS09 - Present State Register */
#define SDHCI_SRS09_CI          (1U << 16)  /* Card inserted */
#define SDHCI_SRS09_CSS         (1U << 17)  /* Card state stable */
#define SDHCI_SRS09_CICMD       (1U << 0)   /* Command inhibit (CMD) */
#define SDHCI_SRS09_CIDAT       (1U << 1)   /* Command inhibit (DAT) */
#define SDHCI_SRS09_DAT0_LVL    (1U << 20)  /* DAT0 signal level */

/* SRS10 - Host Control 1 / Power / Block Gap / Wakeup */
#define SDHCI_SRS10_DTW         (1U << 1)   /* Data transfer width (4-bit) */
#define SDHCI_SRS10_EDTW        (1U << 5)   /* Extended data transfer width (8-bit) */
#define SDHCI_SRS10_HSE         (1U << 2)   /* High speed enable */
#define SDHCI_SRS10_BP          (1U << 8)   /* Bus power */
#define SDHCI_SRS10_BVS_MASK    (0x7U << 9)
#define SDHCI_SRS10_BVS_1_8V    (0x5U << 9)
#define SDHCI_SRS10_BVS_3_0V    (0x6U << 9)
#define SDHCI_SRS10_BVS_3_3V    (0x7U << 9)
#define SDHCI_SRS10_DMA_SDMA    (0x0U << 3)

/* SRS11 - Clock Control / Timeout / Software Reset */
#define SDHCI_SRS11_ICE         (1U << 0)   /* Internal clock enable */
#define SDHCI_SRS11_ICS         (1U << 1)   /* Internal clock stable */
#define SDHCI_SRS11_SDCE        (1U << 2)   /* SD clock enable */
#define SDHCI_SRS11_CGS         (1U << 5)   /* Clock generator select */
#define SDHCI_SRS11_SDCFSL_SHIFT    8
#define SDHCI_SRS11_SDCFSL_MASK     (0xFFU << 8)
#define SDHCI_SRS11_SDCFSH_SHIFT    6
#define SDHCI_SRS11_SDCFSH_MASK     (0x03U << 6)
#define SDHCI_SRS11_DTCV_SHIFT  16
#define SDHCI_SRS11_DTCV_MASK   (0x0FU << 16)
#define SDHCI_SRS11_RESET_DAT_CMD   ((1U << 25) | (1U << 26))

/* SRS12 - Normal Interrupt Status */
#define SDHCI_SRS12_CC          (1U << 0)   /* Command complete */
#define SDHCI_SRS12_TC          (1U << 1)   /* Transfer complete */
#define SDHCI_SRS12_BGE         (1U << 2)   /* Block gap event */
#define SDHCI_SRS12_DMAINT      (1U << 3)   /* DMA interrupt */
#define SDHCI_SRS12_BWR         (1U << 4)   /* Buffer write ready */
#define SDHCI_SRS12_BRR         (1U << 5)   /* Buffer read ready */
#define SDHCI_SRS12_CIN         (1U << 6)   /* Card insertion */
#define SDHCI_SRS12_CR          (1U << 7)   /* Card removal */
#define SDHCI_SRS12_CINT        (1U << 8)   /* Card interrupt */
#define SDHCI_SRS12_EINT        (1U << 15)  /* Error interrupt */
#define SDHCI_SRS12_ECT         (1U << 16)  /* Command timeout error */
#define SDHCI_SRS12_ECCRC       (1U << 17)  /* Command CRC error */
#define SDHCI_SRS12_ECEB        (1U << 18)  /* Command end bit error */
#define SDHCI_SRS12_ECI         (1U << 19)  /* Command index error */
#define SDHCI_SRS12_EDT         (1U << 20)  /* Data timeout error */
#define SDHCI_SRS12_EDCRC       (1U << 21)  /* Data CRC error */
#define SDHCI_SRS12_EDEB        (1U << 22)  /* Data end bit error */
#define SDHCI_SRS12_ECL         (1U << 23)  /* Current limit error */
#define SDHCI_SRS12_EAC         (1U << 24)  /* Auto CMD error */
#define SDHCI_SRS12_EADMA       (1U << 25)  /* ADMA error */
#define SDHCI_SRS12_NORM_STAT   0x0000FFFFU
#define SDHCI_SRS12_ERR_STAT    0xFFFF0000U

/* SRS13 - Normal Interrupt Status Enable */
#define SDHCI_SRS13_CC_SE       (1U << 0)
#define SDHCI_SRS13_TC_SE       (1U << 1)
#define SDHCI_SRS13_BGE_SE      (1U << 2)
#define SDHCI_SRS13_DMAINT_SE   (1U << 3)
#define SDHCI_SRS13_BWR_SE      (1U << 4)
#define SDHCI_SRS13_BRR_SE      (1U << 5)
#define SDHCI_SRS13_CIN_SE      (1U << 6)
#define SDHCI_SRS13_CR_SE       (1U << 7)
#define SDHCI_SRS13_CINT_SE     (1U << 8)
#define SDHCI_SRS13_INT_ONA     (1U << 9)
#define SDHCI_SRS13_INT_ONB     (1U << 10)
#define SDHCI_SRS13_INT_ONC     (1U << 11)
#define SDHCI_SRS13_RTUNE_SE    (1U << 12)
#define SDHCI_SRS13_ECT_SE      (1U << 16)
#define SDHCI_SRS13_ECCRC_SE    (1U << 17)
#define SDHCI_SRS13_ECEB_SE     (1U << 18)
#define SDHCI_SRS13_ECI_SE      (1U << 19)
#define SDHCI_SRS13_EDT_SE      (1U << 20)
#define SDHCI_SRS13_EDCRC_SE    (1U << 21)
#define SDHCI_SRS13_EDEB_SE     (1U << 22)
#define SDHCI_SRS13_ECL_SE      (1U << 23)
#define SDHCI_SRS13_EAC_SE      (1U << 24)
#define SDHCI_SRS13_EADMA_SE    (1U << 25)
#define SDHCI_SRS13_ETUNE_SE    (1U << 26)
#define SDHCI_SRS13_ERSP_SE     (1U << 27)
#define SDHCI_SRS13_CQINT_SE    (1U << 30)

/* SRS14 - Normal Interrupt Signal Enable */
#define SDHCI_SRS14_CC_IE       (1U << 0)
#define SDHCI_SRS14_TC_IE       (1U << 1)
#define SDHCI_SRS14_DMAINT_IE   (1U << 3)
#define SDHCI_SRS14_EDT_IE      (1U << 20)

/* SRS15 - Auto CMD Error Status / Host Control 2 */
#define SDHCI_SRS15_A64         (1U << 29)  /* 64-bit addressing */
#define SDHCI_SRS15_HV4E        (1U << 28)  /* Host version 4 enable */
#define SDHCI_SRS15_UMS_MASK    (0x7U << 16)
#define SDHCI_SRS15_UMS_SDR25   (0x1U << 16)
#define SDHCI_SRS15_DSS_MASK    (0x3U << 20)
#define SDHCI_SRS15_DSS_TYPE_B  (0x0U << 20)
#define SDHCI_SRS15_EXTNG       (1U << 22)  /* Execute tuning */
#define SDHCI_SRS15_SCS         (1U << 23)  /* Sampling clock select */

/* SRS16 - Capabilities 1 */
#define SDHCI_SRS16_TCF_SHIFT   0
#define SDHCI_SRS16_TCF_MASK    (0x3FU << 0)
#define SDHCI_SRS16_TCU         (1U << 7)   /* Timeout clock unit (1=MHz) */
#define SDHCI_SRS16_BCSDCLK_SHIFT   8
#define SDHCI_SRS16_BCSDCLK_MASK    (0xFFU << 8)
#define SDHCI_SRS16_VS33        (1U << 24)  /* 3.3V supported */
#define SDHCI_SRS16_VS30        (1U << 25)  /* 3.0V supported */
#define SDHCI_SRS16_VS18        (1U << 26)  /* 1.8V supported */
#define SDHCI_SRS16_A64S        (1U << 28)  /* 64-bit system bus support */

/* SRS17 - Capabilities 2 */
#define SDHCI_SRS17_SDR50       (1U << 0)
#define SDHCI_SRS17_SDR104      (1U << 1)
#define SDHCI_SRS17_DDR50       (1U << 2)
#define SDHCI_SRS17_TSDR50      (1U << 13)  /* Tuning for SDR50 required */

/* SRS18 - Maximum Current */
#define SDHCI_SRS18_MC33_SHIFT  0
#define SDHCI_SRS18_MC33_MASK   (0xFFU << 0)
#define SDHCI_SRS18_MC18_SHIFT  16
#define SDHCI_SRS18_MC18_MASK   (0xFFU << 16)

/* ============================================================================
 * MMC/SD Command Definitions
 * ============================================================================ */

/* Common MMC/SD commands */
#define MMC_CMD0_GO_IDLE        0
#define MMC_CMD2_ALL_SEND_CID   2
#define MMC_CMD3_SET_REL_ADDR   3
#define MMC_CMD7_SELECT_CARD    7
#define MMC_CMD9_SEND_CSD       9
#define MMC_CMD12_STOP_TRANS    12
#define MMC_CMD13_SEND_STATUS   13
#define MMC_CMD17_READ_SINGLE   17
#define MMC_CMD18_READ_MULTIPLE 18
#define MMC_CMD24_WRITE_SINGLE  24
#define MMC_CMD25_WRITE_MULTIPLE 25

/* SD card-specific commands */
#define SD_CMD6_SWITCH_FUNC     6
#define SD_CMD8_SEND_IF_COND    8
#define SD_CMD16                16
#define SD_CMD19_SEND_TUNING    19
#define SD_CMD55_APP_CMD        55
#define SD_ACMD6_SET_BUS_WIDTH  6
#define SD_ACMD41_SEND_OP_COND  41
#define SD_ACMD51_SEND_SCR      51

/* eMMC-specific commands */
#define MMC_CMD1_SEND_OP_COND   1
#define MMC_CMD6_SWITCH         6

/* SD voltage check */
#define SD_IF_COND_27V_33V      0x1AA

/* SD RCA shift */
#define SD_RCA_SHIFT            16

/* SD/MMC OCR register bits */
#define SDCARD_ACMD41_HCS       (1U << 30)
#define SDCARD_REG_OCR_READY    (1U << 31)
#define SDCARD_REG_OCR_S18RA    (1U << 24)
#define SDCARD_REG_OCR_XPC      (1U << 28)
#define SDCARD_REG_OCR_2_9_3_0  (1U << 17)
#define SDCARD_REG_OCR_3_0_3_1  (1U << 18)
#define SDCARD_REG_OCR_3_1_3_2  (1U << 19)
#define SDCARD_REG_OCR_3_2_3_3  (1U << 20)
#define SDCARD_REG_OCR_3_3_3_4  (1U << 21)

/* SD switch function */
#define SDCARD_SWITCH_FUNC_MODE_CHECK   (0U << 31)
#define SDCARD_SWITCH_FUNC_MODE_SWITCH  (1U << 31)
#define SDCARD_SWITCH_ACCESS_MODE_SDR25 0x01

/* SCR register */
#define SCR_REG_DATA_SIZE       8

/* eMMC-specific constants */
#define MMC_DW_CSD              0x03B70000U
#define MMC_DEVICE_3_3V_VOLT_SET 0x40300000U
#define MMC_OCR_BUSY_BIT        0x80000000U
#define MMC_EMMC_RCA_DEFAULT    1
#define MMC_EXT_CSD_WIDTH_1BIT  0x00U
#define MMC_EXT_CSD_WIDTH_4BIT  0x01U
#define MMC_EXT_CSD_WIDTH_8BIT  0x02U
#define MMC_EXT_CSD_WIDTH_4BIT_DDR  0x05U
#define MMC_EXT_CSD_WIDTH_8BIT_DDR  0x06U

/* IRQ status flags */
#define SDHCI_IRQ_FLAG_CC       0x01
#define SDHCI_IRQ_FLAG_TC       0x02
#define SDHCI_IRQ_FLAG_DMAINT   0x04
#define SDHCI_IRQ_FLAG_ERROR    0x80

/* Response types */
typedef enum {
    SDHCI_RESP_NONE,
    SDHCI_RESP_R1,
    SDHCI_RESP_R1B,
    SDHCI_RESP_R2,
    SDHCI_RESP_R3,
    SDHCI_RESP_R4,
    SDHCI_RESP_R5,
    SDHCI_RESP_R5B,
    SDHCI_RESP_R6,
    SDHCI_RESP_R7,
    SDHCI_RESP_R1A
} sdhci_resp_t;

/* ============================================================================
 * Public API (implemented in src/sdhci.c)
 * ============================================================================ */

/* Initialize controller and card */
int sdhci_init(void);

/* Block read/write */
int sdhci_read(uint32_t cmd_index, uint32_t block_addr, uint32_t* dst, uint32_t sz);
int sdhci_write(uint32_t cmd_index, uint32_t block_addr, const uint32_t* src, uint32_t sz);

/* Send command */
int sdhci_cmd(uint32_t cmd_index, uint32_t cmd_arg, uint8_t resp_type);

/* IRQ handler (call from platform IRQ) */
void sdhci_irq_handler(void);

/* ============================================================================
 * HAL Interface (platform must implement in target HAL file)
 * ============================================================================ */

/* Register access - platform provides base address and implementation */
uint32_t sdhci_reg_read(uint32_t offset);
void sdhci_reg_write(uint32_t offset, uint32_t val);

/* Direct buffer data port access (for tight transfer loops)
 * Default implementation uses sdhci_reg_read/write, but platform may
 * override with direct volatile access for better performance */
#ifndef SDHCI_BUF_READ
#define SDHCI_BUF_READ()     sdhci_reg_read(SDHCI_SRS08)
#endif
#ifndef SDHCI_BUF_WRITE
#define SDHCI_BUF_WRITE(v)   sdhci_reg_write(SDHCI_SRS08, v)
#endif

/* Platform initialization (clocks, resets, pin mux, debounce) */
void sdhci_platform_init(void);

/* Platform interrupt setup (PLIC/NVIC/GIC/etc.) */
void sdhci_platform_irq_init(void);

/* Platform bus mode selection (SD vs eMMC) */
void sdhci_platform_set_bus_mode(int is_emmc);

#endif /* DISK_SDCARD || DISK_EMMC */

#endif /* SDHCI_H */

