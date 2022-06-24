/* app_t2080.c
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

static const char* hex_lut = "0123456789abcdef";

#endif /* DEBUG_UART */


void main(void) {
#ifdef DEBUG_UART
    int i = 0;
    int j = 0;
    int k = 0;
    char snum[8];

    uart_write("Test App\n", 9);
#endif

    /* Wait for reboot */
    while(1) {
#ifdef DEBUG_UART
        for (j=0; j<1000000; j++)
            ;
        i++;

        uart_write("\r\n0x", 4);
        for (k=0; k<8; k++) {
            snum[7 - k] = hex_lut[(i >> 4*k) & 0xf];
        }
        uart_write(snum, 8);
#endif
    }
}
