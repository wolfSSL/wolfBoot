/* unit-compressed-install.c
 *
 * unit tests for the execute-in-place compressed installer
 * (wolfBoot_inflate_update in src/update_flash.c), which inflates the update
 * partition into the boot partition a chunk at a time.
 *
 * Covers what the simulator cannot reach directly: that the destination is
 * erased before it is written and written once, that an interrupted install
 * resumes from the recorded sector flags without rewriting what is already
 * there, and that a payload disagreeing with the manifest is refused.
 *
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <zlib.h>

#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT   0xCD000000
#define MOCK_ADDRESS_SWAP   0xCE000000

#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include "update_flash.c"
#include <check.h>
#include "unit-mock-flash.c"

Suite *wolfboot_suite(void);

void do_boot(const uint32_t *address)
{
    (void)address;
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void arch_reboot(void)
{
}

/* ------------------------------------------------------------------------- */
/* Fixtures                                                                  */
/* ------------------------------------------------------------------------- */

#define TEST_WINDOW_BITS WOLFBOOT_COMPRESSED_WINDOW_BITS
#define TEST_PAYLOAD_SZ  (6U * WOLFBOOT_SECTOR_SIZE)

static uint8_t expect[TEST_PAYLOAD_SZ];
static struct wolfBoot_image boot_img, update_img;

/* Deterministic and partly compressible, so the stream carries both
 * literals and back-references. */
static void fill_payload(uint8_t *p, uint32_t len, unsigned seed)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (i % 64 < 48) ? (uint8_t)('a' + (i % 19))
                             : (uint8_t)((seed >> 16) & 0xFF);
    }
}

static uint32_t deflate_window(const uint8_t *in, uint32_t in_len,
    uint8_t *out, uint32_t out_cap, int window_bits)
{
    z_stream zs;

    memset(&zs, 0, sizeof(zs));
    ck_assert_int_eq(deflateInit2(&zs, 9, Z_DEFLATED, 16 + window_bits, 8,
        Z_DEFAULT_STRATEGY), Z_OK);
    zs.next_in = (Bytef *)in;
    zs.avail_in = in_len;
    zs.next_out = out;
    zs.avail_out = out_cap;
    ck_assert_int_eq(deflate(&zs, Z_FINISH), Z_STREAM_END);
    ck_assert_int_eq(deflateEnd(&zs), Z_OK);
    return (uint32_t)zs.total_out;
}

/* Lay a container into the update partition and point the two image structs
 * at the mocked flash. */
static void prepare_ex(uint32_t payload_sz, int window_bits, int long_match)
{
    uint8_t *upd = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    uint32_t clen;

    ck_assert_int_ge(mmap_file("/tmp/wolfboot-unit-ci-update.bin",
        (uint8_t *)MOCK_ADDRESS_UPDATE, WOLFBOOT_PARTITION_SIZE, NULL), 0);
    ck_assert_int_ge(mmap_file("/tmp/wolfboot-unit-ci-boot.bin",
        (uint8_t *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE, NULL), 0);
    hal_flash_unlock();
    hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);

    fill_payload(expect, payload_sz, 1);
    if (long_match) {
        /* Repeat an earlier run far enough back that the match can only be
         * coded as a distance beyond a small window. */
        memcpy(expect + payload_sz - 1024, expect, 1024);
    }
    memset(upd, 0xFF, IMAGE_HEADER_SIZE);
    memcpy(upd, "WOLF", 4);
    clen = deflate_window(expect, payload_sz, upd + IMAGE_HEADER_SIZE,
        WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE, window_bits);

    memset(&update_img, 0, sizeof(update_img));
    update_img.hdr = upd;
    update_img.fw_base = upd + IMAGE_HEADER_SIZE;
    update_img.not_ext = 1;
    update_img.hdr_ok = 1;
    update_img.part = PART_UPDATE;
    update_img.fw_size = clen;

    memset(&boot_img, 0, sizeof(boot_img));
    boot_img.hdr = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    boot_img.fw_base = boot_img.hdr + IMAGE_HEADER_SIZE;
    boot_img.not_ext = 1;
    boot_img.hdr_ok = 1;
    boot_img.part = PART_BOOT;
}

static void prepare(uint32_t payload_sz, int window_bits)
{
    prepare_ex(payload_sz, window_bits, 0);
}

