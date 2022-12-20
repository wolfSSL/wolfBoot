/* nxp_p1021.c
 *
 * Copyright (C) 2022 wolfSSL Inc.
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
#include "target.h"
#include "printf.h"

/* P1021 */
#define CCSRBAR (0xFF700000)
#define SYS_CLK (400000000)

/* Global Utilities (GUTS) */
#define GUTS_PORPLLSR *((volatile uint32_t*)(CCSRBAR + 0x0UL)) /* POR PLL ratio status register */

/* Local Bus Controller (LBC) */
#define LBC_BASE       (CCSRBAR + 0x5000)
#define LBC_LBCR       *((volatile uint32_t*)(LBC_BASE + 0xD0))
#define LBC_LBCR_ABSWP (1 << 19) /* Address byte swap for 16-bit port size */


/* P1021 PC16552D Dual UART */
#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */

#define UART_BASE(n) (CCSRBAR + 0x4500 + (n * 0x100))

#define UART_RBR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* receiver buffer register */
#define UART_THR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* transmitter holding register */
#define UART_IER(n)  *((volatile uint8_t*)(UART_BASE(n) + 1)) /* interrupt enable register */
#define UART_IIR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* interrupt ID register */
#define UART_FCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* FIFO control register */
#define UART_LCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 3)) /* line control register */
#define UART_MCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 4)) /* modem control register */
#define UART_LSR(n)  *((volatile uint8_t*)(UART_BASE(n) + 5)) /* line status register */

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

/* P1021 LAW - Local Access Window (Memory Map) - RM 2.4 */
#define LAWBAR_BASE(n) (CCSRBAR + 0xC08 + (n * 0x20))
#define LAWBAR(n)      *((volatile uint32_t*)(LAWBAR_BASE(n) + 0x0))
#define LAWAR(n)       *((volatile uint32_t*)(LAWBAR_BASE(n) + 0x4))

#define LAWAR_ENABLE      (1<<31)
#define LAWAR_TRGT_ID(id) (id<<20)

/* P1021 Global Source/Target ID Assignments - RM Table 2-7 */
enum law_target_id {
    LAW_TRGT_ELBC = 0x4, /* eLBC (Enhanced Local Bus Controller) */
    LAW_TRGT_DDR = 0xF,  /* DDR Memory Controller */
};

/* P1021 2.4.2 - size is equal to 2^(enum + 1) */
enum law_sizes {
    LAW_SIZE_4KB = 0x0B,
    LAW_SIZE_8KB,
    LAW_SIZE_16KB,
    LAW_SIZE_32KB,
    LAW_SIZE_64KB,
    LAW_SIZE_128KB, /* 0x10 */
    LAW_SIZE_256KB,
    LAW_SIZE_512KB,
    LAW_SIZE_1MB,
    LAW_SIZE_2MB,
    LAW_SIZE_4MB,
    LAW_SIZE_8MB,
    LAW_SIZE_16MB,
    LAW_SIZE_32MB,
    LAW_SIZE_64MB,
    LAW_SIZE_128MB,
    LAW_SIZE_256MB, /* 0x1B */
    LAW_SIZE_512MB,
    LAW_SIZE_1GB,
    LAW_SIZE_2GB,
    LAW_SIZE_4GB,
    LAW_SIZE_8GB, /* 0x20 */
    LAW_SIZE_16GB,
    LAW_SIZE_32GB,
};


/* MMU Assist Registers E6500RM 2.13.10 */
#define MAS0_TLBSEL_MSK 0x30000000
#define MAS0_TLBSEL(x)  (((x) << 28) & MAS0_TLBSEL_MSK)
#define MAS0_ESEL_MSK   0x0FFF0000
#define MAS0_ESEL(x)    (((x) << 16) & MAS0_ESEL_MSK)
#define MAS0_NV(x)      ((x) & 0x00000FFF)

#define MAS1_VALID      0x80000000
#define MAS1_IPROT      0x40000000
#define MAS1_TID(x)     (((x) << 16) & 0x3FFF0000)
#define MAS1_TS         0x00001000
#define MAS1_TSIZE(x)   (((x) << 7) & 0x00000F80)
#define TSIZE_TO_BYTES(x) (1ULL << ((x) + 10))

