/* unit-mock-state.c
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
#define WOLFBOOT_HASH_SHA256

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
#define NVM_FLASH_WRITEONCE

#define MOCK_PARTITION_TRAILER
#define MOCK_BLOB_TYPE
#include <stdio.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"

static uint8_t* get_trailer_at(uint8_t part, uint32_t at);
static void set_trailer_at(uint8_t part, uint32_t at, uint8_t val);
static void set_partition_magic(uint8_t part);

static uint8_t current_backup_part = 1;
uint8_t image_backup(uint8_t part_id)
{
    printf("Called image_backup\n");
    return current_backup_part;
}

#ifndef PART_TOTAL_IDS
# define PART_TOTAL_IDS 3
#endif

#include "libwolfboot.c"

/* Mocks */

static int locked = 0;
static int hal_flash_write_mock_called = 0;
static uintptr_t hal_flash_write_mock_address = 0U;
static const uint8_t *hal_flash_write_mock_data = NULL;
static int hal_flash_write_mock_len = 0;

static uintptr_t hal_flash_erase_mock_address = 0;
static int hal_flash_erase_mock_len = 0;

static int hal_flash_erase_mock_called = 0;

static void hal_flash_write_mock_reset(void)
{
    hal_flash_write_mock_called = 0;
    hal_flash_write_mock_address = 0U;
    hal_flash_write_mock_data = NULL;
    hal_flash_write_mock_len = 0;
}

static void hal_flash_erase_mock_reset(void)
{
    hal_flash_erase_mock_called = 0;
    hal_flash_erase_mock_address = 0U;
    hal_flash_erase_mock_len = 0;
}

void hal_init(void)
{
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    printf("Called hal_flash_write\r\n");
    hal_flash_write_mock_called++;
    hal_flash_write_mock_address = address;
    hal_flash_write_mock_data = data;
    hal_flash_write_mock_len = len;
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    printf("Called hal_flash_erase\r\n");
    hal_flash_erase_mock_called++;
    hal_flash_erase_mock_address = address;
    hal_flash_erase_mock_len = len;
    return 0;
}
void hal_flash_unlock(void)
{
}
void hal_flash_lock(void)
{
}

void hal_prepare_boot(void)
{
}

static int ext_locked = 1;
static int ext_flash_write_mock_called = 0;
static uintptr_t ext_flash_write_mock_address = 0U;
static const uint8_t *ext_flash_write_mock_data = NULL;
static int ext_flash_write_mock_len = 0;

static int ext_flash_read_mock_called = 0;
static uint32_t ext_flash_read_mock_address = 0U;
static uint8_t *ext_flash_read_mock_data = NULL;
static int ext_flash_read_mock_len = 0;

static uintptr_t ext_flash_erase_mock_address = 0U;
static int ext_flash_erase_mock_len = 0;

static int ext_flash_erase_mock_called = 0;

static void ext_flash_write_mock_reset(void)
{
    ext_flash_write_mock_called = 0;
    ext_flash_write_mock_address = 0U;
    ext_flash_write_mock_data = NULL;
    ext_flash_write_mock_len = 0;
}

static void ext_flash_erase_mock_reset(void)
{
    ext_flash_erase_mock_called = 0;
    ext_flash_erase_mock_address = 0U;
    ext_flash_erase_mock_len = 0;
}

