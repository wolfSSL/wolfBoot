/* unit-psoc-e8-tz.c
 *
 * Unit tests for hal/psoc_e8_tz.c: the memory protection controller block
 * map, the SAU regions, the TRNG entropy source and the console gateway, with
 * the register blocks replaced by host models.
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
#include <stdio.h>
#include <string.h>

/* KIT_PSE84_AI TrustZone layout (config/examples/psoc_e8_ai-tz.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x40000
#define WOLFBOOT_PARTITION_SIZE              0x100000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x08140000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x18240000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x18340000
#define WOLFBOOT_NSC_ADDRESS                 0x1813C000
#define WOLFBOOT_NSC_SIZE                    0x4000

#define TZEN
#define __WOLFBOOT
#define DEBUG_UART
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define PSOC_E8_PERI_TIMEOUT 64U

#include "hal/psoc_e8.h"

/* ---- Memory protection controller model ------------------------------- */

#define MOCK_MPC_PCS   8
#define MOCK_MPC_IDX   64

struct mock_mpc {
    uint32_t base;
    uint32_t ctrl;
    uint32_t blk_max;
    uint32_t blk_cfg;
    uint32_t idx;
    uint32_t pc;
    uint32_t lut[MOCK_MPC_PCS][MOCK_MPC_IDX];
    int ctrl_writes_with_auto_inc;
};

/* SMIF AHB: 128 KB blocks over 64 MB; RAMC1: 4 KB blocks over 512 KB. */
static struct mock_mpc mpc_smif = { SMIF0_AHB_MPC_BASE, MPC_ROT_CTRL_AUTO_INC,
    0x3F, 12 };
static struct mock_mpc mpc_ramc1 = { RAMC1_MPC_BASE, MPC_ROT_CTRL_AUTO_INC,
    0x0F, 7 };

static struct mock_mpc *mpc_of(uint32_t base)
{
    if (base == SMIF0_AHB_MPC_BASE)
        return &mpc_smif;
    ck_assert_uint_eq(base, RAMC1_MPC_BASE);
    return &mpc_ramc1;
}

static uint32_t *mpc_lut(uint32_t base)
{
    struct mock_mpc *m = mpc_of(base);

    ck_assert_uint_lt(m->pc, MOCK_MPC_PCS);
    ck_assert_uint_lt(m->idx, MOCK_MPC_IDX);
    /* The read-modify-write of one LUT word must not run with AUTO_INC. */
    ck_assert_uint_eq(m->ctrl & MPC_ROT_CTRL_AUTO_INC, 0);
    return &m->lut[m->pc][m->idx];
}

#undef MPC_ROT_CTRL
#undef MPC_ROT_BLK_MAX
#undef MPC_ROT_BLK_CFG
#undef MPC_ROT_BLK_IDX
#undef MPC_ROT_BLK_PC
#undef MPC_ROT_BLK_LUT
#define MPC_ROT_CTRL(b)     (mpc_of(b)->ctrl)
#define MPC_ROT_BLK_MAX(b)  (mpc_of(b)->blk_max)
#define MPC_ROT_BLK_CFG(b)  (mpc_of(b)->blk_cfg)
#define MPC_ROT_BLK_IDX(b)  (mpc_of(b)->idx)
#define MPC_ROT_BLK_PC(b)   (mpc_of(b)->pc)
#define MPC_ROT_BLK_LUT(b)  (*mpc_lut(b))

/* ---- SAU model -------------------------------------------------------- */

#define MOCK_SAU_REGIONS 8
static uint32_t mock_sau_ctrl;
static uint32_t mock_sau_rnr;
static uint32_t mock_sau_rbar[MOCK_SAU_REGIONS];
static uint32_t mock_sau_rlar[MOCK_SAU_REGIONS];

static uint32_t *sau_reg(uint32_t *bank)
{
    ck_assert_uint_lt(mock_sau_rnr, MOCK_SAU_REGIONS);
    return &bank[mock_sau_rnr];
}

#undef SAU_CTRL
#undef SAU_RNR
#undef SAU_RBAR
#undef SAU_RLAR
#define SAU_CTRL  mock_sau_ctrl
#define SAU_RNR   mock_sau_rnr
#define SAU_RBAR  (*sau_reg(mock_sau_rbar))
#define SAU_RLAR  (*sau_reg(mock_sau_rlar))

/* ---- TRNG model ------------------------------------------------------- */

