/* unit-nxp-hse.c
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
#include <string.h>

/* A model of the Messaging Unit and of the HSE firmware behind it. */
#define MU          0x40210000UL
#define DESC        0x22C00000UL

static uint32_t mu_status;      /* HSE status, FSR[31:16] */
static uint32_t mu_busy;
static uint32_t mu_tsr;
static uint32_t mu_rsr;
static uint32_t mu_rr[16];
static uint32_t desc_mem[16 * 64];
static uint32_t fake_rsp;
static int fake_answers;
static uint8_t fake_attr[64];
static uint32_t seen_srv, seen_id, seen_len, seen_addr_lo, seen_addr_hi;
static int requests;
static int cleans, invals;
static int barriers, barriers_at_request;
static uint64_t now_us;

static void fake_request(uint32_t ch, uint32_t addr);

static uint32_t fake_rd32(uintptr_t a)
{
    if (a == MU + 0x104)
        return (mu_status << 16) | mu_busy;
    if (a == MU + 0x124)
        return mu_tsr;
    if (a == MU + 0x12C)
        return mu_rsr;
    if (a >= MU + 0x280 && a < MU + 0x2C0) {
        mu_rsr &= ~(1U << ((a - (MU + 0x280)) / 4));
        return mu_rr[(a - (MU + 0x280)) / 4];
    }
    ck_abort_msg("read of unexpected register %lx", (unsigned long)a);
    return 0;
}

static void fake_wr32(uintptr_t a, uint32_t v)
{
    if (a >= DESC && a < DESC + sizeof(desc_mem))
        desc_mem[(a - DESC) / 4] = v;
    else if (a >= MU + 0x200 && a < MU + 0x240)
        fake_request((uint32_t)((a - (MU + 0x200)) / 4), v);
    else
        ck_abort_msg("write of unexpected register %lx", (unsigned long)a);
}

/* Built twice: with 64-bit addresses in the requests (S32G, S32R45) and,
 * with UNIT_NXP_HSE_ADDR32, with 32-bit ones (S32K3). The offsets below are
 * those of NXP's service structures for each. */
#ifndef UNIT_NXP_HSE_ADDR32
#define NXP_HSE_ADDR64
#define T_ADDR_SIZE     8
#define T_FORMAT_RAM    8
#define T_IMPORT_KEY0   12
#define T_IMPORT_KEY1   20
#define T_IMPORT_LEN0   36
#define T_IMPORT_CIPHER 44
#define T_IMPORT_CONT   96
#define T_SMRI_DATA     12
#define T_SMRI_LEN      20
#define T_SMRI_TAG0     24
#define T_SMRI_TAG1     32
#define T_SMRI_TLEN0    40
#define T_SMRI_TLEN1    42
#define T_SMRE_SIZE     12
#define T_SMRE_FLAGS    16
#define T_SMRE_PERIOD   20
#define T_SMRE_KEY      24
#define T_SMRE_SIGN     28
#define T_SMRE_HASH     32
#define T_SMRE_TAG0     44
#define T_SMRE_TAG1     48
#define T_SMRE_DECRYPT  52
#define T_SMRE_VERSION  68
#define T_SMRE_TOTAL    72
#define T_PUBLISH_LEN   12
#define T_PUBLISH_BUF   20
#else
#define NXP_HSE_MU_CHANNELS 4U
#define T_ADDR_SIZE     4
#define T_FORMAT_RAM    4
#define T_IMPORT_KEY0   8
#define T_IMPORT_KEY1   12
#define T_IMPORT_LEN0   20
#define T_IMPORT_CIPHER 28
#define T_IMPORT_CONT   64
#define T_SMRI_DATA     8
#define T_SMRI_LEN      12
#define T_SMRI_TAG0     16
#define T_SMRI_TAG1     20
#define T_SMRI_TLEN0    24
#define T_SMRI_TLEN1    26
#define T_SMRE_SIZE     8
#define T_SMRE_FLAGS    12
#define T_SMRE_PERIOD   16
#define T_SMRE_KEY      20
#define T_SMRE_SIGN     24
#define T_SMRE_HASH     28
#define T_SMRE_TAG0     36
#define T_SMRE_TAG1     40
#define T_SMRE_DECRYPT  44
#define T_SMRE_VERSION  60
#define T_SMRE_TOTAL    64
#define T_PUBLISH_LEN   8
#define T_PUBLISH_BUF   12
#endif

