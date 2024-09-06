/* unit-sectorflags.c
 *
 * Unit test for sector flags functions in libwolfboot.c
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define FLASH_SIZE (33 * 1024)
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define EXT_FLASH 1
#define PART_UPDATE_EXT 1
#define PART_SWAP_EXT 1
#define ENCRYPT_KEY "123456789abcdef0123456789abcdef0123456789abcdef"
#include <stdio.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "user_settings.h"

uint8_t *ut_get_endpart(void);
#include "libwolfboot.c"

/* Mocks */

static int locked = 0;
static int ext_locked = 0;

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

void ext_flash_unlock(void)
{
    //fail_unless(ext_locked, "Double unlock detected\n");
    ext_locked--;
}
void ext_flash_lock(void)
{
    //fail_if(ext_locked, "Double lock detected\n");
    ext_locked++;
}

void hal_prepare_boot(void)
{
}

/* Emulation of external flash with a static buffer of 32KB (update) + 1KB (swap) */
uint8_t flash[FLASH_SIZE];

uint8_t *ut_get_endpart(void)
{
    return flash + WOLFBOOT_PARTITION_SIZE;
}

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
    printf("Called ext_flash_erase %p %d\n", address, len);
    /* Check that the erase address and size are within the bounds of the flash memory */
    ck_assert_int_le(address + len, FLASH_SIZE);

    /* Erase the flash memory by setting each byte to 0xFF, WOLFBOOT_SECTOR_SIZE bytes at a time */
    uint32_t i;
    for (i = address; i < address + len; i += WOLFBOOT_SECTOR_SIZE) {
        memset(&flash[i], 0xFF, WOLFBOOT_SECTOR_SIZE);
    }

    return 0;
}

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


START_TEST(test_partition_flags) {
    uint32_t address = 0;
    uint32_t size = 512;
    uint8_t data[2 * WOLFBOOT_SECTOR_SIZE];
    uint8_t empty_sector[WOLFBOOT_SECTOR_SIZE];
    int rres, wres, eres;
    uint8_t st;

    memset(empty_sector, 0xFF, WOLFBOOT_SECTOR_SIZE);

    /* Write data to the flash memory */
    wres = ext_flash_write(address, test_buffer, size);
    ck_assert_int_eq(wres, 0);

    /* Read data from the flash memory */
    rres = ext_flash_read(address, data, size);
    ck_assert_int_eq(rres, size);

    /* Check that the data read from the flash memory matches the data that was written */
    ck_assert_mem_eq(data, test_buffer, size);

    /* Set partition to updating state */
    wolfBoot_update_trigger();

    /* Get partition state */
    wolfBoot_get_partition_state(PART_UPDATE, &st);
    ck_assert_int_eq(st, IMG_STATE_UPDATING);

    /* Change to IMG_STATE_TESTING */
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_UPDATE, st);
    wolfBoot_get_partition_state(PART_UPDATE, &st);
    ck_assert_int_eq(st, IMG_STATE_TESTING);
    
    /* Change to IMG_STATE_SUCCESS */
    st = IMG_STATE_SUCCESS;
    wolfBoot_set_partition_state(PART_UPDATE, st);
    wolfBoot_get_partition_state(PART_UPDATE, &st);
    ck_assert_int_eq(st, IMG_STATE_SUCCESS);
}
END_TEST

START_TEST(test_sector_flags) {

}
END_TEST


/* End Mocks */

Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    /* Test cases */
    TCase *partition_flags  = tcase_create("External flash operations: partition flags");
    TCase *sector_flags  = tcase_create("External encrypted flash operations");

    /* Set parameters + add to suite */
    tcase_add_test(partition_flags, test_partition_flags);
    tcase_add_test(sector_flags, test_sector_flags);

    tcase_set_timeout(partition_flags, 20);
    tcase_set_timeout(sector_flags, 20);
    suite_add_tcase(s, partition_flags);
    suite_add_tcase(s, sector_flags);

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