static void cleanup(void)
{
#ifdef NVM_FLASH_ECC
    /* No write that was not a whole, erased write unit, and no partial
     * sector erase, reached the boot partition. */
    ck_assert_int_eq(ecc_violations, 0);
#endif
    hal_flash_lock();
    munmap((void *)MOCK_ADDRESS_UPDATE, WOLFBOOT_PARTITION_SIZE);
    munmap((void *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE);
}

/* ------------------------------------------------------------------------- */

START_TEST(test_install_writes_the_whole_image)
{
    int ret;

    prepare(TEST_PAYLOAD_SZ, TEST_WINDOW_BITS);
    ret = wolfBoot_inflate_update(&update_img, &boot_img, TEST_PAYLOAD_SZ,
        TEST_WINDOW_BITS);
    ck_assert_int_eq(ret, (int)TEST_PAYLOAD_SZ);
    ck_assert_mem_eq((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS, expect,
        TEST_PAYLOAD_SZ);
    cleanup();
}
END_TEST

/* A payload that is not a whole number of chunks still has its tail
 * programmed, rather than being left in the write-combining buffer. */
START_TEST(test_install_writes_a_partial_tail)
{
    uint32_t sz = TEST_PAYLOAD_SZ - 37;
    int ret;

    prepare(sz, TEST_WINDOW_BITS);
    ret = wolfBoot_inflate_update(&update_img, &boot_img, sz,
        TEST_WINDOW_BITS);
    ck_assert_int_eq(ret, (int)sz);
    ck_assert_mem_eq((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS, expect, sz);
    cleanup();
}
END_TEST

/* Sectors recorded as installed by an earlier attempt are inflated again,
 * to rebuild the decoder's window, but must not be written back. */
START_TEST(test_install_resumes_without_rewriting)
{
    uint8_t *dst = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    const uint32_t done = 2U * WOLFBOOT_SECTOR_SIZE;
    uint32_t i;
    int ret;

    prepare(TEST_PAYLOAD_SZ, TEST_WINDOW_BITS);
    /* Put the first two sectors in place and record them, as a completed
     * run would have left them. */
    hal_flash_write((haladdr_t)dst, expect, (int)done);
    for (i = 0; i < (done / WOLFBOOT_SECTOR_SIZE); i++) {
        wolfBoot_set_update_sector_flag((uint16_t)i, SECT_FLAG_UPDATED);
    }
    erased_boot = 0;

    ret = wolfBoot_inflate_update(&update_img, &boot_img, TEST_PAYLOAD_SZ,
        TEST_WINDOW_BITS);
    ck_assert_int_eq(ret, (int)TEST_PAYLOAD_SZ);
    /* The whole image must still be correct... */
    ck_assert_mem_eq(dst, expect, TEST_PAYLOAD_SZ);
    /* ...and the sectors already in place must not have been erased again. */
    ck_assert_int_eq(erased_boot, (int)((TEST_PAYLOAD_SZ - done) /
        WOLFBOOT_SECTOR_SIZE));
    cleanup();
}
END_TEST

/* The gzip trailer binds the inflated length; this binds it to the
 * manifest, so a container whose declared size disagrees is refused. */
START_TEST(test_install_rejects_size_mismatch)
{
    int ret;

    prepare(TEST_PAYLOAD_SZ, TEST_WINDOW_BITS);
    ret = wolfBoot_inflate_update(&update_img, &boot_img,
        TEST_PAYLOAD_SZ - WOLFBOOT_SECTOR_SIZE, TEST_WINDOW_BITS);
    ck_assert_int_lt(ret, 0);
    cleanup();
}
END_TEST

/* Compressed with a window larger than this build's buffer: the back-
 * reference cannot be resolved and the install must fail rather than
 * produce a partly correct image. */
START_TEST(test_install_rejects_oversized_window)
{
    /* The larger window is decoded into the buffer this build sized for
     * TEST_WINDOW_BITS, so the payload has to stay inside that buffer:
     * one and a half windows does, and still puts the repeated run further
     * back than a TEST_WINDOW_BITS decoder can reach. */
    const uint32_t sz = (3U << TEST_WINDOW_BITS) / 2U;
    int ret;

    if (TEST_WINDOW_BITS >= GZIP_SINK_WINDOW_MAX_BITS) {
        /* Nothing larger exists to test against in this build. */
        return;
    }
    prepare_ex(sz, GZIP_SINK_WINDOW_MAX_BITS, 1);
    /* Same stream decodes at the window it was made with. */
    ret = wolfBoot_inflate_update(&update_img, &boot_img, sz,
        GZIP_SINK_WINDOW_MAX_BITS);
    ck_assert_int_eq(ret, (int)sz);
    cleanup();

    prepare_ex(sz, GZIP_SINK_WINDOW_MAX_BITS, 1);
    ret = wolfBoot_inflate_update(&update_img, &boot_img, sz,
        TEST_WINDOW_BITS);
    ck_assert_int_lt(ret, 0);
    cleanup();
}
END_TEST

/* A truncated container must be refused, not installed up to the cut. */
START_TEST(test_install_rejects_truncated_container)
{
    int ret;

    prepare(TEST_PAYLOAD_SZ, TEST_WINDOW_BITS);
    update_img.fw_size -= 16;
    ret = wolfBoot_inflate_update(&update_img, &boot_img, TEST_PAYLOAD_SZ,
        TEST_WINDOW_BITS);
    ck_assert_int_lt(ret, 0);
    cleanup();
}
END_TEST

/* ------------------------------------------------------------------------- */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-compressed-install");
    TCase *tc = tcase_create("install");

    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, test_install_writes_the_whole_image);
    tcase_add_test(tc, test_install_writes_a_partial_tail);
    tcase_add_test(tc, test_install_resumes_without_rewriting);
    tcase_add_test(tc, test_install_rejects_size_mismatch);
    tcase_add_test(tc, test_install_rejects_oversized_window);
    tcase_add_test(tc, test_install_rejects_truncated_container);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    SRunner *sr = srunner_create(wolfboot_suite());
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed ? 1 : 0;
}
