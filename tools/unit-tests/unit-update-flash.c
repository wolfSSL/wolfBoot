/* unit-update-flash.c
 *
 * unit tests for update procedures in update_flash.c
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
#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#define TEST_SIZE_SMALL 5300
#define TEST_SIZE_LARGE 9800

#define NO_FORK 0 /* Set to 1 to disable fork mode (e.g. for gdb debugging) */

#include <stdio.h>
#include <stdlib.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include "update_flash.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

const char *argv0;

static void reset_mock_stats(void);
static void prepare_flash(void);
static void cleanup_flash(void);
static int add_payload_type(uint8_t part, uint32_t version, uint32_t size,
    uint16_t img_type);

#ifdef CUSTOM_ENCRYPT_KEY
int wolfBoot_get_encrypt_key(uint8_t *k, uint8_t *nonce)
{
    int i;
    for (i = 0; i < ENCRYPT_KEY_SIZE; i++) {
        k[i] = (uint8_t)(i + 1);
    }
    for (i = 0; i < ENCRYPT_NONCE_SIZE; i++) {
        nonce[i] = (uint8_t)(0xA5 + i);
    }
    return 0;
}

int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce)
{
    (void)key;
    (void)nonce;
    return 0;
}

int wolfBoot_erase_encrypt_key(void)
{
    return 0;
}
#endif

#ifndef UNIT_TEST_SELF_UPDATE_ONLY
START_TEST (test_boot_success_sets_state)
{
    uint8_t state = 0;

    reset_mock_stats();
    prepare_flash();
    hal_flash_unlock();
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
    hal_flash_lock();

    wolfBoot_success();

    ck_assert_int_eq(wolfBoot_get_partition_state(PART_BOOT, &state), 0);
    ck_assert_uint_eq(state, IMG_STATE_SUCCESS);

    cleanup_flash();
}
END_TEST
#endif

Suite *wolfboot_suite(void);

int wolfBoot_staged_ok = 0;
const uint32_t *wolfBoot_stage_address = (uint32_t *) 0xFFFFFFFF;
#ifdef RAM_CODE
static int arch_reboot_called = 0;
unsigned int _start_text = MOCK_ADDRESS_BOOT;
#endif

void do_boot(const uint32_t *address)
{
    /* Mock of do_boot */
#ifndef ARCH_SIM
    if (wolfBoot_panicked)
        return;
#endif
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    printf("Called do_boot with address %p\n", address);
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

static void reset_mock_stats(void)
{
    wolfBoot_staged_ok = 0;
#ifndef ARCH_SIM
    wolfBoot_panicked = 0;
#endif
    erased_boot = 0;
    erased_update = 0;
    erased_swap = 0;
    erased_nvm_bank0 = 0;
    erased_nvm_bank1 = 0;
    erased_vault = 0;
    ext_flash_reset_lock();
#ifdef RAM_CODE
    arch_reboot_called = 0;
#endif
}

static void clear_erase_stats(void)
{
    erased_boot = 0;
    erased_update = 0;
    erased_swap = 0;
    erased_nvm_bank0 = 0;
    erased_nvm_bank1 = 0;
    erased_vault = 0;
}


static void prepare_flash(void)
{
    int ret;
    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin", (void *)MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin", (void *)MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-swap.bin", (void *)MOCK_ADDRESS_SWAP,
            WOLFBOOT_SECTOR_SIZE, NULL);
    ck_assert(ret >= 0);
    hal_flash_unlock();
    hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    hal_flash_lock();
}

