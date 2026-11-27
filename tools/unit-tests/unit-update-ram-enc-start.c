/* unit-update-ram-enc-start.c
 *
 * Unit tests for wolfBoot_start() loading an encrypted image from external
 * flash that is not memory mapped (WOLFBOOT_RAM_DECRYPT_EXT).
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
#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#include "target.h"
static unsigned char wolfboot_ram[2 * WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE];

#define WOLFBOOT_LOAD_ADDRESS (((uintptr_t)wolfboot_ram + IMAGE_HEADER_SIZE))

/* Not a multiple of ENCRYPT_BLOCK_SIZE, so the staged read is rounded up */
#define TEST_SIZE 5300
#define TEST_SIZE_PADDED 5312
#define TEST_TOTAL (IMAGE_HEADER_SIZE + TEST_SIZE_PADDED)

#include <stdio.h>
#include <stdlib.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include "fdt.h"
static uint8_t dts_stage[WOLFBOOT_DTS_MAX_SIZE];
#define WOLFBOOT_LOAD_DTS_ADDRESS ((uintptr_t)dts_stage)

void* hal_get_dts_address(void)
{
    return NULL;
}

int hal_dts_fixup(void *dts_addr, uint32_t capacity)
{
    (void)dts_addr;
    (void)capacity;
    return 0;
}

/* The test images are raw binaries, not FIT */
int fdt_open(fdt_ctx* ctx, void* blob, uint32_t capacity)
{
    (void)ctx;
    (void)blob;
    (void)capacity;
    return -1;
}

uint32_t fdt_size(const fdt_ctx* ctx)
{
    (void)ctx;
    return 0;
}

int fdt_peek_size(const void* hdr, uint32_t hdr_len, uint32_t* totalsize)
{
    (void)hdr;
    (void)hdr_len;
    (void)totalsize;
    return -1;
}

void fdt_set_dtb_authenticated(int authenticated)
{
    (void)authenticated;
}

const char* fit_find_images(fdt_ctx* ctx, const char** pkernel,
    const char** pflat_dt, const char** pramdisk, const char** pfpga)
{
    (void)ctx;
    (void)pkernel;
    (void)pflat_dt;
    (void)pramdisk;
    (void)pfpga;
    return NULL;
}

void* fit_load_image(fdt_ctx* ctx, const char* image, int* lenp)
{
    (void)ctx;
    (void)image;
    (void)lenp;
    return NULL;
}

void* fit_load_kernel(fdt_ctx* ctx, const char* kernel_node, int* lenp)
{
    (void)ctx;
    (void)kernel_node;
    (void)lenp;
    return NULL;
}

/* image.c needs the device tree partitions; the loader under test has none */
#undef WOLFBOOT_DTS_BOOT_ADDRESS
#undef WOLFBOOT_DTS_UPDATE_ADDRESS
#include "update_ram.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#define ext_flash_read mock_ext_flash_read
#include "unit-mock-flash.c"
#undef ext_flash_read
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

const char *argv0;

Suite *wolfboot_suite(void);

/* Reads outside the two partitions are refused and counted: the flash is not
 * memory mapped, so a RAM address is never a valid flash offset. */
static int mock_bad_reads = 0;

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (!((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS &&
           address < WOLFBOOT_PARTITION_BOOT_ADDRESS +
               WOLFBOOT_PARTITION_SIZE) ||
          (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS &&
           address < WOLFBOOT_PARTITION_UPDATE_ADDRESS +
               WOLFBOOT_PARTITION_SIZE))) {
        mock_bad_reads++;
        return -1;
    }
    return mock_ext_flash_read(address, data, len);
}

static uint8_t plain_img[2][TEST_TOTAL];
static uint8_t enc_img[2][TEST_TOTAL];
static int wolfBoot_staged_ok = 0;
static int staged_part = -1;

