/* spi_drv_zynq.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#ifdef TARGET_zynq

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)

void spi_cs_off(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
}

void spi_cs_on(uint32_t base, int pin)
{
    (void)base;
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

#ifdef WOLFBOOT_TPM
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;
    spi_cs_on(SPI_CS_TPM_PIO_BASE, cs);
    for (i = 0; i < sz; i++) {
        spi_write((const char)tx[i]);
        rx[i] = spi_read();
    }
    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        spi_cs_off(SPI_CS_TPM_PIO_BASE, cs);
    }
    return 0;
}
#endif /* WOLFBOOT_TPM */

#endif /* SPI_FLASH | WOLFBOOT_TPM */
#endif /* TARGET_zynq */
