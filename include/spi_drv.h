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

#ifndef SPI_DRV_H_INCLUDED
#define SPI_DRV_H_INCLUDED

#include <stdint.h>
#include "image.h"

/* SPI transfer flags */
#define SPI_XFER_FLAG_NONE     0x0
#define SPI_XFER_FLAG_CONTINUE 0x1 /* keep CS asserted */


#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM) || defined(QSPI_FLASH) || \
    defined(OCTOSPI_FLASH)

#if defined(TARGET_stm32f4) || defined(TARGET_stm32f7) || \
    defined(TARGET_stm32wb) || defined(TARGET_stm32l0) || \
    defined(TARGET_stm32u5) || defined(TARGET_stm32h7)
    #define WOLFBOOT_STM32_SPIDRV
#include "hal/spi/spi_drv_stm32.h"
#endif

#if defined(TARGET_zynq)
#include "hal/spi/spi_drv_zynq.h"
#endif

#if defined(TARGET_nrf52)
#include "hal/spi/spi_drv_nrf52.h"
#endif

#if defined(TARGET_nrf5340)
#include "hal/spi/spi_drv_nrf5340.h"
#endif

#if defined(TARGET_nxp_p1021) || defined(TARGET_nxp_t1024) || \
    defined(TARGET_nxp_ls1028a)
#include "hal/spi/spi_drv_nxp.h"
#endif

#if defined(WOLFBOOT_ARCH_RENESAS_RX)
#include "hal/spi/spi_drv_renesas_rx.h"
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

/* QSPI Configuration */
#ifndef QSPI_ADDR_MODE /* address uses single SPI mode */
    #define QSPI_ADDR_MODE  QSPI_DATA_MODE_SPI
#endif
#ifndef QSPI_ADDR_SZ /* default to 24-bit address */
    #define QSPI_ADDR_SZ    3
#endif
#ifndef QSPI_DATA_MODE /* data defaults to Quad mode */
    #define QSPI_DATA_MODE  QSPI_DATA_MODE_QSPI
#endif

int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode
);

#endif /* QSPI_FLASH || OCTOSPI_FLASH */

#ifndef SPI_CS_FLASH
#define SPI_CS_FLASH    0
#endif
#ifndef SPI_CS_PIO_BASE
#define SPI_CS_PIO_BASE 0UL
#endif

#endif /* SPI_FLASH || WOLFBOOT_TPM || QSPI_FLASH || OCTOSPI_FLASH */

#endif /* !SPI_DRV_H_INCLUDED */
