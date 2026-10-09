/* unit-zynqmp-puf.c
 *
 * Coverage for the ZynqMP PUF regeneration gate and the black-key unwrap.
 * Both are MMIO-bound, so the HAL's pmu_mmio_* accessors and the CSU AES
 * entry points are stubbed here and the functions under test are extracted
 * verbatim from hal/zynq.c by the Makefile.
 *
 * What matters, and why:
 *  - csu_puf_regeneration() must refuse when the eFuse CHASH reads zero. The
 *    CSU regenerates from helper data it reads itself, so on a part that was
 *    never registered a bare REGENERATION command would appear to succeed
 *    while leaving an unrelated key selected.
 *  - csu_puf_black_key_unwrap() routes the decrypted key into the KUP before
 *    the GCM tag is checked, so on any failure the KUP holds unauthenticated
 *    key material and must be zeroized rather than left selectable.
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
#include <stddef.h>
#include <string.h>

/* The CHASH gate is a direct eFuse read, so it only exists in the FSBL
 * variant. PUF_TEST_NO_FSBL builds the other branch, which has no gate. */
#ifndef PUF_TEST_NO_FSBL
#define WOLFBOOT_ZYNQMP_FSBL
#endif
#define WOLFBOOT_ZYNQMP_FSBL_SEC

#include "hal/zynq.h"

/* ---- fake register file ------------------------------------------------ */
#define FAKE_REGS 32
static struct { uint32_t addr; uint32_t val; int used; } fake[FAKE_REGS];
static uint32_t efuse_chash;     /* value the eFuse CHASH cache returns */
static int aes_ret;              /* what the stubbed CSU AES returns */
static int aes_calls;
static int key_zero_calls;
static int last_key_src;
static int last_enc;
static const uint8_t* last_iv;
static const uint8_t* last_in;
static uint32_t last_sz;
static uint32_t kup_wr_at_aes;   /* routing state while the decrypt ran */

static void fake_reset(void)
{
    memset(fake, 0, sizeof(fake));
    efuse_chash = 0;
    aes_ret = 0;
    aes_calls = 0;
    key_zero_calls = 0;
    last_key_src = -1;
    last_enc = -1;
    last_iv = NULL;
    last_in = NULL;
    last_sz = 0;
    kup_wr_at_aes = 0xFFFFFFFFU;
}

static uint32_t *fake_slot(uint32_t addr)
{
    int i;
    for (i = 0; i < FAKE_REGS; i++) {
        if (fake[i].used && fake[i].addr == addr) {
            return &fake[i].val;
        }
    }
    for (i = 0; i < FAKE_REGS; i++) {
        if (!fake[i].used) {
            fake[i].used = 1;
            fake[i].addr = addr;
            fake[i].val = 0;
            return &fake[i].val;
        }
    }
    return NULL;
}

static uint32_t fake_get(uint32_t addr)
{
    uint32_t *p = fake_slot(addr);
    return (p != NULL) ? *p : 0;
}

/* ---- stubs the extracted code calls ------------------------------------ */
uint32_t pmu_mmio_read(uint32_t addr)
{
    if (addr == ZYNQMP_EFUSE_PUF_CHASH) {
        return efuse_chash;
    }
    return fake_get(addr);
}

uint32_t pmu_mmio_write(uint32_t addr, uint32_t val)
{
    uint32_t *p = fake_slot(addr);
    if (p != NULL) {
        *p = val;
    }
    return 0;
}

void hal_delay_ms(uint32_t ms) { (void)ms; }

int csu_aes_ex(int enc, const uint8_t* iv, const uint8_t* in, uint8_t* out,
    uint32_t sz, int keySrc, const uint8_t* kupKey)
{
    (void)out; (void)kupKey;
    aes_calls++;
    last_key_src = keySrc;
    last_enc = enc;
    last_iv = iv;
    last_in = in;
    last_sz = sz;
    /* Routing has to be armed while the decrypt runs; sampling it only after
     * the call cannot tell an armed register from the trailing clear. */
    kup_wr_at_aes = fake_get(CSU_AES_KUP_WR);
    /* The hardware routes plaintext into the KUP as it is produced, whether
     * or not the tag later verifies. */
    return aes_ret;
}

int csu_aes_key_zero(void)
{
    key_zero_calls++;
    return 0;
}

static int printf_calls;
#define wolfBoot_printf(...) do { printf_calls++; } while (0)
static void wc_ForceZero(void *p, unsigned long n) { memset(p, 0, (size_t)n); }

/* Mirrored from the wolfBoot build; the extracted code aligns its DMA sink. */
#ifndef XALIGNED
#define XALIGNED(x) __attribute__((aligned(x)))
#endif

#include "puf_extract.h"

START_TEST(test_regeneration_requires_provisioned_part){
    fake_reset();
    efuse_chash = 0;
    printf_calls = 0;

#ifdef WOLFBOOT_ZYNQMP_FSBL
    /* Nothing to regenerate from: refuse, and do not issue the command */
    ck_assert_int_ne(csu_puf_regeneration(), 0);
    ck_assert_uint_eq(fake_get(CSU_PUF_CMD), 0);
    ck_assert_int_gt(printf_calls, 0);
#else
    /* No eFuse read outside the FSBL, so there is no gate to trip */
    ck_assert_int_eq(csu_puf_regeneration(), 0);
    ck_assert_uint_eq(fake_get(CSU_PUF_CMD), CSU_PUF_CMD_REGENERATION);
#endif

    /* A registered part proceeds and issues REGENERATION */
    fake_reset();
    efuse_chash = 0xC3242C2BU;
    ck_assert_int_eq(csu_puf_regeneration(), 0);
    ck_assert_uint_eq(fake_get(CSU_PUF_CMD), CSU_PUF_CMD_REGENERATION);
}
END_TEST

