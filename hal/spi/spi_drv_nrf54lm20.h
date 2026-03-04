/* spi_drv_nrf54lm20.h
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

#ifndef SPI_DRV_NRF54LM20_H_INCLUDED
#define SPI_DRV_NRF54LM20_H_INCLUDED

#include <stdint.h>

#include "hal/nrf54lm20.h"

/* Default pin-mux matches the nRF54LM20 DK */
#ifndef SPI_CS_PORT
    #define SPI_CS_PORT 0
#endif
#ifndef SPI_CS_PIN
    #define SPI_CS_PIN 25
#endif
#ifndef SPI_SCK_PORT
    #define SPI_SCK_PORT 0
#endif
#ifndef SPI_SCK_PIN
    #define SPI_SCK_PIN 29
#endif
#ifndef SPI_MOSI_PORT
    #define SPI_MOSI_PORT 0
#endif
#ifndef SPI_MOSI_PIN
    #define SPI_MOSI_PIN 28
#endif
#ifndef SPI_MISO_PORT
    #define SPI_MISO_PORT 0
#endif
#ifndef SPI_MISO_PIN
    #define SPI_MISO_PIN 27
#endif

#define SPI_CS_FLASH       SPI_CS_PIN
#define SPI_CS_PIO_BASE    SPI_CS_PORT

#endif /* SPI_DRV_NRF54LM20_H_INCLUDED */
