/* mcxw.h
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

#ifndef MCXW_H
#define MCXW_H

#define MCXW_FLASH_SIZE             0x00100000U
/* The last sector holds the NXP PROD_DATA (Bluetooth address, crystal trim) */
#define MCXW_FLASH_PROD_DATA        0x000FE000U
#define MCXW_FLASH_SECURE_ALIAS     0x10000000U
#define MCXW_FLASH_SECTOR_SIZE      0x2000U
#define MCXW_FLASH_PHRASE_SIZE      16U
#define MCXW_FLASH_PHRASE_WORDS     (MCXW_FLASH_PHRASE_SIZE / 4U)
#ifndef MCXW_FLASH_PTR
#define MCXW_FLASH_PTR(a)           ((volatile uint32_t *)(uintptr_t)(a))
#endif

#define MCXW_FMU_CMD_PROGRAM_PHRASE 0x24U
#define MCXW_FMU_CMD_ERASE_SECTOR   0x42U
/* FSTAT FAIL | CMDABT | PVIOL | ACCERR | CWSABT */
#define MCXW_FSTAT_ERR_MASK         0x00000075U
/* Write-1-to-clear: CMDABT | PVIOL | ACCERR | CWSABT | DFDIF */
#define MCXW_FSTAT_CLEAR_MASK       0x00010074U

/* SMSCM OCMDR0 control values used by the SDK flash driver */
#define MCXW_OCMCF1_SPECULATION_OFF 0x3U
#define MCXW_OCMCF2_CACHE_CLEAR     0x1U

#define MCXW_ELE_WAIT               1000000U
#define MCXW_ELE_RNG_HIGH_QUALITY   1U

/* Non-secure SRAM: the top of STCM4, which has no ECC to initialize */
#define MCXW_NS_RAM_START           0x20016000U
#define MCXW_NS_RAM_END             0x20019FFFU

#define MCXW_UART                   LPUART1
#define MCXW_UART_BAUD              115200U
#define MCXW_UART_NS_START          0x40039000U
#define MCXW_UART_NS_END            0x40039FFFU
#define MCXW_UART_RX_PIN            2U
#define MCXW_UART_TX_PIN            3U

#define MCXW_GPIOA_NS_START         0x48010000U
#define MCXW_GPIOA_NS_END           0x48010FFFU
#define MCXW_LED_GREEN_PIN          19U
#define MCXW_LED_BLUE_PIN           20U
#define MCXW_LED_RED_PIN            21U
#define MCXW_LED_PINS_MASK          ((1U << MCXW_LED_GREEN_PIN) | \
                                     (1U << MCXW_LED_BLUE_PIN) | \
                                     (1U << MCXW_LED_RED_PIN))

#endif /* MCXW_H */
