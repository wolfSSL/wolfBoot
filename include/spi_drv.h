/* spi_drv.h
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 *   * Compile with SPI_FLASH=1
 *   * Define your platform specific SPI driver in spi_drv_$PLATFORM.c,
 *     implementing the spi_ calls below.
 *
 *
 * Copyright (C) 2022 wolfSSL Inc.
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
#include "image.h"

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM) || defined(QSPI_FLASH) || \
    defined(OCTOSPI_FLASH)

#if defined(PLATFORM_stm32f4) || defined(PLATFORM_stm32f7) || \
    defined(PLATFORM_stm32wb) || defined(PLATFORM_stm32l0) || \
    defined(PLATFORM_stm32u5) || defined(PLATFORM_stm32h7)
#include "hal/spi/spi_drv_stm32.h"
#endif

#if defined(PLATFORM_zynq)
#include "hal/spi/spi_drv_zynq.h"
#endif

#if defined(PLATFORM_nrf52)
#include "hal/spi/spi_drv_nrf52.h"
#endif

#if defined(PLATFORM_nxp_p1021)
#include "hal/spi/spi_drv_nxp.h"
#endif

void spi_init(int polarity, int phase);
void spi_release(void);

#ifdef SPI_FLASH
void spi_cs_on(uint32_t base, int pin);
void spi_cs_off(uint32_t base, int pin);
void spi_write(const char byte);
uint8_t spi_read(void);
#endif

#ifdef WOLFBOOT_TPM
/* SPI transfer flags */
#define SPI_XFER_FLAG_NONE     0x0
#define SPI_XFER_FLAG_CONTINUE 0x1 /* keep CS asserted */

/* Perform a SPI transaction.
 * Set flags == SPI_XFER_FLAG_CONTINUE to keep CS asserted after transfer. */
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags);
#endif

#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)

#define QSPI_MODE_WRITE 0
#define QSPI_MODE_READ  1

/* these are used in macro logic, so must be defines */
#define QSPI_DATA_MODE_NONE 0
#define QSPI_DATA_MODE_SPI  1
#define QSPI_DATA_MODE_DSPI 2
#define QSPI_DATA_MODE_QSPI 3

int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode
);
#endif /* QSPI_FLASH || OCTOSPI_FLASH */

#endif /* SPI_FLASH || WOLFBOOT_TPM || QSPI_FLASH || OCTOSPI_FLASH */

#endif /* !SPI_DRV_H_INCLUDED */
