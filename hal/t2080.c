/* t2080.c
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

/* T2080 */
#define CCSRBAR (0xFE000000)
#define SYS_CLK (600000000)

/* T2080 PC16552D Dual UART */
#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */

#define UART_BASE(n) (CCSRBAR + 0x11C500 + (n * 0x1000))

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


/* T2080 LAW - Local Access Window (Memory Map) - RM 2.4 */
#define LAWBAR_BASE(n) (CCSRBAR + 0xC00 + (n * 0x10))
#define LAWBARHn(n)   *((volatile uint32_t*)(LAWBAR_BASE(n) + 0x0))
#define LAWBARLn(n)   *((volatile uint32_t*)(LAWBAR_BASE(n) + 0x4))
#define LAWBARn(n)    *((volatile uint32_t*)(LAWBAR_BASE(n) + 0x8))

#define LAWBARn_ENABLE      (1<<31)
#define LAWBARn_TRGT_ID(id) (id<<20)

/* T2080 Global Source/Target ID Assignments - RM Table 2-1 */
enum law_target_id {
    LAW_TRGT_BMAN = 0x18, /* Buffer Manager (BMan) (control) */
    LAW_TRGT_IFC  = 0x1F, /* Integrated Flash Controller */
};

/* T2080 2.4.3 - size is equal to 2^(enum + 1) */
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
    LAW_SIZE_64GB,
    LAW_SIZE_128GB,
    LAW_SIZE_256GB,
    LAW_SIZE_512GB,
    LAW_SIZE_1TB,
};


/* T2080 IFC (Integrated Flash Controller) - RM 13.3 */
#define IFC_BASE        (CCSRBAR + 0x00124000)
#define IFC_MAX_BANKS   8

#define IFC_CSPR_EXT(n) *((volatile uint32_t*)(IFC_BASE + 0x000C + (n * 0xC))) /* Extended Base Address */
#define IFC_CSPR(n)     *((volatile uint32_t*)(IFC_BASE + 0x0010 + (n * 0xC))) /* Chip-select Property */
#define IFC_AMASK(n)    *((volatile uint32_t*)(IFC_BASE + 0x00A0 + (n * 0xC)))
#define IFC_CSOR(n)     *((volatile uint32_t*)(IFC_BASE + 0x0130 + (n * 0xC)))
#define IFC_CSOR_EXT(n) *((volatile uint32_t*)(IFC_BASE + 0x0134 + (n * 0xC)))
#define IFC_FTIM0(n)    *((volatile uint32_t*)(IFC_BASE + 0x01C0 + (n * 0x30)))
#define IFC_FTIM1(n)    *((volatile uint32_t*)(IFC_BASE + 0x01C4 + (n * 0x30)))
#define IFC_FTIM2(n)    *((volatile uint32_t*)(IFC_BASE + 0x01C8 + (n * 0x30)))
#define IFC_FTIM3(n)    *((volatile uint32_t*)(IFC_BASE + 0x01CC + (n * 0x30)))

#define IFC_CSPR_PHYS_ADDR(x) (((uint32_t)x) & 0xFFFF0000) /* Physical base address */
#define IFC_CSPR_PORT_SIZE_8  0x00000080 /* Port Size 8 */
#define IFC_CSPR_PORT_SIZE_16 0x00000100 /* Port Size 16 */
#define IFC_CSPR_WP           0x00000040 /* Write Protect */
#define IFC_CSPR_MSEL_NOR     0x00000000 /* Mode Select - NOR */
#define IFC_CSPR_MSEL_NAND    0x00000002 /* Mode Select - NAND */
#define IFC_CSPR_MSEL_GPCM    0x00000004 /* Mode Select - GPCM (General-purpose chip-select machine) */
#define IFC_CSPR_V            0x00000001 /* Bank Valid */

