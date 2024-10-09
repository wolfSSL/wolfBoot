/* sama5d3.h
 *
 * Header file for SAMA5D3 HAL
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

#ifndef SAMA5D3_HAL_H
#define SAMA5D3_HAL_H

#include <stdint.h>

/* CPU/Board clock settings */
#define CPU_FREQ 264000000UL
#define MASTER_FREQ 132000000UL
#define CRYSTAL_FREQ 12000000UL
#define MULA		 43

/* PLLA register
 */

#define PLLA_DIVA_SHIFT      0
#define PLLA_DIVA_MASK       (0xFF << PLLA_DIVA_SHIFT)
#define PLLA_COUNT_SHIFT     8
#define PLLA_COUNT_MASK      (0x3F << PLLA_COUNT_SHIFT)
#define PLLA_CKGR_OUTA_SHIFT 14
#define PLLA_CKGR_OUTA_MASK  (0x3 << PLLA_CKGR_OUTA_SHIFT)
#define PLLA_MULA_SHIFT      18
#define PLLA_MULA_MASK       (0x7F << PLLA_MULA_SHIFT)
#define PLLA_CKGR_SRCA       (0x1 << 29)

/* PMC version 1 */
#define PMC_BASE 0xFFFFFC00
#define PMC_SCER    *(volatile uint32_t *)(PMC_BASE + 0x0000)
#define PMC_UCKR    *(volatile uint32_t *)(PMC_BASE + 0x001C)
#define PMC_PLLA    *(volatile uint32_t *)(PMC_BASE + 0x0028)
#define PMC_MCKR    *(volatile uint32_t *)(PMC_BASE + 0x0030)
#define PMC_SR      *(volatile uint32_t *)(PMC_BASE + 0x0068)
#define PMC_PLLICPR *(volatile uint32_t *)(PMC_BASE + 0x0080)
#define PMC_PCR     *(volatile uint32_t *)(PMC_BASE + 0x010C)

#define PMC_PLLADIV_SHIFT    12
#define PMC_PLLADIV_MASK     (0x1 << PMC_PLLADIV_SHIFT)
#define PMC_PLLADIV_1        (0x0 << PMC_PLLADIV_SHIFT)
#define PMC_PLLADIV_2        (0x1 << PMC_PLLADIV_SHIFT)

#define PMC_CSS_SHIFT        0
#define PMC_CSS_MASK         (0x3 << PMC_CSS_SHIFT)
#define PMC_CSS_SLOW_CLK     (0x0 << PMC_CSS_SHIFT)
#define PMC_CSS_MAIN_CLK     (0x1 << PMC_CSS_SHIFT)
#define PMC_CSS_PLLA_CLK     (0x2 << PMC_CSS_SHIFT)
#define PMC_CSS_UPLL_CLK     (0x3 << PMC_CSS_SHIFT)
#define PMC_PRES_SHIFT       2
#define PMC_PRES_MASK        (0xF << PMC_PRES_SHIFT)
#define PMC_ALTPRES_SHIFT    4
#define PMC_ALTPRES_MASK     (0xF << PMC_ALTPRES_SHIFT)
#define PMC_MDIV_SHIFT       8
#define PMC_MDIV_MASK        (0x3 << PMC_MDIV_SHIFT)
#define PMC_MDIV_1           (0x0 << PMC_MDIV_SHIFT)
#define PMC_MDIV_2           (0x1 << PMC_MDIV_SHIFT)
#define PMC_MDIV_3           (0x2 << PMC_MDIV_SHIFT)
#define PMC_MDIV_4           (0x3 << PMC_MDIV_SHIFT)
#define PMC_H32MXDIV_SHIFT   24
#define PMC_H32MXDIV_MASK    (0x1 << PMC_H32MXDIV_SHIFT)


#define PMC_SR_LOCKA         (0x1 << 1)
#define PMC_SR_MCKRDY        (0x1 << 3)

#define PMC_PLLICPR_ICPPLLA_SHIFT 0
#define PMC_PLLICPR_ICPPLLA_MASK  (0x7 << PMC_PLLICPR_ICPPLLA_SHIFT)
#define PMC_PLLICPR_IPLLA_SHIFT   8
#define PMC_PLLICPR_IPLLA_MASK    (0xF << PMC_PLLICPR_IPLLA_SHIFT)

