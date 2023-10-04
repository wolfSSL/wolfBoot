/* spi_drv_nxp.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Pinout: see spi_drv_nxp.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include <stdint.h>
#include <stddef.h>
#include "spi_drv.h"
#include "spi_drv_nxp.h"

#ifdef WOLFBOOT_TPM

/* functions from nxp_p1021.c and nxp_t1024.c hal */
extern void hal_espi_init(uint32_t cs, uint32_t clock_hz, uint32_t mode);
extern int  hal_espi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags);
extern void hal_espi_deinit(void);

#include <wolftpm/tpm2_types.h>

void spi_init(int polarity, int phase)
{
    static int initialized = 0;
    if (!initialized) {
        initialized++;

        hal_espi_init(SPI_CS_TPM, TPM2_SPI_MAX_HZ, (polarity | (phase << 1)));
    }
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
    hal_espi_deinit();
}

int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    return hal_espi_xfer(cs, tx, rx, sz, flags);
}
#endif /* WOLFBOOT_TPM */