void ext_init(void)
{
}
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    printf("Called ext_flash_read\r\n");
    ext_flash_read_mock_called++;
    ext_flash_read_mock_address = address;
    ext_flash_read_mock_data = data;
    ext_flash_read_mock_len = len;
    return 0;
}
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    printf("Called ext_flash_write\r\n");
    ext_flash_write_mock_called++;
    ext_flash_write_mock_address = address;
    ext_flash_write_mock_data = data;
    ext_flash_write_mock_len = len;
    return 0;
}
int ext_flash_erase(uintptr_t address, int len)
{
    printf("Called ext_flash_erase\r\n");
    ext_flash_erase_mock_called++;
    ext_flash_erase_mock_address = address;
    ext_flash_erase_mock_len = len;
    return 0;
}
void ext_flash_unlock(void)
{
    ck_assert_msg(ext_locked, "Double unlock detected (ext)\n");
    ext_locked--;
}
void ext_flash_lock(void)
{
    ck_assert_msg(!ext_locked, "Double lock detected(ext)\n");
    ext_locked++;
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


static uint8_t mock_partition_state;
static int mock_partition_state_retval;
static uint8_t mock_partition_state_arg_part;
static int mock_get_partition_state_called = 0;

struct mock_state {
    uint8_t part;
    uint8_t state;
    int retval;
    int getstate_called;
    int setstate_called;
};

static struct mock_state mock_state[PART_TOTAL_IDS] = {
 { PART_BOOT, IMG_STATE_NEW, -1, 0, 0 },
 { PART_UPDATE, IMG_STATE_NEW, -1, 0, 0 },
 { PART_SWAP, IMG_STATE_NEW, -1, 0, 0 }
};

static void mock_set_initial_partition_state(uint8_t part, uint8_t st)
{
    mock_state[part].retval = 0;
    mock_state[part].state = st;
}

static void mock_reset_partition_states(void)
{
    int i;
    for (i = 0; i < PART_TOTAL_IDS; i++) {
        mock_state[i].retval = -1;
        mock_state[i].state = IMG_STATE_NEW;
        mock_state[i].getstate_called = 0;
        mock_state[i].setstate_called = 0;
    }
}

static uint8_t magic_trailer[4] = { 'B','O','O','T' };
static uint8_t erased_trailer[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

static uint8_t* get_trailer_at(uint8_t part, uint32_t at)
{
    //ck_assert_uint_lt(part, PART_TOTAL_IDS);
    if (part >= PART_TOTAL_IDS)
        return NULL;
    if (at == 1)
        mock_state[part].getstate_called++;
    if ((at == 0) && (mock_state[part].retval == 0)) {
        return magic_trailer;
    } else if ((at == 1) && (mock_state[part].retval == 0)) {
        return &mock_state[part].state;
    } else {
        return erased_trailer;
    }
}

static void set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    ck_assert_uint_lt(part, PART_TOTAL_IDS);
    if (at == 1) {
        printf("Setting part %d state %02x\n", part, val);
        mock_state[part].setstate_called++;
        mock_state[part].state = val;
    }
}

static void set_partition_magic(uint8_t part)
{
    ck_assert_uint_lt(part, PART_TOTAL_IDS);
    mock_state[part].retval = 0;
}

/* End Mocks */

START_TEST(test_wolfBoot_set_partition_state)
{
    int i;
    uint8_t st = 0x0D;
    /* Corner cases: PART_NONE should have no effect */
    mock_reset_partition_states();
    wolfBoot_set_partition_state(PART_NONE, IMG_STATE_SUCCESS);
    ck_assert_uint_eq(mock_state[PART_BOOT].state, IMG_STATE_NEW);
    ck_assert_uint_eq(mock_state[PART_BOOT].getstate_called, 0);
    ck_assert_uint_eq(mock_state[PART_UPDATE].state, IMG_STATE_NEW);
    ck_assert_uint_eq(mock_state[PART_UPDATE].getstate_called, 0);

    /* Ensure 'get_partition_state()' with 'PART_NONE' is invalid and
     * has no side effects
     */
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_NONE, &st), -1);
    for (i = 0; i < PART_TOTAL_IDS - 1; i++) {
        ck_assert_uint_eq(mock_state[i].state, IMG_STATE_NEW);
        ck_assert_int_eq(mock_state[i].retval, -1);
        ck_assert_uint_eq(mock_state[i].getstate_called, 0);
        ck_assert_uint_eq(mock_state[i].setstate_called, 0);
    }

    /* Sunny day set state change */
    mock_reset_partition_states();
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_TESTING);
    ck_assert_uint_eq(mock_state[PART_UPDATE].state, IMG_STATE_TESTING);
    ck_assert_int_eq(mock_state[PART_UPDATE].retval, 0);
    ck_assert_uint_ge(mock_state[PART_UPDATE].getstate_called, 1);
    ck_assert_uint_ge(mock_state[PART_UPDATE].setstate_called, 1);

}

END_TEST

START_TEST(test_wolfBoot_misc_utils)
{
    uint16_t word2 = 0xA0B1;
    uint32_t word4 = 0xA0B1C2D3;
    uint8_t *hdr_cpy_ptr = NULL;
    ext_flash_erase_mock_reset();
    mock_reset_partition_states();
    ck_assert_uint_eq(wb_reverse_word32(word4), 0xD3C2B1A0);

    ck_assert_uint_eq(im2n(word4), word4);
    ck_assert_uint_eq(im2ns(word2), word2);

    ck_assert_ptr_eq(wolfBoot_get_image_from_part(PART_BOOT), (void *)WOLFBOOT_PARTITION_BOOT_ADDRESS);
    ck_assert_ptr_eq(wolfBoot_get_image_from_part(PART_UPDATE), (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS);


}
END_TEST




Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    TCase* tcase_wolfBoot_set_partition_state = tcase_create("wolfBoot_set_partition_state");
    tcase_set_timeout(tcase_wolfBoot_set_partition_state, 20);
    tcase_add_test(tcase_wolfBoot_set_partition_state, test_wolfBoot_set_partition_state);
    suite_add_tcase(s, tcase_wolfBoot_set_partition_state);
    
    TCase* tcase_wolfBoot_misc_utils = tcase_create("wolfBoot_misc_utils");
    tcase_set_timeout(tcase_wolfBoot_misc_utils, 20);
    tcase_add_test(tcase_wolfBoot_misc_utils, test_wolfBoot_misc_utils);
    suite_add_tcase(s, tcase_wolfBoot_misc_utils);
    
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
