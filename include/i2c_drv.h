/* i2c_drv.h
 *
 * Generic I2C master interface for wolfBoot back-end drivers.
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

#ifndef I2C_DRV_H_INCLUDED
#define I2C_DRV_H_INCLUDED

#include <stdint.h>

/* Return codes. A device that is awake but busy answers a transfer with
 * I2C_ERR_NACK, which callers use to decide whether to retry. */
#define I2C_OK          (0)
#define I2C_ERR_NACK    (-1)
#define I2C_ERR_TIMEOUT (-2)
#define I2C_ERR_BUS     (-3)
#define I2C_ERR_ARG     (-4)

#if defined(WOLFBOOT_TPM_I2C)

#if defined(TARGET_psoc_c3)
#include "hal/i2c/i2c_drv_psoc_c3.h"
#endif

/* Bring the controller up. Safe to call more than once. */
void i2c_init(void);

/* Release the bus and leave the pins in a safe state. */
void i2c_release(void);

/* Address a 7-bit slave and write len bytes. A zero stop leaves the bus held
 * so the next call issues a repeated start; a non-zero stop releases it.
 * Which of the two a device needs between selecting a register and reading it
 * is device specific. */
int i2c_write(uint8_t addr, const uint8_t *buf, uint32_t len, int stop);

/* Address a 7-bit slave and read len bytes. The final byte is NACKed, as the
 * protocol requires, before the optional stop. */
int i2c_read(uint8_t addr, uint8_t *buf, uint32_t len, int stop);

#endif /* WOLFBOOT_TPM_I2C */
#endif /* I2C_DRV_H_INCLUDED */
