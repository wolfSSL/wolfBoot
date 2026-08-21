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
 * vdevbuffer carveout, which belongs to the RPMsg transport. The header plus
 * the ring occupy exactly the 64 KiB page below the status block; see
 * hal/imx95_m7.h, which holds the layout and asserts that they do not overlap.
 *
 * Reader contract: `wr` counts bytes ever written and never wraps within a run,
 * so a reader tracks its own position and copies (wr - pos) bytes from
 * data[pos % size]. `size` is published in the header rather than assumed.
 * Overrun is possible and expected for very chatty output; the reader detects
 * it when (wr - pos) > size and reports the gap rather than silently printing
 * corrupt text.
 *
 * Publish ordering is enforced with DMB, not by `volatile`: volatile binds the
 * compiler only, while the A55 reading this ring is a separate observer of a
 * Normal-memory region the M7 may reorder stores to.
 */

#include <stdint.h>
#include "hal/imx95_m7.h"

struct console_hdr {
    volatile uint32_t magic;
    volatile uint32_t wr;   /* total bytes written, monotonic */
    volatile uint32_t size; /* == CONSOLE_SIZE */
    volatile uint32_t rsvd;
};

#define CONSOLE_HDR  ((struct console_hdr *)CONSOLE_BASE)
#define CONSOLE_DATA ((volatile uint8_t *)(CONSOLE_BASE + CONSOLE_HDR_SIZE))

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    struct console_hdr *h = CONSOLE_HDR;

    (void)bitrate;
    (void)data;
    (void)parity;
    (void)stop;

    h->wr = 0;
    h->size = CONSOLE_SIZE;
    /* Magic last, and behind a barrier: a reader that samples mid-init sees no
     * valid console rather than a valid magic with a stale size. */
    DMB();
    h->magic = CONSOLE_MAGIC;
    imx95_dcache_clean((const void *)h, sizeof(*h));
    return 0;
}

int uart_tx(const uint8_t c)
{
    struct console_hdr *h = CONSOLE_HDR;
    uint32_t w;
    uint32_t idx;

    if (h->magic != CONSOLE_MAGIC)
        (void)uart_init(0, 0, 'N', 0);

    w = h->wr;
    idx = w % CONSOLE_SIZE;
    CONSOLE_DATA[idx] = c;
    imx95_dcache_clean((const void *)&CONSOLE_DATA[idx], 1);
    /* Publish the byte before advancing the counter, so a reader never sees a
     * count covering a byte that has not been stored yet. */
    DMB();
    h->wr = w + 1;
    imx95_dcache_clean((const void *)h, sizeof(*h));
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
