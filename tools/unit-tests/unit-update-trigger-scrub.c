/* unit-update-trigger-scrub.c
 *
 * Regression test for F-11037: under NVM_FLASH_WRITEONCE,
 * wolfBoot_update_trigger() stages a whole flash sector into the
 * file-scope NVM_CACHE before rewriting the update partition flags.
 * In EXT_ENCRYPTED builds that sector is where the firmware
 * key/nonce live (ENCRYPT_CACHE aliases NVM_CACHE, and with
 * FLAGS_HOME the update flags sit in the boot trailer), so after an
 * update trigger the plaintext key material sat in the buffer until
 * the next use. The partition-trailer helpers scrub with
 * nvm_cache_scrub() (F-9765); the write-once update path did not.
 *
 * The real function is extracted by the Makefile (together with
 * nvm_cache_scrub()) and run with a test-owned NVM_CACHE, a staged
 * sector carrying a key pattern and stubbed flash calls; the buffer
 * must be zero after the call.
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

#include <check.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RAMFUNCTION
#define WOLFBOOT_SECTOR_SIZE 4096
#define NVM_CACHE_SIZE WOLFBOOT_SECTOR_SIZE
#define XMEMCPY(a, b, n) memcpy((a), (b), (n))
#define PART_UPDATE 0
#define IMG_STATE_UPDATING 0x8F
#define WOLFBOOT_MAGIC_TRAIL 0x0000DEAD
#define SECTOR_FLAGS_SIZE 5
#define FLAGS_UPDATE_EXT() 0

/* The buffer under test (real: file-scope in src/libwolfboot.c). */
static uint8_t NVM_CACHE[NVM_CACHE_SIZE];

/* The staged sector image: the boot/update trailer sector carrying
 * the firmware key/nonce pattern at a fixed offset.  Sector-aligned:
 * wolfBoot_update_trigger() derives the staged sector by rounding the
 * flag address down to a sector boundary, and the sector copy must
 * stay inside this array. */
#define KEY_OFF 0x0F00
#define KEY_LEN 64
static uint8_t g_sector[NVM_CACHE_SIZE]
    __attribute__((aligned(WOLFBOOT_SECTOR_SIZE)));

/* The update partition flags end at the top of the staged sector, so
 * lastSector in wolfBoot_update_trigger() resolves to the base of
 * g_sector (the alignment above makes that true by construction). */
#define PART_UPDATE_ENDFLAGS ((uintptr_t)(g_sector + WOLFBOOT_SECTOR_SIZE))

/* Stubbed flash layer: records calls. */
static int g_flash_writes;
static int g_flash_erases;

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    g_flash_writes++;
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address; (void)len;
    g_flash_erases++;
    return 0;
}

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

/* External-flash stubs: not taken (FLAGS_UPDATE_EXT() == 0) but
 * referenced, so they must link. */
void ext_flash_unlock(void)
{
}

void ext_flash_lock(void)
{
}

void ext_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
}

int nvm_select_fresh_sector(int part)
{
    (void)part;
    return 0;
}

int wolfBoot_set_partition_state(uint8_t part, uint8_t newst)
{
    (void)part; (void)newst;
    return 0;
}

/* The real functions from src/libwolfboot.c (extracted by the
 * Makefile). */
#include "update_trigger_scrub_extract.h"

static int cache_scrubbed(void)
{
    int i;

    for (i = 0; i < NVM_CACHE_SIZE; i++)
        if (NVM_CACHE[i] != 0)
            return 0;
    return 1;
}

static void setup(void)
{
    memset(g_sector, 0x11, sizeof(g_sector));
    memset(g_sector + KEY_OFF, 0xA5, KEY_LEN); /* key/nonce pattern */
    memset(NVM_CACHE, 0, sizeof(NVM_CACHE));
    g_flash_writes = 0;
    g_flash_erases = 0;
}

static void teardown(void)
{
}

/* wolfBoot_update_trigger() must leave NVM_CACHE scrubbed: the
 * write-once path staged the whole trailer sector (key included)
 * before rewriting the flags. Pre-fix the staged pattern remained. */
START_TEST(test_update_trigger_scrubs_cache)
{
    ck_assert_int_eq(cache_scrubbed(), 1);

    wolfBoot_update_trigger();

    /* the write-once path wrote the fresh flags sector and erased
     * both candidate sectors */
    ck_assert_int_eq(g_flash_writes, 1);
    ck_assert_int_eq(g_flash_erases, 2);
    ck_assert_int_eq(cache_scrubbed(), 1);
}
END_TEST

Suite *update_trigger_scrub_suite(void)
{
    Suite *s = suite_create("update-trigger-scrub");
    TCase *tc = tcase_create("update-trigger-scrub");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_update_trigger_scrubs_cache);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = update_trigger_scrub_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
