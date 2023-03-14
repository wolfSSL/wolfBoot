/* unit-extflash.c
 *
 * Unit test for parser functions in libwolfboot.c
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define FLASH_SIZE (33 * 1024)
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define EXT_FLASH

#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128)
    #define WOLFSSL_AES_COUNTER 
    #define WOLFSSL_AES_DIRECT
#endif
#if defined(ENCRYPT_WITH_AES256)
    #define WOLFSSL_AES_256
#endif
#if defined(ENCRYPT_WITH_CHACHA)
    #define HAVE_CHACHA
#endif
#define WC_NO_HARDEN

#define WOLFSSL_USER_SETTINGS
#define ENCRYPT_KEY "123456789abcdef0123456789abcdef0123456789abcdef"
#define UNIT_TEST
#include <stdio.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "user_settings.h"

#include "libwolfboot.c"

#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128)
    #include "wolfcrypt/src/aes.c"
#endif

#if defined(ENCRYPT_WITH_CHACHA)
    #include "wolfcrypt/src/chacha.c"
#endif

/* Mocks */

static int locked = 0;

void hal_init(void)
{
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    return 0;
}
void hal_flash_unlock(void)
{
    fail_unless(locked, "Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    fail_if(locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}

/* Emulation of external flash with a static buffer of 32KB (update) + 1KB (swap) */
uint8_t flash[FLASH_SIZE];

/* Mocks for ext_flash_read, ext_flash_write, and ext_flash_erase functions */
int ext_flash_read(uintptr_t address, uint8_t *data, int len) {
    printf("Called ext_flash_read %p %p %d\n", address, data, len);

    /* Check that the read address and size are within the bounds of the flash memory */
    ck_assert_int_le(address + len, FLASH_SIZE);

    /* Copy the data from the flash memory to the output buffer */
    memcpy(data, &flash[address], len);

    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len) {
    printf("Called ext_flash_write %p %p %d\n", address, data, len);


    /* Check that the write address and size are within the bounds of the flash memory */
    ck_assert_int_le(address + len, FLASH_SIZE);

    /* Copy the data from the input buffer to the flash memory */
    memcpy(&flash[address], data, len);

    return 0;
}

int ext_flash_erase(uintptr_t address, int len) {
    /* Check that the erase address and size are within the bounds of the flash memory */
    ck_assert_int_le(address + len, FLASH_SIZE);

    /* Check that address is aligned to WOLFBOOT_SECTOR_SIZE */
    ck_assert_int_eq(address, address & ~(WOLFBOOT_SECTOR_SIZE - 1));

    /* Check that len is aligned to WOLFBOOT_SECTOR_SIZE */
    ck_assert_int_eq(len, len & ~(WOLFBOOT_SECTOR_SIZE - 1));


    /* Erase the flash memory by setting each byte to 0xFF, WOLFBOOT_SECTOR_SIZE bytes at a time */
    uint32_t i;
    for (i = address; i < address + len; i += WOLFBOOT_SECTOR_SIZE) {
        memset(&flash[i], 0xFF, WOLFBOOT_SECTOR_SIZE);
    }

    return 0;
}
/* Matches all keys:
 *    - chacha (32 + 12)
 *    - aes128 (16 + 16)
 *    - aes256 (32 + 16)
 */
/* Longest key possible: AES256 (32 key + 16 IV = 48) */
static const char enc_key[] = "0123456789abcdef0123456789abcdef"
"0123456789abcdef";

static uint8_t test_buffer[512] = {
    'W',  'O',  'L',  'F',  0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x04, 0x00, 0x0d, 0x0c, 0x0b, 0x0a,
    0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x08, 0x00,
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x20, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /*<-- end of options */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        /* End HDR */
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
};

/* End Mocks */


START_TEST(test_ext_flash_operations) {
    uint32_t address = 0x1000;
    uint32_t size = 512;
    uint8_t data[2 * WOLFBOOT_SECTOR_SIZE];
    uint8_t empty_sector[WOLFBOOT_SECTOR_SIZE];
    int rres, wres, eres;

    memset(empty_sector, 0xFF, WOLFBOOT_SECTOR_SIZE);

    /* Write data to the flash memory */
    wres = ext_flash_write(address, test_buffer, size);
    ck_assert_int_eq(wres, 0);

    /* Read data from the flash memory */
    rres = ext_flash_read(address, data, size);
    ck_assert_int_eq(rres, size);

    /* Check that the data read from the flash memory matches the data that was written */
    ck_assert_mem_eq(data, test_buffer, size);

    /* Erase the first sector */
    eres = ext_flash_erase(address, WOLFBOOT_SECTOR_SIZE);
    ck_assert_int_eq(eres, 0);
    
    /* Read first sector from the flash memory */
    rres = ext_flash_read(address, data, WOLFBOOT_SECTOR_SIZE);
    ck_assert_int_eq(rres, WOLFBOOT_SECTOR_SIZE);


    /* Check that the first sector read is empty */
    ck_assert_mem_eq(data, empty_sector, WOLFBOOT_SECTOR_SIZE);
}
END_TEST

START_TEST(test_ext_enc_flash_operations) {
    uint32_t address = 0x1000;
    uint32_t size = 512;
    uint8_t data[2 * WOLFBOOT_SECTOR_SIZE];
    uint8_t dataw[2 * WOLFBOOT_SECTOR_SIZE];
    int rres, wres, eres;


    /* Write data to the flash memory */
    memcpy(dataw, test_buffer, size);
    wres = ext_flash_check_write(address, dataw, size);
    ck_assert_int_eq(wres, 0);


    /* Read data from the flash memory */
    rres = ext_flash_check_read(address, data, size);
    ck_assert_int_eq(rres, size);

    /* Check that the data read from the flash memory matches the data that was written */
    ck_assert_mem_eq(data, test_buffer, size);

    address = 0x07FF0;
    size = 16;

    /* Write data to the flash memory */
    memcpy(dataw, test_buffer, size);
    wres = ext_flash_check_write(address, dataw, size);
    ck_assert_int_eq(wres, 0);


    /* Read data from the flash memory */
    rres = ext_flash_check_read(address, data, size);
    ck_assert_int_eq(rres, size);
    
    /* Check that the data read from the flash memory matches the data that was written */
    ck_assert_mem_eq(&flash[address], test_buffer, size);

}
END_TEST



Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    /* Test cases */
    TCase *ext_flash_operations  = tcase_create("External flash operations: API");
    TCase *ext_enc_flash_operations  = tcase_create("External encrypted flash operations");

    /* Set parameters + add to suite */
    tcase_add_test(ext_flash_operations, test_ext_flash_operations);
    tcase_add_test(ext_enc_flash_operations, test_ext_enc_flash_operations);

    tcase_set_timeout(ext_flash_operations, 20);
    tcase_set_timeout(ext_enc_flash_operations, 20);
    suite_add_tcase(s, ext_flash_operations);
    suite_add_tcase(s, ext_enc_flash_operations);

    return s;
}


int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