#define PMC_PCR_CMD           (0x1 << 12)
#define PMC_PCR_EN            (0x1 << 28)
#define PMC_PCR_DIV_SHIFT     13
#define PMC_PCR_DIV_MASK      (0x3 << PMC_PCR_DIV_SHIFT)

/* Specific configuration for 264/132/12 MHz */

#define PLL_PCK		         (((CRYSTAL_FREQ * (PLLA_MULA + 1)) / 2))
#define PLL_MCK		         (BOARD_PCK / 2)
#define PLL_CKGR_PLLA        (PLLA_CKGR_SRCA | (0 << PLLA_CKGR_OUTA_SHIFT))
#define PLL_PLLACOUNT        (PLLA_COUNT_MASK)
#define PLL_MULA             ((MULA << PLLA_MULA_SHIFT) & PLLA_MULA_MASK)
#define PLL_DIVA             (0x01 & PLLA_DIVA_MASK)
#define PLLA_CONFIG          (PLL_CKGR_PLLA | PLL_PLLACOUNT | PLL_MULA | PLL_DIVA)

#define PRESCALER_MAIN_CLOCK		(PMC_PLLADIV_2 | PMC_MDIV_2 | PMC_CSS_MAIN_CLK)
#define PRESCALER_PLLA_CLOCK        (PMC_PLLADIV_2 | PMC_MDIV_2 | PMC_CSS_PLLA_CLK)

#define PLLICPR_CONFIG      (0x0 << PMC_PLLICPR_ICPPLLA_SHIFT | 0x3 << PMC_PLLICPR_IPLLA_SHIFT)

/* DBGU
 *
 */
#define DBGU_BASE 0xFFFFEE00
#define DBGU_CR   *(volatile uint32_t *)(DBGU_BASE + 0x00)
#define DBGU_BRGR *(volatile uint32_t *)(DBGU_BASE + 0x20)
#define DBGU_CR_RXEN (1 << 4)
#define DBGU_CR_TXEN (1 << 6)
#define DBGU_PMCID 0x02 /* dec: 2 for SAMA5D3 */

/* Associated pins : GPIOB 30 - 31*/
#define DBGU_PIN_RX 30
#define DBGU_PIN_TX 31
#define DBGU_GPIO   GPIOB

/* PIT
 *
 */
#define PIT_BASE 0xFFFFFE30
#define PIT_MR   *(volatile uint32_t *)(PIT_BASE + 0x00)
#define PIT_SR   *(volatile uint32_t *)(PIT_BASE + 0x04)
#define PIT_PIVR *(volatile uint32_t *)(PIT_BASE + 0x08)
#define PIT_PIIR *(volatile uint32_t *)(PIT_BASE + 0x0C)

/* DRAM setup
 *
 */
