/* nxp_p1021.c
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
#include <stdint.h>
#include "target.h"
#include "image.h"
#include "printf.h"
#include "string.h"

#include "nxp_ppc.h"

/* Debugging */
/* #define DEBUG_EXT_FLASH */
/* #define DEBUG_ESPI 1 */

#define ENABLE_ELBC /* Flash Controller */
#define ENABLE_BUS_CLK_CALC

#ifndef BUILD_LOADER_STAGE1
    /* Tests */
    #if 0
        #define TEST_DDR
        #define TEST_TPM
    #endif
    #define ENABLE_PCIE
    #define ENABLE_CPLD /* Board Configuration and Status Registers (BCSR) */
    #define ENABLE_CONF_IO
    #define ENABLE_QE   /* QUICC Engine */
    #if defined(WOLFBOOT_TPM) || defined(TEST_TPM)
        #define ENABLE_ESPI /* SPI for TPM */
    #endif
    #define ENABLE_MP   /* multi-core support */
    #define ENABLE_IRQ
    /* #define ENABLE_QE_CRC32 */ /* CRC32 check on QE disabled by default */
#endif

/* Foward declarations */
#if defined(ENABLE_DDR) && defined(TEST_DDR)
static int test_ddr(void);
#endif
#if defined(ENABLE_ESPI) && defined(TEST_TPM)
static int test_tpm(void);
#endif

#ifdef ENABLE_ESPI
#include "spi_drv.h" /* for transfer flags and chip select */
#endif

/* P1021 Platform */
/* System input clock */
#define SYS_CLK (66666667) /* 66.666666 MHz */

/* Boot page translation register */
#define RESET_BPTR           ((volatile uint32_t*)(CCSRBAR + 0x20))
#define RESET_BPTR_EN        0x80000000
#define RESET_BPTR_BOOTPG(n) ((n) >> 12)

/* Global Utilities (GUTS) */
#define GUTS_BASE      (CCSRBAR + 0xE0000)
#define GUTS_PORPLLSR  ((volatile uint32_t*)(GUTS_BASE + 0x00UL)) /* POR PLL ratio status register */
#define GUTS_PMUXCR    ((volatile uint32_t*)(GUTS_BASE + 0x60UL))
#define GUTS_PVR       ((volatile uint32_t*)(GUTS_BASE + 0xA0UL))
#define GUTS_SVR       ((volatile uint32_t*)(GUTS_BASE + 0xA4UL))
#define GUTS_DEVDISR   ((volatile uint32_t*)(GUTS_BASE + 0x70UL)) /* Device disable register */
#define GUTS_CPODR(n)  ((volatile uint32_t*)(GUTS_BASE + 0x100 + (n * 32))) /* Open drain register */
#define GUTS_CPDAT(n)  ((volatile uint32_t*)(GUTS_BASE + 0x104 + (n * 32))) /* Data register */
#define GUTS_CPDIR1(n) ((volatile uint32_t*)(GUTS_BASE + 0x108 + (n * 32))) /* Direction register 1 */
#define GUTS_CPDIR2(n) ((volatile uint32_t*)(GUTS_BASE + 0x10C + (n * 32))) /* Direction register 2 */
#define GUTS_CPPAR1(n) ((volatile uint32_t*)(GUTS_BASE + 0x110 + (n * 32))) /* Pin assignment register 1 */
#define GUTS_CPPAR2(n) ((volatile uint32_t*)(GUTS_BASE + 0x114 + (n * 32))) /* Pin assignment register 2 */

#define GUTS_PMUXCR_SDHC_CD 0x40000000
#define GUTS_PMUXCR_SDHC_WP 0x20000000
#define GUTS_PMUXCR_QE0     0x00008000
#define GUTS_PMUXCR_QE3     0x00001000
#define GUTS_PMUXCR_QE9     0x00000040
#define GUTS_PMUXCR_QE12    0x00000008

#define GUTS_DEVDISR_TB0    0x00004000
#define GUTS_DEVDISR_TB1    0x00001000


/* PIC */
#define PIC_BASE    (CCSRBAR + 0x40000)
#define PIC_WHOAMI  ((volatile uint32_t*)(PIC_BASE + 0x0090UL)) /* Returns the ID of the processor core reading this register */
#define PIC_GCR     ((volatile uint32_t*)(PIC_BASE + 0x1020UL)) /* Global configuration register (controls PIC operating mode) */
#define PIC_GCR_RST 0x80000000
#define PIC_GCR_M   0x20000000


/* QUICC Engine */
#define QE_MAX_RISC  1

/* QE microcode/firmware address */
#ifndef QE_FW_NAND
#define QE_FW_NAND   0x01F00000 /* location in NAND flash */
#endif
#ifndef QE_FW_ADDR
#define QE_FW_ADDR   0x10000000 /* location to load into DDR */
#endif
#ifndef QE_FW_LENGTH
#define QE_FW_LENGTH 0x10000    /* Size of microcode (64KB) */
#endif


#define QE_BASE     (CCSRBAR + 0xF000)
#define QE_CEPIER   ((volatile uint32_t*)(QE_BASE + 0x00CUL))
#define QE_CEPIMR   ((volatile uint32_t*)(QE_BASE + 0x010UL))
#define QE_CEPICR   ((volatile uint32_t*)(QE_BASE + 0x014UL))

#define QE_ENGINE_BASE         (CCSRBAR + 0x80000)
#define QE_IRAM                (QE_ENGINE_BASE + 0x000UL) /* Instruction RAM registers */
#define QE_IRAM_IADD           ((volatile uint32_t*)(QE_IRAM + 0x000UL))
#define QE_IRAM_IDATA          ((volatile uint32_t*)(QE_IRAM + 0x004UL))
#define QE_IRAM_IREADY         ((volatile uint32_t*)(QE_IRAM + 0x00CUL))

#define QE_CP                  (QE_ENGINE_BASE + 0x100UL)  /* Configuration register */
#define QE_CP_CECR             ((volatile uint32_t*)(QE_CP + 0x00)) /* command register */
#define QE_CP_CECDR            ((volatile uint32_t*)(QE_CP + 0x08)) /* data register */
#define QE_CP_CERCR            ((volatile uint16_t*)(QE_CP + 0x38)) /* RAM control register */

#define QE_SDMA                (QE_ENGINE_BASE + 0x4000UL) /* Serial DMA */
#define QE_SDMA_SDSR           ((volatile uint32_t*)(QE_SDMA + 0x00))
#define QE_SDMA_SDMR           ((volatile uint32_t*)(QE_SDMA + 0x04))
#define QE_SDMA_SDAQR          ((volatile uint32_t*)(QE_SDMA + 0x38))
#define QE_SDMA_SDAQMR         ((volatile uint32_t*)(QE_SDMA + 0x3C))
#define QE_SDMA_SDEBCR         ((volatile uint32_t*)(QE_SDMA + 0x44))

#define QE_RSP                 (QE_ENGINE_BASE + 0x4100UL) /* Special Registers */
#define QE_RSP_TIBCR(n, i)     ((volatile uint32_t*)(QE_RSP + ((n) * 0x100) + (i)))
#define QE_RSP_ECCR(n)         ((volatile uint32_t*)(QE_RSP + ((n) * 0x100) + 0xF0))

#define QE_IRAM_IADD_AIE       0x80000000 /* Auto Increment Enable */
#define QE_IRAM_IADD_BADDR     0x00080000 /* Base Address */
#define QE_IRAM_READY          0x80000000

#define QE_CP_CERCR_CIR        0x0800 /* Common instruction RAM */

#define QE_CR_FLG              0x00010000
#define QE_CR_PROTOCOL_SHIFT   6

#define QE_SDMR_GLB_1_MSK      0x80000000
#define QE_SDMR_CEN_SHIFT      13
#define QE_SDEBCR_BA_MASK      0x01FFFFFF

/* QE Commands */
#define QE_RESET               0x80000000


/* P1021 PC16552D Dual UART */
#define BAUD_RATE    115200
#define UART_SEL     0 /* select UART 0 or 1 */
#define UART_LCR_VAL (UART_LCR_WLS) /* data=8 bits, stop-1 bit, no parity */

#define UART_BASE(n) (CCSRBAR + 0x4500 + (n * 0x100))

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

/* P1021 eLBC (Enhanced Local Bus Controller) - RM 12.3 */
#define ELBC_BASE        (CCSRBAR + 0x5000UL)
#define ELBC_MAX_BANKS   8
#define ELBC_BANK_SZ     8192

#define ELBC_BR(n)  ((volatile uint32_t*)(ELBC_BASE + 0x0000 + (n * 0x8))) /* Base registers */
#define ELBC_OR(n)  ((volatile uint32_t*)(ELBC_BASE + 0x0004 + (n * 0x8))) /* Options registers */
#define ELBC_MDR    ((volatile uint32_t*)(ELBC_BASE + 0x88))  /* memory data register */
#define ELBC_LSOR   ((volatile uint32_t*)(ELBC_BASE + 0x90))  /* operation initiation register */
#define ELBC_LBCR   ((volatile uint32_t*)(ELBC_BASE + 0xD0))
#define ELBC_LCRR   ((volatile uint32_t*)(ELBC_BASE + 0xD4))  /* clock ratio register */
#define ELBC_FMR    ((volatile uint32_t*)(ELBC_BASE + 0xE0))  /* flash mode register */
#define ELBC_FIR    ((volatile uint32_t*)(ELBC_BASE + 0xE4))  /* flash instruction register */
#define ELBC_FCR    ((volatile uint32_t*)(ELBC_BASE + 0xE8))  /* flash command register */
#define ELBC_FBAR   ((volatile uint32_t*)(ELBC_BASE + 0xEC))  /* flash address register - OR_PGS=0 (shift 5), OR_PGS=1 (shift 6) */
#define ELBC_FPAR   ((volatile uint32_t*)(ELBC_BASE + 0xF0))  /* flash page address register */
#define ELBC_FBCR   ((volatile uint32_t*)(ELBC_BASE + 0xF4))  /* flash byte count register */

#define ELBC_LTESR  ((volatile uint32_t*)(ELBC_BASE + 0xB0))  /* transfer error status register */
#define ELBC_LTEIR  ((volatile uint32_t*)(ELBC_BASE + 0xB8))  /* transfer error interrupt enable register */
#define ELBC_LTEATR ((volatile uint32_t*)(ELBC_BASE + 0xBC))  /* transfer error attributes register */


