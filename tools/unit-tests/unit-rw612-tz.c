/* unit-rw612-tz.c
 *
 * Unit tests for the RW612 secure-world HAL in hal/rw612.c: the TrustZone
 * attribution map and the SA_TRNG entropy source used by wolfCrypt.
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

#define TZEN
#define WOLFCRYPT_SECURE_MODE
/* Build the bootloader side of the HAL, including the XIP cache setup */
#define __WOLFBOOT
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define BOOT_BENCHMARK
/* Replace hal/armv8m_tz.h with a recording SAU */
#define TZ_INCLUDED

#define RW612_SAU_REGIONS 8

struct sau_region {
    uint32_t start;
    uint32_t end;
    int nsc;
    int set;
};

static struct sau_region sau[RW612_SAU_REGIONS];
static int sau_bad_region;
static uint32_t mock_sau_ctrl;
static uint32_t mock_shcsr;

#define SAU_CTRL                 mock_sau_ctrl
#define SAU_INIT_CTRL_ENABLE     (1U << 0)
#define SCB_SHCSR                mock_shcsr
#define SCB_SHCSR_SECUREFAULT_EN (1U << 19)

static void sau_init_region(uint32_t region, uint32_t start_addr,
        uint32_t end_addr, int secure)
{
    if (region >= RW612_SAU_REGIONS) {
        sau_bad_region = 1;
        return;
    }
    sau[region].start = start_addr;
    sau[region].end = end_addr;
    sau[region].nsc = secure;
    sau[region].set = 1;
}

/* RW612 layout (config/examples/rw612*.config) with the 8KB logical sector
 * ML-DSA builds use over the 4KB NOR erase sector, in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x2000
#define WOLFBOOT_PARTITION_SIZE              0x100000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x08100000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x08200000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x08300000
#define WOLFBOOT_NSC_ADDRESS                 0x08060000
#define WOLFBOOT_NSC_SIZE                    0x2000
#define WOLFBOOT_KEYVAULT_ADDRESS            0x08400000
#define WOLFBOOT_KEYVAULT_SIZE               0x20000

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_romapi_flexspi.h"
#include "fsl_trng.h"

TRNG_Type mock_trng;
static int trng_init_calls;
static int trng_read_calls;
static status_t trng_init_ret;
static status_t trng_read_ret;
static uint32_t trng_init_mode;

status_t TRNG_GetDefaultConfig(trng_config_t *userConfig)
{
    userConfig->sampleMode = 0x5AU;
    return kStatus_Success;
}

status_t TRNG_Init(TRNG_Type *base, const trng_config_t *userConfig)
{
    ck_assert_ptr_eq(base, &mock_trng);
    trng_init_calls++;
    trng_init_mode = userConfig->sampleMode;
    return trng_init_ret;
}

status_t TRNG_GetRandomData(TRNG_Type *base, void *data, size_t dataSize)
{
    ck_assert_ptr_eq(base, &mock_trng);
    trng_read_calls++;
    memset(data, 0xA5, dataSize);
    return trng_read_ret;
}

static FLEXSPI_Type mock_flexspi;
static CACHE64_CTRL_Type mock_cache64;
SOCCIU_Type mock_socctrl;
NVIC_Type mock_nvic;
SysTick_Type mock_systick;
uint32_t mock_primask;
int mock_barriers;

FLEXSPI_Type *mock_flexspi_regs(void)
{
    return &mock_flexspi;
}

CACHE64_POLSEL_Type mock_cache64_polsel;
static int cache_invalidations;
static uint32_t cache64_seen;

CACHE64_CTRL_Type *mock_cache64_regs(void)
{
    cache64_seen |= mock_cache64.CCR;
    if ((mock_cache64.CCR & CACHE64_CTRL_CCR_GO_MASK) != 0U) {
        cache_invalidations++;
        mock_cache64.CCR &= ~CACHE64_CTRL_CCR_GO_MASK;
    }
    return &mock_cache64;
}

static int systick_attach_calls;
static clock_attach_id_t systick_attach_id;

void CLOCK_AttachClk(clock_attach_id_t connection)
{
    systick_attach_calls++;
    systick_attach_id = connection;
}

/* Point the SDK peripheral instances at the mocks above */
#undef FLEXSPI
#undef CACHE64_CTRL0
#undef CACHE64_POLSEL0
#undef SOCCTRL
#define FLEXSPI         (mock_flexspi_regs())
#define CACHE64_CTRL0   (mock_cache64_regs())
#define CACHE64_POLSEL0 (&mock_cache64_polsel)
#define SOCCTRL         (&mock_socctrl)
#undef TRNG
#define TRNG            (&mock_trng)

#include "../../hal/rw612.c"

static int sau_covers(uint32_t addr, int nsc)
{
    int i;

    for (i = 0; i < RW612_SAU_REGIONS; i++) {
        if (sau[i].set && (sau[i].nsc == nsc) &&
                (addr >= sau[i].start) && (addr <= sau[i].end))
            return 1;
    }
    return 0;
}

