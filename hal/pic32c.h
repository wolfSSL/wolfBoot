/* pic32c.h
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

#ifndef PIC32C_H
#define PIC32C_H

#include <stdint.h>

#if defined(TEST_FLASH)
void pic32_flash_test(void);
#endif

#if defined(DUALBANK_SWAP)
void pic32_flash_dualbank_swap(void);
#endif

void pic32_fcw_grab(void);
void pic32_fcw_release(void);
int pic32_flash_erase(uint32_t addr, int len);
int pic32_flash_write(uint32_t address, const uint8_t *data, int len);
void pic32_clock_pll0_init(int refdiv, int fbdiv, int bw, int postdiv);
void pic32_clock_fracdiv0_set(int intdiv, int remdiv);
void pic32_clock_gclk_gen0(int mclk_div1, int cpudiv);
void pic32_clock_reset(void);
#if defined(TEST_CLOCK)
void pic32_clock_test(unsigned long cpu_freq);
#endif /* TEST_CLOCK */
#endif /* PIC32C_H */
