/* i2c_drv_psoc_c3.h
 *
 * Pinout and options for the I2C back-end on Infineon PSOC Control C3.
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

#ifndef I2C_DRV_PSOC_C3_H_INCLUDED
#define I2C_DRV_PSOC_C3_H_INCLUDED

#include "hal/psoc_c3.h"

/* Defaults match the PSOC Control C3 evaluation kit: SCB0 on P9.0 (SCL) and
 * P9.2 (SDA), which the kit routes to the mikroBUS and Arduino headers. The
 * pins are not adjacent. Every value is a make option, so another package or
 * board is a configuration change rather than an edit.
 */
#ifndef PSOC_C3_I2C_SCB
#define PSOC_C3_I2C_SCB         0
#endif
#ifndef PSOC_C3_I2C_PORT
#define PSOC_C3_I2C_PORT        9
#endif
#ifndef PSOC_C3_I2C_SCL_PIN
#define PSOC_C3_I2C_SCL_PIN     0
#endif
#ifndef PSOC_C3_I2C_SDA_PIN
#define PSOC_C3_I2C_SDA_PIN     2
#endif
/* The HSIOM selector for the I2C function differs per SCB; it is 15 for SCB0
 * on port 9. A wrong selector leaves the pads as GPIO and the bus idle high,
 * which looks exactly like an absent device. */
#ifndef PSOC_C3_I2C_HSIOM_SEL
#define PSOC_C3_I2C_HSIOM_SEL   15
#endif
#ifndef PSOC_C3_I2C_HZ
#define PSOC_C3_I2C_HZ          100000UL
#endif
/* Peripheral clock feeding the SCB. Divider types are not uniform across
 * groups, so a destination programmed with a type its group does not provide
 * silently produces no clock. Measured on silicon, each SCB's clock sits at
 * index 2*n within group 4; see spi_drv_psoc_c3.h. */
#ifndef PSOC_C3_I2C_PCLK_GR
#define PSOC_C3_I2C_PCLK_GR     4
#endif
#ifndef PSOC_C3_I2C_PCLK_IDX
#define PSOC_C3_I2C_PCLK_IDX    (2 * PSOC_C3_I2C_SCB)
#endif
#ifndef PSOC_C3_I2C_PCLK_DIV
#define PSOC_C3_I2C_PCLK_DIV    0
#endif

#define PSOC_C3_I2C_SCB_BASE    PSOC_C3_SCB_BASE(PSOC_C3_I2C_SCB)

#endif /* I2C_DRV_PSOC_C3_H_INCLUDED */
