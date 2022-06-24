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

/* #define DEBUG_UART */

#define OFFSETOF(type, field) ((uint32_t)&(((type *)0)->field))

#define CCSRBAR 0xFE000000

#ifdef DEBUG_UART
    #define UART0_OFFSET 0x11C500
    #define UART0_BASE  (CCSRBAR + UART0_OFFSET)

    #define SYS_CLK 600000000
    #define BAUD_RATE 115200
#endif

/* T2080 RM 2.4 */
#define CSSR CCSRBAR
#define LAWBARHn(n) *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x0))
#define LAWBARLn(n) *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x4))
#define LAWBARn(n)  *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x8))

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


/* CPLD */
#define CPLD_BASE 0xFFDF0000

#define CPLD_LBMAP_MASK     0x3F
#define CPLD_BANK_SEL_MASK  0x07
#define CPLD_BANK_OVERRIDE  0x40
#define CPLD_LBMAP_ALTBANK  0x44 /* BANK OR | BANK 4 */
#define CPLD_LBMAP_DFLTBANK 0x40 /* BANK OR | BANK 0 */
#define CPLD_LBMAP_RESET    0xFF
#define CPLD_LBMAP_SHIFT    0x03
#define CPLD_BOOT_SEL       0x80
#define CPLD_RSTCON_EDC_RST 0x04 /* RSTCON Register */

/* CPLD register set for T2080RDB */
typedef struct cpld_data {
    uint8_t chip_id1;    /* 0x00 - Chip ID1 register */
    uint8_t chip_id2;    /* 0x01 - Chip ID2 register */
    uint8_t hw_ver;      /* 0x02 - Hardware Revision Register */
    uint8_t sw_ver;      /* 0x03 - Software Revision register */
    uint8_t res0[12];    /* 0x04 - 0x0F - not used */
    uint8_t reset_ctl;   /* 0x10 - Reset control Register */
    uint8_t flash_csr;   /* 0x11 - Flash control and status register */
    uint8_t thermal_csr; /* 0x12 - Thermal control and status register */
    uint8_t led_csr;     /* 0x13 - LED control and status register */
    uint8_t sfp_csr;     /* 0x14 - SFP+ control and status register */
    uint8_t misc_csr;    /* 0x15 - Misc control and status register */
    uint8_t boot_or;     /* 0x16 - Boot config override register */
    uint8_t boot_cfg1;   /* 0x17 - Boot configuration register 1 */
    uint8_t boot_cfg2;   /* 0x18 - Boot configuration register 2 */
} cpld_data;

#define CPLD_READ(reg)         cpld_read( OFFSETOF(struct cpld_data, reg))
#define CPLD_WRITE(reg, value) cpld_write(OFFSETOF(struct cpld_data, reg), value)


/* SATA */
#define SATA_ENBL (*(volatile uint32_t *)(0xB1003F4C)) /* also saw 0xB4003F4C */



/* IO Helpers */
static inline uint8_t in_8(const volatile unsigned char *addr)
{
    uint8_t ret;
    asm volatile("sync;\n"
                 "lbz %0,%1;\n"
                 "isync"
                 : "=r" (ret)
                 : "m" (*addr));
    return ret;
}
static inline void out_8(volatile unsigned char *addr, uint8_t val)
{
    asm volatile("sync;\n"
                 "stb %1,%0;\n"
                 : "=m" (*addr)
                 : "r" (val));
}

/* CPLD */
static uint8_t cpld_read(unsigned int reg)
{
	void *p = (void *)CPLD_BASE;
	return in_8(p + reg);
}

static void cpld_write(unsigned int reg, uint8_t value)
{
	void *p = (void *)CPLD_BASE;
	out_8(p + reg, value);
}

/* Set the boot bank to the default bank */
void cpld_set_defbank(void)
{
	uint8_t reg = CPLD_READ(flash_csr);

	reg = (reg & ~CPLD_BANK_SEL_MASK) | CPLD_LBMAP_DFLTBANK;
	CPLD_WRITE(flash_csr, reg);
	CPLD_WRITE(reset_ctl, CPLD_LBMAP_RESET);
}



#ifdef DEBUG_UART
static void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 300000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);
    register volatile uint8_t* uart = (uint8_t*)UART0_BASE;

    while (!(in_8(uart + 5) & 0x40))
       ;

    /* set ier, fcr, mcr */
    out_8(uart + 1, 0);
    out_8(uart + 4, 3);
    out_8(uart + 2, 7);

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    out_8(uart + 3, 0x83);
    /* set divisor */
    out_8(uart + 0, div & 0xff);
    out_8(uart + 1, (div>>8) & 0xff);
    /* disable rate access (DLAB=0) */
    out_8(uart + 3, 0x03);
}

static void uart_write(const char* buf, uint32_t sz)
{
    volatile uint8_t* uart = (uint8_t*)UART0_BASE;
    uint32_t pos = 0;
    while (sz-- > 0) {
        while (!(in_8(uart + 5) & 0x20))
            ;
        out_8(uart + 0, buf[pos++]);
    }
}
#endif /* DEBUG_UART */


void law_init(void)
{
    /* T2080RM table 2-1 */
    int id = 0x1f; /* IFC */
    LAWBARn (1) = 0;
    LAWBARHn(1) = 0xf;
    LAWBARLn(1) = 0xe8000000;
    LAWBARn (1) = (1<<31) | (id<<20) | LAW_SIZE_256MB;

    id = 0x18;
    LAWBARn (2) = 0;
    LAWBARHn(2) = 0xf;
    LAWBARLn(2) = 0xf4000000;
    LAWBARn (2) = (1<<31) | (id<<20) | LAW_SIZE_32MB;

}

#ifdef DEBUG_UART
static const char* kHexLut = "0123456789abcdef";
static void tohexstr(uint32_t val, char* out)
{
    int i;
    for (i=0; i<sizeof(val)*2; i++) {
        out[(sizeof(val)*2) - i - 1] = kHexLut[(val >> 4*i) & 0xF];
    }
}
#endif

void hal_init(void)
{
#ifdef DEBUG_UART
    char buf[sizeof(uint32_t)*2];

    uart_init();
    uart_write("wolfBoot Init\n", 14);

    uart_write("Board Rev: 0x", 13);
    tohexstr(CPLD_READ(hw_ver)), buf);
    uart_write(buf, 8);
    uart_write(" CPLD ver: 0x", 13);
    tohexstr(CPLD_READ(sw_ver)), buf);
    uart_write(buf, 8);
    uart_write("\n", 1);
#endif


    /* CPLD setup */
    cpld_set_defbank();

    /* Disable SATA Write Protection */
    SATA_ENBL = 0;

}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /* TODO: Implement NOR flash write */
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    /* TODO: Implement NOR flash erase */
    return 0;
}

void hal_flash_unlock(void)
{
    // CSPRn[WP]
    //protect off eff40000 +C0000
}

void hal_flash_lock(void)
{
    //protect on eff40000 +C0000
}

void hal_prepare_boot(void)
{

}

void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
}