#define ELBC_BR_ADDR(n)   (((uint32_t)n) & 0xFFFF8000UL) /* Physical base address - upper 17-bits */
#define ELBC_BR_PS(n)     (((n) & 0x3UL) << 11) /* port size - 1=8-bit, 2=16-bit */
#define ELBC_BR_DECC(n)   (((n) & 0x3UL) << 9)  /* data error checking - 0=disabled, 1=ECC check enable / gen disabled, 2=ECC check/gen enabled */
#define ELBC_BR_WP        (1UL << 8)            /* write protect */
#define ELBC_BR_MSEL(n)   (((n) & 0x7UL) << 5)  /* machine select:
                                               *   0=GPCM (General Purpose Chip-Select Machine)
                                               *   1=FCM (Flash Control Machine),
                                               *   4=UPMA, 5=UPMB, 6=UPMC (User Programmable Machines) */
#define ELBC_BR_V         (1UL << 0)            /* bank valid */

/* eLBC OR */
#define ELBC_OR_AMASK(n)  (((uint32_t)n) & 0xFFFF8000) /* Address mask - upper 17-bits */

/* eLBC OR (FCM) */
#define ELBC_ORF_BCTLD    (1UL << 12) /* buffer control disable */
#define ELBC_ORF_PGS      (1UL << 10) /* page size 0=512, 1=2048 bytes */
#define ELBC_ORF_CSCT     (1UL << 9)  /* chip select to command time - TRLX=0 (0=1, 1=4), TRLX=1 (0=2, 1=8) clock cycles */
#define ELBC_ORF_CST      (1UL << 8)  /* command setup time - TRLX=0 (0=0 or 1=0.25) TRLX=1 (0=0.5 or 1=1) clock cycles */
#define ELBC_ORF_CHT      (1UL << 7)  /* command hold time - TRLX=0 (0=0.5 or 1=1) TRLX=1 (0=1.5 or 1=2) clock cycles */
#define ELBC_ORF_SCY(n)   (((n) & 0x7UL) << 4) /* cycle length in bus clocks (0-7 bus clock cycle wait states) */
#define ELBC_ORF_RST      (1UL << 3)  /* read time setup - read enable asserted 1 clock */
#define ELBC_ORF_TRLX     (1UL << 2)  /* timing related */
#define ELBC_ORF_EHTR     (1UL << 1)  /* extended hold time - LRLX=0 (0=1 or 1=2), LRLX=1 (0=2 or 1=8) inserted idle clock cycles */

/* eLBC OR (GPCM) */
#define ELBC_ORG_CSCT (1UL << 11)
#define ELBC_ORG_XACS (1UL << 8)
#define ELBC_ORG_SCY  (1UL << 4)
#define ELBC_ORG_TRLX (1UL << 2)
#define ELBC_ORG_EHTR (1UL << 1)
#define ELBC_ORG_EAD  (1UL << 0)


#define ELBC_LSOR_BANK(n) ((n) & (ELBC_MAX_BANKS-1UL)) /* flash bank 0-7 */

#define ELBC_LBCR_ABSWP    (1 << 19)    /* Address byte swap for 16-bit port size */
#define ELBC_LBCR_BMTPS(n) ((n) & 0xF) /* Bus monitor timer prescale */

#define ELBC_FMR_CWTO(n)  (((n) & 0xF) << 12) /* command wait timeout 0=256 cycles, 15=8,388,608 cycles of LCLK */
#define ELBC_FMR_BOOT     (1 << 11) /* flash auto-boot lead mode 0=FCM is op normal, 1=eLBC autoload 4-Kbyte boot block */
#define ELBC_FMR_ECCM     (1 << 8)  /* ECC mode 0=ECC is checked/calc 6/8 spare, 1=8/10 spare */
#define ELBC_FMR_AL(n)    (((n) & 0x3UL) << 4) /* address length 0=2 bytes, 1=3 bytes, 2=4 bytes issued for page address */
#define ELBC_FMR_OP(n)    (((n) & 0x3UL) << 0) /* flash operation 0=normal, 1=sim auto-boot block load, 2=exe FIR cmd w/write protect enable, 3=exe FIR cmd */

#define ELBC_FIR_OP(s,op) ((op) & 0xFUL) << (28 - ((s % 8) * 4)) /* up to 8 sequences of instructions */
#define ELBC_FIR_OP_NOP 0  /* No-operation and end of operation sequence */
#define ELBC_FIR_OP_CA  1  /* Issue current column address as set in FPAR, with length set by ORx[PGS] */
#define ELBC_FIR_OP_PA  2  /* Issue current block+page address as set in FBAR and FPAR, with length set by FMR[AL] */
#define ELBC_FIR_OP_UA  3  /* Issue user-defined address byte from next AS field in MDR */
#define ELBC_FIR_OP_CM0 4  /* Issue command from FCR[CMD0] */
#define ELBC_FIR_OP_CM1 5  /* Issue command from FCR[CMD1] */
#define ELBC_FIR_OP_CM2 6  /* Issue command from FCR[CMD2] */
#define ELBC_FIR_OP_CM3 7  /* Issue command from FCR[CMD3] */
#define ELBC_FIR_OP_WB  8  /* Write FBCR bytes of data from current FCM buffer to Flash device */
#define ELBC_FIR_OP_WS  9  /* Write one byte (8b port) of data from next AS field of MDR to Flash device */
#define ELBC_FIR_OP_RB  10 /* Read FBCR bytes of data from Flash device into current FCM RAM buffer */
#define ELBC_FIR_OP_RS  11 /* Read one byte (8b port) of data from Flash device into next AS field of MDR */
#define ELBC_FIR_OP_CW0 12 /* Wait for LFRB to return high or time-out, then issue command from FCR[CMD0] */
#define ELBC_FIR_OP_CW1 13 /* Wait for LFRB to return high or time-out, then issue command from FCR[CMD1] */
#define ELBC_FIR_OP_RBW 14 /* Wait for LFRB to return high or time-out, then read FBCR bytes of data from Flash device into current FCM RAM buffer */
#define ELBC_FIR_OP_RSW 15 /* Wait for LFRB to return high or time-out, then read one byte (8b port) of data from Flash device into next AS field of MDR */
#define ELBC_FCR_CMD(s,cmd) (((cmd) & 0xFFUL) << (24 - ((s % 4) * 8))) /* up to 4 command opcodes */

#define ELBC_LCRR_CLKDIV_MASK 0x0000001F
#define ELBC_LCRR_CLKDIV_4  0x2
#define ELBC_LCRR_CLKDIV_8  0x4 /* default */
#define ELBC_LCRR_CLKDIV_16 0x8

/* SP=Small page */
#define ELBC_FPAR_SP_PI_MASK (0x00007C00)
#define ELBC_FPAR_SP_PI(n)   (((n) << 10) & ELBC_FPAR_SP_PI_MASK) /* page index */
#define ELBC_FPAR_SP_MS      (1 << 9)  /* main/spare region locator (0=main, 1=spare) */
#define ELBC_FPAR_SP_CI(n)   ((n) & 0x1FF) /* Column Index: If FBCR[BC] = 0 a col of zero is always used */
/* LP=Large Page */
#define ELBC_FPAR_LP_PI_MASK (0x0003F000)
#define ELBC_FPAR_LP_PI(n)   (((n) << 12) & ELBC_FPAR_LP_PI_MASK) /* page index */
#define ELBC_FPAR_LP_MS      (1 << 11) /* main/spare region locator (0=main, 1=spare) */
#define ELBC_FPAR_LP_CI(n)   ((n) & 0x7FF) /* Column Index: If FBCR[BC] = 0 a col of zero is always used */

#define ELBC_LTESR_FCT (1 << 30) /* FCM command timeout */
#define ELBC_LTESR_PAR (1 << 29) /* Parity of ECC error */
#define ELBC_LTESR_CC  (1 << 0)  /* FCM command completion event */

#define ELBC_NAND_MASK (ELBC_LTESR_FCT | ELBC_LTESR_PAR | ELBC_LTESR_CC)


/* eLBC AMASK - RM Table 12-6 - Count of MSB minus 1 */
enum elbc_amask_sizes {
    ELBC_AMASK_32KB =  0xFFFF8000,
    ELBC_AMASK_64KB =  0xFFFF0000,
    ELBC_AMASK_128KB = 0xFFFE0000,
    ELBC_AMASK_256KB = 0xFFFC0000,
    ELBC_AMASK_512KB = 0xFFF80000,
    ELBC_AMASK_1MB   = 0xFFF00000,
    ELBC_AMASK_2MB   = 0xFFE00000,
    ELBC_AMASK_4MB   = 0xFFC00000,
    ELBC_AMASK_8MB   = 0xFF800000,
    ELBC_AMASK_16MB  = 0xFF000000,
    ELBC_AMASK_32MB  = 0xFE000000,
    ELBC_AMASK_64MB  = 0xFC000000,
    ELBC_AMASK_128MB = 0xF8000000,
    ELBC_AMASK_256MB = 0xF0000000,
    ELBC_AMASK_512MB = 0xE0000000,
    ELBC_AMASK_1GB   = 0xC0000000,
    ELBC_AMASK_2GB   = 0x80000000,
    ELBC_AMASK_4GB   = 0x00000000,
};


/* NAND Flash */
#define FLASH_BANK        0 /* CS0 */
#define FLASH_PAGE_SIZE   (512) /* only 512 or 2048 (large) */

#define FLASH_TIMEOUT_TRIES 1000000

/* NAND Flash Commands */
#define NAND_CMD_READ_ID      0x90
#define NAND_CMD_STATUS       0x70
#define NAND_CMD_READA        0x00
#define NAND_CMD_READB        0x01
#define NAND_CMD_READC        0x50
#define NAND_CMD_PAGE_PROG1   0x80
#define NAND_CMD_PAGE_PROG2   0x10
#define NAND_CMD_BLOCK_ERASE1 0x60
#define NAND_CMD_BLOCK_ERASE2 0xD0
#define NAND_CMD_RESET        0xFF

#define NAND_CMD_READSTART    0x30 /* Extended command for large page devices */


/* DDR */
/* DDR3: 512MB, 333.333 MHz (666.667 MT/s) */
#define DDR_CS0_BNDS_VAL       0x0000001F
#define DDR_CS0_CONFIG_VAL     0x80014202
#define DDR_CS_CONFIG_2_VAL    0x00000000

