/* uart_drv_imx95_m7.c
 *
 * Console for the Cortex-M7 on the NXP i.MX95.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* This is a shared-memory console, not a peripheral driver.
 *
 * The M7's LPUART is not routed to an accessible header on the Toradex SMARC
 * Development Board: markers written to /dev/ttyLP1..3 from Linux appear on no
 * host serial channel, and Linux owns all four LPUART instances anyway, so an
 * M7 writing to one would contend with it. Rather than depend on unproven pin
 * routing, the console is a ring buffer in the M7's own reserved DDR window,
 * which the A55 cluster reads with the memtool helper.
 *
 * The buffer sits at the top of the M7's 16 MiB carveout (memory@80000000),
 * clear of the wolfBoot partitions lower down (BOOT 0x80100000, UPDATE
 * 0x80500000, SWAP 0x80900000). It deliberately does NOT use the RPMsg
 * vdevbuffer carveout, which belongs to the RPMsg transport.
 *
 * Reader contract: `wr` counts bytes ever written and never wraps within a run,
 * so a reader tracks its own position and copies (wr - pos) bytes from
 * data[pos % size]. Overrun is possible and expected for very chatty output;
 * the reader detects it when (wr - pos) > size and reports the gap rather than
 * silently printing corrupt text.
 */

#include <stdint.h>

#define CONSOLE_BASE  0x80F00000UL
#define CONSOLE_MAGIC 0x4E4F4357UL /* "WCON" little-endian */
#define CONSOLE_SIZE  0x10000UL    /* 64 KiB of text */

struct console_hdr {
    volatile uint32_t magic;
    volatile uint32_t wr;   /* total bytes written, monotonic */
    volatile uint32_t size; /* == CONSOLE_SIZE */
    volatile uint32_t rsvd;
};

#define CONSOLE_HDR  ((struct console_hdr *)CONSOLE_BASE)
#define CONSOLE_DATA ((volatile uint8_t *)(CONSOLE_BASE + sizeof(struct console_hdr)))

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    struct console_hdr *h = CONSOLE_HDR;

    (void)bitrate;
    (void)data;
    (void)parity;
    (void)stop;

    h->wr = 0;
    h->size = CONSOLE_SIZE;
    /* Magic last: a reader that samples mid-init sees no valid console rather
     * than a valid magic with a stale size. */
    h->magic = CONSOLE_MAGIC;
    return 0;
}

int uart_tx(const uint8_t c)
{
    struct console_hdr *h = CONSOLE_HDR;
    uint32_t w;

    if (h->magic != CONSOLE_MAGIC)
        (void)uart_init(0, 0, 'N', 0);

    w = h->wr;
    CONSOLE_DATA[w % CONSOLE_SIZE] = c;
    /* Publish the byte before advancing the counter, so a reader never sees a
     * count covering a byte that has not been stored yet. */
    h->wr = w + 1;
    return 0;
}

int uart_rx(uint8_t *c)
{
    (void)c;
    return -1; /* console is write-only */
}

void uart_write(const char *buf, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++)
        (void)uart_tx((const uint8_t)buf[i]);
}
