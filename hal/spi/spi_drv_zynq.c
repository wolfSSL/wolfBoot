/* spi_drv_zynq.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for Zynq, using QSPI.
 *
 * Pinout: see spi_drv_zynq.h
 */

#include <stdint.h>
#include "spi_drv.h"
#include "spi_drv_zynq.h"

#ifdef SPI_FLASH

void spi_cs_off(int pin)
{
    (void)pin;
}

void spi_cs_on(int pin)
{
    (void)pin;
}

uint8_t spi_read(void)
{
    return 0;
}

void spi_write(const char byte)
{

}


void spi_init(int polarity, int phase)
{
    static int initialized = 0;
    if (!initialized) {
        initialized++;

        (void)polarity;
        (void)phase;
    }
}

void spi_release(void)
{

}

#endif
