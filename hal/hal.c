/* hal.c
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

/* Code shared between all HAL's */

#include <stdint.h>
#include "hal.h"
#include "string.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

/* Test for internal flash erase/write */
/* Use TEST_EXT_FLASH to test ext flash (see spi_flash.c or qspi_flash.c) */
#ifdef TEST_FLASH

#ifndef TEST_ADDRESS
    #define TEST_SZ      WOLFBOOT_SECTOR_SIZE
    #define TEST_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS
    #define TEST_ADDRESS_BANKA WOLFBOOT_PARTITION_BOOT_ADDRESS
    #define TEST_ADDRESS_BANKB WOLFBOOT_PARTITION_UPDATE_ADDRESS
#endif

int hal_flash_test(void)
{
    int ret = 0;
    uint32_t i;
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
    hal_flash_unlock();
    ret = hal_flash_write(TEST_ADDRESS, (uint8_t*)pageData, sizeof(pageData));
    hal_flash_lock();
    wolfBoot_printf("Write Page: Ret %d\n", ret);
    if (ret != 0)
        return ret;
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

#ifndef TEST_FLASH_READONLY
int hal_flash_test_write_once(void)
{
    uint8_t test_byte, expected_byte;
    unsigned int b;
    int ret = 0;

    /* Erase the test sector */
    hal_flash_unlock();
    ret = hal_flash_erase(TEST_ADDRESS, TEST_SZ);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
        return -1;
    }

    expected_byte = 0xFF;
    /* For each bit in the byte (from LSB to MSB) */
    for (b = 0; b < 8; b++) {
        /* Toggle bit from 1 to 0 */
        test_byte = 0xFF & ~(1 << b);
        expected_byte &= ~(1 << b);

        /* Write the data */
        hal_flash_unlock();
        ret = hal_flash_write(TEST_ADDRESS, &test_byte, sizeof(test_byte));
        hal_flash_lock();

        if (ret != 0) {
            wolfBoot_printf("Write failed at bit %d: Ret %d\n", b, ret);
            return -1;
        }

        /* Verify the write by direct comparison */
        if (memcmp((void*)TEST_ADDRESS, &expected_byte, sizeof(expected_byte)) != 0) {
            wolfBoot_printf("Verification failed at byte %d\n", b);
            return -1;
        }
    }

    wolfBoot_printf("Write-once test passed\n");
    return 0;
}

/* Test if unaligned write works. First test writing 1 bytes at SECTOR + 1.
 * Then test writing 2 bytes that span the sector boundary.
 */
int hal_flash_test_align(void)
{
    int ret = 0;
    uint8_t test_data_1 = 0xAA;
    uint8_t test_data_2[2] = {0xBB, 0xCC};
    uint8_t read_back[2];

    /* erase both sectors */
    hal_flash_unlock();
    ret = hal_flash_erase(TEST_ADDRESS, TEST_SZ * 2);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
        return -1;
    }

    /* Write 1 byte at SECTOR + 1 */
    hal_flash_unlock();
    ret = hal_flash_write(TEST_ADDRESS + 1, &test_data_1, 1);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Unaligned write (1 byte) failed: Ret %d\n", ret);
        return -1;
    }

    /* Verify 1 byte write */
    if (*(uint8_t*)(TEST_ADDRESS + 1) != test_data_1) {
        wolfBoot_printf("Unaligned write verification (1 byte) failed\n");
        return -1;
    }

    /* Write 2 bytes spanning sector boundary */
    hal_flash_unlock();
    ret = hal_flash_write(TEST_ADDRESS + TEST_SZ - 1, test_data_2, 2);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Unaligned write (2 bytes) failed: Ret %d\n", ret);
        return -1;
    }

    /* Verify 2 bytes write */
    memcpy(read_back, (void*)(TEST_ADDRESS + TEST_SZ - 1), 2);
    if (read_back[0] != test_data_2[0] || read_back[1] != test_data_2[1]) {
        wolfBoot_printf("Unaligned write verification (2 bytes) failed\n");
        return -1;
    }

    wolfBoot_printf("Unaligned write test passed\n");
    return 0;
}