/* NOR Timings (IFC clocks) */
#define IFC_FTIM0_NOR_TACSE(n) (((n) & 0x0F) << 28) /* After address hold cycle */
#define IFC_FTIM0_NOR_TEADC(n) (((n) & 0x3F) << 16) /* External latch address delay cycles */
#define IFC_FTIM0_NOR_TAVDS(n) (((n) & 0x3F) << 8)  /* Delay between CS assertion */
#define IFC_FTIM0_NOR_TEAHC(n) (((n) & 0x3F) << 0)  /* External latch address hold cycles */
#define IFC_FTIM1_NOR_TACO(n)  (((n) & 0xFF) << 24) /* CS assertion to output enable */
#define IFC_FTIM1_NOR_TRAD(n)  (((n) & 0x3F) << 8)  /* read access delay */
#define IFC_FTIM1_NOR_TSEQ(n)  (((n) & 0x3F) << 0)  /* sequential read access delay */
#define IFC_FTIM2_NOR_TCS(n)   (((n) & 0x0F) << 24) /* Chip-select assertion setup time */
#define IFC_FTIM2_NOR_TCH(n)   (((n) & 0x0F) << 18) /* Chip-select hold time */
#define IFC_FTIM2_NOR_TWPH(n)  (((n) & 0x3F) << 10) /* Chip-select hold time */
#define IFC_FTIM2_NOR_TWP(n)   (((n) & 0xFF) << 0)  /* Write enable pulse width */

/* GPCM Timings (IFC clocks) */
#define IFC_FTIM0_GPCM_TACSE(n) (((n) & 0x0F) << 28) /* After address hold cycle */
#define IFC_FTIM0_GPCM_TEADC(n) (((n) & 0x3F) << 16) /* External latch address delay cycles */
#define IFC_FTIM0_GPCM_TEAHC(n) (((n) & 0x3F) << 0)  /* External latch address hold cycles */
#define IFC_FTIM1_GPCM_TACO(n)  (((n) & 0xFF) << 24) /* CS assertion to output enable */
#define IFC_FTIM1_GPCM_TRAD(n)  (((n) & 0x3F) << 8)  /* read access delay */
#define IFC_FTIM2_GPCM_TCS(n)   (((n) & 0x0F) << 24) /* Chip-select assertion setup time */
#define IFC_FTIM2_GPCM_TCH(n)   (((n) & 0x0F) << 18) /* Chip-select hold time */
#define IFC_FTIM2_GPCM_TWP(n)   (((n) & 0xFF) << 0)  /* Write enable pulse width */

/* IFC AMASK - RM Table 13-3 - Count of MSB minus 1 */
enum ifc_amask_sizes {
    IFC_AMASK_64KB =  0xFFFF,
    IFC_AMASK_128KB = 0xFFFE,
    IFC_AMASK_256KB = 0xFFFC,
    IFC_AMASK_512KB = 0xFFF8,
    IFC_AMASK_1MB   = 0xFFF0,
    IFC_AMASK_2MB   = 0xFFC0,
    IFC_AMASK_4MB   = 0xFF80,
    IFC_AMASK_8MB   = 0xFF00,
    IFC_AMASK_16MB  = 0xFE00,
    IFC_AMASK_32MB  = 0xFC00,
    IFC_AMASK_128MB = 0xF800,
    IFC_AMASK_256MB = 0xF000,
    IFC_AMASK_512MB = 0xE000,
    IFC_AMASK_1GB   = 0xC000,
    IFC_AMASK_2GB   = 0x8000,
    IFC_AMASK_4GB   = 0x0000,
};


/* NOR Flash */
#define FLASH_BASE        0xE8000000
#define FLASH_BANK_SIZE   (128*1024*1024)
#define FLASH_PAGE_SIZE   (1024) /* program buffer */
#define FLASH_SECTOR_SIZE (128*1024)
#define FLASH_SECTORS     (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)
#define FLASH_CFI_16BIT   0x02 /* word */
#define FLASH_CFI_WIDTH   FLASH_CFI_16BIT

