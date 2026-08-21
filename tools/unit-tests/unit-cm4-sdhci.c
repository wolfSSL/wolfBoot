/* unit-cm4-sdhci.c
 *
 * Unit tests for the CM4 (BCM2711 EMMC2) SDHCI register-translation glue in
 * hal/cm4.c: sdhci_reg_read/sdhci_reg_write translate the generic driver's
 * Cadence-style SRS offsets to the standard Arasan SDHCI layout.
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

/* 256-byte mock of the EMMC2 standard SDHCI register window. Retarget the CM4
 * HAL's EMMC2 base at it so sdhci_reg_read/write hit host memory, not MMIO. */
static uint8_t cm4_mock_emmc[0x100];
#define BCM2711_EMMC2_BASE ((uintptr_t)cm4_mock_emmc)

/* Compile the CM4 SDHCI glue for the host: pull in only the DISK_SDCARD block. */
#define ARCH_AARCH64
#define DISK_SDCARD 1

#include "../../hal/cm4.c"

/* Cadence SRS offset the generic driver uses (0x200 + std). */
#define SRS(n) (CADENCE_SRS_OFFSET + (n))

static void clear_mock(void)
{
    memset(cm4_mock_emmc, 0, sizeof(cm4_mock_emmc));
}

/* SRS10 (std 0x28) write is decomposed into four 8-bit registers. */
START_TEST(test_srs10_byte_decomposition)
{
    clear_mock();
    sdhci_reg_write(SRS(0x28), 0x44332211);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_HOST_CTRL1], 0x11);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_POWER_CTRL],  0x22);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_BLKGAP_CTRL], 0x33);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_WAKEUP_CTRL], 0x44);
}
END_TEST

/* SRS11 (std 0x2C) write is a 16-bit clock reg + two 8-bit regs. */
START_TEST(test_srs11_split)
{
    uint16_t clk;
    clear_mock();
    sdhci_reg_write(SRS(0x2C), 0xAA55BBCC);
    memcpy(&clk, &cm4_mock_emmc[STD_SDHCI_CLK_CTRL], sizeof(clk));
    ck_assert_uint_eq(clk, 0xBBCC);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_TIMEOUT_CTRL], 0x55);
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_SW_RESET],     0xAA);
}
END_TEST

/* SRS15 (std 0x3C) write must mask out the unsupported HV4E / A64 bits. */
START_TEST(test_srs15_masks_hv4e_a64)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(STD_SDHCI_HOST_CTRL2),
        0x0000FFFF | SDHCI_SRS15_HV4E | SDHCI_SRS15_A64);
    memcpy(&v, &cm4_mock_emmc[STD_SDHCI_HOST_CTRL2], sizeof(v));
    ck_assert_uint_eq(v & (SDHCI_SRS15_HV4E | SDHCI_SRS15_A64), 0);
    ck_assert_uint_eq(v & 0xFFFF, 0xFFFF);
}
END_TEST

/* SRS16 (std 0x40, Capabilities) read must mask out A64S. */
START_TEST(test_srs16_masks_a64s)
{
    uint32_t caps;
    clear_mock();
    /* Pattern with bit 28 clear, so the OR is what sets A64S (load-bearing). */
    caps = 0x02345678 | SDHCI_SRS16_A64S;
    memcpy(&cm4_mock_emmc[0x40], &caps, sizeof(caps));
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x40)) & SDHCI_SRS16_A64S, 0);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x40)), caps & ~SDHCI_SRS16_A64S);
}
END_TEST

/* SRS22 (std 0x58) maps to the legacy SDMA address at std 0x00. */
START_TEST(test_srs22_maps_to_sdma_addr)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(0x58), 0xDEADBEEF);
    memcpy(&v, &cm4_mock_emmc[STD_SDHCI_SDMA_ADDR], sizeof(v));
    ck_assert_uint_eq(v, 0xDEADBEEF);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x58)), 0xDEADBEEF);
}
END_TEST

/* SRS23 (std 0x5C) has no 64-bit addressing: write is a no-op, read is 0. */
START_TEST(test_srs23_noop)
{
    clear_mock();
    sdhci_reg_write(SRS(0x5C), 0xFFFFFFFF);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x5C)), 0);
    /* nothing was written to the window */
    ck_assert_uint_eq(cm4_mock_emmc[0x5C], 0);
}
END_TEST

/* A plain 32-bit SRS register (e.g. SRS01 std 0x04) round-trips unchanged. */
START_TEST(test_plain_reg_roundtrip)
{
    uint32_t v;
    clear_mock();
    sdhci_reg_write(SRS(0x04), 0xCAFEF00D);
    memcpy(&v, &cm4_mock_emmc[0x04], sizeof(v));
    ck_assert_uint_eq(v, 0xCAFEF00D);
    ck_assert_uint_eq(sdhci_reg_read(SRS(0x04)), 0xCAFEF00D);
}
END_TEST

/* Offsets below the SRS window are the HRS region, not present on this Arasan
 * block: reads return 0 and do not index the mock. */
START_TEST(test_hrs_read_fallthrough)
{
    clear_mock();
    cm4_mock_emmc[0x10] = 0xAB; /* a would-be HRS byte */
    ck_assert_uint_eq(sdhci_reg_read(0x10), 0);
}
END_TEST

/* Writes below the SRS window are silently dropped (the one place a stray
 * offset is discarded). */
START_TEST(test_hrs_write_noop)
{
    clear_mock();
    sdhci_reg_write(0x10, 0xFFFFFFFF);
    ck_assert_uint_eq(cm4_mock_emmc[0x10], 0);
}
END_TEST

/* sdhci_platform_init issues a controller soft reset: SRA is written to the
 * standard software-reset register (the poll then spins to timeout on the mock,
 * which never clears the bit). */
START_TEST(test_platform_init_soft_reset)
{
    clear_mock();
    sdhci_platform_init();
    ck_assert_uint_eq(cm4_mock_emmc[STD_SDHCI_SW_RESET] & STD_SDHCI_SRA,
        STD_SDHCI_SRA);
}
END_TEST

Suite *cm4_sdhci_suite(void)
{
    Suite *s = suite_create("cm4-sdhci");
    TCase *tc = tcase_create("reg_translation");

    tcase_add_test(tc, test_srs10_byte_decomposition);
    tcase_add_test(tc, test_srs11_split);
    tcase_add_test(tc, test_srs15_masks_hv4e_a64);
    tcase_add_test(tc, test_srs16_masks_a64s);
    tcase_add_test(tc, test_srs22_maps_to_sdma_addr);
    tcase_add_test(tc, test_srs23_noop);
    tcase_add_test(tc, test_plain_reg_roundtrip);
    tcase_add_test(tc, test_hrs_read_fallthrough);
    tcase_add_test(tc, test_hrs_write_noop);
    tcase_add_test(tc, test_platform_init_soft_reset);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = cm4_sdhci_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