static void cleanup_flash(void)
{
    munmap((void *)MOCK_ADDRESS_UPDATE, WOLFBOOT_PARTITION_SIZE);
    munmap((void *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE);
    munmap((void *)MOCK_ADDRESS_SWAP, WOLFBOOT_SECTOR_SIZE);
}


#define DIGEST_TLV_OFF_IN_HDR 28
static int add_payload(uint8_t part, uint32_t version, uint32_t size)
{
    return add_payload_type(part, version, size,
            HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP);
}

static int add_payload_type(uint8_t part, uint32_t version, uint32_t size,
    uint16_t img_type)
{
    uint32_t word;
    int i;
    uint8_t *base = (uint8_t *)(uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;


    if (part == PART_UPDATE)
        base = (uint8_t *)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    srandom(part); /* Ensure reproducible "random" image */


    hal_flash_unlock();
    hal_flash_write((uintptr_t)base, "WOLF", 4);
    printf("Written magic: \"WOLF\"\n");

    hal_flash_write((uintptr_t)base + 4, (void *)&size, 4);
    printf("Written size: %u\n", size);

    /* Headers */
    word = 4 << 16 | HDR_VERSION;
    hal_flash_write((uintptr_t)base + 8, (void *)&word, 4);
    hal_flash_write((uintptr_t)base + 12, (void *)&version, 4);
    printf("Written version: %u\n", version);

    word = 2 << 16 | HDR_IMG_TYPE;
    hal_flash_write((uintptr_t)base + 16, (void *)&word, 4);
    hal_flash_write((uintptr_t)base + 20, (void *)&img_type, 2);
    printf("Written img_type: %04X\n", img_type);

    /* Add 28B header to sha calculation */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    /* Payload */
    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < size; i+=4) {
        uint32_t word = (random() << 16) | random();
        hal_flash_write((uintptr_t)base + i, (void *)&word, 4);
    }
    for (i = IMAGE_HEADER_SIZE; i < size; i+= WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((size - i) < len)
            len = size - i;
        ret = wc_Sha256Update(&sha, base + i, len);
        if (ret != 0)
            return ret;
    }

    /* Calculate final digest */
    ret = wc_Sha256Final(&sha, digest);
    if (ret != 0)
        return ret;
    wc_Sha256Free(&sha);

    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    hal_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR, (void *)&word, 4);
    hal_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
            SHA256_DIGEST_SIZE);
    printf("SHA digest written\n");
    for (i = 0; i < 32; i++) {
        printf("%02x ", digest[i]);
    }
    printf("\n");
    hal_flash_lock();

}

#ifdef RAM_CODE
void arch_reboot(void)
{
    arch_reboot_called++;
}

START_TEST (test_self_update_sameversion_erased)
{
    reset_mock_stats();
    prepare_flash();
    clear_erase_stats();
    add_payload_type(PART_UPDATE, WOLFBOOT_VERSION, TEST_SIZE_SMALL,
        HDR_IMG_TYPE_WOLFBOOT | HDR_IMG_TYPE_AUTH);
    ext_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
    ext_flash_lock();

    wolfBoot_check_self_update();

    ck_assert_int_eq(arch_reboot_called, 0);
    ck_assert_int_ge(erased_update, 1);
    ck_assert_uint_eq(*(uint32_t *)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        0xFFFFFFFFu);
    cleanup_flash();
}
END_TEST

START_TEST (test_self_update_oldversion_erased)
{
    reset_mock_stats();
    prepare_flash();
    clear_erase_stats();
    add_payload_type(PART_UPDATE, WOLFBOOT_VERSION - 1, TEST_SIZE_SMALL,
        HDR_IMG_TYPE_WOLFBOOT | HDR_IMG_TYPE_AUTH);
    ext_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
    ext_flash_lock();

    wolfBoot_check_self_update();

    ck_assert_int_eq(arch_reboot_called, 0);
    ck_assert_int_ge(erased_update, 1);
    ck_assert_uint_eq(*(uint32_t *)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        0xFFFFFFFFu);
    cleanup_flash();
}
END_TEST

START_TEST (test_self_update_newversion_invalid_integrity_denied)
{
    uint8_t bad_digest[SHA256_DIGEST_SIZE];

    reset_mock_stats();
    prepare_flash();
    clear_erase_stats();
    add_payload_type(PART_UPDATE, WOLFBOOT_VERSION + 1, TEST_SIZE_SMALL,
        HDR_IMG_TYPE_WOLFBOOT | HDR_IMG_TYPE_AUTH);
    memset(bad_digest, 0xBA, sizeof(bad_digest));
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4,
        bad_digest, sizeof(bad_digest));
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
    ext_flash_lock();

    wolfBoot_check_self_update();

    ck_assert_int_eq(arch_reboot_called, 0);
    ck_assert_int_eq(erased_update, 0);
    ck_assert_uint_eq(*(uint32_t *)(uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS,
        0xFFFFFFFFu);
    cleanup_flash();
}
END_TEST