#define FLASH_ERASE_TOUT  60000 /* Flash Erase Timeout (ms) */
#define FLASH_WRITE_TOUT  500   /* Flash Write Timeout (ms) */


/* CPLD */
#define CPLD_BASE              (0xFFDF0000)

#define CPLD_SPARE              0x00
#define CPLD_SATA_MUX_SEL       0x02
#define CPLD_BANK_SEL           0x04
#define CPLD_FW_REV             0x06
#define CPLD_TTL_RW             0x08
#define CPLD_TTL_LPBK           0x0A
#define CPLD_TTL_DATA           0x0C
#define CPLD_PROC_STATUS        0x0E /* write 1 to enable proc reset function, reset default value is 0 */
#define CPLD_FPGA_RDY           0x10 /* read only when reg read 0x0DB1 then fpga is ready */
#define CPLD_PCIE_SW_RESET      0x12 /* write 1 to reset the PCIe switch */
#define CPLD_WR_TTL_INT_EN      0x14
#define CPLD_WR_TTL_INT_DIR     0x16
#define CPLD_INT_STAT           0x18
#define CPLD_WR_TEMP_ALM_OVRD   0x1A /* write 0 to enable temp shutdown. reset default value is 1 */
#define CPLD_PWR_DWN_CMD        0x1C
#define CPLD_TEMP_ALM_INT_STAT  0x1E
#define CPLD_WR_TEMP_ALM_INT_EN 0x20

#define CPLD_FLASH_BANK_0       0x00
#define CPLD_FLASH_BANK_1       0x01

#define CPLD_DATA(n) *((volatile uint8_t*)(CPLD_BASE + n))


/* SATA */
#define SATA_ENBL (*(volatile uint32_t *)(0xB1003F4C)) /* also saw 0xB4003F4C */


/* DDR */
/* NAII 68PPC2 - 8GB discrete DDR3 IM8G08D3EBDG-15E */


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
    /* IFC - NOR Flash */
    LAWBARn (1) = 0; /* reset */
    LAWBARHn(1) = 0xF;
    LAWBARLn(1) = FLASH_BASE;
    LAWBARn (1) = LAWBARn_ENABLE | LAWBARn_TRGT_ID(LAW_TRGT_IFC) | LAW_SIZE_128MB;

    /* IFC - CPLD */
    LAWBARn (2) = 0; /* reset */
    LAWBARHn(2) = 0xF;
    LAWBARLn(2) = CPLD_BASE;
    LAWBARn (2) = LAWBARn_ENABLE | LAWBARn_TRGT_ID(LAW_TRGT_IFC) | LAW_SIZE_4KB;

    /* Buffer Manager (BMan) (control) */
    LAWBARn (3) = 0; /* reset */
    LAWBARHn(3) = 0xF;
    LAWBARLn(3) = 0xF4000000;
    LAWBARn (3) = LAWBARn_ENABLE | LAWBARn_TRGT_ID(LAW_TRGT_BMAN) | LAW_SIZE_32MB;
}

#ifdef DEBUG_UART
static const char* kHexLut = "0123456789abcdef";
static void tohexstr(uint32_t val, char* out)
{
    int i;
    for (i=0; i<(int)sizeof(val)*2; i++) {
        out[(sizeof(val)*2) - i - 1] = kHexLut[(val >> 4*i) & 0xF];
    }
}
#endif