static uint32_t mock_crypto_ctl;
static uint32_t mock_tr_ctl0;
static uint32_t mock_tr_ctl1;
static uint32_t mock_tr_ctl2;
static uint32_t mock_tr_cmd;
static uint32_t mock_tr_garo;
static uint32_t mock_tr_firo;
static uint32_t mock_tr_mon_ctl;
static uint32_t mock_tr_mon_cmd;
static uint32_t mock_tr_mon_rc;
static uint32_t mock_tr_mon_ap;
static uint32_t mock_crypto_status;
static uint32_t mock_intr;
static int mock_initialized;      /* TR_INITIALIZED is reported */
static int mock_health_error_at;  /* word index that reports a failure */
static uint32_t mock_words;       /* words produced so far */

/* A started generation reports DATA_AVAILABLE (or a health failure). */
static uint32_t intr_read(void)
{
    uint32_t v = mock_intr;

    if (mock_initialized)
        v |= CRYPTO_INTR_TR_INITIALIZED;
    if (mock_tr_cmd != 0U) {
        if ((int)mock_words == mock_health_error_at)
            v |= CRYPTO_INTR_TR_RC_ERROR;
        else
            v |= CRYPTO_INTR_TR_DATA_AVAILABLE;
    }
    return v;
}

static uint32_t result_read(void)
{
    ck_assert_uint_eq(mock_tr_ctl2, 32U);
    mock_tr_cmd = 0;
    return 0xA0B0C0D0U + mock_words++;
}

#undef CRYPTO_CTL
#undef CRYPTO_INTR
#undef CRYPTO_INTR_CLR
#undef CRYPTO_TR_CTL0
#undef CRYPTO_TR_CTL1
#undef CRYPTO_TR_CTL2
#undef CRYPTO_TR_CMD
#undef CRYPTO_TR_RESULT
#undef CRYPTO_TR_GARO_CTL
#undef CRYPTO_TR_FIRO_CTL
#undef CRYPTO_TR_MON_CTL
#undef CRYPTO_TR_MON_CMD
#undef CRYPTO_TR_MON_RC_CTL
#undef CRYPTO_TR_MON_AP_CTL
#undef CRYPTO_STATUS
#define CRYPTO_CTL            mock_crypto_ctl
#define CRYPTO_INTR           intr_read()
#define CRYPTO_INTR_CLR(m)    (mock_intr &= ~(m))
#define CRYPTO_TR_CTL0        mock_tr_ctl0
#define CRYPTO_TR_CTL1        mock_tr_ctl1
#define CRYPTO_TR_CTL2        mock_tr_ctl2
#define CRYPTO_TR_CMD         mock_tr_cmd
#define CRYPTO_TR_RESULT      result_read()
#define CRYPTO_TR_GARO_CTL    mock_tr_garo
#define CRYPTO_TR_FIRO_CTL    mock_tr_firo
#define CRYPTO_TR_MON_CTL     mock_tr_mon_ctl
#define CRYPTO_TR_MON_CMD     mock_tr_mon_cmd
#define CRYPTO_TR_MON_RC_CTL  mock_tr_mon_rc
#define CRYPTO_TR_MON_AP_CTL  mock_tr_mon_ap
#define CRYPTO_STATUS         mock_crypto_status

/* ---- Console ---------------------------------------------------------- */

static char uart_buf[512];
static unsigned int uart_len;

void uart_write(const char *buf, unsigned int sz)
{
    ck_assert_uint_le(uart_len + sz, sizeof(uart_buf));
    memcpy(uart_buf + uart_len, buf, sz);
    uart_len += sz;
}

#include "../../hal/psoc_e8_tz.c"

static uint32_t lut_nibble(const struct mock_mpc *m, uint32_t pc, uint32_t blk)
{
    return (m->lut[pc][blk / 8U] >> ((blk % 8U) * 4U)) & 0xFU;
}

static void reset_mocks(void)
{
    memset(mpc_smif.lut, 0, sizeof(mpc_smif.lut));
    memset(mpc_ramc1.lut, 0, sizeof(mpc_ramc1.lut));
    mpc_smif.ctrl = mpc_ramc1.ctrl = MPC_ROT_CTRL_AUTO_INC;
    mpc_smif.idx = mpc_ramc1.idx = 0;
    mpc_smif.pc = mpc_ramc1.pc = 0;
    memset(mock_sau_rbar, 0, sizeof(mock_sau_rbar));
    memset(mock_sau_rlar, 0, sizeof(mock_sau_rlar));
    mock_sau_ctrl = 0;
    mock_sau_rnr = 0;
    mock_intr = 0;
    mock_initialized = 1;
    mock_health_error_at = -1;
    mock_words = 0;
    mock_tr_cmd = 0;
    mock_tr_ctl2 = 0;
    mock_crypto_status = 0;
    uart_len = 0;
}

