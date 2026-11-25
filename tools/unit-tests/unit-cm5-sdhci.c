/* unit-cm5-sdhci.c
 *
 * Unit tests for the CM5 (BCM2712 SDIO1) SDHCI glue in hal/cm5.c:
 * sdhci_reg_read/sdhci_reg_write translate the generic driver's Cadence-style
 * SRS offsets to the standard SDHCI layout, and sdhci_platform_init sets up
 * the SDIO_CFG block.
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

/* Mocks of the SDHCI register window and the SDIO_CFG block. Retarget the CM5
 * HAL at them so the glue hits host memory, not MMIO. */
static uint32_t cm5_mock_sdhci_words[0x100 / 4];
static uint32_t cm5_mock_cfg[0x100 / 4];
#define cm5_mock_sdhci ((uint8_t *)cm5_mock_sdhci_words)
#define BCM2712_SDIO1_BASE     ((uintptr_t)cm5_mock_sdhci_words)
#define BCM2712_SDIO1_CFG_BASE ((uintptr_t)cm5_mock_cfg)

#define ARCH_AARCH64
#define TARGET_cm5
#define DISK_EMMC 1

#include "../../hal/cm5.c"
#include "../../hal/rpi_common.c"

#define SRS(n) (CADENCE_SRS_OFFSET + (n))

static void clear_mock(void)
{
    memset(cm5_mock_sdhci_words, 0, sizeof(cm5_mock_sdhci_words));
    memset(cm5_mock_cfg, 0, sizeof(cm5_mock_cfg));
}

START_TEST(test_srs10_byte_decomposition)
{
    clear_mock();
    sdhci_reg_write(SRS(0x28), 0x44332211);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_HOST_CTRL1], 0x11);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_POWER_CTRL],  0x22);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_BLKGAP_CTRL], 0x33);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_WAKEUP_CTRL], 0x44);
}
END_TEST

START_TEST(test_srs11_split)
{
    uint16_t clk;
    clear_mock();
    sdhci_reg_write(SRS(0x2C), 0xAA55BBCC);
    memcpy(&clk, &cm5_mock_sdhci[STD_SDHCI_CLK_CTRL], sizeof(clk));
    ck_assert_uint_eq(clk, 0xBBCC);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_TIMEOUT_CTRL], 0x55);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_SW_RESET],     0xAA);
}
END_TEST

START_TEST(test_srs15_masks_hv4e_a64)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(STD_SDHCI_HOST_CTRL2),
        0x0000FFFF | SDHCI_SRS15_HV4E | SDHCI_SRS15_A64);
    memcpy(&v, &cm5_mock_sdhci[STD_SDHCI_HOST_CTRL2], sizeof(v));
    ck_assert_uint_eq(v & (SDHCI_SRS15_HV4E | SDHCI_SRS15_A64), 0);
    ck_assert_uint_eq(v & 0xFFFF, 0xFFFF);
}
END_TEST

/* The BCM2712 controller advertises 64-bit addressing (CAPS 0x55EEC832 on
 * hardware); the glue hides it because the driver's v4 path does not apply to
 * this v3.0 block. */
START_TEST(test_srs16_masks_a64s)
{
    uint32_t caps = 0x55EEC832;
    clear_mock();
    ck_assert_uint_ne(caps & SDHCI_SRS16_A64S, 0);
    memcpy(&cm5_mock_sdhci[0x40], &caps, sizeof(caps));
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x40)), caps & ~SDHCI_SRS16_A64S);
}
END_TEST

START_TEST(test_srs22_maps_to_sdma_addr)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(0x58), 0xDEADBEEF);
    memcpy(&v, &cm5_mock_sdhci[STD_SDHCI_SDMA_ADDR], sizeof(v));
    ck_assert_uint_eq(v, 0xDEADBEEF);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x58)), 0xDEADBEEF);
}
END_TEST

START_TEST(test_srs23_noop)
{
    clear_mock();
    sdhci_reg_write(SRS(0x5C), 0xFFFFFFFF);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x5C)), 0);
    ck_assert_uint_eq(cm5_mock_sdhci[0x5C], 0);
}
END_TEST

START_TEST(test_plain_reg_roundtrip)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(0x04), 0xCAFEF00D);
    memcpy(&v, &cm5_mock_sdhci[0x04], sizeof(v));
    ck_assert_uint_eq(v, 0xCAFEF00D);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x04)), 0xCAFEF00D);
}
END_TEST

START_TEST(test_hrs_region_absent)
{
    clear_mock();
    cm5_mock_sdhci[0x10] = 0xAB;
    ck_assert_uint_eq(sdhci_reg_read(0x10), 0);
    sdhci_reg_write(0x14, 0xFFFFFFFF);
    ck_assert_uint_eq(cm5_mock_sdhci[0x14], 0);
}
END_TEST

/* sdhci_platform_init forces card presence, selects the eMMC pin mode without
 * disturbing the other SDIO_CFG bits, and issues a controller soft reset. */
START_TEST(test_platform_init)
{
    clear_mock();
    cm5_mock_cfg[SDIO_CFG_CTRL / 4] =
        SDIO_CFG_CTRL_SDCD_N_TEST_LEV | 0x00003c03;
    cm5_mock_cfg[SDIO_CFG_SD_PIN_SEL / 4] = 0xFFFFFFF2;
    sdhci_platform_init();
    ck_assert_uint_eq(cm5_mock_cfg[SDIO_CFG_CTRL / 4],
        SDIO_CFG_CTRL_SDCD_N_TEST_EN | 0x00003c03);
    ck_assert_uint_eq(cm5_mock_cfg[SDIO_CFG_SD_PIN_SEL / 4],
        0xFFFFFFF0 | SDIO_CFG_SD_PIN_SEL_MMC);
    ck_assert_uint_eq(cm5_mock_sdhci[STD_SDHCI_SW_RESET] & STD_SDHCI_SRA,
        STD_SDHCI_SRA);
}
END_TEST

Suite *cm5_sdhci_suite(void)
{
    Suite *s = suite_create("cm5-sdhci");
    TCase *tc = tcase_create("reg_translation");

    tcase_add_test(tc, test_srs10_byte_decomposition);
    tcase_add_test(tc, test_srs11_split);
    tcase_add_test(tc, test_srs15_masks_hv4e_a64);
    tcase_add_test(tc, test_srs16_masks_a64s);
    tcase_add_test(tc, test_srs22_maps_to_sdma_addr);
    tcase_add_test(tc, test_srs23_noop);
    tcase_add_test(tc, test_plain_reg_roundtrip);
    tcase_add_test(tc, test_hrs_region_absent);
    tcase_add_test(tc, test_platform_init);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = cm5_sdhci_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