#define MAS2_EPN        0xFFFFF000
#define MAS2_X0         0x00000040
#define MAS2_X1         0x00000020
#define MAS2_W          0x00000010
#define MAS2_I          0x00000008
#define MAS2_M          0x00000004
#define MAS2_G          0x00000002
#define MAS2_E          0x00000001

#define MAS3_RPN        0xFFFFF000
#define MAS3_U0         0x00000200
#define MAS3_U1         0x00000100
#define MAS3_U2         0x00000080
#define MAS3_U3         0x00000040
#define MAS3_UX         0x00000020
#define MAS3_SX         0x00000010
#define MAS3_UW         0x00000008
#define MAS3_SW         0x00000004
#define MAS3_UR         0x00000002
#define MAS3_SR         0x00000001

#define MAS7_RPN        0xFFFFFFFF

#define BOOKE_PAGESZ_4K    2
#define BOOKE_PAGESZ_16M   14
#define BOOKE_PAGESZ_256M  18
#define BOOKE_PAGESZ_2G    21

#define BOOKE_MAS0(tlbsel,esel,nv) \
        (MAS0_TLBSEL(tlbsel) | MAS0_ESEL(esel) | MAS0_NV(nv))
#define BOOKE_MAS1(v,iprot,tid,ts,tsize) \
        ((((v) << 31) & MAS1_VALID)         | \
        (((iprot) << 30) & MAS1_IPROT)      | \
        (MAS1_TID(tid))                     | \
        (((ts) << 12) & MAS1_TS)            | \
        (MAS1_TSIZE(tsize)))
#define BOOKE_MAS2(epn, wimge) \
        (((epn) & MAS3_RPN) | (wimge))
#define BOOKE_MAS3(rpn, user, perms) \
        (((rpn) & MAS3_RPN) | (user) | (perms))
#define BOOKE_MAS7(rpn) \
        (((uint64_t)(rpn)) >> 32)


/* P1021 eLBC (Enhanced Local Bus Controller) - RM 12.3 */
#define ELBC_BASE        (CCSRBAR + 0x5000)
#define ELBC_MAX_BANKS   8

#define ELBC_BR(n)  *((volatile uint32_t*)(ELBC_BASE + 0x0000 + (n * 0x8))) /* Base registers */
#define ELBC_OR(n)  *((volatile uint32_t*)(ELBC_BASE + 0x0000 + (n * 0x8))) /* Options registers */
#define ELBC_MDR    *((volatile uint32_t*)(ELBC_BASE + 0x88))  /* memory data register */
#define ELBC_LSOR   *((volatile uint32_t*)(ELBC_BASE + 0x90))  /* operation initiation register */
#define ELBC_FMR    *((volatile uint32_t*)(ELBC_BASE + 0xE0))  /* flash mode register */
#define ELBC_FIR    *((volatile uint32_t*)(ELBC_BASE + 0xE4))  /* flash instruction register */
#define ELBC_FCR    *((volatile uint32_t*)(ELBC_BASE + 0xE8))  /* flash command register */
#define ELBC_FBAR   *((volatile uint32_t*)(ELBC_BASE + 0xEC))  /* flash address register - OR_PGS=0 (shift 5), OR_PGS=1 (shift 6) */
#define ELBC_FPAR   *((volatile uint32_t*)(ELBC_BASE + 0xF0))  /* flash page address register */
#define ELBC_FBCR   *((volatile uint32_t*)(ELBC_BASE + 0xF4))  /* flash byte count register */

#define ELBC_LTESR  *((volatile uint32_t*)(ELBC_BASE + 0xB0))  /* transfer error status register */
#define ELBC_LTEIR  *((volatile uint32_t*)(ELBC_BASE + 0xB8))  /* transfer error interrupt enable register */
#define ELBC_LTEATR *((volatile uint32_t*)(ELBC_BASE + 0xBC))  /* transfer error attributes register */


#define ELBC_BR_ADDR(n)   (((uint32_t)n) & 0xFFFF8000) /* Physical base address - upper 17-bits */
#define ELBC_BR_PS(n)     (((n) & 0x3) << 12) /* port size - 1=8-bit, 2=16-bit */
#define ELBC_BR_DECC(n)   (((n) & 0x3) << 10) /* data error checking - 0=disabled, 1=ECC check enable / gen disabled, 2=ECC check/gen enabled */
#define ELBC_BR_WP        (1 << 8)            /* write protect */
#define ELBC_BR_MSEL(n)   ((n) & 0x7 << 7)    /* machine select:
                                               *   0=GPCM (General Purpose Chip-Select Machine)
                                               *   1=FCM (Flash Control Machine),
                                               *   4=UPMA, 5=UPMB, 6=UPMC (User Programmable Machines) */