START_TEST(test_gtzc_boot_partition_and_sram1)
{
    uint32_t b;

    reset_mocks();
    hal_gtzc_init();

    /* Boot partition: QSPI offset 0x140000, 1 MB = SMIF blocks 10..17. */
    for (b = 0; b < 64U; b++) {
        uint32_t want = ((b >= 10U) && (b <= 17U)) ? MPC_ROT_NS_RW : 0U;
        ck_assert_uint_eq(lut_nibble(&mpc_smif, PSOC_E8_PC_CM33, b), want);
    }
    /* All of SRAM1 (128 blocks of 4 KB). */
    for (b = 0; b < 128U; b++)
        ck_assert_uint_eq(lut_nibble(&mpc_ramc1, PSOC_E8_PC_CM33, b),
                MPC_ROT_NS_RW);
    /* Only the CM33 context is touched. */
    for (b = 0; b < MOCK_MPC_IDX; b++) {
        ck_assert_uint_eq(mpc_smif.lut[0][b], 0);
        ck_assert_uint_eq(mpc_smif.lut[7][b], 0);
    }
    /* AUTO_INC is restored afterwards. */
    ck_assert_uint_eq(mpc_smif.ctrl, MPC_ROT_CTRL_AUTO_INC);
    ck_assert_uint_eq(mpc_ramc1.ctrl, MPC_ROT_CTRL_AUTO_INC);
}
END_TEST

START_TEST(test_mpc_preserves_neighbours)
{
    reset_mocks();
    /* Secure entries already in the word must survive the update. */
    mpc_smif.lut[PSOC_E8_PC_CM33][1] = 0x66666666U;
    mpc_set_ns(SMIF0_AHB_MPC_BASE, 0x140000U, 0x20000U);    /* block 10 */
    ck_assert_uint_eq(mpc_smif.lut[PSOC_E8_PC_CM33][1], 0x66666766U);
}
END_TEST

START_TEST(test_mpc_rejects_unaligned)
{
    uint32_t i;

    reset_mocks();
    mpc_set_ns(SMIF0_AHB_MPC_BASE, 0x140000U + 0x1000U, 0x20000U);
    mpc_set_ns(SMIF0_AHB_MPC_BASE, 0x140000U, 0x20000U + 0x10U);
    mpc_set_ns(SMIF0_AHB_MPC_BASE, 0x140000U, 0U);
    for (i = 0; i < MOCK_MPC_IDX; i++)
        ck_assert_uint_eq(mpc_smif.lut[PSOC_E8_PC_CM33][i], 0);
}
END_TEST

START_TEST(test_mpc_rejects_out_of_range)
{
    uint32_t i;

    reset_mocks();
    /* RAMC1 covers 512 KB; one block past the end is refused outright. */
    mpc_set_ns(RAMC1_MPC_BASE, 0x7F000U, 0x2000U);
    for (i = 0; i < MOCK_MPC_IDX; i++)
        ck_assert_uint_eq(mpc_ramc1.lut[PSOC_E8_PC_CM33][i], 0);
}
END_TEST

START_TEST(test_mpc_init_busy)
{
    uint32_t i;

    reset_mocks();
    /* A controller that never finishes initialising is left untouched. */
    mpc_smif.blk_cfg |= MPC_ROT_BLK_CFG_INIT_BUSY;
    mpc_set_ns(SMIF0_AHB_MPC_BASE, 0x140000U, 0x20000U);
    mpc_smif.blk_cfg &= ~MPC_ROT_BLK_CFG_INIT_BUSY;
    for (i = 0; i < MOCK_MPC_IDX; i++)
        ck_assert_uint_eq(mpc_smif.lut[PSOC_E8_PC_CM33][i], 0);
}
END_TEST