#define DDR_TIMING_CFG_0_VAL   0x00330004
#define DDR_TIMING_CFG_1_VAL   0x5D5BD746
#define DDR_TIMING_CFG_2_VAL   0x0FA8C8CD
#define DDR_TIMING_CFG_3_VAL   0x00010000
#define DDR_TIMING_CFG_4_VAL   0x00220001
#define DDR_TIMING_CFG_5_VAL   0x03402400

#define DDR_SDRAM_MODE_VAL     0x40461320
#define DDR_SDRAM_MODE_2_VAL   0x8000C000
#define DDR_SDRAM_MD_CNTL_VAL  0x00000000

#define DDR_SDRAM_CFG_VAL      0x670C0000
#define DDR_SDRAM_CFG_2_VAL    0x04401050

#define DDR_SDRAM_INTERVAL_VAL 0x0A280000

#ifndef DDR_DATA_INIT_VAL
#define DDR_DATA_INIT_VAL      0x1021BABE
#endif
#define DDR_SDRAM_CLK_CNTL_VAL 0x03000000
#define DDR_ZQ_CNTL_VAL        0x89080600

#define DDR_WRLVL_CNTL_VAL     0x86559608

#define DDR_DDRCDR_1_VAL       0x000EAA00
#define DDR_DDRCDR_2_VAL       0x00000000

#define DDR_ERR_INT_EN_VAL     0x0000000D
#define DDR_ERR_SBE_VAL        0x00010000


/* 12.4 DDR Memory Map */
#define DDR_BASE           (CCSRBAR + 0x2000)

#define DDR_CS_BNDS(n)     ((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   ((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_CS_CONFIG_2(n) ((volatile uint32_t*)(DDR_BASE + 0x0C0 + (n * 4))) /* Chip select n configuration 2 */
#define DDR_SDRAM_CFG      ((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    ((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_INTERVAL ((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_INIT_ADDR      ((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  ((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_DATA_INIT      ((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_TIMING_CFG_0   ((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   ((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   ((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_TIMING_CFG_3   ((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_4   ((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   ((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_ZQ_CNTL        ((volatile uint32_t*)(DDR_BASE + 0x170)) /* DDR ZQ calibration control */
#define DDR_WRLVL_CNTL     ((volatile uint32_t*)(DDR_BASE + 0x174)) /* DDR write leveling control */
#define DDR_WRLVL_CNTL_2   ((volatile uint32_t*)(DDR_BASE + 0x190)) /* DDR write leveling control 2 */
#define DDR_WRLVL_CNTL_3   ((volatile uint32_t*)(DDR_BASE + 0x194)) /* DDR write leveling control 3 */
#define DDR_SR_CNTR        ((volatile uint32_t*)(DDR_BASE + 0x17C)) /* DDR Self Refresh Counter */
#define DDR_SDRAM_RCW_1    ((volatile uint32_t*)(DDR_BASE + 0x180)) /* DDR Register Control Word 1 */
#define DDR_SDRAM_RCW_2    ((volatile uint32_t*)(DDR_BASE + 0x184)) /* DDR Register Control Word 2 */
#define DDR_DDRCDR_1       ((volatile uint32_t*)(DDR_BASE + 0xB28)) /* DDR Control Driver Register 1 */
#define DDR_DDRCDR_2       ((volatile uint32_t*)(DDR_BASE + 0xB2C)) /* DDR Control Driver Register 2 */
#define DDR_DDRDSR_1       ((volatile uint32_t*)(DDR_BASE + 0xB20)) /* DDR Debug Status Register 1 */
#define DDR_DDRDSR_2       ((volatile uint32_t*)(DDR_BASE + 0xB24)) /* DDR Debug Status Register 2 */
#define DDR_ERR_DISABLE    ((volatile uint32_t*)(DDR_BASE + 0xE44)) /* Memory error disable */
#define DDR_ERR_INT_EN     ((volatile uint32_t*)(DDR_BASE + 0xE48)) /* Memory error interrupt enable */
#define DDR_ERR_SBE        ((volatile uint32_t*)(DDR_BASE + 0xE58)) /* Single-Bit ECC memory error management */
#define DDR_SDRAM_MODE     ((volatile uint32_t*)(DDR_BASE + 0x118)) /* DDR SDRAM mode configuration */
#define DDR_SDRAM_MODE_2   ((volatile uint32_t*)(DDR_BASE + 0x11C)) /* DDR SDRAM mode configuration 2 */
#define DDR_SDRAM_MD_CNTL  ((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_CLK_CNTL ((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG_ECC_EN   0x20000000
#define DDR_SDRAM_CFG_32_BE    0x00080000
#define DDR_SDRAM_CFG_2_D_INIT 0x00000010 /* data initialization in progress */
#define DDR_SDRAM_CFG_BI       0x00000001 /* Bypass initialization */


/* CPLD - Board Configuration and Status Registers */
#define BCSR_BASE            0xF8000000
#define BCSR11               ((volatile uint8_t*)(BCSR_BASE + 11))
#define BCSR11_ENET_MICRST   0x20

#define ECM_BASE             (CCSRBAR + 0x1000)
#define ECM_EEBACR           ((volatile uint32_t*)(ECM_BASE + 0x00)) /* ECM CCB address configuration register */
#define ECM_EEBPCR           ((volatile uint32_t*)(ECM_BASE + 0x10)) /* ECM CCB port configuration register */
#define ECM_EEBPCR_CPU_EN(n) ((n) << 24)


/* eSPI */
#define ESPI_MAX_CS_NUM      4
#define ESPI_MAX_RX_LEN      (1 << 16)
#define ESPI_FIFO_WORD       4

#define ESPI_BASE            (CCSRBAR + 0x7000)
#define ESPI_SPMODE          ((volatile uint32_t*)(ESPI_BASE + 0x00)) /* controls eSPI general operation mode */
#define ESPI_SPIE            ((volatile uint32_t*)(ESPI_BASE + 0x04)) /* controls interrupts and report events */
#define ESPI_SPIM            ((volatile uint32_t*)(ESPI_BASE + 0x08)) /* enables/masks interrupts */
#define ESPI_SPCOM           ((volatile uint32_t*)(ESPI_BASE + 0x0C)) /* command frame information */
#define ESPI_SPITF           ((volatile uint32_t*)(ESPI_BASE + 0x10)) /* transmit FIFO access register (32-bit) */
#define ESPI_SPIRF           ((volatile uint32_t*)(ESPI_BASE + 0x14)) /* read-only receive data register (32-bit) */
#define ESPI_SPITF8          ((volatile uint8_t*)( ESPI_BASE + 0x10)) /* transmit FIFO access register (8-bit) */
#define ESPI_SPIRF8          ((volatile uint8_t*)( ESPI_BASE + 0x14)) /* read-only receive data register (8-bit) */
#define ESPI_SPCSMODE(x)     ((volatile uint32_t*)(ESPI_BASE + 0x20 + ((cs) * 4))) /* controls master operation with chip select 0-3 */

#define ESPI_SPMODE_EN       (0x80000000) /* Enable eSPI */
#define ESPI_SPMODE_TXTHR(x) ((x) << 8)   /* Tx FIFO threshold (1-32) */
#define ESPI_SPMODE_RXTHR(x) ((x) << 0)   /* Rx FIFO threshold (0-31) */

#define ESPI_SPCOM_CS(x)     ((x) << 30)       /* Chip select-chip select for which transaction is destined */
#define ESPI_SPCOM_RXSKIP(x) ((x) << 16)       /* Number of characters skipped for reception from frame start */
#define ESPI_SPCOM_TRANLEN(x) (((x) - 1) << 0) /* Transaction length */

#define ESPI_SPIE_TXE        (1 << 15) /* transmit empty */
#define ESPI_SPIE_DON        (1 << 14) /* Last character was transmitted */
#define ESPI_SPIE_RXT        (1 << 13) /* Rx FIFO has more than RXTHR bytes */
#define ESPI_SPIE_RNE        (1 << 9)  /* receive not empty */
#define ESPI_SPIE_TNF        (1 << 8)  /* transmit not full */
#define ESPI_SPIE_RXCNT(n)   (((n) >> 24) & 0x3F) /* The current number of full Rx FIFO bytes */

#define ESPI_CSMODE_CI       0x80000000 /* Inactive high */
#define ESPI_CSMODE_CP       0x40000000 /* Begin edge clock */
#define ESPI_CSMODE_REV      0x20000000 /* MSB first */
#define ESPI_CSMODE_DIV16    0x10000000 /* divide system clock by 16 */
#define ESPI_CSMODE_PM(x)    (((x) & 0xF) << 24) /* presale modulus select */
#define ESPI_CSMODE_POL      0x00100000  /* asserted low */
#define ESPI_CSMODE_LEN(x)   ((((x) - 1) & 0xF) << 16) /* Character length in bits per character */
#define ESPI_CSMODE_CSBEF(x) (((x) & 0xF) << 12) /* CS assertion time in bits before frame start */
#define ESPI_CSMODE_CSAFT(x) (((x) & 0xF) << 8)  /* CS assertion time in bits after frame end */
#define ESPI_CSMODE_CSCG(x)  (((x) & 0xF) << 3)  /* Clock gaps between transmitted frames according to this size */

#ifdef ENABLE_ELBC

static volatile uint8_t* flash_buf;
static uint32_t          flash_idx;

/* forward declaration */
int ext_flash_read(uintptr_t address, uint8_t *data, int len);
#endif

/* generic share NXP QorIQ driver code */
#include "nxp_ppc.c"


/* local functions */
#ifdef ENABLE_BUS_CLK_CALC
static uint32_t hal_get_bus_clk(void)
{
    /* compute bus clock (system input 66MHz * ratio) */
    uint32_t bus_clk;
    uint32_t plat_ratio = get32(GUTS_PORPLLSR);
    /* mask and shift by 1 to get platform ratio */
    plat_ratio = ((plat_ratio & 0x3E) >> 1);
    bus_clk = SYS_CLK * plat_ratio;
    return bus_clk;
}
#else
#define hal_get_bus_clk() (uint32_t)(SYS_CLK * 6)
#endif

#define DELAY_US (hal_get_bus_clk() / 1000000)
static void udelay(uint32_t delay_us)
{
    wait_ticks(delay_us * DELAY_US);
}

#if 0 /* useful timer code */

uint64_t hal_timer_ms(void)
{
    uint64_t val;
    /* time base is updated every 8 CCB clocks */
    uint64_t cntfrq = hal_get_bus_clk() / 8;
    uint64_t cntpct = get_ticks();
    val = (cntpct * 1000ULL) / cntfrq;
    return val;
}

