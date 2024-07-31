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
#include <stddef.h>
#include "spi_drv.h"

#if defined(TARGET_nxp_p1021) || defined(TARGET_nxp_t1024)
#ifdef WOLFBOOT_TPM

/* functions from nxp_p1021.c and nxp_t1024.c hal */
extern void hal_espi_init(uint32_t cs, uint32_t clock_hz, uint32_t mode);
extern int  hal_espi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags);
extern void hal_espi_deinit(void);

#if defined(TARGET_nxp_ls1028a)
/* Functions from hal/nxp_ls1028a.c */
extern void nxp_ls1028a_spi_init(unsigned int sel);
extern int nxp_ls1028a_spi_xfer(unsigned int sel, unsigned int cs,
        const unsigned char *out, unsigned char *in,
        unsigned int size, int cont);
extern void nxp_ls1028a_spi_deinit(unsigned int sel);
#endif

#include <wolftpm/tpm2_types.h>

static int initialized = 0;

void spi_init(int polarity, int phase)
{
    if (!initialized) {
        initialized++;

#if defined(TARGET_nxp_p1021) || defined(TARGET_nxp_t1024)
        hal_espi_init(SPI_CS_TPM, TPM2_SPI_MAX_HZ, (polarity | (phase << 1)));
#elif defined(TARGET_nxp_ls1028a)
        nxp_ls1028a_spi_init(SPI_SEL_TPM);
#endif
    }
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
    if (initialized) {
        initialized--;

#if defined(TARGET_nxp_p1021) || defined(TARGET_nxp_t1024)
        hal_espi_deinit();
#elif defined(TARGET_nxp_ls1028a)
        nxp_ls1028a_spi_deinit(SPI_SEL_TPM);
#endif

    }
}

int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
#if defined(TARGET_nxp_p1021) || defined(TARGET_nxp_t1024)
    return hal_espi_xfer(cs, tx, rx, sz, flags);
#elif defined(TARGET_nxp_ls1028a)
    return nxp_ls1028a_spi_xfer(SPI_SEL_TPM, cs, tx, rx, sz, flags);
#endif

}
#endif /* WOLFBOOT_TPM */
#endif /* TARGET_ */