#define ELBC_BR_V         (1 << 0)            /* bank valid */

#define ELBC_OR_AMASK(n)  ((((uint32_t)n) & 0xFFFF8) << 15) /* Address mask - upper 17-bits */
#define ELBC_OR_BCTLD     (1 << 12) /* buffer control disable */
#define ELBC_OR_PGS       (1 << 10) /* page size 0=512, 1=2048 bytes */
#define ELBC_OR_CSCT      (1 << 9)  /* chip select to command time - TRLX=0 (0=1, 1=4), TRLX=1 (0=2, 1=8) clock cycles */
#define ELBC_OR_CST       (1 << 8)  /* command setup time - TRLX=0 (0=0 or 1=0.25) TRLX=1 (0=0.5 or 1=1) clock cycles */
#define ELBC_OR_CHT       (1 << 7)  /* command hold time - TRLX=0 (0=0.5 or 1=1) TRLX=1 (0=1.5 or 1=2) clock cycles */
#define ELBC_OR_SCY(n)    (((n) & 0x7) << 4) /* cycle length in bus clocks (0-7 bus clock cycle wait states) */
#define ELBC_OR_RST       (1 << 3)  /* read time setup - read enable asserted 1 clock */
#define ELBC_OR_TRLX      (1 << 2)  /* timing related */
#define ELBC_OR_EHTR      (1 << 1)  /* extended hold time - LRLX=0 (0=1 or 1=2), LRLX=1 (0=2 or 1=8) inserted idle clock cycles */

#define ELBC_LSOR_BANK(n) ((n) & 0x7) /* flash bank 0-7 */

#define ELBC_FMR_CWTO(n)  /* command wait timeout */
#define ELBC_FMR_BOOT     (1 << 11) /* flash auto-boot lead mode 0=FCM is op normal, 1=eLBC autoload 4-Kbyte boot block */
#define ELBC_FMR_ECCM     (1 << 8)  /* ECC mode 0=ECC is checked/calc 6/8 spare, 1=8/10 spare */
#define ELBC_FMR_AL(n)    (((n) & 0x3) << 4) /* address length */
#define ELBC_FMR_OP(n)    (((n) & 0x3) << 0) /* flash operation 0=normal, 1=sim auto-boot block load, 2=exe FIR cmd w/write protect enable, 3=exe FIR cmd */

#define ELBC_FIR_OP(s,op) ((op) & 0xF) << (28 - ((s % 8) * 4)) /* up to 8 sequences of instructions */
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
#define ELBC_FCR_CMD(s,cmd) (((cmd) & 0xFF) << (24 - ((s % 4) * 8))) /* up to 4 command opcodes */

#define ELBC_FBAR_ADDR(n) (((n) >> 5) & 0xFFFFFF)
#define ELBC_FPAR_PI(n)   (((n) & 0x7) << 10) /* page index */
#define ELBC_FPAR_MS      (1 << 9) /* main/spare region locator */
#define ELBC_FPAR_CI(n)   ((n) & 0x1FF)

#define ELBC_LTESR_FCT (1 << 30) /* FCM command timeout */
#define ELBC_LTESR_PAR (1 << 29) /* Parity of ECC error */
#define ELBC_LTESR_CC  (1 << 0)  /* FCM command completion event */

#define ELBC_NAND_MASK (ELBC_LTESR_FCT | ELBC_LTESR_PAR | ELBC_LTESR_CC)


/* IFC AMASK - RM Table 12-6 - Count of MSB minus 1 */
enum elbc_amask_sizes {
    ELBC_AMASK_32KB =  0x1FFFF,
    ELBC_AMASK_64KB =  0x0FFFF,
    ELBC_AMASK_128KB = 0x0FFFE,
    ELBC_AMASK_256KB = 0x0FFFC,
    ELBC_AMASK_512KB = 0x0FFF8,
    ELBC_AMASK_1MB   = 0x0FFF0,
    ELBC_AMASK_2MB   = 0x0FFE0,
    ELBC_AMASK_4MB   = 0x0FFC0,
    ELBC_AMASK_8MB   = 0x0FF80,
    ELBC_AMASK_16MB  = 0x0FF00,
    ELBC_AMASK_32MB  = 0x0FE00,
    ELBC_AMASK_64MB  = 0x0FC00,
    ELBC_AMASK_128MB = 0x0F800,
    ELBC_AMASK_256MB = 0x0F000,
    ELBC_AMASK_512MB = 0x0E000,
    ELBC_AMASK_1GB   = 0x0C000,
    ELBC_AMASK_2GB   = 0x08000,
    ELBC_AMASK_4GB   = 0x00000,
};