START_TEST(test_unwrap_rejects_bad_arguments){
    uint8_t blob[CSU_AES_KEY_SZ + CSU_AES_GCM_TAG_SZ];
    uint8_t iv[CSU_AES_IV_SZ];

    memset(blob, 0xA5, sizeof(blob));
    memset(iv, 0x5A, sizeof(iv));

    fake_reset();
    efuse_chash = 0xC3242C2BU;
    ck_assert_int_eq(csu_puf_black_key_unwrap(NULL, iv), -1);
    ck_assert_int_eq(csu_puf_black_key_unwrap(blob, NULL), -1);
    /* refused before anything reached the controller */
    ck_assert_int_eq(aes_calls, 0);
    ck_assert_uint_eq(fake_get(CSU_PUF_CMD), 0);
}
END_TEST

START_TEST(test_unwrap_refuses_unprovisioned_part){
    uint8_t blob[CSU_AES_KEY_SZ + CSU_AES_GCM_TAG_SZ];
    uint8_t iv[CSU_AES_IV_SZ];

    memset(blob, 0xA5, sizeof(blob));
    memset(iv, 0x5A, sizeof(iv));

    fake_reset();
    efuse_chash = 0;
#ifdef WOLFBOOT_ZYNQMP_FSBL
    ck_assert_int_ne(csu_puf_black_key_unwrap(blob, iv), 0);
    /* no decrypt attempted, so nothing was ever routed to the KUP */
    ck_assert_int_eq(aes_calls, 0);
    ck_assert_uint_eq(fake_get(CSU_AES_KUP_WR), 0);
#else
    /* Ungated: the unwrap runs and still routes through the KUP */
    ck_assert_int_eq(csu_puf_black_key_unwrap(blob, iv), 0);
    ck_assert_int_eq(aes_calls, 1);
    ck_assert_uint_eq(kup_wr_at_aes,
        (uint32_t)(CSU_AES_KUP_WR_KEY | CSU_AES_KUP_WR_IV));
#endif
}
END_TEST

START_TEST(test_unwrap_routes_to_kup_and_clears){
    uint8_t blob[CSU_AES_KEY_SZ + CSU_AES_GCM_TAG_SZ];
    uint8_t iv[CSU_AES_IV_SZ];

    memset(blob, 0xA5, sizeof(blob));
    memset(iv, 0x5A, sizeof(iv));

    fake_reset();
    efuse_chash = 0xC3242C2BU;
    aes_ret = 0;

    ck_assert_int_eq(csu_puf_black_key_unwrap(blob, iv), 0);
    ck_assert_int_eq(aes_calls, 1);
    /* the wrapping key is the device key, which is where the KEK lands */
    ck_assert_int_eq(last_key_src, CSU_AES_KEY_SRC_DEVICE_KEY);
    /* the recovered key must go to the KUP rather than the memory sink */
    ck_assert_uint_eq(kup_wr_at_aes,
        (uint32_t)(CSU_AES_KUP_WR_KEY | CSU_AES_KUP_WR_IV));
    /* decrypt the caller's blob under the caller's IV, one key length */
    ck_assert_int_eq(last_enc, CSU_AES_CFG_DEC);
    ck_assert_ptr_eq((const void*)last_iv, (const void*)iv);
    ck_assert_ptr_eq((const void*)last_in, (const void*)blob);
    ck_assert_uint_eq(last_sz, (uint32_t)CSU_AES_KEY_SZ);
    /* routing must not be left enabled */
    ck_assert_uint_eq(fake_get(CSU_AES_KUP_WR), 0);
    /* a successful unwrap must leave the key in the KUP */
    ck_assert_int_eq(key_zero_calls, 0);
}
END_TEST

START_TEST(test_unwrap_zeroizes_kup_on_tag_failure){
    uint8_t blob[CSU_AES_KEY_SZ + CSU_AES_GCM_TAG_SZ];
    uint8_t iv[CSU_AES_IV_SZ];

    memset(blob, 0xA5, sizeof(blob));
    memset(iv, 0x5A, sizeof(iv));

    fake_reset();
    efuse_chash = 0xC3242C2BU;
    aes_ret = -3;   /* GCM tag mismatch */

    ck_assert_int_ne(csu_puf_black_key_unwrap(blob, iv), 0);
    /* The plaintext reached the KUP before the tag was checked, so the
     * unauthenticated key must not be left selectable. */
    ck_assert_int_eq(key_zero_calls, 1);
    ck_assert_uint_eq(fake_get(CSU_AES_KUP_WR), 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot zynqmp puf");
    TCase *tc = tcase_create("puf");

    tcase_add_test(tc, test_regeneration_requires_provisioned_part);
    tcase_add_test(tc, test_unwrap_rejects_bad_arguments);
    tcase_add_test(tc, test_unwrap_refuses_unprovisioned_part);
    tcase_add_test(tc, test_unwrap_routes_to_kup_and_clears);
    tcase_add_test(tc, test_unwrap_zeroizes_kup_on_tag_failure);
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