#define NXP_HSE_MU_BASE       MU
#define NXP_HSE_DESC_BASE     DESC
#define NXP_HSE_RD32(a)       fake_rd32((uintptr_t)(a))
#define NXP_HSE_WR32(a, v)    fake_wr32((uintptr_t)(a), (v))
#define NXP_HSE_CLEAN(p, n)   (cleans++)
#define NXP_HSE_INVAL(p, n)   (invals++)
#define NXP_HSE_BARRIER()     (barriers++)

uint64_t hal_get_timer_us(void)
{
    now_us += 1000;
    return now_us;
}

#include "../../hal/nxp_hse.c"

static void fake_request(uint32_t ch, uint32_t addr)
{
    const uint32_t *d = &desc_mem[ch * 64];

    requests++;
    barriers_at_request = barriers;
    ck_assert_uint_eq(addr, DESC + ch * 256);
    seen_srv = d[0];
    ck_assert_uint_eq(d[1], 0);
    seen_id = d[2] & 0xFFFF;
    seen_len = d[3];
    seen_addr_lo = d[4];
    seen_addr_hi = d[5];
    if (!fake_answers)
        return;
    if (fake_rsp == NXP_HSE_RSP_OK && seen_srv == HSE_SRV_GET_ATTR)
        memcpy(hse_attr_buf, fake_attr, seen_len);
    mu_rr[ch] = fake_rsp;
    mu_rsr |= 1U << ch;
}

static void setup(void)
{
    mu_status = NXP_HSE_STATUS_INIT_OK;
    mu_busy = 0;
    mu_tsr = 0xFFFF;
    mu_rsr = 0;
    fake_rsp = NXP_HSE_RSP_OK;
    fake_answers = 1;
    requests = 0;
    cleans = 0;
    invals = 0;
    barriers = 0;
    barriers_at_request = 0;
    memset(desc_mem, 0xA5, sizeof(desc_mem));
    memset(fake_attr, 0, sizeof(fake_attr));
}

START_TEST(test_wait_init)
{
    ck_assert_int_eq(nxp_hse_wait_init(100000), 0);
    mu_status = 0;
    ck_assert_int_eq(nxp_hse_wait_init(100000), -1);
}
END_TEST

START_TEST(test_get_attr)
{
    struct nxp_hse_fw_version ver;
    uint32_t rsp = 0;
    static const uint8_t raw[8] = { 0, 3, 1, 0, 0, 2, 86, 0 };

    memcpy(fake_attr, raw, sizeof(raw));
    ck_assert_int_eq(nxp_hse_get_attr(NXP_HSE_ATTR_FW_VERSION,
        (uint8_t*)&ver, sizeof(ver), &rsp), 0);
    ck_assert_uint_eq(rsp, NXP_HSE_RSP_OK);
    ck_assert_int_eq(requests, 1);
    ck_assert_uint_eq(seen_srv, 0x00A50002);
    ck_assert_uint_eq(seen_id, NXP_HSE_ATTR_FW_VERSION);
    ck_assert_uint_eq(seen_len, sizeof(ver));
    ck_assert_uint_eq(seen_addr_lo, (uint32_t)(uintptr_t)hse_attr_buf);
#ifdef NXP_HSE_ADDR64
    ck_assert_uint_eq(seen_addr_hi,
        (uint32_t)((uint64_t)(uintptr_t)hse_attr_buf >> 32));
#else
    ck_assert_uint_eq(seen_addr_hi, 0);
#endif
    /* the rest of the descriptor slot is cleared, not left stale */
    ck_assert_uint_eq(desc_mem[6], 0);
    ck_assert_uint_eq(desc_mem[63], 0);
    ck_assert_int_eq(cleans, 1);
    ck_assert_int_eq(invals, 1);
    /* the descriptor is complete before the request is signalled */
    ck_assert_int_eq(barriers_at_request, 1);
    ck_assert_uint_eq(ver.soc_type, 3);
    ck_assert_uint_eq(ver.fw_type, 1);
    ck_assert_uint_eq(ver.major, 0);
    ck_assert_uint_eq(ver.minor, 2);
    ck_assert_uint_eq(ver.patch, 86);
    /* the response register was read, so the channel is free again */
    ck_assert_uint_eq(mu_rsr, 0);
}
END_TEST