static void setup(void)
{
    memset(sau, 0, sizeof(sau));
    memset(&mock_nvic, 0, sizeof(mock_nvic));
    sau_bad_region = 0;
    mock_sau_ctrl = 0;
    mock_shcsr = 0;
    memset(&mock_cache64, 0, sizeof(mock_cache64));
    memset(&mock_cache64_polsel, 0, sizeof(mock_cache64_polsel));
    cache_invalidations = 0;
    cache64_seen = 0;
    hal_init();
    hal_trng_fini();
    trng_init_calls = trng_read_calls = 0;
    trng_init_ret = trng_read_ret = kStatus_Success;
    trng_init_mode = 0;
}

START_TEST(test_sau_enabled_and_well_formed)
{
    int i;

    ck_assert_int_eq(sau_bad_region, 0);
    ck_assert_uint_ne(mock_sau_ctrl & SAU_INIT_CTRL_ENABLE, 0);
    ck_assert_uint_ne(mock_shcsr & SCB_SHCSR_SECUREFAULT_EN, 0);
    for (i = 0; i < RW612_SAU_REGIONS; i++) {
        if (!sau[i].set)
            continue;
        ck_assert_uint_le(sau[i].start, sau[i].end);
        ck_assert_uint_eq(sau[i].start & 0x1FU, 0);
        ck_assert_uint_eq(sau[i].end & 0x1FU, 0x1FU);
    }
}
END_TEST

START_TEST(test_nsc_region_is_veneers_only)
{
    int i;
    int nsc_regions = 0;

    for (i = 0; i < RW612_SAU_REGIONS; i++) {
        if (sau[i].set && sau[i].nsc) {
            nsc_regions++;
            ck_assert_uint_eq(sau[i].start, WOLFBOOT_NSC_ADDRESS);
            ck_assert_uint_eq(sau[i].end,
                    WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1);
        }
    }
    ck_assert_int_eq(nsc_regions, 1);
}
END_TEST

START_TEST(test_application_resources_non_secure)
{
    static const uint32_t ns_addrs[] = {
        WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1,
        0x20040000, 0x2012FFFF,     /* non-secure SRAM */
        0x40020000, 0x40021000,     /* RSTCTL1, CLKCTL1 */
        0x40100000,                 /* GPIO */
        0x40106000, 0x40109000      /* FLEXCOMM0, FLEXCOMM3 (console) */
    };
    unsigned int i;

    for (i = 0; i < sizeof(ns_addrs) / sizeof(ns_addrs[0]); i++)
        ck_assert_msg(sau_covers(ns_addrs[i], 0), "0x%08x not non-secure",
                ns_addrs[i]);
}
END_TEST

START_TEST(test_secure_resources_not_non_secure)
{
    static const uint32_t s_addrs[] = {
        0x08000000, 0x08000400,     /* wolfBoot, FCB */
        WOLFBOOT_NSC_ADDRESS,
        WOLFBOOT_KEYVAULT_ADDRESS,
        WOLFBOOT_KEYVAULT_ADDRESS + WOLFBOOT_KEYVAULT_SIZE - 1,
        WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        WOLFBOOT_PARTITION_SWAP_ADDRESS,
        0x20000000, 0x2003FFFF,     /* wolfBoot SRAM */
        0x40000000, 0x40000040,     /* RSTCTL0, PRSTCTL0_SET */
        0x40001000,                 /* CLKCTL0 */
        0x40004000,                 /* IO_MUX */
        0x40006000, 0x40007000,     /* PUF, ELS */
        0x40009000, 0x4000A000,     /* PKC, OCOTP */
        0x40014000, 0x40024000,     /* TRNG, ITRC */
        0x40033000,                 /* CACHE64_CTRL0 */
        0x40104000, 0x40105000,     /* DMA0, DMA1 */
        0x4010A000,                 /* past FLEXCOMM3 */
        0x4010F000,                 /* DBGMAILBOX */
        0x40134000,                 /* FLEXSPI */
        0x40148000,                 /* AHB_SECURE_CTRL */
        0x4014E000,                 /* GDMA */
        0x48000000,                 /* uncached FlexSPI alias */
        0x48000000 + (WOLFBOOT_KEYVAULT_ADDRESS - 0x08000000)
    };
    unsigned int i;

    for (i = 0; i < sizeof(s_addrs) / sizeof(s_addrs[0]); i++)
        ck_assert_msg(!sau_covers(s_addrs[i], 0), "0x%08x is non-secure",
                s_addrs[i]);
}
END_TEST

