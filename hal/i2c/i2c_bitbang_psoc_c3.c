/* i2c_bitbang_psoc_c3.c
 *
 * Open-drain pin control for the bit-banged I2C back-end on PSOC Control C3.
 * The pins are the same ones the SCB driver uses, so the two back-ends are
 * interchangeable without rewiring.
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

#include "hal/psoc_c3.h"
#include "hal/i2c/i2c_drv_psoc_c3.h"
#include "i2c_bitbang.h"

void i2c_bitbang_pins_init(void)
{
    psoc_c3_peri_init();
    /* Mux 0 is plain GPIO; open drain drives low and leaves the line to the
     * board's pull-up otherwise, which is exactly I2C signalling. */
    psoc_c3_pin_setup(PSOC_C3_I2C_PORT, PSOC_C3_I2C_SCL_PIN, 0,
            GPIO_CFG_DM_OD_LOW);
    psoc_c3_pin_setup(PSOC_C3_I2C_PORT, PSOC_C3_I2C_SDA_PIN, 0,
            GPIO_CFG_DM_OD_LOW);
    GPIO_PRT_OUT(PSOC_C3_I2C_PORT) |= (1UL << PSOC_C3_I2C_SCL_PIN) |
                                      (1UL << PSOC_C3_I2C_SDA_PIN);
}

void i2c_bitbang_scl(int high)
{
    if (high)
        GPIO_PRT_OUT(PSOC_C3_I2C_PORT) |= (1UL << PSOC_C3_I2C_SCL_PIN);
    else
        GPIO_PRT_OUT(PSOC_C3_I2C_PORT) &= ~(1UL << PSOC_C3_I2C_SCL_PIN);
}

void i2c_bitbang_sda(int high)
{
    if (high)
        GPIO_PRT_OUT(PSOC_C3_I2C_PORT) |= (1UL << PSOC_C3_I2C_SDA_PIN);
    else
        GPIO_PRT_OUT(PSOC_C3_I2C_PORT) &= ~(1UL << PSOC_C3_I2C_SDA_PIN);
}

int i2c_bitbang_sda_read(void)
{
    return (int)((GPIO_PRT_IN(PSOC_C3_I2C_PORT) >> PSOC_C3_I2C_SDA_PIN) & 1U);
}

int i2c_bitbang_scl_read(void)
{
    return (int)((GPIO_PRT_IN(PSOC_C3_I2C_PORT) >> PSOC_C3_I2C_SCL_PIN) & 1U);
}