/* example usage */
//uint64_t start = hal_get_tick_count();
// do some work
//wolfBoot_printf("done (%lu ms)\n", (uint32_t)hal_elapsed_time_ms(start));

/* Calculate elapsed time in milliseconds, handling timer overflow properly */
uint64_t hal_elapsed_time_ms(uint64_t start_ticks)
{
    uint64_t current_ticks, elapsed_ticks;
    uint64_t cntfrq = hal_get_bus_clk() / 8;

    current_ticks = get_ticks();

    /* Handle timer overflow using unsigned arithmetic
     * This works correctly even if the timer has rolled over,
     * as long as the elapsed time is less than the full timer range
     */
    elapsed_ticks = current_ticks - start_ticks;

    /* Convert elapsed ticks to milliseconds */
    return (elapsed_ticks * 1000ULL) / cntfrq;
}

/* Get current tick count for use with hal_elapsed_time_ms() */
uint64_t hal_get_tick_count(void)
{
    return get_ticks();
}
#endif

/* ---- eSPI Driver ---- */
#ifdef ENABLE_ESPI
void hal_espi_init(uint32_t cs, uint32_t clock_hz, uint32_t mode)
{
    uint32_t spibrg = hal_get_bus_clk() / 2, pm, csmode;

    /* Enable eSPI with TX threadshold 4 and RX threshold 3 */
    set32(ESPI_SPMODE, (ESPI_SPMODE_EN | ESPI_SPMODE_TXTHR(4) |
        ESPI_SPMODE_RXTHR(3)));

    set32(ESPI_SPIE, 0xffffffff); /* Clear all eSPI events */
    set32(ESPI_SPIM, 0x00000000); /* Mask all eSPI interrupts */

    csmode = (ESPI_CSMODE_REV | ESPI_CSMODE_POL | ESPI_CSMODE_LEN(8) |
        ESPI_CSMODE_CSBEF(0) | ESPI_CSMODE_CSAFT(0) | ESPI_CSMODE_CSCG(1));

    /* calculate clock divisor */
    if (spibrg / clock_hz > 16) {
        csmode |= ESPI_CSMODE_DIV16;
        pm = (spibrg / (clock_hz * 16));
    }
    else {
        pm = (spibrg / (clock_hz));
    }
    if (pm > 0)
        pm--;

    csmode |= ESPI_CSMODE_PM(pm);

    if (mode & 1)
        csmode |= ESPI_CSMODE_CP;
    if (mode & 2)
        csmode |= ESPI_CSMODE_CI;

    /* configure CS */
    set32(ESPI_SPCSMODE(cs), csmode);
}

int hal_espi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz,
    int flags)
{
    uint32_t mosi, miso, xfer, event;

#ifdef DEBUG_ESPI
    wolfBoot_printf("CS %d, Sz %d, Flags %x\n", cs, sz, flags);
#endif

    if (sz > 0) {
        /* assert CS - use max length and control CS with mode enable toggle */
        set32(ESPI_SPCOM, ESPI_SPCOM_CS(cs) | ESPI_SPCOM_TRANLEN(0x10000));
        set32(ESPI_SPIE, 0xffffffff); /* Clear all eSPI events */
    }
    while (sz > 0) {
        xfer = ESPI_FIFO_WORD;
        if (xfer > sz)
            xfer = sz;

        /* Transfer 4 or 1 */
        if (xfer == ESPI_FIFO_WORD) {
            set32(ESPI_SPITF, *((uint32_t*)tx));
        }
        else {
            xfer = 1;
            set8(ESPI_SPITF8, *((uint8_t*)tx));
        }

        /* wait till TX fifo is empty or done */
        while (1) {
            event = get32(ESPI_SPIE);
            if (event & (ESPI_SPIE_TXE | ESPI_SPIE_DON)) {
                /* clear events */
                set32(ESPI_SPIE, (ESPI_SPIE_TXE | ESPI_SPIE_DON));
                break;
            }
        }

        /* wait till RX has enough data */
        while (1) {
            event = get32(ESPI_SPIE);
            if ((event & ESPI_SPIE_RNE) == 0)
                continue;
        #if defined(DEBUG_ESPI) && DEBUG_ESPI > 1
            wolfBoot_printf("event %x\n", event);
        #endif
            if (ESPI_SPIE_RXCNT(event) >= xfer)
                break;
        }
        if (xfer == ESPI_FIFO_WORD) {
            *((uint32_t*)rx) = get32(ESPI_SPIRF);
        }
        else {
            *((uint8_t*)rx) = get8(ESPI_SPIRF8);
        }

#ifdef DEBUG_ESPI
        wolfBoot_printf("MOSI %x, MISO %x\n",
            *((uint32_t*)tx), *((uint32_t*)rx));
#endif
        tx += xfer;
        rx += xfer;
        sz -= xfer;
    }

    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        /* toggle ESPI_SPMODE_EN - to deassert CS */
        set32(ESPI_SPMODE, get32(ESPI_SPMODE) & ~ESPI_SPMODE_EN);
        set32(ESPI_SPMODE, get32(ESPI_SPMODE) | ESPI_SPMODE_EN);
    }

    return 0;
}
void hal_espi_deinit(void)
{
    /* do nothing */
}
#endif /* ENABLE_ESPI */

/* ---- DUART Driver ---- */
#ifdef DEBUG_UART

void uart_init(void)
{
    /* calc divisor for UART
     * baud rate = CCSRBAR frequency ÷ (16 x [UDMB||UDLB])
     */
    /* compute UART divisor - round up */
    uint32_t div = (hal_get_bus_clk() + (16/2 * BAUD_RATE)) / (16 * BAUD_RATE);

    while (!(get8(UART_LSR(UART_SEL)) & UART_LSR_TEMT))
       ;

    /* set ier, fcr, mcr */
    set8(UART_IER(UART_SEL), 0);
    set8(UART_FCR(UART_SEL), (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN));

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    set8(UART_LCR(UART_SEL), (UART_LCR_DLAB | UART_LCR_WLS));
    /* set divisor */
    set8(UART_DLB(UART_SEL), (div & 0xff));
    set8(UART_DMB(UART_SEL), ((div>>8) & 0xff));
    /* disable rate access (DLAB=0) */
    set8(UART_LCR(UART_SEL), (UART_LCR_WLS));
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
            set8(UART_THR(UART_SEL), '\r');
        }
        while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
        set8(UART_THR(UART_SEL), c);
    }
}
#endif /* DEBUG_UART */

/* ---- eLBC Driver ---- */
#ifdef ENABLE_ELBC
static void hal_flash_set_addr(int page, int col)
{
    uint32_t buf_num;
    uint32_t fbar, fpar;

#if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
    /* large page - ELBC_ORF_PGS=1 */
    fbar = (page >> 6);
    fpar = (ELBC_FPAR_LP_PI(page) | ELBC_FPAR_LP_CI(col));
    buf_num = (page & 1) << 2; /* 0 or 4 */
#else
    /* small page */
    fbar = (page >> 5);
    fpar = (ELBC_FPAR_SP_PI(page) | ELBC_FPAR_SP_CI(col));
    buf_num = (page & 7); /* 0-7 */
#endif
    set32(ELBC_FBAR, fbar);
    set32(ELBC_FPAR, fpar);

    /* calculate buffer for FCM - there are 8 1KB pages */
    flash_buf = (uint8_t*)(FLASH_BASE_ADDR + (buf_num * 1024));
    flash_idx = col;

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("set addr %p, page %d, col %d, fbar 0x%x, fpar 0x%x\n",
        flash_buf, page, col, fbar, fpar);
#endif
}

/* iswrite (read=0, write=1) */
static int hal_flash_command(uint8_t iswrite)
{
    int ret = 0;
    int timeout = 0;
    uint32_t fmr =
        ELBC_FMR_CWTO(15) |       /* max timeout */
        ELBC_FMR_AL(2) |          /* 4 byte address */
        ELBC_FMR_OP(2 + iswrite); /* execute FIR with write support */
#if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
    /* large page - ELBC_ORF_PGS=1 */
    fmr |= ELBC_FMR_ECCM; /* large page should have ECCM=1 */
#endif

    set32(ELBC_FMR, fmr);
    set32(ELBC_LSOR, ELBC_LSOR_BANK(FLASH_BANK)); /* start special op on bank */

    /* wait for FCM complete flag */
    while (!(get32(ELBC_LTESR) & ELBC_LTESR_CC) &&
        timeout++ < FLASH_TIMEOUT_TRIES) {
        /* NOP */
    };
    if (timeout == FLASH_TIMEOUT_TRIES) {
        ret = -1;
    }

    /* clear interrupt */
    set32(ELBC_LTESR, get32(ELBC_LTESR) & ELBC_NAND_MASK);
    set32(ELBC_LTEATR, 0);

    return ret;
}

/* assume input/output buffers are 32-bit aligned */
static void hal_flash_read_bytes(uint8_t* data, size_t len)
{
#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("read %p to %p, len %d\n",
        &flash_buf[flash_idx], data, len);
#endif
    /* copy data from internal eLBC FCM buffer */
    while (flash_idx < len) {
        *((volatile uint32_t*)data) =
            *(volatile uint32_t*)(&flash_buf[flash_idx]);
        flash_idx += 4;
        data += 4;
    }
}
/* assume input/output buffers are 32-bit aligned */
static void hal_flash_write_bytes(const uint8_t* data, size_t len)
{
#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("write %p to %p, len %d\n",
        data, &flash_buf[flash_idx], len);
#endif
    /* copy data to internal eLBC FCM buffer */
    while (flash_idx < len) {
        *(volatile uint32_t*)(&flash_buf[flash_idx]) =
            *((volatile uint32_t*)data);
        flash_idx += 4;
        data += 4;
    }
}

static int hal_flash_reset(void)
{
    /* Reset the chip */
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0));
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_RESET));
    return hal_flash_command(0);
}

static int hal_flash_read_id(uint32_t* flash_id)
{
    int ret;

    /* Read Flash ID */
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_UA) |
                    ELBC_FIR_OP(2, ELBC_FIR_OP_RS) |
                    ELBC_FIR_OP(3, ELBC_FIR_OP_RS) |
                    ELBC_FIR_OP(4, ELBC_FIR_OP_RS) |
                    ELBC_FIR_OP(5, ELBC_FIR_OP_RS) |
                    ELBC_FIR_OP(6, ELBC_FIR_OP_RS));
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_READ_ID));
    set32(ELBC_FBCR, 0);
    set32(ELBC_MDR, 0);
    hal_flash_set_addr(0, 0);
    ret = hal_flash_command(0);

    *flash_id = get32(ELBC_MDR); /* 0x76207620 = ST NAND512W3A,
                                  * 0x9580F12C = Micron MT29F1G08ABAEA */

    return ret;
}

