/* nrf52.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef _HAL_NRF52_H_
#define _HAL_NRF52_H_

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")

/* Instantiation */
#define CLOCK_CONTROL_BASE (0x40000000)
#define NVMC_BASE          (0x4001E000)


/* Flash write/erase control */
#define NVMC_CONFIG    *((volatile uint32_t *)(NVMC_BASE + 0x504))
#define NVMC_ERASEPAGE *((volatile uint32_t *)(NVMC_BASE + 0x508))
#define NVMC_READY     *((volatile uint32_t *)(NVMC_BASE + 0x400))
#define NVMC_CONFIG_REN 0
#define NVMC_CONFIG_WEN 1
#define NVMC_CONFIG_EEN 2

#define FLASH_PAGE_SIZE (4096)

/* Clock control */
#define TASKS_HFCLKSTART   *((volatile uint32_t *)(CLOCK_CONTROL_BASE + 0x000))
#define TASKS_HFCLKSTOP    *((volatile uint32_t *)(CLOCK_CONTROL_BASE + 0x004))
#define TASKS_HFCLKSTARTED *((volatile uint32_t *)(CLOCK_CONTROL_BASE + 0x100))

/* GPIO */
#define GPIO_BASE (0x50000000)
#define GPIO_OUT        *((volatile uint32_t *)(GPIO_BASE + 0x504))
#define GPIO_OUTSET     *((volatile uint32_t *)(GPIO_BASE + 0x508))
#define GPIO_OUTCLR     *((volatile uint32_t *)(GPIO_BASE + 0x50C))
#define GPIO_DIRSET     *((volatile uint32_t *)(GPIO_BASE + 0x518))
#define GPIO_PIN_CNF     ((volatile uint32_t *)(GPIO_BASE + 0x700)) /* Array */

#define GPIO_CNF_IN  0
#define GPIO_CNF_OUT 3

/* UART */
#define UART0_BASE (0x40002000)
#define UART0_TASK_STARTTX *((volatile uint32_t *)(UART0_BASE + 0x008))
#define UART0_TASK_STOPTX  *((volatile uint32_t *)(UART0_BASE + 0x00C))
#define UART0_EVENT_ENDTX  *((volatile uint32_t *)(UART0_BASE + 0x120))
#define UART0_ENABLE       *((volatile uint32_t *)(UART0_BASE + 0x500))
#define UART0_TXD_PTR      *((volatile uint32_t *)(UART0_BASE + 0x544))
#define UART0_TXD_MAXCOUNT *((volatile uint32_t *)(UART0_BASE + 0x548))
#define UART0_BAUDRATE     *((volatile uint32_t *)(UART0_BASE + 0x524))

#define BAUD_115200 0x01D7E000

/* SPI */
#define SPI0 (0x40003000)
#define SPI1 (0x40004000)
#define SPI2 (0x40023000)

#define SPI SPI0
#define SPI_TASKS_START   *((volatile uint32_t *)(SPI + 0x10))
#define SPI_TASKS_STOP    *((volatile uint32_t *)(SPI + 0x14))
#define SPI_EVENTS_ENDRX  *((volatile uint32_t *)(SPI + 0x110))
#define SPI_EVENTS_END    *((volatile uint32_t *)(SPI + 0x118))
#define SPI_EVENTS_ENDTX  *((volatile uint32_t *)(SPI + 0x120))
#define SPI_EV_RDY        *((volatile uint32_t *)(SPI + 0x108))
#define SPI_INTENSET      *((volatile uint32_t *)(SPI + 0x304))
#define SPI_INTENCLR      *((volatile uint32_t *)(SPI + 0x308))
#define SPI_ENABLE        *((volatile uint32_t *)(SPI + 0x500))
#define SPI_PSEL_SCK      *((volatile uint32_t *)(SPI + 0x508))
#define SPI_PSEL_MOSI     *((volatile uint32_t *)(SPI + 0x50C))
#define SPI_PSEL_MISO     *((volatile uint32_t *)(SPI + 0x510))
#define SPI_RXDATA        *((volatile uint32_t *)(SPI + 0x518))
#define SPI_TXDATA        *((volatile uint32_t *)(SPI + 0x51C))
#define SPI_FREQUENCY     *((volatile uint32_t *)(SPI + 0x524))
#define SPI_CONFIG        *((volatile uint32_t *)(SPI + 0x554))

#define K125 0x02000000
#define K250 0x04000000
#define K500 0x08000000
#define M1   0x10000000
#define M2   0x20000000
#define M4   0x40000000
#define M8   0x80000000

#endif /* !_HAL_NRF52_H_ */