START_TEST (test_self_update_newversion_copies_and_reboots)
{
    reset_mock_stats();
    prepare_flash();
    clear_erase_stats();
    add_payload_type(PART_UPDATE, WOLFBOOT_VERSION + 1, TEST_SIZE_SMALL,
        HDR_IMG_TYPE_WOLFBOOT | HDR_IMG_TYPE_AUTH);
    ext_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
    ext_flash_lock();

    wolfBoot_check_self_update();

    ck_assert_int_eq(arch_reboot_called, 1);
    ck_assert_int_ge(erased_boot, 1);
    ck_assert_mem_eq((const void *)(uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS,
        (const void *)(uintptr_t)(WOLFBOOT_PARTITION_UPDATE_ADDRESS + IMAGE_HEADER_SIZE),
        FLASHBUFFER_SIZE);
    cleanup_flash();
}
END_TEST
#endif

#ifndef UNIT_TEST_SELF_UPDATE_ONLY
#ifdef EXT_ENCRYPTED
static int build_image_buffer(uint8_t part, uint32_t version, uint32_t size,
    uint8_t *buf, uint32_t buf_sz)
{
    uint32_t word;
    uint16_t word16;
    uint32_t total = size + IMAGE_HEADER_SIZE;
    int i;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    if (buf_sz < total)
        return -1;

    memset(buf, 0xFF, buf_sz);

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;

    memcpy(buf, "WOLF", 4);
    memcpy(buf + 4, &size, 4);

    word = 4 << 16 | HDR_VERSION;
    memcpy(buf + 8, &word, 4);
    memcpy(buf + 12, &version, 4);

    word = 2 << 16 | HDR_IMG_TYPE;
    memcpy(buf + 16, &word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    memcpy(buf + 20, &word16, 2);

    ret = wc_Sha256Update(&sha, buf, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    srandom(part);
    for (i = IMAGE_HEADER_SIZE; i < (int)total; i += 4) {
        uint32_t rnd = (random() << 16) | random();
        memcpy(buf + i, &rnd, 4);
    }
    for (i = IMAGE_HEADER_SIZE; i < (int)total; i += WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((int)total - i < len)
            len = (int)total - i;
        ret = wc_Sha256Update(&sha, buf + i, len);
        if (ret != 0)
            return ret;
    }

    ret = wc_Sha256Final(&sha, digest);
    if (ret != 0)
        return ret;
    wc_Sha256Free(&sha);

    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    memcpy(buf + DIGEST_TLV_OFF_IN_HDR, &word, 4);
    memcpy(buf + DIGEST_TLV_OFF_IN_HDR + 4, digest, SHA256_DIGEST_SIZE);

    return 0;
}

static int add_payload_encrypted(uint8_t part, uint32_t version, uint32_t size,
    int use_fallback_iv)
{
    uint32_t total = size + IMAGE_HEADER_SIZE;
    uint8_t *buf = NULL;
    uintptr_t base = (uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    int ret;
    int prev = 0;

    if (part == PART_BOOT)
        base = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    else if (part == PART_UPDATE)
        base = (uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    else
        return -1;

    buf = malloc(total);
    if (!buf)
        return -1;

    ret = build_image_buffer(part, version, size, buf, total);
    if (ret != 0) {
        free(buf);
        return ret;
    }

    if (use_fallback_iv)
        prev = wolfBoot_enable_fallback_iv(1);

    ext_flash_unlock();
    {
        uint32_t off = 0;
        while (off < total) {
            uint32_t chunk = total - off;
            if (chunk > WOLFBOOT_SECTOR_SIZE)
                chunk = WOLFBOOT_SECTOR_SIZE;
            if (use_fallback_iv)
                wolfBoot_enable_fallback_iv(1);
            ret = ext_flash_encrypt_write(base + off, buf + off, chunk);
            if (ret != 0)
                break;
            off += chunk;
        }
    }
    ext_flash_lock();

    if (use_fallback_iv)
        wolfBoot_enable_fallback_iv(prev);

    free(buf);
    return ret;
}
#endif

START_TEST (test_empty_panic)
{
    reset_mock_stats();
    prepare_flash();
    wolfBoot_start();
    ck_assert(!wolfBoot_staged_ok);
    ck_assert(wolfBoot_panicked);
    cleanup_flash();

}
END_TEST

#ifdef EXT_ENCRYPTED
START_TEST (test_fallback_image_verification_rejects_corruption)
{
    int ret;
    uint8_t bad = 0x00;

    reset_mock_stats();
    prepare_flash();

    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    ret = add_payload_encrypted(PART_UPDATE, 2, TEST_SIZE_SMALL, 1);
    ck_assert_int_eq(ret, 0);

    ext_flash_unlock();
    ext_flash_write((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS +
        IMAGE_HEADER_SIZE + 16, &bad, 1);
    ext_flash_lock();

    ret = wolfBoot_update(1);
    ck_assert_int_eq(ret, -1);

    cleanup_flash();
}
END_TEST
#endif

START_TEST (test_sunnyday_noupdate)
{
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();

}
END_TEST

START_TEST (test_forward_update_samesize_notrigger) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_samesize) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_tolarger) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_tosmaller) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_LARGE);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_sameversion_denied) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    ck_assert(*(uint32_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4) == TEST_SIZE_SMALL);
    cleanup_flash();
}
END_TEST

