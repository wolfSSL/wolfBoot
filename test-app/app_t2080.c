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

#define CCSRBAR 0xFE000000

/* T2080 PC16552D Dual UART */
#define UART0_OFFSET 0x11C500
#define UART1_OFFSET 0x11D500
#define UART0_BASE  (CCSRBAR + UART0_OFFSET)
#define UART1_BASE  (CCSRBAR + UART1_OFFSET)

#define UART_RBR 0 /* receiver buffer register */
#define UART_THR 0 /* transmitter holding register */
#define UART_IER 1 /* interrupt enable register */
#define UART_IIR 2 /* interrupt ID register */
#define UART_FCR 2 /* FIFO control register */
#define UART_FCR_TFR 0x04 /* Transmitter FIFO reset */
#define UART_FCR_RFR 0x02 /* Receiver FIFO reset */
#define UART_FCR_FEN 0x01 /* FIFO enable */

#define UART_LCR 3 /* line control register */
#define UART_LCR_DLAB 0x80 /* Divisor latch access bit */
#define UART_LCR_WLS  0x03 /* Word length select: 8-bits */
#define UART_MCR 4 /* modem control register */

#define UART_LSR 5 /* line status register */
#define UART_LSR_TEMT 0x40 /* Transmitter empty */
#define UART_LSR_THRE 0x20 /* Transmitter holding register empty */

#define UART_DLB 0 /* divisor least significant byte register */
#define UART_DMB 1 /* divisor most significant byte register */

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

static void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 300000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);
    register volatile uint8_t* uart = (uint8_t*)UART0_BASE;

    while (!(in_8(uart + UART_LSR) & UART_LSR_TEMT))
       ;

    /* set ier, fcr, mcr */
    out_8(uart + UART_IER, 0);
    out_8(uart + UART_FCR, (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN));

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    out_8(uart + UART_LCR, (UART_LCR_DLAB | UART_LCR_WLS));
    /* set divisor */
    out_8(uart + UART_DLB, div & 0xff);
    out_8(uart + UART_DMB, (div>>8) & 0xff);
    /* disable rate access (DLAB=0) */
    out_8(uart + UART_LCR, (UART_LCR_WLS));
}

static void uart_write(const char* buf, uint32_t sz)
{
    volatile uint8_t* uart = (uint8_t*)UART0_BASE;
    uint32_t pos = 0;
    while (sz-- > 0) {
        while (!(in_8(uart + UART_LSR) & UART_LSR_THRE))
            ;
        out_8(uart + UART_THR, buf[pos++]);
    }
}

static const char* hex_lut = "0123456789abcdef";

void main(void)
{
    int i = 0;
    int j = 0;
    int k = 0;
    char snum[8];

    uart_write("Test App\n", 9);

    /* Wait for reboot */
    while(1) {
        for (j=0; j<1000000; j++)
            ;
        i++;

        uart_write("\r\n0x", 4);
        for (k=0; k<8; k++) {
            snum[7 - k] = hex_lut[(i >> 4*k) & 0xf];
        }
        uart_write(snum, 8);
    }
}