static int hal_flash_read_status(uint32_t* status)
{
    int ret;

    /* Read flash status */
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_RS));
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_STATUS));
    set32(ELBC_FBCR, 0);
    set32(ELBC_MDR, 0);
    hal_flash_set_addr(0, 0);
    ret = hal_flash_command(0);

    *status = get32(ELBC_MDR) & 0xFF;

    return ret;
}
#endif /* ENABLE_ELBC */

static int hal_flash_init(void)
{
    int ret = 0;
#ifdef ENABLE_ELBC
    uint32_t reg;
    uint32_t flash_id[1] = {0};

    /* eLBC - NAND Flash */
    set_law(4, 0, FLASH_BASE_ADDR, LAW_TRGT_ELBC, LAW_SIZE_1MB, 1);

#ifdef BOOT_ROM_ADDR
    /* if this code is executing from BOOT ROM we cannot init eLBC yet */
    if ((get_pc() & BOOT_ROM_ADDR) == BOOT_ROM_ADDR) {
        return -1;
    }
#endif

    /* Set eLBC clock divisor = 8 */
    reg = get32(ELBC_LCRR);
    reg &= ~ELBC_LCRR_CLKDIV_MASK;
    set32(ELBC_LCRR, (reg | ELBC_LCRR_CLKDIV_8));

    /* NAND Definitions (CS0) */
    /* FCM, 8-bit, ECC check/gen enable, valid */
    set32(ELBC_BR(FLASH_BANK), ELBC_BR_ADDR(FLASH_BASE_ADDR) |
        ELBC_BR_MSEL(1) | ELBC_BR_PS(1) | ELBC_BR_DECC(2) | ELBC_BR_V);

    /* Set address mask, page size, relaxed timing */
    set32(ELBC_OR(FLASH_BANK), (
        ELBC_ORF_CSCT | ELBC_ORF_CST | ELBC_ORF_CHT | ELBC_ORF_SCY(1) |
        ELBC_ORF_TRLX | ELBC_ORF_EHTR
    #if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
        /* Large page size and 256KB address mask */
        | ELBC_ORF_PGS | ELBC_OR_AMASK(ELBC_AMASK_256KB)
    #else
        /* Small page size and 32KB address mask */
        | ELBC_OR_AMASK(ELBC_AMASK_32KB)
    #endif
    ));

    /* Clear event registers */
    set32(ELBC_LTESR, ELBC_NAND_MASK);
    set32(ELBC_LTEATR, 0);

    /* Enable interrupts */
    set32(ELBC_LTEIR, ELBC_NAND_MASK);

    /* Enable LBC address byte swap */
    set32(ELBC_LBCR, ELBC_LBCR_ABSWP);

    /* Reset chip */
    ret = hal_flash_reset();
    if (ret == 0) {
        /* Read Flash ID */
        ret = hal_flash_read_id(flash_id);
    }

#ifdef PRINTF_ENABLED
    wolfBoot_printf("Flash Init: Ret %d, ID 0x%08lx\n", ret, flash_id[0]);
#elif defined(DEBUG_UART) && !defined(BUILD_LOADER_STAGE1)
    uart_write("Flash Init\n", 11);
#endif
#endif /* ENABLE_ELBC */
    return ret;
}

static void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint32_t reg;

    /* Map LAW for DDR */
    set_law(6, 0, DDR_ADDRESS, LAW_TRGT_DDR, LAW_SIZE_512MB, 0);

    /* If DDR is already enabled then just return */
    if ((get32(DDR_SDRAM_CFG) & DDR_SDRAM_CFG_MEM_EN)) {
        return;
    }

    /* Setup DDR CS (chip select) bounds */
    set32(DDR_CS_BNDS(0), DDR_CS0_BNDS_VAL);
    set32(DDR_CS_CONFIG(0), DDR_CS0_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(0), DDR_CS_CONFIG_2_VAL);

    /* DDR SDRAM timing configuration */
    set32(DDR_TIMING_CFG_3, DDR_TIMING_CFG_3_VAL);
    set32(DDR_TIMING_CFG_0, DDR_TIMING_CFG_0_VAL);
    set32(DDR_TIMING_CFG_1, DDR_TIMING_CFG_1_VAL);
    set32(DDR_TIMING_CFG_2, DDR_TIMING_CFG_2_VAL);

    set32(DDR_SDRAM_MODE,   DDR_SDRAM_MODE_VAL);
    set32(DDR_SDRAM_MODE_2, DDR_SDRAM_MODE_2_VAL);
    set32(DDR_SDRAM_MD_CNTL, DDR_SDRAM_MD_CNTL_VAL);
    set32(DDR_SDRAM_INTERVAL, DDR_SDRAM_INTERVAL_VAL);
    set32(DDR_DATA_INIT, DDR_DATA_INIT_VAL);
    set32(DDR_SDRAM_CLK_CNTL, DDR_SDRAM_CLK_CNTL_VAL);
    set32(DDR_TIMING_CFG_4, DDR_TIMING_CFG_4_VAL);
    set32(DDR_TIMING_CFG_5, DDR_TIMING_CFG_5_VAL);
    set32(DDR_ZQ_CNTL, DDR_ZQ_CNTL_VAL);
    set32(DDR_WRLVL_CNTL, DDR_WRLVL_CNTL_VAL);

    set32(DDR_SR_CNTR, 0);
    set32(DDR_SDRAM_RCW_1, 0);
    set32(DDR_SDRAM_RCW_2, 0);

    set32(DDR_DDRCDR_1, DDR_DDRCDR_1_VAL);


    set32(DDR_SDRAM_CFG_2, DDR_SDRAM_CFG_2_VAL);
    set32(DDR_INIT_ADDR, 0);
    set32(DDR_INIT_EXT_ADDR, 0);
    set32(DDR_DDRCDR_2, DDR_DDRCDR_2_VAL);

    /* Set values, but do not enable the DDR yet */
    set32(DDR_SDRAM_CFG, ((DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN)));
    __asm__ __volatile__("sync;isync");

    /* busy wait for ~500us */
    udelay(500);

    /* Enable controller */
    reg = get32(DDR_SDRAM_CFG) & ~DDR_SDRAM_CFG_BI;
    set32(DDR_SDRAM_CFG, reg | DDR_SDRAM_CFG_MEM_EN);
    __asm__ __volatile__("sync;isync");

    /* Wait for data initialization to complete */
    while (get32(DDR_SDRAM_CFG_2) & DDR_SDRAM_CFG_2_D_INIT) {
        /* busy wait loop - throttle polling */
        udelay(1);
    }
#endif /* ENABLE_DDR */
}

void hal_early_init(void)
{
    hal_ddr_init();
}


#ifdef ENABLE_PCIE
#define CONFIG_SYS_PCIE1_MEM_PHYS 0xc0000000
#define CONFIG_SYS_PCIE1_IO_PHYS  0xffc20000
#define CONFIG_SYS_PCIE1_MEM_VIRT 0xc0000000
#define CONFIG_SYS_PCIE1_IO_VIRT  0xffc20000

#define CONFIG_SYS_PCIE2_MEM_PHYS 0xa0000000
#define CONFIG_SYS_PCIE2_IO_PHYS  0xffc10000
#define CONFIG_SYS_PCIE2_MEM_VIRT 0xa0000000
#define CONFIG_SYS_PCIE2_IO_VIRT  0xffc10000
static int hal_pcie_init(void)
{
    /* Map LAW for PCIe */
    set_law(0, 0, CONFIG_SYS_PCIE1_MEM_PHYS, LAW_TRGT_PCIE1, LAW_SIZE_512MB, 1);
    set_law(1, 0, CONFIG_SYS_PCIE1_IO_PHYS,  LAW_TRGT_PCIE1, LAW_SIZE_64KB, 1);
    set_law(2, 0, CONFIG_SYS_PCIE2_MEM_PHYS, LAW_TRGT_PCIE2, LAW_SIZE_512MB, 1);
    set_law(3, 0, CONFIG_SYS_PCIE2_IO_PHYS,  LAW_TRGT_PCIE2, LAW_SIZE_64KB, 1);

    /* Map TLB for PCIe */
    set_tlb(1, 2, CONFIG_SYS_PCIE2_MEM_VIRT, CONFIG_SYS_PCIE2_MEM_PHYS, 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256M, 1);
    set_tlb(1, 3, (CONFIG_SYS_PCIE2_MEM_VIRT + 0x10000000),
                  (CONFIG_SYS_PCIE2_MEM_PHYS + 0x10000000), 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256M, 1);
    set_tlb(1, 4, CONFIG_SYS_PCIE1_MEM_VIRT, CONFIG_SYS_PCIE1_MEM_PHYS, 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256M, 1);
    set_tlb(1, 5, (CONFIG_SYS_PCIE1_MEM_VIRT + 0x10000000),
                  (CONFIG_SYS_PCIE1_MEM_PHYS + 0x10000000), 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256M, 1);

    set_tlb(1, 6, CONFIG_SYS_PCIE2_IO_VIRT, CONFIG_SYS_PCIE2_IO_PHYS, 0,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256K, 1);
    return 0;
}
#endif


#ifdef ENABLE_CPLD
static int hal_cpld_init(void)
{
    /* Setup Local Access Window (LAW) for CPLD/BCSR */
    set_law(5, 0, BCSR_BASE, LAW_TRGT_ELBC, LAW_SIZE_256KB, 1);
    /* Setup TLB MMU (Translation Lookaside Buffer) for CPLD/BCSR */
    set_tlb(1, 8, BCSR_BASE, BCSR_BASE, 0, MAS3_SX | MAS3_SW | MAS3_SR,
        MAS2_I | MAS2_G, 0, BOOKE_PAGESZ_256K, 1);

    /* setup eLBC for CPLD (CS1), 8-bit */
    set32(ELBC_BR(1), (ELBC_BR_ADDR(BCSR_BASE) | ELBC_BR_MSEL(0) |
        ELBC_BR_PS(1) | ELBC_BR_V));
    set32(ELBC_OR(1), (ELBC_OR_AMASK(ELBC_AMASK_32KB) |
        ELBC_ORG_CSCT | ELBC_ORG_XACS | ELBC_ORG_SCY | ELBC_ORG_TRLX |
        ELBC_ORG_EHTR | ELBC_ORG_EAD));

    return 0;
}
#endif


