/* wolfhal.c
 *
 * Generic wolfHAL port for wolfBoot. wolfBoot's HAL contract
 * (hal_flash_*, ext_flash_*, uart_write) is satisfied here by forwarding to
 * the wolfHAL API. The chip drivers behind the wolfHAL API are configured
 * in the board directory; this file passes the BOARD_*_DEV handles defined
 * in wolfHAL_board.h:
 *   - BOARD_FLASH_DEV: internal flash. A board without internal flash does
 *     not define it, and hal_flash_write/erase then fail.
 *   - BOARD_EXT_FLASH_DEV: external flash, with EXT_FLASH=1. Program and
 *     erase are refused outside the wolfBoot BOOT, UPDATE and SWAP
 *     partitions.
 *   - BOARD_UART_DEV: console, with DEBUG_UART=1. With WOLFHAL_UART_CRLF
 *     (set in board.mk), '\n' is sent as "\r\n".
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

#include <stdint.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "wolfHAL_board.h"

#ifdef BOARD_FLASH_DEV
void RAMFUNCTION hal_flash_unlock(void)
{
    whal_Flash_Unlock(BOARD_FLASH_DEV, 0, 0);
}

void RAMFUNCTION hal_flash_lock(void)
{
    whal_Flash_Lock(BOARD_FLASH_DEV, 0, 0);
}

int RAMFUNCTION hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    return whal_Flash_Write(BOARD_FLASH_DEV, (size_t)address, data, (size_t)len);
}

int RAMFUNCTION hal_flash_erase(haladdr_t address, int len)
{
    return whal_Flash_Erase(BOARD_FLASH_DEV, (size_t)address, (size_t)len);
}
#else
/* No internal flash: wolfBoot's partitions are all in external flash. */
void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int RAMFUNCTION hal_flash_erase(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}
#endif /* BOARD_FLASH_DEV */

#ifdef EXT_FLASH
/* Program and erase must fit inside BOOT, UPDATE or SWAP, where that
 * partition is in the external flash. Anything else is refused. */
#ifdef PART_SWAP_EXT
/* SWAP size is a variable so the unit tests can change it. */
static uint32_t wolfhal_swap_size = (uint32_t)WOLFBOOT_SECTOR_SIZE;
#endif

static int wolfhal_range_in_partition(uint32_t base, uint32_t size,
                                      uint32_t addr, uint32_t len)
{
    uint32_t end;

    if (len == 0) {
        return 0;
    }
    /* base + size must not wrap. */
    if (size > UINT32_MAX - base) {
        return 0;
    }
    end = base + size;
    /* [addr, addr+len) must sit inside [base, end). Check the start first
     * so (end - addr) cannot go below zero. */
    if (addr < base) {
        return 0;
    }
    if (addr > end) {
        return 0;
    }
    if (len > end - addr) {
        return 0;
    }
    return 1;
}

/* 1 if a flash address fits the guard's 32 bits. A larger one would be cut
 * by the casts below and could then look like it is inside a partition. */
static int wolfhal_addr_fits(uintptr_t address)
{
    return ((uint64_t)address <= 0xFFFFFFFFULL);
}

/* A partition counts when its PART_*_EXT flag says it is in the external
 * flash. Its address may be 0. */
static int wolfhal_ext_flash_guard_ok(uint32_t addr, uint32_t len)
{
#ifdef PART_BOOT_EXT
    if (wolfhal_range_in_partition((uint32_t)WOLFBOOT_PARTITION_BOOT_ADDRESS,
                                   (uint32_t)WOLFBOOT_PARTITION_SIZE,
                                   addr, len)) {
        return 1;
    }
#endif
#ifdef PART_UPDATE_EXT
    if (wolfhal_range_in_partition(
            (uint32_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            (uint32_t)WOLFBOOT_PARTITION_UPDATE_SIZE, addr, len)) {
        return 1;
    }
#endif
#ifdef PART_SWAP_EXT
    if (wolfhal_range_in_partition((uint32_t)WOLFBOOT_PARTITION_SWAP_ADDRESS,
                                   wolfhal_swap_size, addr, len)) {
        return 1;
    }
#endif
    (void)addr;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (data == NULL || len <= 0) {
        return 0;
    }
    if (whal_Flash_Read(BOARD_EXT_FLASH_DEV, (size_t)address, data,
                        (size_t)len) != WHAL_SUCCESS) {
        return -1;
    }
    return len;
}

/* Write and erase return 0 on success and -1 on error (docs/HAL.md). */
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    if (len == 0) {
        return 0;
    }
    if (data == NULL || len < 0) {
        return -1;
    }
    if (!wolfhal_addr_fits(address) ||
        wolfhal_ext_flash_guard_ok((uint32_t)address, (uint32_t)len) == 0) {
        return -1;
    }
    if (whal_Flash_Write(BOARD_EXT_FLASH_DEV, (size_t)address, data,
                         (size_t)len) != WHAL_SUCCESS) {
        return -1;
    }
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    if (len == 0) {
        return 0;
    }
    if (len < 0) {
        return -1;
    }
    if (!wolfhal_addr_fits(address) ||
        wolfhal_ext_flash_guard_ok((uint32_t)address, (uint32_t)len) == 0) {
        return -1;
    }
    if (whal_Flash_Erase(BOARD_EXT_FLASH_DEV, (size_t)address,
                         (size_t)len) != WHAL_SUCCESS) {
        return -1;
    }
    return 0;
}

void ext_flash_lock(void)
{
    (void)whal_Flash_Lock(BOARD_EXT_FLASH_DEV, 0, 0);
}

void ext_flash_unlock(void)
{
    (void)whal_Flash_Unlock(BOARD_EXT_FLASH_DEV, 0, 0);
}
#endif /* EXT_FLASH */

#ifdef DEBUG_UART
#ifdef WOLFHAL_UART_CRLF
/* Send len bytes, with '\n' sent as "\r\n" */
void uart_write(const char *buf, unsigned int len)
{
    unsigned int start = 0;
    unsigned int i;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (i > start) {
                whal_Uart_Send(BOARD_UART_DEV, (const uint8_t *)buf + start,
                               (size_t)(i - start));
            }
            whal_Uart_Send(BOARD_UART_DEV, (const uint8_t *)"\r\n", 2);
            start = i + 1;
        }
    }
    if (len > start) {
        whal_Uart_Send(BOARD_UART_DEV, (const uint8_t *)buf + start,
                       (size_t)(len - start));
    }
}
#else
void uart_write(const char *buf, unsigned int len)
{
    whal_Uart_Send(BOARD_UART_DEV, (const uint8_t *)buf, (size_t)len);
}
#endif /* WOLFHAL_UART_CRLF */
#endif /* DEBUG_UART */
