/* pic32ck.c
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

#include <hal/pic32c.h>
#include <stdint.h>

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return pic32_flash_write(address, data, len);
}

void hal_flash_lock(void)
{
    pic32_fcw_grab();
}

void hal_flash_unlock(void)
{
    pic32_fcw_release();
}

int hal_flash_erase(uint32_t addr, int len)
{
    return pic32_flash_erase(addr, len);
}

#ifdef DUALBANK_SWAP
void hal_flash_dualbank_swap(void)
{
    pic32_flash_dualbank_swap();
}
#endif /* DUALBANK_SWAP */

void hal_init(void)
{
#if defined(TEST_CLOCK)
    /* toggle led at 1hz for 10sec */
    pic32_clock_test(48000000U);
#endif
    pic32_clock_fracdiv0_set(0, 0);
    pic32_clock_pll0_init(12, 240, 1, 8);
    pic32_clock_gclk_gen0(1, 1);
#if defined(TEST_FLASH)
    pic32_flash_test();
    while (1) {}
#endif
#if defined(TEST_CLOCK)
    /* toggle led at 1hz */
    pic32_clock_test(120000000U);
    pic32_clock_reset();
    pic32_clock_test(48000000U);
    while (1) {};
#endif
}

void hal_prepare_boot(void)
{
#ifndef TZEN
    pic32_clock_reset();
#endif
}