#define MPDDRC_BASE 0xFFFFEA00
#define MPDDRC_MR *(volatile uint32_t *)(MPDDRC_BASE + 0x00) /* Mode Register */
#define MPDDRC_RTR *(volatile uint32_t *)(MPDDRC_BASE + 0x04) /* Refresh Timer Register */
#define MPDDRC_CR *(volatile uint32_t *)(MPDDRC_BASE + 0x08) /* Configuration Register */
#define MPDDRC_TPR0 *(volatile uint32_t *)(MPDDRC_BASE + 0x0C) /* Timing Parameter 0 Register */
#define MPDDRC_TPR1 *(volatile uint32_t *)(MPDDRC_BASE + 0x10) /* Timing Parameter 1 Register */
#define MPDDRC_TPR2 *(volatile uint32_t *)(MPDDRC_BASE + 0x14) /* Timing Parameter 2 Register */
/* Reserved 0x18 */
#define MPDDRC_LPR *(volatile uint32_t *)(MPDDRC_BASE + 0x1C) /* Low-power Register */
#define MPDDRC_MD *(volatile uint32_t *)(MPDDRC_BASE + 0x20) /* Memory Device Register */
#define MPDDRC_HS *(volatile uint32_t *)(MPDDRC_BASE + 0x24) /* High Speed Register */
#define MPDDRC_LPDDR2_LPR *(volatile uint32_t *)(MPDDRC_BASE + 0x28) /* LPDDR2 Low-power Register */
#define MPDDRC_LPDDR2_CAL_MR4 *(volatile uint32_t *)(MPDDRC_BASE + 0x2C) /* LPDDR2 Calibration and MR4 Register */
#define MPDDRC_LPDDR2_TIM_CAL *(volatile uint32_t *)(MPDDRC_BASE + 0x30) /* LPDDR2 Timing Calibration Register */
#define MPDDRC_IO_CALIBR *(volatile uint32_t *)(MPDDRC_BASE + 0x34) /* I/O Calibration Register */
#define MPDDRC_OCMS *(volatile uint32_t *)(MPDDRC_BASE + 0x38) /* OCMS Register */
#define MPDDRC_OCMS_KEY1 *(volatile uint32_t *)(MPDDRC_BASE + 0x3C) /* OCMS Key 1 Register */
#define MPDDRC_OCMS_KEY2 *(volatile uint32_t *)(MPDDRC_BASE + 0x40) /* OCMS Key 2 Register */
/* Reserved 0x44 to 0x58 */
#define MPDDRC_RD_DATA_PATH *(volatile uint32_t *)(MPDDRC_BASE + 0x5C) /* Read Data Path Register */
/* Reserved 0x60 to 0x70 */
#define MPDDRC_DLL_MO *(volatile uint32_t *)(MPDDRC_BASE + 0x74) /* DLL Master Offset Register */
#define MPDDRC_DLL_SOF *(volatile uint32_t *)(MPDDRC_BASE + 0x78) /* DLL Slave Offset Register */
#define MPDDRC_DLL_MS *(volatile uint32_t *)(MPDDRC_BASE + 0x7C) /* DLL Master Status Register */
#define MPDDRC_DLL_SS0 *(volatile uint32_t *)(MPDDRC_BASE + 0x80) /* DLL Slave 0 Status Register */
#define MPDDRC_DLL_SS1 *(volatile uint32_t *)(MPDDRC_BASE + 0x84) /* DLL Slave 1 Status Register */
#define MPDDRC_DLL_SS2 *(volatile uint32_t *)(MPDDRC_BASE + 0x88) /* DLL Slave 2 Status Register */
#define MPDDRC_DLL_SS3 *(volatile uint32_t *)(MPDDRC_BASE + 0x8C) /* DLL Slave 3 Status Register */
/* Reserved 0x90 to 0xE0 */
#define MPDDRC_WPMR *(volatile uint32_t *)(MPDDRC_BASE + 0xE4) /* Write Protection Mode Register */
#define MPDDRC_WPSR *(volatile uint32_t *)(MPDDRC_BASE + 0xE8) /* Write Protection Status Register */

/* MPDDRC_CR: shift, mask, values */
#define MPDDRC_NC_SHIFT 0  /* Number of Column Bits */
#define MPDDRC_NC_MASK (0x3 << MPDDRC_NC_SHIFT)
#define MPDDRC_NC_9 (0x0 << MPDDRC_NC_SHIFT)
#define MPDDRC_NC_10 (0x1 << MPDDRC_NC_SHIFT)
#define MPDDRC_NC_11 (0x2 << MPDDRC_NC_SHIFT)
#define MPDDRC_NC_12 (0x3 << MPDDRC_NC_SHIFT)

#define MPDDRC_NR_SHIFT 2  /* Number of Row Bits */
#define MPDDRC_NR_MASK (0x3 << MPDDRC_NR_SHIFT)
#define MPDDRC_NR_11 (0x0 << MPDDRC_NR_SHIFT)
#define MPDDRC_NR_12 (0x1 << MPDDRC_NR_SHIFT)
#define MPDDRC_NR_13 (0x2 << MPDDRC_NR_SHIFT)
#define MPDDRC_NR_14 (0x3 << MPDDRC_NR_SHIFT)

#define MPDDRC_CAS_SHIFT 4 /* CAS Latency */
#define MPDDRC_CAS_MASK (0x7 << MPDDRC_CAS_SHIFT)
#define MPDDRC_NB_SHIFT 20   /* Number of Banks */
#define MPDDRC_NB_MASK (0x1 << MPDDRC_NB_SHIFT)

#define MPDDRC_MD_DBW_SHIFT 4 /* Data Bus Width */
#define MPDDRC_MD_DBW_MASK (0x1 << MPDDRC_MD_DBW_SHIFT)

