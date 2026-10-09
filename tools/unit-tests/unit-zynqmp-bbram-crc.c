/* unit-zynqmp-bbram-crc.c
 *
 * Known-answer test for the ZynqMP BBRAM key CRC. The BBRAM controller does
 * not allow the programmed AES key to be read back: writing the CRC and
 * checking STS.AES_CRC_PASS is the only verification available, so a wrong
 * CRC makes the controller reject a correct key with no way to tell the two
 * failures apart on hardware.
 *
 * The algorithm is not a CRC32 over the key bytes. A 5-bit row address is
 * folded in after each 32-bit word, with rows counted down from one past the
 * last key word. The vectors below were produced by the Xilinx XilSKey
 * implementation (XilSKey_ZynqMp_Bbram_CrcCalc) that the hardware is known to
 * agree with.
 *
 * hal/zynq.c drags in the full ZynqMP HAL, so the pure helpers are extracted
 * verbatim by the Makefile, with the constants and brick mask they use.
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

#include <check.h>
#include <stdint.h>

/* Constants and the brick mask are extracted from hal/zynq.h by the Makefile
 * rather than mirrored here, so a change to either fails this test. */
#include "bbram_crc_extract.h"

struct bbram_crc_vector {
    uint32_t key[ZYNQMP_BBRAM_KEY_WORDS];
    uint32_t crc;
};

static const struct bbram_crc_vector vectors[] = {
    /* all zero */
    { { 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
        0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U }, 0xC2D22827U },
    /* all ones */
    { { 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU,
        0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU }, 0xFD88EF91U },
    /* sequential bytes 0x00..0x1f */
    { { 0x00010203U, 0x04050607U, 0x08090A0BU, 0x0C0D0E0FU,
        0x10111213U, 0x14151617U, 0x18191A1BU, 0x1C1D1E1FU }, 0x8325DF84U },
    /* NIST SP 800-38A F.5.5 AES-256 key */
    { { 0x603DEB10U, 0x15CA71BEU, 0x2B73AEF0U, 0x857D7781U,
        0x1F352C07U, 0x3B6108D7U, 0x2D9810A3U, 0x0914DFF4U }, 0xE643A8EDU }
};

START_TEST(test_bbram_crc_known_answers){
    unsigned i;

    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        ck_assert_uint_eq(zynqmp_bbram_key_crc(vectors[i].key),
            vectors[i].crc);
    }
}
END_TEST

/* The row address is what distinguishes this from a plain CRC over the key.
 * Swapping two key words keeps the byte multiset identical but changes which
 * row each word is folded with, so the CRC must change. */
START_TEST(test_bbram_crc_row_order_matters){
    uint32_t key[ZYNQMP_BBRAM_KEY_WORDS];
    uint32_t swapped[ZYNQMP_BBRAM_KEY_WORDS];
    unsigned i;

    for (i = 0; i < ZYNQMP_BBRAM_KEY_WORDS; i++) {
        key[i] = vectors[2].key[i];
        swapped[i] = vectors[2].key[i];
    }
    swapped[0] = key[ZYNQMP_BBRAM_KEY_WORDS - 1];
    swapped[ZYNQMP_BBRAM_KEY_WORDS - 1] = key[0];

    ck_assert_uint_ne(zynqmp_bbram_key_crc(key),
        zynqmp_bbram_key_crc(swapped));
}
END_TEST

/* The controller takes the key in reverse word order relative to the hex
 * string it is written as. Vector produced by running the Xilinx XilSKey path
 * (ConvertStringToHexLE then a u32 cast) over the NIST SP 800-38A F.5.5 key,
 * which is what bootgen's aeskeyfile encodes. Getting this wrong still passes
 * the controller CRC check, so only a vector catches it. */
START_TEST(test_bbram_key_word_order){
    static const uint8_t key[32] = {
        0x60,0x3D,0xEB,0x10, 0x15,0xCA,0x71,0xBE,
        0x2B,0x73,0xAE,0xF0, 0x85,0x7D,0x77,0x81,
        0x1F,0x35,0x2C,0x07, 0x3B,0x61,0x08,0xD7,
        0x2D,0x98,0x10,0xA3, 0x09,0x14,0xDF,0xF4
    };
    static const uint32_t expect[ZYNQMP_BBRAM_KEY_WORDS] = {
        0x0914DFF4U, 0x2D9810A3U, 0x3B6108D7U, 0x1F352C07U,
        0x857D7781U, 0x2B73AEF0U, 0x15CA71BEU, 0x603DEB10U
    };
    uint32_t words[ZYNQMP_BBRAM_KEY_WORDS];
    unsigned i;

    zynqmp_bbram_key_words(key, words);
    for (i = 0; i < ZYNQMP_BBRAM_KEY_WORDS; i++) {
        ck_assert_uint_eq(words[i], expect[i]);
    }
    /* CRC the controller accepts for that key, from the same XilSKey path */
    ck_assert_uint_eq(zynqmp_bbram_key_crc(words), 0xD93D3AA2U);
}
END_TEST

/* The brick-class SEC_CTRL fuses must be unreachable at their real
 * programming coordinates (page 0, row 22) -- not at the cache-read offset.
 * Columns are enumerated explicitly so loosening the mask fails here. */
START_TEST(test_efuse_brick_fuse_rejected){
    static const uint32_t protected_cols[] = {
        0, 1,              /* AES_RDLK, AES_WRLK */
        2,                 /* ENC_ONLY */
        5,                 /* JTAG_DIS */
        10,                /* SEC_LOCK */
        11, 12, 13, 14,    /* RSA_EN */
        26, 27, 28,        /* PPK0_WRLK, PPK0_INVLD */
        29, 30, 31         /* PPK1_WRLK, PPK1_INVLD */
    };
    static const uint32_t free_cols[] = { 3, 4, 6, 7, 8, 9, 15, 20, 25 };
    unsigned i;

    /* Pin the coordinates to literals. Asserting only through the macros
     * would let a change of ZYNQMP_EFUSE_ROW_SEC_CTRL to the wrong row pass
     * while leaving the real SEC_CTRL row unprotected. */
    ck_assert_uint_eq(ZYNQMP_EFUSE_PAGE_SEC_CTRL, 0);
    ck_assert_uint_eq(ZYNQMP_EFUSE_ROW_SEC_CTRL, 22);
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(0, 22, 5), 1);
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(0, 21, 5), 0);

    for (i = 0; i < sizeof(protected_cols) / sizeof(protected_cols[0]); i++) {
        ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
            ZYNQMP_EFUSE_ROW_SEC_CTRL, protected_cols[i]), 1);
    }
    for (i = 0; i < sizeof(free_cols) / sizeof(free_cols[0]); i++) {
        ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
            ZYNQMP_EFUSE_ROW_SEC_CTRL, free_cols[i]), 0);
    }
    /* row 22 is the only protected row; row 0 is an ordinary OTP row */
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(0, 0, 5), 0);
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(0, 23, 5), 0);
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(1,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 5), 0);
    /* A column past the register width must not shift out of range */
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 32), 0);
    ck_assert_int_eq(zynqmp_efuse_is_brick_fuse(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 63), 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot zynqmp bbram crc");
    TCase *tc = tcase_create("bbram-crc");

    tcase_add_test(tc, test_bbram_crc_known_answers);
    tcase_add_test(tc, test_bbram_crc_row_order_matters);
    tcase_add_test(tc, test_bbram_key_word_order);
    tcase_add_test(tc, test_efuse_brick_fuse_rejected);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? 0 : 1;
}
