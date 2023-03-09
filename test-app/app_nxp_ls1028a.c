/* app_nxp_ls1028.c
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

/* P1021 */
#define CCSRBAR (0x1000000)
#define SYS_CLK (400000000)

/* P1021 PC16552D Dual UART */
#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */

#define UART_BASE(n) (0x21C0500 + (n * 100))

#define UART_RBR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* receiver buffer register */
#define UART_THR(n)  *((volatile uint8_t*)(UART_BASE(n) + 0)) /* transmitter holding register */
#define UART_IER(n)  *((volatile uint8_t*)(UART_BASE(n) + 1)) /* interrupt enable register */
#define UART_FCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* FIFO control register */
#define UART_IIR(n)  *((volatile uint8_t*)(UART_BASE(n) + 2)) /* interrupt ID register */
#define UART_LCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 3)) /* line control register */
#define UART_LSR(n)  *((volatile uint8_t*)(UART_BASE(n) + 5)) /* line status register */
#define UART_SCR(n)  *((volatile uint8_t*)(UART_BASE(n) + 7)) /* scratch register */

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
    UART_DMB(UART_SEL) = ((div >> 8) & 0xff);
    /* disable rate access (DLAB=0) */
    UART_LCR(UART_SEL) = (UART_LCR_WLS);
}

static void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while (!(UART_LSR(UART_SEL) & UART_LSR_THRE))
            ;
        UART_THR(UART_SEL) = buf[pos++];
    }
}

static const char* hex_lut = "0123456789abcdef";

__attribute__((section(".boot")))
void main(void)
{
    int i = 0;
    int j = 0;
    int k = 0;
    char snum[8];

    uart_write("Test App\n", 9);

    /* Wait for reboot */
    while(1) {
        for (j=0; j<1000000; j++);
        i++;

        uart_write("\r\n0x", 4);
        for (k=0; k<8; k++) {
            snum[7 - k] = hex_lut[(i >> 4*k) & 0xf];
        }
        uart_write(snum, 8);
    }
}