#define MPDDRC_NQDS_DISABLED_SHIFT 21 /* NAND Data Queue in DDR2 SDRAM */
#define MPDDRC_NDQS_DISABLED (0x1 << MPDDRC_NQDS_DISABLED_SHIFT)

#define MPDDRC_UNAL_SHIFT 23 /* Support Unaligned Access */
#define MPDDRC_UNAL (0x1 << MPDDRC_UNAL_SHIFT)

#define REF_WIN 32
#define REF_CYCLE 2048

/* Configuration register */
#define MPDDRC_CR_NC_SHIFT 0
#define MPDDRC_CR_NC_MASK (0x3 << MPDDRC_CR_NC_SHIFT)
#define MPDDRC_CR_NR_SHIFT 2
#define MPDDRC_CR_NR_MASK (0x3 << MPDDRC_CR_NR_SHIFT)
#define MPDDRC_CR_CAS_SHIFT 4
#define MPDDRC_CR_CAS_MASK (0x7 << MPDDRC_CR_CAS_SHIFT)

#define MPDDRC_CR_ENABLE_DLL_RESET (1 << 7)

#define MPDDRC_CR_NB_SHIFT 8
#define MPDDRC_CR_NB_MASK (0x1 << MPDDRC_CR_NB_SHIFT)

#define MPDDRC_CR_DECOD_INTERLEAVED (1 << 22)


/* Memory device register */
#define MPDDRC_MD_SDRAM (0x0 << 0)
#define MPDDRC_MD_LP_SDRAM (0x1 << 0)
#define MPDDRC_MD_DDR_SDRAM (0x2 << 0)
#define MPDDRC_MD_LP_DDR_SDRAM (0x3 << 0)
#define MPDDRC_MD_DDR3_SDRAM (0x4 << 0)
#define MPDDRC_MD_LPDDR3_SDRAM (0x5 << 0)
#define MPDDRC_MD_DDR2_SDRAM (0x6 << 0)
#define MPDDRC_MD_LPDDR2_SDRAM (0x7 << 0)

#define MPDDRC_MD_DBW_32BIT (0x0 << 4)
#define MPDDRC_MD_DBW_16BIT (0x1 << 4)


/* Mode register */
#define MPDDRC_MR_MODE_NORMAL 0
#define MPDDRC_MR_MODE_NOP 1
#define MPDDRC_MR_MODE_PRECHARGE 2
#define MPDDRC_MR_MODE_LOAD 3
#define MPDDRC_MR_MODE_AUTO_REFRESH 4
#define MPDDRC_MR_MODE_EXT_LOAD 5
#define MPDDRC_MR_MODE_DEEP_POWER 6
#define MPDDRC_MR_MODE_LPDDR2_PDE 7

#define MPDDRC_CR_OCD_DEFAULT (0x7 << 12)


#define MPDDRC_TRAS_SHIFT 0
#define MPDDRC_TRCD_SHIFT 4
#define MPDDRC_TWR_SHIFT 8
#define MPDDRC_TRC_SHIFT 12
#define MPDDRC_TRP_SHIFT 16
#define MPDDRC_TRRD_SHIFT 20
#define MPDDRC_TWTR_SHIFT 24
#define MPDDRC_TMRD_SHIFT 28

#define MPDDRC_TRFC_SHIFT 0
#define MPDDRC_TXSNR_SHIFT 8
#define MPDDRC_TXSRD_SHIFT 16
#define MPDDRC_TXP_SHIFT 24

#define MPDDRC_TXARD_SHIFT 0
#define MPDDRC_TXARDS_SHIFT 4
#define MPDDRC_TRPA_SHIFT 8
#define MPDDRC_TRTP_SHIFT 12
#define MPDDRC_TFAW_SHIFT 16

/* Calibration register */
#define MPDDRC_IOCALIBR_RDIV_SHIFT 0
#define MPDDRC_IOCALIBR_RDIV_MASK (0x7 << MPDDRC_IOCALIBR_RDIV_SHIFT)
#define MPDDRC_IOCALIBR_RDIV_DDR2_RZQ_50 (4 << MPDDRC_IOCALIBR_RDIV_SHIFT)

#define MPDDRC_IOCALIBR_TZQIO_SHIFT 8
#define MPDDRC_IOCALIBR_TZQIO_MASK (0x7F << MPDDRC_IOCALIBR_TZQIO_SHIFT)