#ifdef ENABLE_CONF_IO

#define NUM_OF_PINS 32

typedef struct {
    unsigned char port;
    unsigned char pin;
    int           dir;
    int           open_drain;
    int           assign;
} io_pin_map_t;

static const io_pin_map_t io_pin_conf[] = {
    /* QE_MUX */
    {1, 19, 1, 0, 1}, /* QE_MUX_MDC */
    {1, 20, 3, 0, 1}, /* QE_MUX_MDIO */

    /* UCC_1_MII */
    {0, 23, 2, 0, 2}, /* CLK12 */
    {0, 24, 2, 0, 1}, /* CLK9 */
    {0,  7, 1, 0, 2}, /* ENET1_TXD0_SER1_TXD0 */
    {0,  9, 1, 0, 2}, /* ENET1_TXD1_SER1_TXD1 */
    {0, 11, 1, 0, 2}, /* ENET1_TXD2_SER1_TXD2 */
    {0, 12, 1, 0, 2}, /* ENET1_TXD3_SER1_TXD3 */
    {0,  6, 2, 0, 2}, /* ENET1_RXD0_SER1_RXD0 */
    {0, 10, 2, 0, 2}, /* ENET1_RXD1_SER1_RXD1 */
    {0, 14, 2, 0, 2}, /* ENET1_RXD2_SER1_RXD2 */
    {0, 15, 2, 0, 2}, /* ENET1_RXD3_SER1_RXD3 */
    {0,  5, 1, 0, 2}, /* ENET1_TX_EN_SER1_RTS_B*/
    {0, 13, 1, 0, 2}, /* ENET1_TX_ER */
    {0,  4, 2, 0, 2}, /* ENET1_RX_DV_SER1_CTS_B */
    {0,  8, 2, 0, 2}, /* ENET1_RX_ER_SER1_CD_B */
    {0, 17, 2, 0, 2}, /* ENET1_CRS */
    {0, 16, 2, 0, 2}, /* ENET1_COL */

    /* UCC_5_RMII */
    {1, 11, 2, 0, 1}, /* CLK13 */
    {1,  7, 1, 0, 2}, /* ENET5_TXD0_SER5_TXD0 */
    {1, 10, 1, 0, 2}, /* ENET5_TXD1_SER5_TXD1 */
    {1,  6, 2, 0, 2}, /* ENET5_RXD0_SER5_RXD0 */
    {1,  9, 2, 0, 2}, /* ENET5_RXD1_SER5_RXD1 */
    {1,  5, 1, 0, 2}, /* ENET5_TX_EN_SER5_RTS_B */
    {1,  4, 2, 0, 2}, /* ENET5_RX_DV_SER5_CTS_B */
    {1,  8, 2, 0, 2}, /* ENET5_RX_ER_SER5_CD_B */

    {0,  0, 0, 0, -1} /* Last */
};

static void config_io_pin(uint8_t port, uint8_t pin, int dir, int open_drain,
    int assign)
{
    uint32_t pin_2bit_mask, pin_2bit_dir, pin_2bit_assign;
    uint32_t pin_1bit_mask, tmp_val;

    /* Calculate pin location and 2bit mask and dir */
    pin_2bit_mask = (uint32_t)(0x3 << (NUM_OF_PINS -
        (pin % (NUM_OF_PINS / 2) + 1) * 2));
    pin_2bit_dir =  (uint32_t)(dir << (NUM_OF_PINS -
        (pin % (NUM_OF_PINS / 2) + 1) * 2));

    /* Setup the direction */
    tmp_val = (pin > (NUM_OF_PINS / 2) - 1) ?
        get32(GUTS_CPDIR2(port)) :
        get32(GUTS_CPDIR1(port));

    if (pin > (NUM_OF_PINS / 2) - 1) {
        set32(GUTS_CPDIR2(port), ~pin_2bit_mask & tmp_val);
        set32(GUTS_CPDIR2(port),  pin_2bit_dir  | tmp_val);
    }
    else {
        set32(GUTS_CPDIR1(port), ~pin_2bit_mask & tmp_val);
        set32(GUTS_CPDIR1(port),  pin_2bit_dir  | tmp_val);
    }

    /* Calculate pin location for 1bit mask */
    pin_1bit_mask = (uint32_t)(1 << (NUM_OF_PINS - (pin+1)));

    /* Setup the open drain */
    tmp_val = get32(GUTS_CPODR(port));
    if (open_drain) {
        set32(GUTS_CPODR(port),  pin_1bit_mask | tmp_val);
    }
    else {
        set32(GUTS_CPODR(port), ~pin_1bit_mask & tmp_val);
    }

    /* Setup the assignment */
    tmp_val = (pin > (NUM_OF_PINS/2) - 1) ?
        get32(GUTS_CPPAR2(port)):
        get32(GUTS_CPPAR1(port));
    pin_2bit_assign = (uint32_t)(assign <<
        (NUM_OF_PINS - (pin % (NUM_OF_PINS / 2) + 1) * 2));

    /* Clear and set 2 bits mask */
    if (pin > (NUM_OF_PINS/2) - 1) {
        set32(GUTS_CPPAR2(port), ~pin_2bit_mask   & tmp_val);
        set32(GUTS_CPPAR2(port),  pin_2bit_assign | tmp_val);
    }
    else {
        set32(GUTS_CPPAR1(port), ~pin_2bit_mask   & tmp_val);
        set32(GUTS_CPPAR1(port),  pin_2bit_assign | tmp_val);
    }
}

static void read_io_pin(uint8_t port, uint8_t pin, int *data)
{
    uint32_t tmp_val;

    /* Read the data */
    tmp_val = get32(GUTS_CPDAT(port));
    *data = (tmp_val >> (NUM_OF_PINS - (pin + 1))) & 0x1;
}

static void write_io_pin(uint8_t port, uint8_t pin, int data)
{
    uint32_t pin_1bit_mask, tmp_val;

    /* Calculate pin location for 1bit mask */
    pin_1bit_mask = (uint32_t)(1 << (NUM_OF_PINS - (pin + 1)));

    /* Write the data */
    tmp_val = get32(GUTS_CPDAT(port));
    if (data) {
        set32(GUTS_CPDAT(port),  pin_1bit_mask | tmp_val);
    }
    else {
        set32(GUTS_CPDAT(port), ~pin_1bit_mask & tmp_val);
    }
}

static void hal_io_init(void)
{
    uint8_t port, pin;
    int     dir, open_drain, assign;
    int     i;

    for (i = 0; io_pin_conf[i].assign != -1; i++) {
        port       = io_pin_conf[i].port;
        pin        = io_pin_conf[i].pin;
        dir        = io_pin_conf[i].dir;
        open_drain = io_pin_conf[i].open_drain;
        assign     = io_pin_conf[i].assign;
        config_io_pin(port, pin, dir, open_drain, assign);
    }
#ifdef ENABLE_UART_RTS
    write_io_pin(2, 0, 0x0); /* RTS enable */
#else
    write_io_pin(2, 0, 0x1); /* RTS disable */
#endif

    /* Enable signal multiplex control:
     *   SDHC: WP and CD
     *   QE0/QE3:  Ethernet UCC1 and UCC5
     *   QE9/QE12: QE MII managment signals */
    set32(GUTS_PMUXCR, (GUTS_PMUXCR_SDHC_CD | GUTS_PMUXCR_SDHC_WP |
                        GUTS_PMUXCR_QE0 | GUTS_PMUXCR_QE3 |
                        GUTS_PMUXCR_QE9 | GUTS_PMUXCR_QE12));
}
#endif /* ENABLE_CONF_IO */


/* ---- QUICC Engine Driver ---- */
#ifdef ENABLE_QE

/* Structure packing */
#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
    defined(__GNUC__)
    #define QE_PACKED __attribute__ ((packed))
#else
    #define QE_PACKED
#endif

/* QE based on work from Shlomi Gridish and Dave Liu at Freescale/NXP */

struct qe_header {
    uint32_t length;      /* Length of the entire structure, in bytes */
    uint8_t  magic[3];    /* Set to { 'Q', 'E', 'F' } */
    uint8_t  version;     /* Version of this layout. First ver is '1' */
} QE_PACKED;

struct qe_soc {
    uint16_t model;       /* The SOC model  */
    uint8_t  major;       /* The SOC revision major */
    uint8_t  minor;       /* The SOC revision minor */
} QE_PACKED;

struct qe_microcode {
    uint8_t  id[32];      /* Null-terminated identifier */
    uint32_t traps[16];   /* Trap addresses, 0 == ignore */
    uint32_t eccr;        /* The value for the ECCR register */
    uint32_t iram_offset; /* Offset into I-RAM for the code */
    uint32_t count;       /* Number of 32-bit words of the code */
    uint32_t code_offset; /* Offset of the actual microcode */
    uint8_t  major;       /* The microcode version major */
    uint8_t  minor;       /* The microcode version minor */
    uint8_t  revision;    /* The microcode version revision */
    uint8_t  padding;     /* Reserved, for alignment */
    uint8_t  reserved[4]; /* Reserved, for future expansion */
} QE_PACKED;

struct qe_firmware {
    struct qe_header    header;
    uint8_t             id[62];         /* Null-terminated identifier string */
    uint8_t             split;          /* 0 = shared I-RAM, 1 = split I-RAM */
    uint8_t             count;          /* Number of microcode[] structures */
    struct qe_soc       soc;
    uint8_t             padding[4];     /* Reserved, for alignment */
    uint64_t            extended_modes; /* Extended modes */
    uint32_t            vtraps[8];      /* Virtual trap addresses */
    uint8_t             reserved[4];    /* Reserved, for future expansion */
    struct qe_microcode microcode[1];
    /* All microcode binaries should be located here */
    /* CRC32 should be located here, after the microcode binaries */
} QE_PACKED;

static void qe_upload_microcode(const struct qe_firmware *firmware,
    const struct qe_microcode *ucode)
{
    const uint32_t *code = (uint32_t*)((uint8_t *)firmware + ucode->code_offset);
    unsigned int i;

    wolfBoot_printf("QE: uploading '%s' version %u.%u.%u\n",
        ucode->id, ucode->major, ucode->minor, ucode->revision);

    /* Use auto-increment */
    set32(QE_IRAM_IADD, ucode->iram_offset |
        QE_IRAM_IADD_AIE | QE_IRAM_IADD_BADDR);

    /* Copy 32-bits at a time to iRAM */
    for (i = 0; i < ucode->count; i++) {
        set32(QE_IRAM_IDATA, code[i]);
    }
}

