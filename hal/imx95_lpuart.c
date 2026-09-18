/* imx95_lpuart.c
 *
 * LPUART1 console for wolfBoot on the NXP i.MX95.
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

#include <stdint.h>
#include "printf.h"
#include "hal/imx95_a55.h"

static inline uint32_t rd32(uintptr_t a)
{
    return *(volatile uint32_t*)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
    *(volatile uint32_t*)a = v;
}

#if defined(DEBUG_UART)

/* SPL, BL31 and OP-TEE have all driven this port already, so it is clocked
 * and at 115200; rewriting BAUD would need the reference rate the System
 * Manager owns and risks garbling a console that works. */
void uart_init(void)
{
    /* The prior stages configured the port. */
}

static void uart_tx(char c)
{
    while ((rd32(IMX95_LPUART1_BASE + LPUART_STAT_OFF) & LPUART_STAT_TDRE) == 0)
        ;
    wr32(IMX95_LPUART1_BASE + LPUART_DATA_OFF, (uint32_t)(uint8_t)c);
}

#ifdef IMX95_LOG_RING
/* The ring is in the M7 carveout, which the EL2 map marks Normal
 * Non-Cacheable so readers outside this cluster's coherency see it without
 * maintenance. */
static void log_ring_putc(char c)
{
    static int ring_ready;
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)IMX95_LOG_RING_BASE;
    volatile uint8_t *data =
        (volatile uint8_t *)(uintptr_t)(IMX95_LOG_RING_BASE + IMX95_LOG_RING_HDR);
    uint32_t wr;

    if (!ring_ready) {
        /* Publish the magic last: until it is set a reader treats the region
         * as absent rather than reading a stale count from the last boot. */
        hdr[1] = 0;
        hdr[2] = (uint32_t)IMX95_LOG_RING_SIZE;
        hdr[3] = 0;
        hdr[0] = (uint32_t)IMX95_LOG_RING_MAGIC;
        ring_ready = 1;
    }
    wr = hdr[1];
    data[wr % (uint32_t)IMX95_LOG_RING_SIZE] = (uint8_t)c;
    hdr[1] = wr + 1;   /* monotonic: the reader derives wrap from it */
}
#endif /* IMX95_LOG_RING */

/* printf does not expand newlines. */
void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n')
            uart_tx('\r');
        uart_tx(buf[i]);
#ifdef IMX95_LOG_RING
        log_ring_putc(buf[i]);
#endif
    }
    while ((rd32(IMX95_LPUART1_BASE + LPUART_STAT_OFF) & LPUART_STAT_TC) == 0)
        ;
}

#endif /* DEBUG_UART */
