/* spi_drv.h
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 *   * Compile with SPI_FLASH=1
 *   * Define your platform specific SPI driver in spi_drv_$PLATFORM.c,
 *     implementing the spi_ calls below.
 *
 *
 * Copyright (C) 2019 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#ifndef SPI_DRV_H_INCLUDED
#define SPI_DRV_H_INCLUDED

#include <stdint.h>

void spi_init(int polarity, int phase);
void spi_write(const char byte);
uint8_t spi_read(void);
void spi_cs_on(void);
void spi_cs_off(void);

#endif /* !SPI_DRV_H_INCLUDED */