/* NAND Flash */
#define FLASH_BASE        0xFC000000 /* memory used for transfering block to/from NAND */

#define FLASH_BANK_SIZE   (64*1024*1024)
#define FLASH_PAGE_SIZE   (1024) /* program buffer */
#define FLASH_SECTOR_SIZE (128*1024)
#define FLASH_SECTORS     (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_CFI_16BIT   0x02 /* word */
#define FLASH_CFI_WIDTH   FLASH_CFI_16BIT

#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */


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


/* DDR */
#if 1
    #define ENABLE_DDR
#endif

/* DDR3: 512MB, 333.333 MHz (666.667 MT/s) */
#define DDR_ADDRESS     0x00000000
#define DDR_SIZE        (512 * 1024 * 1024)

#define DDR_CS0_BNDS_VAL       0x0000001F
#define DDR_CS0_CONFIG_VAL     0x80014202
#define DDR_CS_CONFIG_2_VAL    0x00000000

#define DDR_TIMING_CFG_0_VAL   0x00330004
#define DDR_TIMING_CFG_1_VAL   0x5d5bd746
#define DDR_TIMING_CFG_2_VAL   0x0fa8c8cd
#define DDR_TIMING_CFG_3_VAL   0x00010000
#define DDR_TIMING_CFG_4_VAL   0x00220001
#define DDR_TIMING_CFG_5_VAL   0x03402400

#define DDR_SDRAM_MODE_VAL     0x40461320
#define DDR_SDRAM_MODE_2_VAL   0x8000C000
#define DDR_SDRAM_MODE_3_8_VAL 0x00000000
#define DDR_SDRAM_MD_CNTL_VAL  0x00000000

#define DDR_SDRAM_CFG_VAL      0x47000000
#define DDR_SDRAM_CFG_2_VAL    0x04401040

#define DDR_SDRAM_INTERVAL_VAL 0x0a280000
#define DDR_DATA_INIT_VAL      0x1021BABE
#define DDR_SDRAM_CLK_CNTL_VAL 0x03000000
#define DDR_ZQ_CNTL_VAL        0x89080600

#define DDR_WRLVL_CNTL_VAL     0x86559608

#define DDR_DDRCDR_1_VAL       0x000EAA00
#define DDR_DDRCDR_2_VAL       0x00000000

#define DDR_ERR_INT_EN_VAL     0x0000000D
#define DDR_ERR_SBE_VAL        0x00010000


/* 12.4 DDR Memory Map */
#define DDR_BASE           (CCSRBAR + 0x8000)

