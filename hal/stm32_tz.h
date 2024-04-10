/* stm32_tz.h
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#ifndef STM32_TZ_INCLUDED
#define STM32_TZ_INCLUDED
#include <stdint.h>

/* SAU registers, used to define memory mapped regions */
#define SAU_CTRL   (*(volatile uint32_t *)(0xE000EDD0))
#define SAU_RNR (*(volatile uint32_t *)(0xE000EDD8)) /** SAU_RNR - region number register **/
#define SAU_RBAR (*(volatile uint32_t *)(0xE000EDDC)) /** SAU_RBAR - region base address register **/
#define SAU_RLAR (*(volatile uint32_t *)(0xE000EDE0)) /** SAU_RLAR - region limit address register **/

#define SAU_REGION_MASK 0x000000FF
#define SAU_ADDR_MASK 0xFFFFFFE0 /* LS 5 bit are reserved or used for flags */

/* Flag for the SAU region limit register */
#define SAU_REG_ENABLE (1 << 0) /* Indicates that the region is enabled. */
#define SAU_REG_SECURE (1 << 1) /* When on, the region is S or NSC */

#define SAU_INIT_CTRL_ENABLE (1 << 0)
#define SAU_INIT_CTRL_ALLNS  (1 << 1)

#define SCB_SHCSR     (*(volatile uint32_t *)(0xE000ED24))
#define SCB_SHCSR_SECUREFAULT_EN            (1<<19)

static inline void sau_init_region(uint32_t region, uint32_t start_addr,
        uint32_t end_addr, int secure)
{
    uint32_t secure_flag = 0;
    if (secure)
        secure_flag = SAU_REG_SECURE;
    SAU_RNR = region & SAU_REGION_MASK;
    SAU_RBAR = start_addr & SAU_ADDR_MASK;
    SAU_RLAR = (end_addr & SAU_ADDR_MASK)
        | secure_flag | SAU_REG_ENABLE;
}

#endif