/* Upload a microcode to the I-RAM at a specific address */
static int qe_upload_firmware(const struct qe_firmware *firmware)
{
    unsigned int i, j;
#ifdef ENABLE_QE_CRC32
    uint32_t crc;
#endif
    size_t calc_size = sizeof(struct qe_firmware);
    size_t length;
    const struct qe_header *hdr;

    hdr = &firmware->header;
    length = hdr->length;

    /* Check the magic */
    if ((hdr->magic[0] != 'Q') || (hdr->magic[1] != 'E') ||
        (hdr->magic[2] != 'F')) {
        wolfBoot_printf("QE firmware header invalid!\n");
        return -1;
    }

    /* Check the version */
    if (hdr->version != 1) {
        wolfBoot_printf("QE version %d unsupported!\n", hdr->version);
        return -1;
    }

    /* Validate some of the fields */
    if ((firmware->count < 1) || (firmware->count > QE_MAX_RISC)) {
        wolfBoot_printf("QE count %d invalid!\n", firmware->count);
        return -1;
    }

    /* Validate the length and check if there's a CRC */
    calc_size += (firmware->count - 1) * sizeof(struct qe_microcode);
    for (i = 0; i < firmware->count; i++) {
        /* For situations where the second RISC uses the same microcode
         * as the first, the 'code_offset' and 'count' fields will be
         * zero, so it's okay to add those. */
        calc_size += sizeof(uint32_t) * firmware->microcode[i].count;
    }

    /* Validate the length */
    if (length != calc_size + sizeof(uint32_t)) {
        wolfBoot_printf("QE length %d invalid!\n", length);
        return -1;
    }

#ifdef ENABLE_QE_CRC32
    /* Validate the CRC */
    crc = *(uint32_t *)((void *)firmware + calc_size);
    if (crc != (crc32(-1, (const void *) firmware, calc_size) ^ -1)) {
        wolfBoot_printf("QE firmware CRC is invalid\n");
        return -1;
    }
#endif

    /* Use common instruction RAM if not split (default is split) */
    if (!firmware->split) {
        set16(QE_CP_CERCR, get16(QE_CP_CERCR) | QE_CP_CERCR_CIR);
    }

    wolfBoot_printf("QE: Length %d, Count %d\n", length, firmware->count);

    /* Loop through each microcode. */
    for (i = 0; i < firmware->count; i++) {
        const struct qe_microcode *ucode = &firmware->microcode[i];
        uint32_t trapCount = 0;

        /* Upload a microcode if it's present */
        if (ucode->code_offset) {
            qe_upload_microcode(firmware, ucode);
        }

        /* Program the traps for this processor (max 16) */
        for (j = 0; j < 16; j++) {
            uint32_t trap = ucode->traps[j];
            if (trap) {
                trapCount++;
                set32(QE_RSP_TIBCR(i, j), trap);
            }
        }

        /* Enable traps */
        set32(QE_RSP_ECCR(i), ucode->eccr);
        wolfBoot_printf("QE: Traps %d\n", trapCount);
    }

    return 0;
}

static void qe_issue_cmd(uint32_t cmd, uint32_t sbc, uint8_t mcn,
    uint32_t cmd_data)
{
    set32(QE_CP_CECDR, cmd_data);
    set32(QE_CP_CECR,
        sbc |       /* sub block code */
        QE_CR_FLG | /* flag: set by software, cleared by hardware */
        ((uint32_t)mcn << QE_CR_PROTOCOL_SHIFT) | /* MCC/QMC channel number */
        cmd         /* opcode (reset sets 0x8000_0000) */
    );

    /* Wait for the command semaphore flag to clear */
    while (get32(QE_CP_CECR) & QE_CR_FLG);
}

static int hal_qe_init(void)
{
    int ret;
    uint32_t sdma_base;

    /* Load microcode from NAND to DDR */
    ret = ext_flash_read(QE_FW_NAND, (uint8_t*)QE_FW_ADDR, QE_FW_LENGTH);
    if (ret == QE_FW_LENGTH) {
        /* Upload microcode to IRAM */
        ret = qe_upload_firmware((const struct qe_firmware *)QE_FW_ADDR);
    }
    if (ret == 0) {
        /* enable the microcode in IRAM */
        set32(QE_IRAM_IREADY, QE_IRAM_READY);

        /* Serial DMA */
        /* All of DMA transaction in bus 1 */
        set32(QE_SDMA_SDAQR, 0);
        set32(QE_SDMA_SDAQMR, 0);

        /* Allocate 2KB temporary buffer for sdma */
        sdma_base = 0; /* offset in QE_MURAM */
        set32(QE_SDMA_SDEBCR, sdma_base & QE_SDEBCR_BA_MASK);

        /* Clear sdma status */
        set32(QE_SDMA_SDSR, 0x03000000);

        /* Enable global mode on bus 1, and 2KB buffer size */
        set32(QE_SDMA_SDMR, QE_SDMR_GLB_1_MSK | (0x3 << QE_SDMR_CEN_SHIFT));

        /* Reset QUICC Engine */
        qe_issue_cmd(QE_RESET, 0, 0, 0);
    }

    return ret;
}
#endif /* ENABLE_QUICC */


/* SMP Multi-Processor Driver */
#ifdef ENABLE_MP

/* from boot_ppc_core.S */
extern uint32_t _secondary_start_page;
extern uint32_t _second_half_boot_page;
extern uint32_t _spin_table;
extern uint32_t _spin_table_addr;
extern uint32_t _bootpg_addr;

/* Startup additional cores with spin table and synchronize the timebase */
static void hal_mp_up(uint32_t bootpg)
{
    uint32_t up, cpu_up_mask, whoami, bpcr, devdisr;
    int timeout = 50, i;

    /* Get current running core number */
    whoami = get32(PIC_WHOAMI);

    wolfBoot_printf("MP: Starting core 2 (boot page %p, spin table %p)\n",
        bootpg, (uint32_t)&_spin_table);

    /* Set the boot page translation register */
    set32(RESET_BPTR, RESET_BPTR_EN | RESET_BPTR_BOOTPG(bootpg));

    /* Disable time base on inactive core */
    devdisr = get32(GUTS_DEVDISR);
    if (whoami)
        devdisr |= GUTS_DEVDISR_TB0;
    else
        devdisr |= GUTS_DEVDISR_TB1;
    set32(GUTS_DEVDISR, devdisr);

    /* Enable the CPU core(s) */
    up = ((1 << CPU_NUMCORES) - 1);
    bpcr = get32(ECM_EEBPCR);
    bpcr |= ECM_EEBPCR_CPU_EN(up);
    set32(ECM_EEBPCR, bpcr);
    __asm__ __volatile__("sync; isync; msync");

    /* wait for other core to start */
    cpu_up_mask = (1 << whoami);
    while (timeout) {
        for (i = 0; i < CPU_NUMCORES; i++) {
            uint32_t* entry = (uint32_t*)(
                  (uint8_t*)&_spin_table + (i * ENTRY_SIZE) + ENTRY_ADDR_LOWER);
            if (*entry) {
                cpu_up_mask |= (1 << i);
            }
        }
        if ((cpu_up_mask & up) == up) {
            break;
        }

        udelay(100);
        timeout--;
    }

    if (timeout == 0) {
        wolfBoot_printf("MP: Timeout enabling additional cores!\n");
    }

    /* Disable our timebase */
    if (whoami)
        devdisr |= GUTS_DEVDISR_TB1;
    else
        devdisr |= GUTS_DEVDISR_TB0;
    set32(GUTS_DEVDISR, devdisr);

    /* Reset our timebase */
    mtspr(SPRN_TBWU, 0);
    mtspr(SPRN_TBWL, 0);

    /* Enable timebase for all cores */
    devdisr &= ~(GUTS_DEVDISR_TB0 | GUTS_DEVDISR_TB1);
    set32(GUTS_DEVDISR, devdisr);
}

static void hal_mp_init(void)
{
    uint32_t *fixup = (uint32_t*)&_secondary_start_page;
    uint32_t bootpg;
    int i_tlb = 0; /* always 0 */
    size_t i;
    const volatile uint32_t *s;
    volatile uint32_t *d;

    /* Assign virtual boot page at end of DDR */
    bootpg = DDR_ADDRESS + DDR_SIZE - BOOT_ROM_SIZE;

    /* Store the boot page address for use by additional CPU cores */
    _bootpg_addr = (uint32_t)&_second_half_boot_page;

    /* Store location of spin table for other cores */
    _spin_table_addr = (uint32_t)&_spin_table;

    /* Flush bootpg before copying to invalidate any stale cache lines */
    flush_cache(bootpg, BOOT_ROM_SIZE);

    /* Map reset page to bootpg so we can copy code there */
    disable_tlb1(i_tlb);
    set_tlb(1, i_tlb, BOOT_ROM_ADDR, bootpg, 0, /* tlb, epn, rpn, urpn */
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), /* perms, wimge */
        0, BOOKE_PAGESZ_4K, 1); /* ts, esel, tsize, iprot */

    /* copy startup code to virtually mapped boot address */
    /* do not use memcpy due to compiler array bounds report (not valid) */
    s = (const uint32_t*)fixup;
    d = (uint32_t*)BOOT_ROM_ADDR;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* start core and wait for it to be enabled */
    hal_mp_up(bootpg);
}
#endif /* ENABLE_MP */

#ifdef ENABLE_IRQ
static void hal_irq_init(void)
{
    uint32_t reg;

    /* Reset the Programmable Interrupt Controller */
    set32(PIC_GCR, PIC_GCR_RST);
    while (get32(PIC_GCR) & PIC_GCR_RST);

    set32(PIC_GCR, PIC_GCR_M); /* eanble mixed-mode */
    reg = get32(PIC_GCR); /* read back */
    (void)reg;
}
#endif

void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    #if !defined(BUILD_LOADER_STAGE1)
    uart_write("wolfBoot HAL Init\n", 18);
    #endif
#endif
#ifdef ENABLE_PCIE
    hal_pcie_init();
#endif
#ifdef ENABLE_CONF_IO
    hal_io_init();