int hal_flash_test_unaligned_src(void)
{
    uint32_t src[9];
    unsigned int i;
    uint8_t *ptr;
    int ret;

    /* force unaligned pointer */
    ptr = (uint8_t*)(uintptr_t)src;
    ptr++;

    for (i = 0; i < sizeof(src); i++) {
        ptr[i] = i & 0xff;
    }

    hal_flash_unlock();
    ret = hal_flash_erase(TEST_ADDRESS, TEST_SZ);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
        return -1;
    }

    hal_flash_unlock();
    ret = hal_flash_write(TEST_ADDRESS, ptr, sizeof(src) - 1);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("writing for unaligned source failed: Ret %d\n", ret);
        return -1;
    }
    if (memcmp(ptr, (uint8_t*)TEST_ADDRESS, sizeof(src) - 1) != 0) {
        wolfBoot_printf("unaligned source verification failed\n");
        return -1;
    }

    return 0;
}

#endif /* !TEST_FLASH_READONLY */

/* This test can be run only if swapping the flash do not reboot the board */
#if defined(DUALBANK_SWAP) && !defined(TEST_FLASH_READONLY)
int hal_flash_test_dualbank(void)
{
    int ret = 0;
    uint32_t i;
    uint8_t cur_fill = 0xb0;
    uint8_t new_fill = 0xf0;
    uint8_t fill;
    uint32_t pagePtr;

    wolfBoot_printf("swap flash test at 0x%x\n", TEST_ADDRESS);

    for (i = 0; i < 2; i++) {
        fill = (i == 0) ? cur_fill : new_fill;
        pagePtr = (i == 0) ? TEST_ADDRESS_BANKA : TEST_ADDRESS_BANKB;

        /* Erase sector */
        hal_flash_unlock();
        ret = hal_flash_erase(pagePtr, WOLFBOOT_SECTOR_SIZE);
        hal_flash_lock();
        if (ret != 0) {
            wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
            return -1;
        }

        /* Write Page */
        hal_flash_unlock();
        ret = hal_flash_write(pagePtr, (uint8_t*)&fill, sizeof(fill));
        hal_flash_lock();
        if (ret != 0) {
            wolfBoot_printf("Write Page failed: Ret %d\n", ret);
            return -1;
        }
    }

    if (*((uint8_t*)(TEST_ADDRESS_BANKA)) != cur_fill) {
        wolfBoot_printf("Bank A data mismatch: %x != %x\n", *((uint8_t*)TEST_ADDRESS_BANKA), cur_fill);
        return -1;
    }
    if (*((uint8_t*)(TEST_ADDRESS_BANKB)) != new_fill) {
        wolfBoot_printf("Bank B data mismatch: %x != %x\n", *((uint8_t*)TEST_ADDRESS_BANKB), new_fill);
        return -1;
    }
    hal_flash_dualbank_swap();

    if (*((uint8_t*)(TEST_ADDRESS_BANKA)) != new_fill) {
        wolfBoot_printf("Bank A data mismatch after swap: %x != %x\n", *((uint8_t*)TEST_ADDRESS_BANKA), new_fill);
        return -1;
    }
    if (*((uint8_t*)(TEST_ADDRESS_BANKB)) != cur_fill) {
        wolfBoot_printf("Bank B data mismatch after swap: %x != %x\n", *((uint8_t*)TEST_ADDRESS_BANKB), cur_fill);
        return -1;
    }

    wolfBoot_printf("DUALBANK_SWAP test passed");
    return 0;
}
#endif /* DUALBANK_SWAP */

#endif /* TEST_FLASH */

WEAKFUNCTION int hal_uds_derive_key(uint8_t *out, size_t out_len)
{
    (void)out;
    (void)out_len;
    return -1;
}

WEAKFUNCTION int hal_attestation_get_lifecycle(uint32_t *lifecycle)
{
    (void)lifecycle;
    return -1;
}

WEAKFUNCTION int hal_attestation_get_implementation_id(uint8_t *buf, size_t *len)
{
    (void)buf;
    (void)len;
    return -1;
}

WEAKFUNCTION int hal_attestation_get_ueid(uint8_t *buf, size_t *len)
{
    (void)buf;
    (void)len;
    return -1;
}

WEAKFUNCTION int hal_attestation_get_iak_private_key(uint8_t *buf, size_t *len)
{
    (void)buf;
    (void)len;
    return -1;
}
