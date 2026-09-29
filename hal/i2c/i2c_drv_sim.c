/* i2c_drv_sim.c
 *
 * I2C back-end for the simulator target.
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
#include "i2c_drv.h"

#ifdef WOLFBOOT_TPM_I2C

/* There is no bus under the simulator: the simulated TPM is reached over a
 * socket instead. These exist so the I2C transport compiles and links on the
 * host, which is what the build test covers. Every transfer reports that
 * nothing acknowledged, so a caller that reaches them fails rather than
 * believing an empty buffer. */

void i2c_init(void)
{
}

void i2c_release(void)
{
}

int i2c_write(uint8_t addr, const uint8_t *buf, uint32_t len, int stop)
{
    (void)addr;
    (void)buf;
    (void)len;
    (void)stop;
    return I2C_ERR_NACK;
}

int i2c_read(uint8_t addr, uint8_t *buf, uint32_t len, int stop)
{
    (void)addr;
    (void)buf;
    (void)len;
    (void)stop;
    return I2C_ERR_NACK;
}

#endif /* WOLFBOOT_TPM_I2C */
