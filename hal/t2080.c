/* t2080.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

/* #define DEBUG_UART */

#define CCSRBAR 0xFE000000

#ifdef DEBUG_UART
#define UART0_OFFSET 0x11C500
#define UART0_BASE  (CCSRBAR + UART0_OFFSET)

#define SYS_CLK 600000000
#define BAUD_RATE 115200

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

static void uart_init(void) {
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

    /* enable buad rate access (DLAB=1) - divisor latch access bit*/
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


/* T2080 RM 2.4 */
#define CSSR CCSRBAR
#define LAWBARHn(n) *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x0))
#define LAWBARLn(n) *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x4))
#define LAWBARn(n)  *((volatile uint32_t*)(CSSR + 0xC00 + n*0x10 + 0x8))

/* T2080 2.4.3 - size is equal to 2^(enum + 1) */
enum {
    LAW_SIZE_4KB = 0xb,
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
    LAW_SIZE_256MB, /* 0x1b */
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

static void law_init(void) {
    /* T2080RM table 2-1 */
    int id = 0x1f; /* IFC */
    LAWBARn (0) = 0;
    LAWBARHn(0) = 0xf;
    LAWBARLn(0) = 0xe8000000;
    LAWBARn (0) = (1<<31) | (id<<20) | LAW_SIZE_256MB;

    id = 0x18;
    LAWBARn (1) = 0;
    LAWBARHn(1) = 0xf;
    LAWBARLn(1) = 0xf4000000;
    LAWBARn (1) = (1<<31) | (id<<20) | LAW_SIZE_32MB;

}

void hal_init(void) {
    law_init();
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot\n", 9);
#endif
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len) {
  return 0;
}

int hal_flash_erase(uint32_t address, int len) {
  return 0;
}

void hal_flash_unlock(void) {
}

void hal_flash_lock(void) {
}

void hal_prepare_boot(void) {}
