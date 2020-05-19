/* spi_drv_nrf52.h
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

#ifndef SPI_DRV_NRF52_H_INCLUDED
#define SPI_DRV_NRF52_H_INCLUDED
#include <stdint.h>
/** SPI settings **/


#define GPIO_BASE (0x50000000)
#define GPIO_OUT        *((volatile uint32_t *)(GPIO_BASE + 0x504))
#define GPIO_OUTSET     *((volatile uint32_t *)(GPIO_BASE + 0x508))
#define GPIO_OUTCLR     *((volatile uint32_t *)(GPIO_BASE + 0x50C))
#define GPIO_DIRSET     *((volatile uint32_t *)(GPIO_BASE + 0x518))
#define GPIO_PIN_CNF     ((volatile uint32_t *)(GPIO_BASE + 0x700)) // Array

#define GPIO_CNF_IN 0
#define GPIO_CNF_OUT 3


/* Pinout (P0.x) */
#if 1
#define SPI_CS_PIN 13
#define SPI_MOSI_PIN 4
#define SPI_MISO_PIN 5
#define SPI_SCLK_PIN 30
#endif
#if 0
#define SPI_SCLK_PIN 5
#define SPI_MISO_PIN 6
#define SPI_MOSI_PIN 7
#define SPI_CS_PIN 8
#endif


#define SPI_CS_FLASH SPI_CS_PIN



#endif