START_TEST (test_update_oldversion_denied) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 2);
    ck_assert(*(uint32_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4) == TEST_SIZE_SMALL);
    cleanup_flash();
}

START_TEST (test_invalid_update_type) {
    reset_mock_stats();
    prepare_flash();
    uint16_t word16 = 0xBAAD;
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 20, (void *)&word16, 2);
    ext_flash_lock();
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}

START_TEST (test_invalid_update_auth_type) {
    reset_mock_stats();
    prepare_flash();
    uint16_t word16 = HDR_IMG_TYPE_AUTH_ECC256 | HDR_IMG_TYPE_APP;
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 20, (void *)&word16, 2);
    ext_flash_lock();
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}

START_TEST (test_update_toolarge) {
    uint32_t very_large = WOLFBOOT_PARTITION_SIZE;
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_LARGE);
    /* Change the size in the header to be larger than the actual size */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 4, (void *)&very_large, 4);
    ext_flash_lock();

    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}

START_TEST (test_invalid_sha) {
    uint8_t bad_digest[SHA256_DIGEST_SIZE];
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);

    memset(bad_digest, 0xBA, SHA256_DIGEST_SIZE);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4, bad_digest, SHA256_DIGEST_SIZE);
    ext_flash_lock();
    wolfBoot_update_trigger();
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}

START_TEST (test_emergency_rollback) {
    uint8_t testing_flags[5] = { IMG_STATE_TESTING, 'B', 'O', 'O', 'T' };
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_SMALL);
    /* Set the testing flag in the last five bytes of the BOOT partition */
    hal_flash_unlock();
    hal_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 5,
            testing_flags, 5);
    hal_flash_lock();

    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 1);
    cleanup_flash();
}

START_TEST (test_emergency_rollback_failure_due_to_bad_update) {
    uint8_t testing_flags[5] = { IMG_STATE_TESTING, 'B', 'O', 'O', 'T' };
    uint8_t wrong_update_magic[4] = { 'G', 'O', 'L', 'F' };
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_SMALL);
    /* Set the testing flag in the last five bytes of the BOOT partition */
    hal_flash_unlock();
    hal_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 5,
            testing_flags, 5);
    hal_flash_lock();

    /* Corrupt the update */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS, wrong_update_magic, 4);
    ext_flash_lock();

    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 2);
    cleanup_flash();
}

START_TEST (test_empty_boot_partition_update) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_UPDATE, 5, TEST_SIZE_SMALL);
    wolfBoot_start();
    ck_assert(!wolfBoot_panicked);
    ck_assert(wolfBoot_staged_ok);
    ck_assert(wolfBoot_current_firmware_version() == 5);
    cleanup_flash();
}

START_TEST (test_empty_boot_but_update_sha_corrupted_denied) {
    uint8_t bad_digest[SHA256_DIGEST_SIZE];
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_UPDATE, 5, TEST_SIZE_SMALL);
    memset(bad_digest, 0xBA, SHA256_DIGEST_SIZE);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4, bad_digest, SHA256_DIGEST_SIZE);
    ext_flash_lock();
    wolfBoot_start();
    /* We expect to panic */
    ck_assert(wolfBoot_panicked);
    ck_assert(!wolfBoot_staged_ok);
    cleanup_flash();
}

START_TEST (test_swap_resume_noop)
{
    reset_mock_stats();
    prepare_flash();
    ext_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_NEW);
    ext_flash_lock();
    ck_assert_int_eq(wolfBoot_swap_and_final_erase(1), -1);
    cleanup_flash();
}
END_TEST

