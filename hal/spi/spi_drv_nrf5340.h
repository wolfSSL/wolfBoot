/* spi_drv_nrf5340.h
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

#ifndef SPI_DRV_NRF53_H_INCLUDED
#define SPI_DRV_NRF53_H_INCLUDED

#include <stdint.h>

#include "hal/nrf5340.h"

/* Default SPI interface (0-2) */
#ifndef SPI_PORT
#define SPI_PORT 0
#endif

/* SPI Pin Configuration (P1.x) */
/* Default for nRF5340-DK is Arduino shield P4 P1.12-15 */
/* CLK=P1.15, CS=P1.12, MOSI=P1.13, MISO=P1.14 */
#ifndef SPI_CS_PIO_BASE
    #define SPI_CS_PIO_BASE 1
#endif
#ifndef SPI_CS_TPM
    #define SPI_CS_TPM      11
#endif
#ifndef SPI_CS_FLASH
    #define SPI_CS_FLASH    12
#endif
#ifndef SPI_MOSI_PIN
    #define SPI_MOSI_PIN    13
#endif
#ifndef SPI_MISO_PIN
    #define SPI_MISO_PIN    14
#endif
#ifndef SPI_CLK_PIN
    #define SPI_CLK_PIN     15
#endif


/* QSPI Pin Configuration */
/* Default is nRF5340-DK QSPI connected to MX25R6435F */
/* CLK=P0.17, CS=P0.18, IO0=P0.13, IO1=P0.14, IO2=P0.15, IO3=P0.16 */
/* QSPI CLK PB2 (alt OCTOSPIM_P1_CLK)*/
#ifndef QSPI_CLK_PIN
    #define QSPI_CLK_PORT   0
    #define QSPI_CLK_PIN    17
#endif
#ifndef QSPI_CS_PIN
    #define QSPI_CS_PORT    0
    #define QSPI_CS_PIN     18
#endif
#ifndef QSPI_IO0_PIN
    #define QSPI_IO0_PORT   0
    #define QSPI_IO0_PIN    13
#endif
#ifndef QSPI_IO1_PIN
    #define QSPI_IO1_PORT   0
    #define QSPI_IO1_PIN    14
#endif
#ifndef QSPI_IO2_PIN
    #define QSPI_IO2_PORT   0
    #define QSPI_IO2_PIN    15
#endif
#ifndef QSPI_IO3_PIN
    #define QSPI_IO3_PORT   0
    #define QSPI_IO3_PIN    16
#endif

#ifndef QSPI_CLOCK_MHZ /* default 48MHz (up to 96MHz) */
    #define QSPI_CLOCK_MHZ  48000000UL
#endif

/* Optional power pin for QSPI enable */
//#define QSPI_PWR_CTRL_PORT 1
//#define QSPI_PWR_CTRL_PIN  0

/* MX25R6435F */
#define QSPI_NO_SR2

#define QSPI_CLK 96000000UL
#if QSPI_CLOCK_MHZ <= 24000000
    #define QSPI_CLK_DIV    CLOCK_HFCLK192MCTRL_DIV4
#elif QSPI_CLOCK_MHZ <= 48000000
    /* Note: Power consumption higher for DIV2/DIV1 */
    #define QSPI_CLK_DIV    CLOCK_HFCLK192MCTRL_DIV2
#else
    /* Note: Power consumption higher for DIV2/DIV1 */
    #define QSPI_CLK_DIV    CLOCK_HFCLK192MCTRL_DIV1
#endif

/* Calculate the IFCONFIG1_SCKFREG divisor */
#define QSPI_CLK_FREQ_DIV ((QSPI_CLK / (QSPI_CLK_DIV+1) / QSPI_CLOCK_MHZ) - 1)

#endif /* !SPI_DRV_NRF53_H_INCLUDED */
