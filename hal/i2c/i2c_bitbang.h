/* i2c_bitbang.h
 *
 * Pin control a target must provide for the bit-banged I2C back-end.
 *
 * Both lines are open drain: driving a line means pulling it low, releasing it
 * means letting the board's pull-up raise it. A target that cannot configure
 * open drain can emulate it by switching the pin between output-low and input,
 * but it must never drive a line high.
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
#ifndef I2C_BITBANG_H_INCLUDED
#define I2C_BITBANG_H_INCLUDED

/* Claim both pins as open-drain GPIOs and leave them released. Safe to call
 * more than once. */
void i2c_bitbang_pins_init(void);

/* Release the line when high is non-zero, pull it low otherwise. */
void i2c_bitbang_scl(int high);
void i2c_bitbang_sda(int high);

/* Sample a line. Reading SCL is what detects a slave stretching the clock, so
 * a target must keep the input buffer enabled on both pins. */
int i2c_bitbang_sda_read(void);
int i2c_bitbang_scl_read(void);

#endif /* I2C_BITBANG_H_INCLUDED */