START_TEST (test_diffbase_version_reads)
{
    uint32_t word;
    uint32_t version = 0x01020304;
    uint32_t delta_base = 0x33445566;
    uint16_t img_type = HDR_IMG_TYPE_AUTH | HDR_IMG_TYPE_APP;

    reset_mock_stats();
    prepare_flash();

    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            (const uint8_t *)"WOLF", 4);
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 4,
            (const uint8_t *)&version, sizeof(version));

    word = (4u << 16) | HDR_VERSION;
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 8,
            (const uint8_t *)&word, sizeof(word));
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 12,
            (const uint8_t *)&version, sizeof(version));

    word = (2u << 16) | HDR_IMG_TYPE;
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 16,
            (const uint8_t *)&word, sizeof(word));
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 20,
            (const uint8_t *)&img_type, sizeof(img_type));

    word = (4u << 16) | HDR_IMG_DELTA_BASE;
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 24,
            (const uint8_t *)&word, sizeof(word));
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 28,
            (const uint8_t *)&delta_base, sizeof(delta_base));
    ext_flash_lock();

    ck_assert_uint_eq(wolfBoot_get_diffbase_version(PART_UPDATE), delta_base);
    ck_assert_uint_eq(wolfBoot_get_diffbase_version(PART_BOOT), 0);
    ck_assert_uint_eq(wolfBoot_get_image_version(PART_UPDATE), version);
    ck_assert_uint_eq(wolfBoot_get_image_type(PART_UPDATE), img_type);

    cleanup_flash();
}
END_TEST
#endif


Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfboot");

    /* Test cases */
#ifdef UNIT_TEST_SELF_UPDATE_ONLY
#ifdef RAM_CODE
    TCase *self_update_sameversion = tcase_create("Self update same version erased");
    TCase *self_update_oldversion = tcase_create("Self update older version erased");
    TCase *self_update_invalid_integrity = tcase_create("Self update invalid integrity denied");
    TCase *self_update_success = tcase_create("Self update success");

    tcase_add_test(self_update_sameversion, test_self_update_sameversion_erased);
    tcase_add_test(self_update_oldversion, test_self_update_oldversion_erased);
    tcase_add_test(self_update_invalid_integrity, test_self_update_newversion_invalid_integrity_denied);
    tcase_add_test(self_update_success, test_self_update_newversion_copies_and_reboots);

    suite_add_tcase(s, self_update_sameversion);
    suite_add_tcase(s, self_update_oldversion);
    suite_add_tcase(s, self_update_invalid_integrity);
    suite_add_tcase(s, self_update_success);
#endif
    return s;
#else
#ifdef UNIT_TEST_FALLBACK_ONLY
    TCase *fallback_verify = tcase_create("Fallback verify");
#else
    TCase *empty_panic = tcase_create("Empty partition panic test");
    TCase *sunnyday_noupdate =
        tcase_create("Sunny day test with no update available");
    TCase *forward_update_samesize =
        tcase_create("Forward update with same size");
    TCase *forward_update_tolarger =
        tcase_create("Forward update to larger size");
    TCase *forward_update_tosmaller = tcase_create("Forward update to smaller size");
    TCase *forward_update_sameversion_denied =
        tcase_create("Forward update to same version denied");
    TCase *update_oldversion_denied =
        tcase_create("Update to older version denied");
    TCase *invalid_update_type =
        tcase_create("Invalid update type");
    TCase *invalid_update_auth_type =
        tcase_create("Invalid update auth type");
    TCase *update_toolarge = tcase_create("Update too large");
    TCase *invalid_sha = tcase_create("Invalid SHA digest");
    TCase *emergency_rollback = tcase_create("Emergency rollback");
    TCase *emergency_rollback_failure_due_to_bad_update = tcase_create("Emergency rollback failure due to bad update");
    TCase *empty_boot_partition_update = tcase_create("Empty boot partition update");
    TCase *empty_boot_but_update_sha_corrupted_denied = tcase_create("Empty boot partition but update SHA corrupted");
    TCase *swap_resume = tcase_create("Swap resume noop");
    TCase *diffbase_version = tcase_create("Diffbase version lookup");
    TCase *boot_success = tcase_create("Boot success state");