/* Read data path register */
#define MPDDRC_RD_DATA_PATH_CYCLES_SHIFT 0
#define MPDDRC_RD_DATA_PATH_CYCLES_MASK (0x3 << MPDDRC_RD_DATA_PATH_CYCLES_SHIFT)



/* MPDDRC Device clock */
#define MPDDRC_PMCID 0x31 /* dec: 49 for SAMA5D3 */
#define MPDDRC_SCERID (1 << 2)

/* PIT device clock */
#define PIT_PMCID 0x03 /* dec: 3 for SAMA5D3 */
#define MAX_PIV		0xfffff
#define PIT_MR_EN (1 << 24)

/* GPIO PMC IDs */
#define GPIOA_PMCID 0x06
#define GPIOB_PMCID 0x07
#define GPIOC_PMCID 0x08
#define GPIOD_PMCID 0x09
#define GPIOE_PMCID 0x0A

struct dram {
    struct dram_timing {
        uint32_t tras;
        uint32_t trcd;
        uint32_t twr;
        uint32_t trc;
        uint32_t trp;
        uint32_t trrd;
        uint32_t twtr;
        uint32_t tmrd;
        uint32_t trfc;
        uint32_t txsnr;
        uint32_t txsrd;
        uint32_t txp;
        uint32_t txard;
        uint32_t txards;
        uint32_t trpa;
        uint32_t trtp;
        uint32_t tfaw;
    } timing;
};

/* Watchdog
 *
 */
#define WDT_BASE 0xFFFFFD40
#define WDT_CR   *(volatile uint32_t *)(WDT_BASE + 0x00)
#define WDT_MR   *(volatile uint32_t *)(WDT_BASE + 0x04)
#define WDT_SR   *(volatile uint32_t *)(WDT_BASE + 0x08)

#define WDT_MD_WDDIS (0x1 << 15)
#define WDT_MD_WDRSTEN (0x1 << 14)

static inline void watchdog_disable(void)
{
    WDT_MR |= WDT_MD_WDDIS;
}

/*
 *
 * NAND flash
 */

/* Fixed addresses */
extern void *kernel_addr, *update_addr, *dts_addr;

#if defined(EXT_FLASH) && defined(NAND_FLASH)

/* Constant for local buffers */
#define NAND_FLASH_PAGE_SIZE 0x800 /* 2KB */
#define NAND_FLASH_OOB_SIZE 0x40   /* 64B */

/* Address space mapping for atsama5d3 */
#define DRAM_BASE         0x20000000UL
#define CS1_BASE          0x40000000UL
#define CS2_BASE          0x50000000UL
#define CS3_BASE          0x60000000UL
#define NFC_CMD_BASE      0x70000000UL

/* NAND flash is mapped to CS3 */
#define NAND_BASE            CS3_BASE

#define NAND_MASK_ALE        (1 << 21)
#define NAND_MASK_CLE        (1 << 22)
#define NAND_CMD  (*((volatile uint8_t *)(NAND_BASE | NAND_MASK_CLE)))
#define NAND_ADDR (*((volatile uint8_t *)(NAND_BASE | NAND_MASK_ALE)))
#define NAND_DATA (*((volatile uint8_t *)(NAND_BASE)))

/* Command set */
#define NAND_CMD_STATUS       0x70
#define NAND_CMD_READ1        0x00
#define NAND_CMD_READ2        0x30
#define NAND_CMD_READID       0x90
#define NAND_CMD_RESET        0xFF
#define NAND_CMD_ERASE1       0x60
#define NAND_CMD_ERASE2       0xD0
#define NAND_CMD_WRITE1       0x80
#define NAND_CMD_WRITE2       0x10

/* Small block */
#define NAND_CMD_READ_A0      0x00
#define NAND_CMD_READ_A1      0x01
#define NAND_CMD_READ_C       0x50
#define NAND_CMD_WRITE_A      0x00
#define NAND_CMD_WRITE_C      0x50


/* ONFI */
#define NAND_CMD_READ_ONFI  0xEC

/* Features set/get */
#define NAND_CMD_GET_FEATURES 0xEE
#define NAND_CMD_SET_FEATURES 0xEF

/* ONFI parameters and definitions */
#define ONFI_PARAMS_SIZE		256