START_TEST(test_sau_regions)
{
    reset_mocks();
    /* Regions extended boot already programmed must be left alone. */
    mock_sau_rbar[0] = 0x42000000U;
    mock_sau_rlar[0] = 0x43FFFFE1U;
    mock_sau_ctrl = SAU_CTRL_ENABLE;
    hal_tz_sau_init();

    ck_assert_uint_eq(mock_sau_rbar[0], 0x42000000U);
    ck_assert_uint_eq(mock_sau_rlar[0], 0x43FFFFE1U);
    ck_assert_uint_eq(mock_sau_rbar[2], 0x08140000U);
    ck_assert_uint_eq(mock_sau_rlar[2], 0x0823FFE0U | SAU_RLAR_ENABLE);
    ck_assert_uint_eq(mock_sau_rbar[3], 0x24080000U);
    ck_assert_uint_eq(mock_sau_rlar[3], 0x240FFFE0U | SAU_RLAR_ENABLE);
    ck_assert_uint_eq(mock_sau_rbar[4], 0x1813C000U);
    ck_assert_uint_eq(mock_sau_rlar[4],
            0x1813FFE0U | SAU_RLAR_NSC | SAU_RLAR_ENABLE);
    ck_assert_uint_ne(mock_sau_ctrl & SAU_CTRL_ENABLE, 0);
}
END_TEST

START_TEST(test_trng_init_config)
{
    reset_mocks();
    hal_trng_init();
    ck_assert_uint_ne(mock_crypto_ctl & CRYPTO_CTL_ENABLED, 0);
    ck_assert_uint_eq(mock_tr_ctl0, CRYPTO_TR_CTL0_DEFAULT);
    ck_assert_uint_eq(mock_tr_ctl1, CRYPTO_TR_CTL1_ALL_RO);
    ck_assert_uint_eq(mock_tr_garo, CRYPTO_TR_POLY);
    ck_assert_uint_eq(mock_tr_firo, CRYPTO_TR_POLY);
    ck_assert_uint_eq(mock_tr_mon_cmd, CRYPTO_TR_MON_START_AP_RC);
    hal_trng_fini();
    ck_assert_uint_eq(mock_tr_mon_cmd, 0);
    ck_assert_uint_eq(mock_tr_ctl1, 0);
}
END_TEST

START_TEST(test_trng_entropy_bytes)
{
    unsigned char out[7];

    reset_mocks();
    memset(out, 0, sizeof(out));
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), 0);
    /* Two words, little-endian, the second only partly used. */
    ck_assert_uint_eq(mock_words, 2);
    ck_assert_uint_eq(out[0], 0xD0);
    ck_assert_uint_eq(out[3], 0xA0);
    ck_assert_uint_eq(out[4], 0xD1);
    ck_assert_uint_eq(out[6], 0xB0);
}
END_TEST

START_TEST(test_trng_health_failure)
{
    unsigned char out[16];

    reset_mocks();
    mock_health_error_at = 1;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
}
END_TEST

START_TEST(test_trng_not_initialized)
{
    unsigned char out[4];

    reset_mocks();
    mock_initialized = 0;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
    ck_assert_uint_eq(mock_words, 0);
}
END_TEST

START_TEST(test_trng_busy)
{
    unsigned char out[4];

    reset_mocks();
    mock_crypto_status = 1;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
}
END_TEST

START_TEST(test_nsc_uart_bounds)
{
    static char big[PSOC_E8_NSC_UART_MAX + 1];

    reset_mocks();
    psoc_e8_nsc_uart_write(NULL, 4);
    psoc_e8_nsc_uart_write("abc", 0);
    psoc_e8_nsc_uart_write(big, sizeof(big));
    ck_assert_uint_eq(uart_len, 0);
    psoc_e8_nsc_uart_write("hello", 5);
    ck_assert_uint_eq(uart_len, 5);
    ck_assert_int_eq(memcmp(uart_buf, "hello", 5), 0);
}
END_TEST

static Suite *psoc_e8_tz_suite(void)
{
    Suite *s = suite_create("psoc_e8_tz");
    TCase *tc = tcase_create("tz");

    tcase_add_test(tc, test_gtzc_boot_partition_and_sram1);
    tcase_add_test(tc, test_mpc_preserves_neighbours);
    tcase_add_test(tc, test_mpc_rejects_unaligned);
    tcase_add_test(tc, test_mpc_rejects_out_of_range);
    tcase_add_test(tc, test_mpc_init_busy);
    tcase_add_test(tc, test_sau_regions);
    tcase_add_test(tc, test_trng_init_config);
    tcase_add_test(tc, test_trng_entropy_bytes);
    tcase_add_test(tc, test_trng_health_failure);
    tcase_add_test(tc, test_trng_not_initialized);
    tcase_add_test(tc, test_trng_busy);
    tcase_add_test(tc, test_nsc_uart_bounds);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = psoc_e8_tz_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
