/* app_stm32h7s.c
 *
 * Test bare-metal application for STM32H7S / STM32H7R.
 *
 * The application executes in place from the external Octo-SPI NOR, which
 * wolfBoot has already put into memory-mapped mode. It must therefore not
 * call hal_init(): re-running the XSPI bring-up would drop the controller
 * out of memory-mapped mode and the next instruction fetch would fault.
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
#include "hal.h"
#include "hal/stm32h7s.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int len);

static void uart_print(const char *s)
{
    unsigned int n = 0;

    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void uart_print_u32(uint32_t v)
{
    char buf[11];
    int n = 0;
    int i;

    if (v == 0) {
        uart_print("0");
        return;
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = n - 1; i >= 0; i--)
        uart_write(&buf[i], 1);
}

/* Report the clock tree the device is actually running on, recomputed from
 * the live RCC registers rather than from the build-time constants. A PLL
 * that failed to lock leaves the system on HSI and everything still runs,
 * roughly nine times slower, which is otherwise invisible. */
static void clock_report(void)
{
    uint32_t sws, sysclk, cpu_ck, hclk, pclk, m, n, p;
    uint32_t cpre, bmpre, ppre1;

    sws = (RCC_CFGR & RCC_CFGR_SWS_MASK) >> 3;

    uart_print("Clock source: ");
    if (sws == 0) {
        uart_print("HSI\r\n");
        sysclk = HSI_HZ;
    }
    else if (sws == 3) {
        uart_print("PLL1\r\n");
        m = (RCC_PLLCKSELR >> 4) & 0x3F;
        n = ((RCC_PLL1DIVR1 >> 0) & 0x1FF) + 1;
        p = ((RCC_PLL1DIVR1 >> 9) & 0x7F) + 1;
        if (m == 0)
            m = 1;
        sysclk = ((HSI_HZ / m) * n) / p;
    }
    else {
        uart_print("other\r\n");
        sysclk = 0;
    }

    /* CPRE/BMPRE encode /1 as 0 and /2,/4,/8.. as 0x8,0x9,0xA..; PPRE
     * does the same from 0x4. The bus clock is sysclk / CPRE / BMPRE, so
     * the CPU prescaler has to be folded in or HCLK over-reports by that
     * factor whenever it is not /1. */
    cpre = RCC_CDCFGR & 0xF;
    cpu_ck = (cpre >= 8) ? (sysclk >> ((cpre - 8) + 1)) : sysclk;
    bmpre = RCC_BMCFGR & 0xF;
    hclk = (bmpre >= 8) ? (cpu_ck >> ((bmpre - 8) + 1)) : cpu_ck;
    ppre1 = RCC_APBCFGR & 0x7;
    pclk = (ppre1 >= 4) ? (hclk >> ((ppre1 - 4) + 1)) : hclk;

    uart_print("SYSCLK: ");
    uart_print_u32(sysclk / 1000000);
    uart_print(" MHz, HCLK: ");
    uart_print_u32(hclk / 1000000);
    uart_print(" MHz, PCLK1: ");
    uart_print_u32(pclk / 1000000);
    uart_print(" MHz\r\n");

    uart_print("XSPI2 kernel: ");
    if ((RCC_CCIPR1 & RCC_CCIPR1_XSPI2SEL_MASK) ==
            RCC_CCIPR1_XSPI2SEL_PLL2S)
        uart_print("PLL2S\r\n");
    else
        uart_print("HCLK\r\n");

    if (sysclk != (uint32_t)SYSCLK_HZ) {
        uart_print("WARNING: expected SYSCLK ");
        uart_print_u32((uint32_t)SYSCLK_HZ / 1000000);
        uart_print(" MHz, clock setup did not take\r\n");
    }
}


void main(void)
{
    uint32_t version;
    uint32_t update;

    /* Deliberately no hal_init(): see the file header. */
    uart_init();
    uart_print("\r\n=== wolfBoot test app: STM32H7S ===\r\n");

    clock_report();

    version = wolfBoot_current_firmware_version();
    uart_print("Firmware version: ");
    uart_print_u32(version);
    uart_print("\r\n");

    update = wolfBoot_get_image_version(PART_UPDATE);
    uart_print("Update partition version: ");
    uart_print_u32(update);
    uart_print("\r\n");

    if (update > version) {
        /* Ask for the update but do not reset here. Resetting on every boot
         * that sees a pending update turns an empty or rejected update slot
         * into an endless reset loop, and the application never stays up
         * long enough to be useful. */
        uart_print("Update requested, reset to apply\r\n");
        wolfBoot_update_trigger();
    }
    else {
        uart_print("Confirming this image\r\n");
        wolfBoot_success();
        uart_print("Update confirmed\r\n");
    }

    while (1)
        ;
}
