/* hal.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

/* Code shared between all HAL's */

#include <stdint.h>
#include "hal.h"
#include "string.h"
#include "printf.h"

/* Test for internal flash erase/write */
/* Use TEST_EXT_FLASH to test ext flash (see spi_flash.c or qspi_flash.c) */
#ifdef TEST_FLASH

#ifndef TEST_ADDRESS
    #define TEST_SZ      WOLFBOOT_SECTOR_SIZE
    #define TEST_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS
#endif

int hal_flash_test(void)
{
    int ret = 0;
    uint32_t i, len;
    uint8_t* pagePtr = (uint8_t*)TEST_ADDRESS;
    static uint8_t pageData[TEST_SZ];

    wolfBoot_printf("Internal flash test at 0x%x\n", TEST_ADDRESS);

    /* Setup test data */
    for (i=0; i<sizeof(pageData); i++) {
        ((uint8_t*)pageData)[i] = (i & 0xff);
    }

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    hal_flash_unlock();
    ret = hal_flash_erase(TEST_ADDRESS, sizeof(pageData));
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
        return ret;
    }

    /* Write Page */
    ret = hal_flash_write(TEST_ADDRESS, (uint8_t*)pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Compare Page */
    ret = memcmp((void*)TEST_ADDRESS, pageData, sizeof(pageData));
    if (ret != 0) {
        wolfBoot_printf("Check Data @ %d failed\n", ret);
        return ret;
    }

    wolfBoot_printf("Internal Flash Test Passed\n");
    return ret;
}
#endif /* TEST_FLASH */