START_TEST(test_error_response)
{
    uint8_t out[8];
    uint32_t rsp = 0;

    memset(out, 0x5A, sizeof(out));
    memset(fake_attr, 0x77, sizeof(fake_attr));
    fake_rsp = 0xAA55A21C;
    ck_assert_int_eq(nxp_hse_get_attr(12, out, 1, &rsp), -5);
    ck_assert_uint_eq(rsp, 0xAA55A21C);
    ck_assert_uint_eq(out[0], 0x5A);
}
END_TEST

START_TEST(test_no_answer_times_out)
{
    uint8_t out[8];
    uint32_t rsp = 1;

    fake_answers = 0;
    ck_assert_int_eq(nxp_hse_get_attr(1, out, sizeof(out), &rsp), -4);
    ck_assert_uint_eq(rsp, 0);
    ck_assert_int_eq(invals, 1);
}
END_TEST

START_TEST(test_channel_not_idle)
{
    uint8_t out[8];
    uint32_t rsp;

    mu_status = 0;
    ck_assert_int_eq(nxp_hse_get_attr(1, out, sizeof(out), &rsp), -2);
    mu_status = NXP_HSE_STATUS_INIT_OK;
    mu_busy = 1;
    ck_assert_int_eq(nxp_hse_get_attr(1, out, sizeof(out), &rsp), -3);
    mu_busy = 0;
    mu_tsr = 0xFFFE;
    ck_assert_int_eq(nxp_hse_get_attr(1, out, sizeof(out), &rsp), -3);
    mu_tsr = 0xFFFF;
    mu_rsr = 1;
    ck_assert_int_eq(nxp_hse_get_attr(1, out, sizeof(out), &rsp), -3);
    ck_assert_int_eq(requests, 0);
}
END_TEST

START_TEST(test_bad_arguments)
{
    uint8_t d[260];
    uint8_t out[80];
    uint32_t rsp;

    memset(d, 0, sizeof(d));
    ck_assert_int_eq(nxp_hse_request(0, NULL, 16, &rsp), -1);
    ck_assert_int_eq(nxp_hse_request(0, d, 16, NULL), -1);
    ck_assert_int_eq(nxp_hse_request(NXP_HSE_MU_CHANNELS, d, 16, &rsp), -1);
    ck_assert_int_eq(nxp_hse_request(0, d, 260, &rsp), -1);
    ck_assert_int_eq(nxp_hse_request(0, d, 18, &rsp), -1);
    ck_assert_int_eq(nxp_hse_get_attr(1, NULL, 8, &rsp), -1);
    ck_assert_int_eq(nxp_hse_get_attr(1, out, 0, &rsp), -1);
    ck_assert_int_eq(nxp_hse_get_attr(1, out, 65, &rsp), -1);
    ck_assert_int_eq(requests, 0);
    /* a full-size descriptor on the last channel is accepted */
    ck_assert_int_eq(nxp_hse_request(NXP_HSE_MU_CHANNELS - 1, d, 256, &rsp),
        0);
}
END_TEST

static uint64_t addr_of(const void *p)
{
    return (uint64_t)(uintptr_t)p;
}

/* What an address field of this layout holds for a */
static uint64_t field_of(uint64_t a)
{
    return (T_ADDR_SIZE == 8) ? a : (uint64_t)(uint32_t)a;
}

/* A request other than an attribute read goes out on the admin channel:
 * its parameters, behind the 8-byte descriptor header. */
