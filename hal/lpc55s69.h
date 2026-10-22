/* lpc55s69.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#ifndef LPC55S69_H
#define LPC55S69_H

/* Flash programs and erases in 512-byte pages; an erased page that was
 * never programmed faults on read (ECC), so erased pages are filled 0xFF */
#define LPC55S69_FLASH_PAGE_SIZE        512U
#define LPC55S69_FLASH_SECURE_ALIAS     0x10000000U
/* 630KB of user flash below the protected flash region (PFR) */
#define LPC55S69_FLASH_SIZE             0x9DE00U
#ifndef LPC55S69_FLASH_PTR
#define LPC55S69_FLASH_PTR(a)           ((const uint8_t *)(uintptr_t)(a))
#endif

/* AHB secure controller flash rules cover 32KB each */
#define LPC55S69_AHBSC_FLASH_REGION     0x8000U

/* SRAM2, the non-secure application RAM */
#define LPC55S69_NS_SRAM_START          0x20020000U
#define LPC55S69_NS_SRAM_END            0x2002FFFFU
/* Non-secure aliases of the peripherals the application keeps */
#define LPC55S69_FLEXCOMM0_NS_BASE      0x40086000U
#define LPC55S69_FLEXCOMM0_NS_SIZE      0x1000U
#define LPC55S69_GPIO_NS_BASE           0x4008C000U
#define LPC55S69_GPIO_NS_SIZE           0x4000U

#define LPC55S69_UART_BAUD              115200U
#define LPC55S69_UART_CLK_HZ            12000000U
#define LPC55S69_UART_PORT              0U
#define LPC55S69_UART_RX_PIN            29U
#define LPC55S69_UART_TX_PIN            30U

/* LPCXpresso55S69 RGB LED on port 1, active low */
#define LPC55S69_LED_PORT               1U
#define LPC55S69_LED_BLUE_PIN           4U
#define LPC55S69_LED_RED_PIN            6U
#define LPC55S69_LED_GREEN_PIN          7U

#endif /* LPC55S69_H */
