/* spi_drv_nrf54l.h
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

#ifndef SPI_DRV_NRF54L_H_INCLUDED
#define SPI_DRV_NRF54L_H_INCLUDED

#include <stdint.h>

#include "hal/nrf54l.h"

/* Default pin-mux matches the nRF54L15-DK */
#ifndef SPI_CS_PORT
    #define SPI_CS_PORT 2
#endif
#ifndef SPI_CS_PIN
    #define SPI_CS_PIN 5
#endif
#ifndef SPI_SCK_PORT
    #define SPI_SCK_PORT 2
#endif
#ifndef SPI_SCK_PIN
    #define SPI_SCK_PIN 1
#endif
#ifndef SPI_MOSI_PORT
    #define SPI_MOSI_PORT 2
#endif
#ifndef SPI_MOSI_PIN
    #define SPI_MOSI_PIN 2
#endif
#ifndef SPI_MISO_PORT
    #define SPI_MISO_PORT 2
#endif
#ifndef SPI_MISO_PIN
    #define SPI_MISO_PIN 4
#endif

#ifndef SPI_CS_TPM
    #define SPI_CS_TPM         SPI_CS_PIN
#endif

#define SPI_CS_FLASH           SPI_CS_PIN
#define SPI_CS_PIO_BASE        SPI_CS_PORT
#define SPI_CS_TPM_PIO_BASE    SPI_CS_PORT

#endif /* SPI_DRV_NRF54L_H_INCLUDED */