START_TEST(test_non_secure_peripheral_irqs_targeted)
{
    unsigned int i;

    ck_assert_uint_eq(mock_nvic.ITNS[0],
            (1UL << GPIO_INTA_IRQn) | (1UL << GPIO_INTB_IRQn) |
            (0xFUL << FLEXCOMM0_IRQn));
    for (i = 1; i < sizeof(mock_nvic.ITNS) / sizeof(mock_nvic.ITNS[0]); i++)
        ck_assert_uint_eq(mock_nvic.ITNS[i], 0);
}
END_TEST

START_TEST(test_xip_cache_enabled_at_init)
{
    const uint32_t inv = CACHE64_CTRL_CCR_INVW0_MASK |
        CACHE64_CTRL_CCR_INVW1_MASK;

    ck_assert_int_eq(cache_invalidations, 1);
    ck_assert_uint_eq(cache64_seen & inv, inv);
    ck_assert_uint_eq(mock_cache64.CCR,
            CACHE64_CTRL_CCR_ENWRBUF_MASK | CACHE64_CTRL_CCR_ENCACHE_MASK);
    ck_assert_uint_eq(mock_cache64_polsel.REG0_TOP, 0x07FFFC00U);
    ck_assert_uint_eq(mock_cache64_polsel.REG1_TOP, 0U);
    ck_assert_uint_eq(mock_cache64_polsel.POLSEL, 1U);
}
END_TEST

START_TEST(test_xip_cache_already_on_left_alone)
{
    mock_cache64.CCR = CACHE64_CTRL_CCR_ENCACHE_MASK;
    mock_cache64_polsel.POLSEL = 0xA5U;
    cache_invalidations = 0;
    hal_init();
    ck_assert_int_eq(cache_invalidations, 0);
    ck_assert_uint_eq(mock_cache64.CCR, CACHE64_CTRL_CCR_ENCACHE_MASK);
    ck_assert_uint_eq(mock_cache64_polsel.POLSEL, 0xA5U);
}
END_TEST

START_TEST(test_benchmark_timer_counts_lposc_ticks)
{
    memset(&mock_systick, 0, sizeof(mock_systick));
    ck_assert_uint_eq(hal_get_timer_us(), 0);
    ck_assert_int_eq(systick_attach_calls, 1);
    ck_assert_int_eq(systick_attach_id, kLPOSC_to_SYSTICK_CLK);
    ck_assert_uint_eq(mock_systick.LOAD, SysTick_LOAD_RELOAD_Msk);
    ck_assert_uint_eq(mock_systick.CTRL, SysTick_CTRL_ENABLE_Msk);

    /* Down-counter wraps from 0 to the 24-bit reload value */
    mock_systick.VAL = SysTick_LOAD_RELOAD_Msk - 99U;
    ck_assert_uint_eq(hal_get_timer_us(), 100);
    mock_systick.VAL -= 400U;
    ck_assert_uint_eq(hal_get_timer_us(), 500);
    ck_assert_int_eq(systick_attach_calls, 1);
}
END_TEST

START_TEST(test_trng_needs_init)
{
    unsigned char buf[8];

    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(trng_read_calls, 0);
}
END_TEST

START_TEST(test_trng_init_and_read)
{
    unsigned char buf[8];
    unsigned int i;

    memset(buf, 0, sizeof(buf));
    hal_trng_init();
    ck_assert_int_eq(trng_init_calls, 1);
    ck_assert_uint_eq(trng_init_mode, 0x5AU);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), 0);
    ck_assert_int_eq(trng_read_calls, 1);
    for (i = 0; i < sizeof(buf); i++)
        ck_assert_uint_eq(buf[i], 0xA5);
}
END_TEST

START_TEST(test_trng_init_failure)
{
    unsigned char buf[8];

    trng_init_ret = kStatus_Fail;
    hal_trng_init();
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(trng_read_calls, 0);
}
END_TEST

START_TEST(test_trng_read_failure)
{
    unsigned char buf[8];

    hal_trng_init();
    trng_read_ret = kStatus_Fail;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    hal_trng_fini();
    trng_read_ret = kStatus_Success;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(trng_read_calls, 1);
}
END_TEST

static Suite *rw612_tz_suite(void)
{
    Suite *s = suite_create("rw612-tz");
    TCase *tc = tcase_create("rw612-tz");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_sau_enabled_and_well_formed);
    tcase_add_test(tc, test_nsc_region_is_veneers_only);
    tcase_add_test(tc, test_application_resources_non_secure);
    tcase_add_test(tc, test_secure_resources_not_non_secure);
    tcase_add_test(tc, test_non_secure_peripheral_irqs_targeted);
    tcase_add_test(tc, test_xip_cache_enabled_at_init);
    tcase_add_test(tc, test_xip_cache_already_on_left_alone);
    tcase_add_test(tc, test_benchmark_timer_counts_lposc_ticks);
    tcase_add_test(tc, test_trng_needs_init);
    tcase_add_test(tc, test_trng_init_and_read);
    tcase_add_test(tc, test_trng_init_failure);
    tcase_add_test(tc, test_trng_read_failure);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = rw612_tz_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