#endif
#ifdef ENABLE_CPLD
    hal_cpld_init();
#endif
#ifdef ENABLE_IRQ
    hal_irq_init();
#endif
    hal_flash_init();
#ifdef ENABLE_QE
    if (hal_qe_init() != 0) {
        wolfBoot_printf("QE: Engine init failed!\n");
    }
#endif
#ifdef ENABLE_MP
    hal_mp_init();
#endif

    /* Hardware Tests */
#if defined(ENABLE_DDR) && defined(TEST_DDR)
    if (test_ddr() != 0) {
        wolfBoot_printf("DDR Test Failed!\n");
    }
#endif

#if defined(ENABLE_ESPI) && defined(TEST_TPM)
    if (test_tpm() != 0) {
        wolfBoot_printf("TPM Test Failed!\n");
    }
#endif
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    /* This platform only support external flash via eLBC */
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    /* This platform only support external flash via eLBC */
    return 0;
}

void hal_flash_unlock(void)
{

}

void hal_flash_lock(void)
{

}

void hal_prepare_boot(void)
{

}

/* See P1021RM (12.4.3.2.5 FCM data write instructions) */
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int ret = 0, pos = 0;
    uint32_t block_size, page_size, write_size;

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext write: addr 0x%x, dst 0x%x, len %d\n",
        address, data, len);
#endif

#if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
    /* large page - ELBC_ORF_PGS=1 */
    block_size = (128 * 1024);
    page_size = 2048;
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_PAGE_PROG1) |
                    ELBC_FCR_CMD(1, NAND_CMD_STATUS) |
                    ELBC_FCR_CMD(2, NAND_CMD_PAGE_PROG2));
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_CA) |
                    ELBC_FIR_OP(2, ELBC_FIR_OP_PA) |
                    ELBC_FIR_OP(3, ELBC_FIR_OP_WB) |
                    ELBC_FIR_OP(4, ELBC_FIR_OP_CM2) |
                    ELBC_FIR_OP(5, ELBC_FIR_OP_CW1) |
                    ELBC_FIR_OP(6, ELBC_FIR_OP_RS));
#else
    /* Small page */
    block_size = (16 * 1024);
    page_size = 512;
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_READA) |
                    ELBC_FCR_CMD(1, NAND_CMD_PAGE_PROG2) |
                    ELBC_FCR_CMD(2, NAND_CMD_PAGE_PROG1));
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CW0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_CM2) |
                    ELBC_FIR_OP(2, ELBC_FIR_OP_CA) |
                    ELBC_FIR_OP(3, ELBC_FIR_OP_PA) |
                    ELBC_FIR_OP(4, ELBC_FIR_OP_WB) |
                    ELBC_FIR_OP(5, ELBC_FIR_OP_CW1));
#endif
    (void)block_size; /* not used - shown for reference */

    /* page write loop */
    while (pos < len) {
        /* Calculate page address */
        uint32_t page = (address / page_size);
        uint32_t col = (address % page_size);
        uint32_t status;

        /* bytes to read */
        write_size = len;
        if (write_size > page_size) {
            write_size = page_size;
        }
        /* set page and FCM buffer */
        hal_flash_set_addr(page, col);

        set32(ELBC_FBCR, col); /* size of write (0=full page) */

        /* copy page to FCM buffer */
        hal_flash_write_bytes(data, write_size);

        /* execute write */
        ret = hal_flash_command(1);
        if (ret != 0)
            break;

        /* status returned in MDR */
        status = get32(ELBC_MDR) & 0xFF;
#ifdef DEBUG_EXT_FLASH
        wolfBoot_printf("write page %d, col %d, status %x\n",
            page, col, status);
#endif
        (void)status;
        address += page_size - col;
        pos += page_size - col;
        data += page_size - col;
        col = 0; /* remainder is page aligned */
    };

    return ret;
}

/* Reads page into FCM buffer and copies only what is needed */
/* See P1021RM (12.4.3.2.4 FCM data read instructions)*/
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uint32_t block_size, page_size, read_size;
    int ret = 0, pos = 0, i = 0;
    int bad_marker;

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext read: addr 0x%x, dst 0x%x, len %d\n",
        address, data, len);
#endif

#if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
    /* large page - ELBC_ORF_PGS=1 */
    block_size = (128 * 1024);
    page_size = 2048;
    bad_marker = page_size;
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_READA) |
                    ELBC_FCR_CMD(1, NAND_CMD_READSTART));
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_CA) |
                    ELBC_FIR_OP(2, ELBC_FIR_OP_PA) |
                    ELBC_FIR_OP(3, ELBC_FIR_OP_CM1) |
                    ELBC_FIR_OP(4, ELBC_FIR_OP_RBW));
#else
    /* Small page */
    block_size = (16 * 1024);
    page_size = 512;
    bad_marker = page_size + 5;
    set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_READA));
    set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CW0) |
                    ELBC_FIR_OP(1, ELBC_FIR_OP_CA) |
                    ELBC_FIR_OP(2, ELBC_FIR_OP_PA) |
                    ELBC_FIR_OP(3, ELBC_FIR_OP_RBW));
#endif

    /* total download loop */
    while (pos < len) {
        /* block loop */
        do {
            /* Calculate page address */
            uint32_t page = (address / page_size);
            uint32_t col = (address % page_size);

            set32(ELBC_FBCR, col);

            /* bytes to read */
            read_size = len;
            if (read_size > page_size) {
                read_size = page_size;
            }

            /* read page into FCM buffer */
            hal_flash_set_addr(page, col);
            ret = hal_flash_command(0);
            if (ret != 0)
                break;

            /* check for bad page. if either of the first two pages are bad then
             * skip to next block */
            if (i++ < 2 && flash_buf[bad_marker] != 0xFF) {
                /* skip block - advance address by block and restart position */
                address = (address + block_size) & ~(block_size - 1);
                pos &= ~(block_size - 1);
                break;
            }

            /* copy from FCM buffer to data buffer */
            hal_flash_read_bytes(data, read_size);
            address += page_size - col;
            pos += page_size - col;
            data += page_size - col;
            col = 0; /* remainder is page aligned */

        } while ((address & (block_size - 1)) && (pos < len));
    };

    /* on success return size read */
    if (ret == 0) {
        ret = len;
    }

    return ret;
}

/* For NAND the erase is Block */
int ext_flash_erase(uintptr_t address, int len)
{
    int ret = 0;
    uint32_t block_size, page_size;

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext erase: addr 0x%x, len %d\n", address, len);
#endif

#if defined(FLASH_PAGE_SIZE) && FLASH_PAGE_SIZE == 2048
    /* large page - ELBC_ORF_PGS=1 */
    block_size = (128 * 1024);
    page_size = 2048;
#else
    /* small page */
    block_size = (16 * 1024);
    page_size = 512;
#endif

    while (len > 0) {
        /* Calculate page address, however block will be erased */
        uint32_t page = (address / page_size);
        uint32_t status;

        /* Erase Block */
        set32(ELBC_FIR, ELBC_FIR_OP(0, ELBC_FIR_OP_CM0) |
                        ELBC_FIR_OP(1, ELBC_FIR_OP_PA) |
                        ELBC_FIR_OP(2, ELBC_FIR_OP_CM2) |
                        ELBC_FIR_OP(3, ELBC_FIR_OP_CW1) |
                        ELBC_FIR_OP(4, ELBC_FIR_OP_RS));
        set32(ELBC_FCR, ELBC_FCR_CMD(0, NAND_CMD_BLOCK_ERASE1) |
                        ELBC_FCR_CMD(1, NAND_CMD_STATUS) |
                        ELBC_FCR_CMD(2, NAND_CMD_BLOCK_ERASE2));
        set32(ELBC_FBCR, 0);
        hal_flash_set_addr(page, 0);
        ret = hal_flash_command(1);
        if (ret != 0)
            break;

        /* status returned in MDR */
        status = get32(ELBC_MDR) & 0xFF;
#ifdef DEBUG_EXT_FLASH
        wolfBoot_printf("erase page %d, status %x\n", page, status);
#endif
        (void)status;
        len -= block_size;
    }

    return ret;
}

void ext_flash_lock(void)
{

}

void ext_flash_unlock(void)
{

}

#ifdef MMU
void* hal_get_dts_address(void)
{
    return NULL; /* WOLFBOOT_LOAD_DTS_ADDRESS not required */
}
#endif


#if defined(ENABLE_DDR) && defined(TEST_DDR)

#ifndef TEST_DDR_OFFSET
#define TEST_DDR_OFFSET     (1 * 1024 * 1024)
#endif
#ifndef TEST_DDR_TOTAL_SIZE
#define TEST_DDR_TOTAL_SIZE (2 * 1024)
#endif
#ifndef TEST_DDR_CHUNK_SIZE
#define TEST_DDR_CHUNK_SIZE 1024
#endif

static int test_ddr(void)
{
    int ret = 0;
    int i;
    uint32_t *ptr = (uint32_t*)(DDR_ADDRESS + TEST_DDR_OFFSET);
    uint32_t tmp[TEST_DDR_CHUNK_SIZE/4];
    uint32_t total = 0;

    while (total < TEST_DDR_TOTAL_SIZE) {
        /* test write to DDR */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            ptr[i] = (uint32_t)i;
        }

        /* test read from DDR */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            tmp[i] = ptr[i];
        }

        /* compare results */
        for (i=0; i<TEST_DDR_CHUNK_SIZE/4; i++) {
            if (tmp[i] != (uint32_t)i) {
                ret = -1;
                break;
            }
        }
        total += TEST_DDR_CHUNK_SIZE;
        ptr += TEST_DDR_CHUNK_SIZE;
    }

    return ret;
}
#endif /* ENABLE_DDR && TEST_DDR */

#if defined(ENABLE_ESPI) && defined(TEST_TPM)
int test_tpm(void)
{
    /* Read 4 bytes at TIS address D40F00. Assumes 0 wait state on TPM */
    uint8_t tx[8] = {0x83, 0xD4, 0x0F, 0x00,
                     0x00, 0x00, 0x00, 0x00};
    uint8_t rx[8] = {0};

    hal_espi_init(SPI_CS_TPM, 2000000, 0);
    hal_espi_xfer(SPI_CS_TPM, tx, rx, (uint32_t)sizeof(rx), 0);

    wolfBoot_printf("RX: 0x%x\n", *((uint32_t*)&rx[4]));
    return rx[4] != 0xFF ? 0 : -1;
}
#endif