static void hal_flash_init(void)
{
    /* NOR IFC Flash Timing Parameters */
    IFC_FTIM0(0) = (IFC_FTIM0_NOR_TACSE(4) | \
                    IFC_FTIM0_NOR_TEADC(5) | \
                    IFC_FTIM0_NOR_TEAHC(5));
    IFC_FTIM1(0) = (IFC_FTIM1_NOR_TACO(53) |
                    IFC_FTIM1_NOR_TRAD(26) |
                    IFC_FTIM1_NOR_TSEQ(19));
    IFC_FTIM2(0) = (IFC_FTIM2_NOR_TCS(4) |
                    IFC_FTIM2_NOR_TCH(4) |
                    IFC_FTIM2_NOR_TWPH(14) |
                    IFC_FTIM2_NOR_TWP(28));
    IFC_FTIM3(0) = 0;
    /* NOR IFC Definitions (CS0) */
    IFC_CSPR_EXT(0) = (0xF);
    IFC_CSPR(0) =     (IFC_CSPR_PHYS_ADDR(FLASH_BASE) | \
                       IFC_CSPR_PORT_SIZE_16 | \
                       IFC_CSPR_MSEL_NOR | \
                       IFC_CSPR_V);
    IFC_AMASK(0) = IFC_AMASK_128MB;
    IFC_CSOR(0) = 0x0000000C; /* TRHZ (80 clocks for read enable high) */
}

static void hal_cpld_init(void)
{
    /* CPLD IFC Timing Parameters */
    IFC_FTIM0(3) = (IFC_FTIM0_GPCM_TACSE(16) |
                    IFC_FTIM0_GPCM_TEADC(16) |
                    IFC_FTIM0_GPCM_TEAHC(16));
    IFC_FTIM1(3) = (IFC_FTIM1_GPCM_TACO(16) |
                    IFC_FTIM1_GPCM_TRAD(31));
    IFC_FTIM2(3) = (IFC_FTIM2_GPCM_TCS(16) |
                    IFC_FTIM2_GPCM_TCH(8) |
                    IFC_FTIM2_GPCM_TWP(31));
    IFC_FTIM3(3) = 0;

    /* CPLD IFC Definitions (CS3) */
    IFC_CSPR_EXT(3) = (0xF);
    IFC_CSPR(3) =     (IFC_CSPR_PHYS_ADDR(CPLD_BASE) |
                       IFC_CSPR_PORT_SIZE_16 |
                       IFC_CSPR_MSEL_GPCM |
                       IFC_CSPR_V);
    IFC_AMASK(3) = IFC_AMASK_64KB;
    IFC_CSOR(3) = 0;
}

void hal_init(void)
{
#ifdef DEBUG_UART
    uint8_t fw;
    char buf[sizeof(uint32_t)*2];

    uart_init();
    uart_write("wolfBoot Init\n", 14);
#endif

    hal_flash_init();
    hal_cpld_init();

#if 0 /* NOT TESTED */
    CPLD_DATA(CPLD_PROC_STATUS) = 1; /* Enable proc reset */
    CPLD_DATA(CPLD_WR_TEMP_ALM_OVRD) = 0; /* Enable temp alarm */

#ifdef DEBUG_UART
    fw = CPLD_DATA(CPLD_FW_REV);

    uart_write("CPLD FW Rev: 0x", 15);
    tohexstr(fw, buf);
    uart_write(buf, (uint32_t)sizeof(buf));
    uart_write("\n", 1);
#endif

    /* Disable SATA Write Protection */
    SATA_ENBL = 0;
#endif
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    /* TODO: Implement NOR flash write using IFC */
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    /* TODO: Implement NOR flash erase using IFC */
    return 0;
}

void hal_flash_unlock(void)
{
    /* Disable all flash protection bits */
    /* enter Non-volatile protection mode (C0h) */
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xAAAA;
    *((volatile uint16_t*)(FLASH_BASE + 0x554)) = 0x5555;
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xC0C0;
    /* clear all protection bit (80h/30h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x8080;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x3030;
    /* exit Non-volatile protection mode (90h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x9090;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
}

void hal_flash_lock(void)
{
    /* Enable all flash protection bits */
    /* enter Non-volatile protection mode (C0h) */
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xAAAA;
    *((volatile uint16_t*)(FLASH_BASE + 0x554)) = 0x5555;
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xC0C0;
    /* set all protection bit (A0h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0xA0A0;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
    /* exit Non-volatile protection mode (90h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x9090;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
}

void hal_prepare_boot(void)
{

}

void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
}
