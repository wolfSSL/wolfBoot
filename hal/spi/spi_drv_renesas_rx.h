/* spi_drv_renesas_rx.h
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

#ifndef SPI_DRV_RENESAS_RX_H_INCLUDED
#define SPI_DRV_RENESAS_RX_H_INCLUDED

#include <stdint.h>

#ifndef FLASH_RSPI_PORT
#define FLASH_RSPI_PORT 1 /* RSPI1 */
#endif

/* use RSPI HW chip select */
#define FLASH_SPI_USE_HW_CS

#endif /* !SPI_DRV_RENESAS_RX_H_INCLUDED */