#define DDR_CS_BNDS(n)     *((volatile uint32_t*)(DDR_BASE + 0x000 + (n * 8))) /* Chip select n memory bounds */
#define DDR_CS_CONFIG(n)   *((volatile uint32_t*)(DDR_BASE + 0x080 + (n * 4))) /* Chip select n configuration */
#define DDR_CS_CONFIG_2(n) *((volatile uint32_t*)(DDR_BASE + 0x0C0 + (n * 4))) /* Chip select n configuration 2 */
#define DDR_SDRAM_CFG      *((volatile uint32_t*)(DDR_BASE + 0x110)) /* DDR SDRAM control configuration */
#define DDR_SDRAM_CFG_2    *((volatile uint32_t*)(DDR_BASE + 0x114)) /* DDR SDRAM control configuration 2 */
#define DDR_SDRAM_INTERVAL *((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_INIT_ADDR      *((volatile uint32_t*)(DDR_BASE + 0x148)) /* DDR training initialization address */
#define DDR_INIT_EXT_ADDR  *((volatile uint32_t*)(DDR_BASE + 0x14C)) /* DDR training initialization extended address */
#define DDR_DATA_INIT      *((volatile uint32_t*)(DDR_BASE + 0x128)) /* DDR training initialization value */
#define DDR_TIMING_CFG_0   *((volatile uint32_t*)(DDR_BASE + 0x104)) /* DDR SDRAM timing configuration 0 */
#define DDR_TIMING_CFG_1   *((volatile uint32_t*)(DDR_BASE + 0x108)) /* DDR SDRAM timing configuration 1 */
#define DDR_TIMING_CFG_2   *((volatile uint32_t*)(DDR_BASE + 0x10C)) /* DDR SDRAM timing configuration 2 */
#define DDR_TIMING_CFG_3   *((volatile uint32_t*)(DDR_BASE + 0x100)) /* DDR SDRAM timing configuration 3 */
#define DDR_TIMING_CFG_4   *((volatile uint32_t*)(DDR_BASE + 0x160)) /* DDR SDRAM timing configuration 4 */
#define DDR_TIMING_CFG_5   *((volatile uint32_t*)(DDR_BASE + 0x164)) /* DDR SDRAM timing configuration 5 */
#define DDR_TIMING_CFG_6   *((volatile uint32_t*)(DDR_BASE + 0x168)) /* DDR SDRAM timing configuration 6 */
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
#define DDR_SDRAM_MD_CNTL  *((volatile uint32_t*)(DDR_BASE + 0x120)) /* DDR SDRAM mode control */
#define DDR_SDRAM_INTERVAL *((volatile uint32_t*)(DDR_BASE + 0x124)) /* DDR SDRAM interval configuration */
#define DDR_SDRAM_CLK_CNTL *((volatile uint32_t*)(DDR_BASE + 0x130)) /* DDR SDRAM clock control */

#define DDR_SDRAM_CFG_MEM_EN   0x80000000 /* SDRAM interface logic is enabled */
#define DDR_SDRAM_CFG2_D_INIT  0x00000010 /* data initialization in progress */


#ifdef DEBUG_UART
static void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 300000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);

    while (!(UART_LSR(UART_SEL) & UART_LSR_TEMT))
       ;

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
        while (!(UART_LSR(UART_SEL) & UART_LSR_THRE))
            ;
        UART_THR(UART_SEL) = buf[pos++];
    }
}
#endif /* DEBUG_UART */

/* called from boot_ppc_start.S */
void law_init(void)
{

}

extern void write_tlb(uint32_t mas0, uint32_t mas1, uint32_t mas2, uint32_t mas3,
    uint32_t mas7);

void set_tlb(uint8_t tlb, uint8_t esel, uint32_t epn, uint64_t rpn,
             uint8_t perms, uint8_t wimge,
             uint8_t ts, uint8_t tsize, uint8_t iprot)
{
    uint32_t _mas0, _mas1, _mas2, _mas3, _mas7;

    _mas0 = BOOKE_MAS0(tlb, esel, 0);
    _mas1 = BOOKE_MAS1(1, iprot, 0, ts, tsize);
    _mas2 = BOOKE_MAS2(epn, wimge);
    _mas3 = BOOKE_MAS3(rpn, 0, perms);
    _mas7 = BOOKE_MAS7(rpn);

    write_tlb(_mas0, _mas1, _mas2, _mas3, _mas7);
}

static void hal_flash_set_addr(int col, int page)
{
    ELBC_FBAR = ELBC_FBAR_ADDR(page);
    ELBC_FPAR = ELBC_FPAR_PI(page) | ELBC_FPAR_CI(col);
}

static int hal_flash_command()
{

    /* Read Flash ID */
    ELBC_FIR = ELBC_FIR_OP(0, ELBC_FIR_OP_CW0) |
               ELBC_FIR_OP(1, ELBC_FIR_OP_UA) |
               ELBC_FIR_OP(2, ELBC_FIR_OP_RBW);
    ELBC_FCR = ELBC_FCR_CMD(0, NAND_CMD_READ_ID);
    ELBC_FBCR = 5;
    hal_flash_set_addr(0, 0);

    ELBC_FMR = ELBC_FMR_OP(3); /* execure FIR with write support */
    ELBC_MDR = 0;
    ELBC_LSOR = ELBC_LSOR_BANK(0);

    /* wait for FCM complete flag */
    while (!(ELBC_LTESR & ELBC_LTESR_CC));
    /* clear interrupt */
    ELBC_LTESR &= ELBC_NAND_MASK;
    ELBC_LTEATR = 0;

    /* TODO: Download from correct base buffer page (8 - 1KB pages) */
    /* Example: FLASH_BASE + (0x400 * page) + */

    return 0;
}

