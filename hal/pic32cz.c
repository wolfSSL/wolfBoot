/* pic32cz.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include "image.h"
#include <stdint.h>
#include <string.h>

#include <hal/pic32c.h>

#define SUPC_BASE (0x44020000U)
#define SUPC_VREGCTRL (*(volatile uint32_t *)(SUPC_BASE + 0x1CU))
#define SUPC_STATUS (*(volatile uint32_t *)(SUPC_BASE + 0x0CU))

#define SUPC_VREGCTRL_AVREGEN_PLLREG_EN (4)
#define SUPC_VREGCTRL_AVREGEN_SHIFT (16)
#define SUPC_STATUS_ADDVREGRDY_PLL (4)
#define SUPC_STATUS_ADDVREGRDY_SHIFT (8)

static void pic32_supc_vreg_pll_enable(void)
{
    SUPC_VREGCTRL |= SUPC_VREGCTRL_AVREGEN_PLLREG_EN
        << SUPC_VREGCTRL_AVREGEN_SHIFT;

    /* wait for the vreg to be ready */
    while (!(SUPC_STATUS &
             (SUPC_STATUS_ADDVREGRDY_PLL << SUPC_STATUS_ADDVREGRDY_SHIFT))) {}
}

#ifdef DUALBANK_SWAP
void hal_flash_dualbank_swap(void)
{
    pic32_flash_dualbank_swap();
}
#endif /* DUALBANK_SWAP */

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return pic32_flash_write(address, data, len);
}

void hal_flash_unlock(void)
{
    pic32_fcw_grab();
}

void hal_flash_lock(void)
{
    pic32_fcw_release();
}

int hal_flash_erase(uint32_t addr, int len)
{
    return pic32_flash_erase(addr, len);
}

static void pic32_delay_cnt(uint32_t ticks)
{
    uint32_t i = 0;
    for (i = 0; i < ticks; i++) {
        __asm__("nop");
    }
}

void hal_init(void)
{
#if defined(TEST_CLOCK)
    pic32_clock_test(48000000);
#endif
    pic32_supc_vreg_pll_enable();
    pic32_clock_pll0_init(12, 225, 1, 3);
    pic32_clock_gclk_gen0(2, 1);
    pic32_delay_cnt(700);
#if defined(TEST_FLASH)
    pic32_flash_test();
    while (1) {}
#endif
#if defined(TEST_CLOCK)
    pic32_clock_test(300000000);
    pic32_clock_reset();
    pic32_clock_test(48000000);
    while (1) {};
#endif
}

void hal_prepare_boot(void)
{
#ifdef WOLFBOOT_RESTORE_CLOCK
    pic32_clock_reset();
#endif
}