void do_boot(const uint32_t *address, const uint32_t *dts)
{
    (void)dts;
    if (wolfBoot_panicked)
        return;
    wolfBoot_staged_ok++;
    staged_part = wolfBoot_ramboot_part();
    ck_assert_uint_eq((uintptr_t)address, WOLFBOOT_LOAD_ADDRESS);
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

static void prepare_flash(void)
{
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    unsigned int i;
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin",
            (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin",
            (void *)(uintptr_t)MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ext_flash_unlock();
    ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_lock();

    for (i = 0; i < ENCRYPT_KEY_SIZE; i++)
        key[i] = (uint8_t)(i + 1);
    for (i = 0; i < ENCRYPT_NONCE_SIZE; i++)
        nonce[i] = (uint8_t)(0xA0 + i);
    encrypt_initialized = 0;
    ck_assert_int_eq(wolfBoot_set_encrypt_key(key, nonce), 0);
    ck_assert_int_eq(crypto_init(), 0);

    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
    staged_part = -1;
    mock_bad_reads = 0;
    memset(wolfboot_ram, 0, sizeof(wolfboot_ram));
}

static void cleanup_flash(void)
{
    munmap((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    munmap((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
}

#define DIGEST_TLV_OFF_IN_HDR 28
/* Build a hashed image, encrypt it the way the sign tool does and write the
 * ciphertext to the partition. */
static void add_enc_payload(uint8_t part, uint32_t version)
{
    uint8_t *plain = plain_img[part];
    uint8_t *enc = enc_img[part];
    uintptr_t base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t word;
    uint16_t word16;
    uint32_t size = TEST_SIZE;
    uint32_t off;
    wc_Sha256 sha;
    int i;

    if (part == PART_UPDATE)
        base = WOLFBOOT_PARTITION_UPDATE_ADDRESS;

    memset(plain, 0xFF, TEST_TOTAL);
    memcpy(plain, "WOLF", 4);
    memcpy(plain + 4, &size, 4);
    word = 4 << 16 | HDR_VERSION;
    memcpy(plain + 8, &word, 4);
    memcpy(plain + 12, &version, 4);
    word = 2 << 16 | HDR_IMG_TYPE;
    memcpy(plain + 16, &word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    memcpy(plain + 20, &word16, 2);
    for (i = 0; i < TEST_SIZE; i++)
        plain[IMAGE_HEADER_SIZE + i] = (uint8_t)(i * 7 + 3 + part);

    ck_assert_int_eq(wc_InitSha256_ex(&sha, NULL, INVALID_DEVID), 0);
    ck_assert_int_eq(wc_Sha256Update(&sha, plain, DIGEST_TLV_OFF_IN_HDR), 0);
    ck_assert_int_eq(wc_Sha256Update(&sha, plain + IMAGE_HEADER_SIZE,
            TEST_SIZE), 0);
    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    memcpy(plain + DIGEST_TLV_OFF_IN_HDR, &word, 4);
    ck_assert_int_eq(wc_Sha256Final(&sha,
            plain + DIGEST_TLV_OFF_IN_HDR + 4), 0);
    wc_Sha256Free(&sha);

    for (off = 0; off < TEST_TOTAL; off += ENCRYPT_BLOCK_SIZE) {
        wolfBoot_crypto_set_iv(encrypt_iv_nonce, off / ENCRYPT_BLOCK_SIZE);
        crypto_encrypt(enc + off, plain + off, ENCRYPT_BLOCK_SIZE);
    }

    ext_flash_unlock();
    ext_flash_write(base, enc, TEST_TOTAL);
    ext_flash_lock();
}

static void tamper_flash(uint8_t part, uint32_t off)
{
    uint8_t *base = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint8_t b;

    if (part == PART_UPDATE)
        base = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    b = base[off] ^ 0x01;
    ext_flash_unlock();
    ext_flash_write((uintptr_t)base + off, &b, 1);
    ext_flash_lock();
}

START_TEST(test_enc_ext_start_valid)
{
    prepare_flash();
    add_enc_payload(PART_BOOT, 1);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(wolfBoot_staged_ok, 1);
    ck_assert_int_eq(staged_part, PART_BOOT);
    ck_assert_int_eq(mock_bad_reads, 0);
    ck_assert_mem_eq(wolfboot_ram, plain_img[PART_BOOT],
            IMAGE_HEADER_SIZE + TEST_SIZE);
    /* The ciphertext in flash is left as written */
    ck_assert_mem_eq((uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS,
            enc_img[PART_BOOT], TEST_TOTAL);
    cleanup_flash();
}
END_TEST

START_TEST(test_enc_ext_start_tampered_rejected)
{
    prepare_flash();
    add_enc_payload(PART_BOOT, 1);
    tamper_flash(PART_BOOT, IMAGE_HEADER_SIZE + 100);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_staged_ok, 0);
    ck_assert_int_ne(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_bad_reads, 0);
    cleanup_flash();
}
END_TEST

START_TEST(test_enc_ext_start_tampered_update_falls_back)
{
    prepare_flash();
    add_enc_payload(PART_BOOT, 1);
    add_enc_payload(PART_UPDATE, 2);
    tamper_flash(PART_UPDATE, IMAGE_HEADER_SIZE + 100);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(wolfBoot_staged_ok, 1);
    ck_assert_int_eq(staged_part, PART_BOOT);
    ck_assert_int_eq(mock_bad_reads, 0);
    ck_assert_mem_eq(wolfboot_ram, plain_img[PART_BOOT],
            IMAGE_HEADER_SIZE + TEST_SIZE);
    cleanup_flash();
}
END_TEST

/* The slots stay encrypted, so confirming an image must leave the key in
 * place for the next boot. */
START_TEST(test_enc_ext_start_boots_again_after_confirm)
{
    uint8_t st = 0;

    prepare_flash();
    add_enc_payload(PART_BOOT, 1);

    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_staged_ok, 1);

    wolfBoot_success_part(PART_BOOT);
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_BOOT, &st), 0);
    ck_assert_uint_eq(st, IMG_STATE_SUCCESS);

    /* Next boot: the cipher is set up again from the stored key */
    encrypt_initialized = 0;
    memset(wolfboot_ram, 0, sizeof(wolfboot_ram));
    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(wolfBoot_staged_ok, 2);
    ck_assert_int_eq(mock_bad_reads, 0);
    ck_assert_mem_eq(wolfboot_ram, plain_img[PART_BOOT],
            IMAGE_HEADER_SIZE + TEST_SIZE);
    cleanup_flash();
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-ram-decrypt-start");
    TCase *tc = tcase_create("encrypted ext flash RAM loader");

    tcase_add_test(tc, test_enc_ext_start_valid);
    tcase_add_test(tc, test_enc_ext_start_tampered_rejected);
    tcase_add_test(tc, test_enc_ext_start_tampered_update_falls_back);
    tcase_add_test(tc, test_enc_ext_start_boots_again_after_confirm);
    tcase_set_timeout(tc, 5);
    suite_add_tcase(s, tc);
    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;

    (void)argc;
    argv0 = strdup(argv[0]);
    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