static void hal_flash_init(void)
{
    uint8_t flash_id[8];

    /* NAND Definitions (CS0) */
    /* FSM, 8-bit, ECC check/gen enable, valid */
    ELBC_BR(0) = ELBC_BR_ADDR(FLASH_BASE) |
        ELBC_BR_MSEL(1) | ELBC_BR_PS(1) | ELBC_BR_DECC(2) | ELBC_BR_V;

    /* Mask=32MB, Page Size = 512, relaxed timing */
    ELBC_OR(0) = ELBC_OR_AMASK(ELBC_AMASK_32MB) |
        ELBC_OR_CSCT | ELBC_OR_CST | ELBC_OR_CHT | ELBC_OR_SCY(1) |
        ELBC_OR_TRLX | ELBC_OR_EHTR;

    /* Clear event registers */
    ELBC_LTESR = ELBC_NAND_MASK;
    ELBC_LTEATR = 0;

    /* Enable interrupts */
    ELBC_LTEIR = ELBC_NAND_MASK;

}

void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    /* Setup DDR CS (chip select) bounds */
    DDR_CS_BNDS(0) = DDR_CS0_BNDS_VAL;
    DDR_CS_CONFIG(0) = DDR_CS0_CONFIG_VAL;
    DDR_CS_CONFIG_2(0) = DDR_CS_CONFIG_2_VAL;

    /* DDR SDRAM timing configuration */
    DDR_TIMING_CFG_0 = DDR_TIMING_CFG_0_VAL;
    DDR_TIMING_CFG_1 = DDR_TIMING_CFG_1_VAL;
    DDR_TIMING_CFG_2 = DDR_TIMING_CFG_2_VAL;
    DDR_TIMING_CFG_3 = DDR_TIMING_CFG_3_VAL;
    DDR_TIMING_CFG_4 = DDR_TIMING_CFG_4_VAL;
    DDR_TIMING_CFG_5 = DDR_TIMING_CFG_5_VAL;

    /* DDR SDRAM mode configuration */
    DDR_SDRAM_MODE =   DDR_SDRAM_MODE_VAL;
    DDR_SDRAM_MODE_2 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_3 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MODE_4 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MODE_5 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MODE_6 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MODE_7 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MODE_8 = DDR_SDRAM_MODE_3_8_VAL;
    DDR_SDRAM_MD_CNTL = DDR_SDRAM_MD_CNTL_VAL;

    /* DDR Configuration */
    DDR_SDRAM_INTERVAL = DDR_SDRAM_INTERVAL_VAL;
    DDR_SDRAM_CLK_CNTL = DDR_SDRAM_CLK_CNTL_VAL;
    DDR_DATA_INIT = DDR_DATA_INIT_VAL;
    DDR_ZQ_CNTL = DDR_ZQ_CNTL_VAL;
    DDR_WRLVL_CNTL = DDR_WRLVL_CNTL_VAL;
    DDR_SR_CNTR = 0;
    DDR_SDRAM_RCW_1 = 0;
    DDR_SDRAM_RCW_2 = 0;
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

    /* TODO: Errata A009942 */

    /* Enable controller */
    DDR_SDRAM_CFG |= DDR_SDRAM_CFG_MEM_EN;
    asm volatile("sync;isync");

    /* Map LAW for DDR */
    LAWAR(4) = 0; /* reset */
    LAWBAR(4) = 0x0000000;
    LAWAR(4) = LAWAR_ENABLE | LAWAR_TRGT_ID(LAW_TRGT_DDR) | LAW_SIZE_8GB;

    /* Wait for data initialization is complete */
    while ((DDR_SDRAM_CFG_2 & DDR_SDRAM_CFG2_D_INIT));

    /* DDR - TBL=1, Entry 19 */
    set_tlb(1, 19, DDR_ADDRESS, 0,
        MAS3_SX | MAS3_SW | MAS3_SR, 0,
        0, BOOKE_PAGESZ_2G, 1);
#endif
}

void hal_init(void)
{
    LBC_LBCR |= LBC_LBCR_ABSWP; /* Enable LBC address byte swap */

#ifdef DEBUG_UART
    uint32_t fw;

    uart_init();
    uart_write("wolfBoot Init\n", 14);
#endif

    hal_flash_init();
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    /* TODO: Implement NAND flash write using IFC */
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    /* TODO: Implement NAND flash erase using IFC */
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


int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    return 0;
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
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
}
#endif