#define PARAMS_POS_REVISION		4
#define		PARAMS_REVISION_1_0	(0x1 << 1)
#define		PARAMS_REVISION_2_0	(0x1 << 2)
#define		PARAMS_REVISION_2_1	(0x1 << 3)

#define PARAMS_POS_FEATURES		6
#define		PARAMS_FEATURE_BUSWIDTH		(0x1 << 0)
#define		PARAMS_FEATURE_EXTENDED_PARAM	(0x1 << 7)

#define PARAMS_POS_OPT_CMD		8
#define		PARAMS_OPT_CMD_SET_GET_FEATURES	(0x1 << 2)

#define PARAMS_POS_EXT_PARAM_PAGE_LEN	12
#define PARAMS_POS_PARAMETER_PAGE		14
#define PARAMS_POS_PAGESIZE		        80
#define PARAMS_POS_OOBSIZE		        84
#define PARAMS_POS_BLOCKSIZE		    92
#define PARAMS_POS_NBBLOCKS		        96
#define PARAMS_POS_ECC_BITS		        112

#define PARAMS_POS_TIMING_MODE	        129
#define		PARAMS_TIMING_MODE_0	    (1 << 0)
#define		PARAMS_TIMING_MODE_1	    (1 << 1)
#define		PARAMS_TIMING_MODE_2	    (1 << 2)
#define		PARAMS_TIMING_MODE_3	    (1 << 3)
#define		PARAMS_TIMING_MODE_4	    (1 << 4)
#define		PARAMS_TIMING_MODE_5	    (1 << 5)

#define PARAMS_POS_CRC		254

#define ONFI_CRC_BASE			0x4F4E

#define ONFI_MAX_SECTIONS		8

#define ONFI_SECTION_TYPE_0		0
#define ONFI_SECTION_TYPE_1		1
#define ONFI_SECTION_TYPE_2		2

/* Read access modes */
#define NAND_MODE_DATAPAGE      1
#define NAND_MODE_INFO          2
#define NAND_MODE_DATABLOCK    3

#define nand_flash_read ext_flash_read
#define nand_flash_write ext_flash_write
#define nand_flash_erase ext_flash_erase
#define nand_flash_unlock ext_flash_unlock
#define nand_flash_lock ext_flash_lock

#define MAX_ECC_BYTES 8
#endif

#define GPIOB   0xFFFFF400
#define GPIOC   0xFFFFF600
#define GPIOE   0xFFFFFA00

#define GPIO_PER(base)     *(volatile uint32_t *)(base + 0x00)
#define GPIO_PDR(base)     *(volatile uint32_t *)(base + 0x04)
#define GPIO_PSR(base)     *(volatile uint32_t *)(base + 0x08)
#define GPIO_OER(base)     *(volatile uint32_t *)(base + 0x10)
#define GPIO_ODR(base)     *(volatile uint32_t *)(base + 0x14)
#define GPIO_OSR(base)     *(volatile uint32_t *)(base + 0x18)
#define GPIO_SODR(base)     *(volatile uint32_t *)(base + 0x30)
#define GPIO_CODR(base)     *(volatile uint32_t *)(base + 0x34)
#define GPIO_IER(base)     *(volatile uint32_t *)(base + 0x40)
#define GPIO_IDR(base)     *(volatile uint32_t *)(base + 0x44)
#define GPIO_MDER(base)     *(volatile uint32_t *)(base + 0x50)
#define GPIO_MDDR(base)     *(volatile uint32_t *)(base + 0x54)
#define GPIO_PPUDR(base)    *(volatile uint32_t *)(base + 0x60)
#define GPIO_PPUER(base)    *(volatile uint32_t *)(base + 0x64)
#define GPIO_ASR(base)      *(volatile uint32_t *)(base + 0x70)
#define GPIO_PPDDR(base)    *(volatile uint32_t *)(base + 0x90)


/* PMC Macro to enable clock */
#define PMC_CLOCK_EN(id) { \
    register uint32_t pmc_pcr; \
    PMC_PCR = id; \
    pmc_pcr = PMC_PCR & (~PMC_PCR_DIV_MASK); \
    pmc_pcr |= PMC_PCR_CMD | PMC_PCR_EN; \
    PMC_PCR = pmc_pcr; \
}


#endif