static const uint8_t *admin_desc(void)
{
    return (const uint8_t*)&desc_mem[NXP_HSE_CHANNEL_ADMIN * 64];
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

static uint64_t addr_at(const uint8_t *p)
{
    uint64_t v = le32(p);

    if (T_ADDR_SIZE == 8)
        v |= (uint64_t)le32(p + 4) << 32;
    return v;
}

START_TEST(test_layout_sizes)
{
    ck_assert_uint_eq(NXP_HSE_SMR_ENTRY_SIZE, T_SMRE_TOTAL);
    ck_assert_uint_eq(NXP_HSE_CR_ENTRY_SIZE, 32);
    ck_assert_uint_eq(NXP_HSE_KEY_INFO_SIZE, 16);
    ck_assert_uint_eq(NXP_HSE_KEY_GROUP_SIZE, 8);
}
END_TEST

START_TEST(test_import_ecc_pub)
{
    uint8_t info[NXP_HSE_KEY_INFO_SIZE];
    uint8_t pub[64];
    const uint8_t *d = admin_desc();
    const uint8_t *p = d + 8;
    uint32_t rsp = 0;
    static const uint8_t want_info[16] = { 0x08, 0, 0x00, 0x01, 0, 0, 0, 0,
        0, 0, 0, 0, 0x88, 1, 0, 0 };

    memset(info, 0xEE, sizeof(info));
    ck_assert_int_eq(nxp_hse_import_ecc_pub(NXP_HSE_KEY_NVM(0, 0),
        NXP_HSE_CURVE_P256, 256, addr_of(info), addr_of(pub), &rsp), 0);
    ck_assert_int_eq(requests, 1);
    ck_assert_uint_eq(le32(d + 0), 0x00000104);
    ck_assert_uint_eq(le32(d + 4), 0);
    ck_assert_uint_eq(le32(p + 0), 0x00010000);
    ck_assert(addr_at(p + 4) == field_of(addr_of(info)));
    ck_assert(addr_at(p + T_IMPORT_KEY0) == field_of(addr_of(pub)));
    ck_assert(addr_at(p + T_IMPORT_KEY1) == 0);
    ck_assert_uint_eq(le16(p + T_IMPORT_LEN0), 64);
    ck_assert_uint_eq(le16(p + T_IMPORT_LEN0 + 2), 0);
    ck_assert_uint_eq(le32(p + T_IMPORT_CIPHER), 0xFFFFFFFF);
    ck_assert_uint_eq(le32(p + T_IMPORT_CONT), 0xFFFFFFFF);
    ck_assert_mem_eq(info, want_info, sizeof(info));
    ck_assert_int_ge(cleans, 1);
}
END_TEST

START_TEST(test_smr_and_core_reset)
{
    uint8_t e[NXP_HSE_SMR_ENTRY_SIZE + 8];
    uint8_t cr[NXP_HSE_CR_ENTRY_SIZE + 8];
    uint8_t data[4], r[32], s[32];
    const uint8_t *d = admin_desc();
    const uint8_t *p = d + 8;
    uint32_t rsp = 0;

    memset(e, 0xEE, sizeof(e));
    memset(cr, 0xEE, sizeof(cr));
    ck_assert_int_eq(nxp_hse_smr_install(0, addr_of(e), 0x2240, 0x3409FE40,
        0x24400, NXP_HSE_MEDIUM_QSPI, NXP_HSE_KEY_NVM(0, 0), 0x26688,
        0x266A8, addr_of(data), addr_of(r), addr_of(s), 32, &rsp), 0);
    ck_assert_uint_eq(le32(d + 0), 0x00000501);
    ck_assert_uint_eq(p[0], 0);                     /* one pass */
    ck_assert_uint_eq(p[1], 0);                     /* entry index */
    ck_assert(addr_at(p + 4) == field_of(addr_of(e)));
    ck_assert(addr_at(p + T_SMRI_DATA) == field_of(addr_of(data)));
    ck_assert_uint_eq(le32(p + T_SMRI_LEN), 0x24400);
    ck_assert(addr_at(p + T_SMRI_TAG0) == field_of(addr_of(r)));
    ck_assert(addr_at(p + T_SMRI_TAG1) == field_of(addr_of(s)));
    ck_assert_uint_eq(le16(p + T_SMRI_TLEN0), 32);
    ck_assert_uint_eq(le16(p + T_SMRI_TLEN1), 32);
    ck_assert_uint_eq(le32(e + 0), 0x2240);
    ck_assert(addr_at(e + 4) == 0x3409FE40);
    ck_assert_uint_eq(le32(e + T_SMRE_SIZE), 0x24400);
    ck_assert_uint_eq(e[T_SMRE_FLAGS], 0x04);       /* QuadSPI, keep checking */
    ck_assert_uint_eq(le32(e + T_SMRE_PERIOD), 0);  /* no periodic check */
    ck_assert_uint_eq(le32(e + T_SMRE_KEY), 0x00010000);
    ck_assert_uint_eq(e[T_SMRE_SIGN], 0x80);        /* ECDSA */
    ck_assert_uint_eq(e[T_SMRE_HASH], 0x04);        /* SHA-256 */
    ck_assert_uint_eq(le32(e + T_SMRE_TAG0), 0x26688);
    ck_assert_uint_eq(le32(e + T_SMRE_TAG1), 0x266A8);
    ck_assert_uint_eq(le32(e + T_SMRE_DECRYPT), 0); /* not encrypted */
    ck_assert_uint_eq(le32(e + T_SMRE_VERSION), 0);
    /* nothing written behind the entry */
    ck_assert_uint_eq(e[T_SMRE_TOTAL], 0xEE);

    ck_assert_int_eq(nxp_hse_cr_install(0, addr_of(cr), 3, 1, 0x340A0000,
        &rsp), 0);
    ck_assert_uint_eq(le32(d + 0), 0x00000503);
    ck_assert_uint_eq(p[0], 0);
    ck_assert(addr_at(p + 4) == field_of(addr_of(cr)));
    ck_assert_uint_eq(cr[0], 3);
    ck_assert_uint_eq(le16(cr + 2), 0x7455);
    ck_assert_uint_eq(le32(cr + 4), 1);
    ck_assert_uint_eq(le32(cr + 8), 0x340A0000);
    ck_assert_uint_eq(le32(cr + 12), 0);
    ck_assert_uint_eq(le32(cr + 16), 0);
    ck_assert_uint_eq(le32(cr + 20), 0);
    ck_assert_uint_eq(le16(cr + 24), 0x35A5);
    ck_assert_uint_eq(cr[32], 0xEE);

    fake_rsp = NXP_HSE_RSP_VERIFY_FAILED;
    ck_assert_int_eq(nxp_hse_smr_verify(0, &rsp), -5);
    ck_assert_uint_eq(le32(d + 0), 0x00000502);
    ck_assert_uint_eq(rsp, 0x55A5A164);
}
END_TEST

START_TEST(test_sysimg)
{
    const uint8_t *d = admin_desc();
    const uint8_t *p = d + 8;
    uint32_t rsp = 0;

    ck_assert_int_eq(nxp_hse_format_catalogs(0x90000000, 0x90000040, &rsp),
        0);
    ck_assert_uint_eq(le32(d + 0), 0x00000101);
    ck_assert(addr_at(p) == 0x90000000);
    ck_assert(addr_at(p + T_FORMAT_RAM) == 0x90000040);
    ck_assert_int_eq(nxp_hse_sysimg_size(0x90000200, &rsp), 0);
    ck_assert_uint_eq(le32(d + 0), 0x00000012);
    ck_assert(addr_at(p) == 0x90000200);
    ck_assert_int_eq(nxp_hse_sysimg_publish(0x90000204, 0x90000208,
        0x90300000, &rsp), 0);
    ck_assert_uint_eq(le32(d + 0), 0x00000011);
    ck_assert_uint_eq(p[0], 2);                     /* all data sets */
    ck_assert(addr_at(p + 4) == 0x90000204);
    ck_assert(addr_at(p + T_PUBLISH_LEN) == 0x90000208);
    ck_assert(addr_at(p + T_PUBLISH_BUF) == 0x90300000);
    ck_assert_int_eq(requests, 3);
}
END_TEST

static Suite *hse_suite(void)
{
    Suite *s = suite_create("nxp-hse");
    TCase *tc = tcase_create("mu");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_wait_init);
    tcase_add_test(tc, test_get_attr);
    tcase_add_test(tc, test_error_response);
    tcase_add_test(tc, test_no_answer_times_out);
    tcase_add_test(tc, test_channel_not_idle);
    tcase_add_test(tc, test_bad_arguments);
    tcase_add_test(tc, test_layout_sizes);
    tcase_add_test(tc, test_import_ecc_pub);
    tcase_add_test(tc, test_smr_and_core_reset);
    tcase_add_test(tc, test_sysimg);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(hse_suite());

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