#ifdef RAM_CODE
    TCase *self_update_sameversion = tcase_create("Self update same version erased");
    TCase *self_update_oldversion = tcase_create("Self update older version erased");
    TCase *self_update_invalid_integrity = tcase_create("Self update invalid integrity denied");
    TCase *self_update_success = tcase_create("Self update success");
#endif
#ifdef EXT_ENCRYPTED
    TCase *fallback_verify = tcase_create("Fallback verify");
#endif
#endif


#ifdef UNIT_TEST_FALLBACK_ONLY
#ifdef EXT_ENCRYPTED
    tcase_add_test(fallback_verify, test_fallback_image_verification_rejects_corruption);
    suite_add_tcase(s, fallback_verify);
#endif
    return s;
#else
    tcase_add_test(empty_panic, test_empty_panic);
    tcase_add_test(sunnyday_noupdate, test_sunnyday_noupdate);
    tcase_add_test(forward_update_samesize, test_forward_update_samesize);
    tcase_add_test(forward_update_tolarger, test_forward_update_tolarger);
    tcase_add_test(forward_update_tosmaller, test_forward_update_tosmaller);
    tcase_add_test(forward_update_sameversion_denied, test_forward_update_sameversion_denied);
    tcase_add_test(update_oldversion_denied, test_update_oldversion_denied);
    tcase_add_test(invalid_update_type, test_invalid_update_type);
    tcase_add_test(invalid_update_auth_type, test_invalid_update_auth_type);
    tcase_add_test(update_toolarge, test_update_toolarge);
    tcase_add_test(invalid_sha, test_invalid_sha);
    tcase_add_test(emergency_rollback, test_emergency_rollback);
    tcase_add_test(emergency_rollback_failure_due_to_bad_update, test_emergency_rollback_failure_due_to_bad_update);
    tcase_add_test(empty_boot_partition_update, test_empty_boot_partition_update);
    tcase_add_test(empty_boot_but_update_sha_corrupted_denied, test_empty_boot_but_update_sha_corrupted_denied);
    tcase_add_test(swap_resume, test_swap_resume_noop);
    tcase_add_test(diffbase_version, test_diffbase_version_reads);
    tcase_add_test(boot_success, test_boot_success_sets_state);
#ifdef RAM_CODE
    tcase_add_test(self_update_sameversion, test_self_update_sameversion_erased);
    tcase_add_test(self_update_oldversion, test_self_update_oldversion_erased);
    tcase_add_test(self_update_invalid_integrity, test_self_update_newversion_invalid_integrity_denied);
    tcase_add_test(self_update_success, test_self_update_newversion_copies_and_reboots);
#endif
#ifdef EXT_ENCRYPTED
    tcase_add_test(fallback_verify, test_fallback_image_verification_rejects_corruption);
#endif

    suite_add_tcase(s, empty_panic);
    suite_add_tcase(s, sunnyday_noupdate);
    suite_add_tcase(s, forward_update_samesize);
    suite_add_tcase(s, forward_update_tolarger);
    suite_add_tcase(s, forward_update_tosmaller);
    suite_add_tcase(s, forward_update_sameversion_denied);
    suite_add_tcase(s, update_oldversion_denied);
    suite_add_tcase(s, invalid_update_type);
    suite_add_tcase(s, invalid_update_auth_type);
    suite_add_tcase(s, update_toolarge);
    suite_add_tcase(s, invalid_sha);
    suite_add_tcase(s, emergency_rollback);
    suite_add_tcase(s, emergency_rollback_failure_due_to_bad_update);
    suite_add_tcase(s, empty_boot_partition_update);
    suite_add_tcase(s, empty_boot_but_update_sha_corrupted_denied);
    suite_add_tcase(s, swap_resume);
    suite_add_tcase(s, diffbase_version);
    suite_add_tcase(s, boot_success);
#ifdef RAM_CODE
    suite_add_tcase(s, self_update_sameversion);
    suite_add_tcase(s, self_update_oldversion);
    suite_add_tcase(s, self_update_invalid_integrity);
    suite_add_tcase(s, self_update_success);
#endif
#ifdef EXT_ENCRYPTED
    suite_add_tcase(s, fallback_verify);
#endif
#endif



    return s;
#endif
}


int main(int argc, char *argv[])
{
    int fails;
    argv0 = strdup(argv[0]);
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
#if (NO_FORK == 1)
    srunner_set_fork_status(sr, CK_NOFORK);
#endif
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
