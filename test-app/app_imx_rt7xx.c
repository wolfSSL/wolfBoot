/* app_imx_rt7xx.c
 *
 * Test application for the NXP i.MX RT700 (MIMXRT798S-EVK)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include "target.h"
#include "wolfboot/wolfboot.h"

/* LPUART0 (LP_FLEXCOMM0) Non-secure alias. wolfBoot brings the console up with
 * DEBUG_UART before the jump, so the app only polls TDRE and writes. */
#define LPUART0_NS_BASE 0x40110000u
#define LPUART0_STAT    (*(volatile uint32_t *)(LPUART0_NS_BASE + 0x14u))
#define LPUART0_DATA    (*(volatile uint32_t *)(LPUART0_NS_BASE + 0x1Cu))
#define LPUART0_TDRE    0x00800000u

static void uart_write_str(const char *s)
{
    while (*s != '\0') {
        while ((LPUART0_STAT & LPUART0_TDRE) == 0u) {
            /* wait for the transmit data register to drain */
        }
        LPUART0_DATA = (uint32_t)(uint8_t)*s;
        s++;
    }
}

void main(void)
{
    uint32_t version;
    uint8_t state = IMG_STATE_NEW;
    char msg[] = "wolfBoot test app v0\r\n";

    version = wolfBoot_current_firmware_version();
    msg[19] = (char)('0' + (int)(version % 10u));
    uart_write_str(msg);

    if ((wolfBoot_get_partition_state(PART_BOOT, &state) == 0) &&
            (state == IMG_STATE_TESTING)) {
        wolfBoot_success();
    }

    if ((version == 1u) && (wolfBoot_update_firmware_version() != 0u)) {
        wolfBoot_update_trigger();
    }

    while (1) {
        __asm__ volatile("wfi");
    }
}
