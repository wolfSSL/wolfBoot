/* rw612.h
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

#ifndef RW612_H
#define RW612_H

#define RW612_FLASH_BASE            0x08000000U
#define RW612_FLASH_ALIAS_MASK      0x0FFFFFFFU
#ifndef RW612_FCB_ADDRESS
#define RW612_FCB_ADDRESS           (RW612_FLASH_BASE + 0x400U)
#endif
#define RW612_FLASH_PAGE_SIZE       256U
/* NOR erase unit; WOLFBOOT_SECTOR_SIZE may be any multiple of it */
#define RW612_FLASH_SECTOR_SIZE     0x1000U
#define RW612_FLEXSPI_INSTANCE      0U

#ifndef RW612_ROM_API_TREE_A0
#define RW612_ROM_API_TREE_A0       0x13024100U
#endif
#ifndef RW612_ROM_API_TREE_A1
#define RW612_ROM_API_TREE_A1       0x13030000U
#endif
#define RW612_ROM_API_FLEXSPI_IDX   5U

#define RW612_UART_CLK_HZ           16000000U
#define RW612_UART_BAUD             115200U

#endif /* RW612_H */
