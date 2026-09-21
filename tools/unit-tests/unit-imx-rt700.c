/* unit-imx-rt700.c
 *
 * Unit tests for the i.MX RT700 XSPI0 flash address gate.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include "../../hal/imx_rt7xx.h"

#define NS   IMX_RT7XX_XSPI0_NS_BASE
#define SEC  IMX_RT7XX_XSPI0_S_BASE
#define SZ   IMX_RT7XX_XSPI0_SIZE

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_addr_ok_accepts_both_apertures(void)
{
    /* Start, an interior offset, and the last byte, in each aperture. */
    CHECK(imx_rt7xx_xspi0_addr_ok(NS, 1) == 0);
    CHECK(imx_rt7xx_xspi0_addr_ok(NS + 0x40000u, 0x1000) == 0);
    CHECK(imx_rt7xx_xspi0_addr_ok(NS + SZ - 1u, 1) == 0);
    CHECK(imx_rt7xx_xspi0_addr_ok(SEC, 1) == 0);
    CHECK(imx_rt7xx_xspi0_addr_ok(SEC + 0x180000u, 0x1000) == 0);
    CHECK(imx_rt7xx_xspi0_addr_ok(SEC + SZ - 1u, 1) == 0);
}

static void test_addr_ok_rejects_bad_length(void)
{
    CHECK(imx_rt7xx_xspi0_addr_ok(NS, 0) == -1);
    CHECK(imx_rt7xx_xspi0_addr_ok(NS, -1) == -1);
}

static void test_addr_ok_rejects_out_of_range(void)
{
    CHECK(imx_rt7xx_xspi0_addr_ok(NS + SZ - 2u, 4) == -1);
    CHECK(imx_rt7xx_xspi0_addr_ok(NS + SZ, 1) == -1);
}

static void test_addr_ok_rejects_foreign_apertures(void)
{
    /* Register block, secure SRAM, and a below-base address are not NOR. */
    CHECK(imx_rt7xx_xspi0_addr_ok(IMX_RT7XX_XSPI0_REGS_NS, 1) == -1);
    CHECK(imx_rt7xx_xspi0_addr_ok(0x30180000u, 1) == -1);
    CHECK(imx_rt7xx_xspi0_addr_ok(0x08000000u, 1) == -1);
}

static void test_aperture_and_offset_math(void)
{
    CHECK(IMX_RT7XX_XSPI0_OFFSET(NS + 0x40000u) == 0x40000u);
    CHECK(IMX_RT7XX_XSPI0_OFFSET(SEC + 0x40000u) == 0x40000u);
    CHECK(IMX_RT7XX_XSPI0_APERTURE(NS + 0x40000u) == NS);
    CHECK(IMX_RT7XX_XSPI0_APERTURE(SEC + 0x40000u) == SEC);
    /* Both apertures resolve to the same device offset. */
    CHECK(IMX_RT7XX_XSPI0_OFFSET(NS + 0x1234u) ==
          IMX_RT7XX_XSPI0_OFFSET(SEC + 0x1234u));
}

int main(void)
{
    test_addr_ok_accepts_both_apertures();
    test_addr_ok_rejects_bad_length();
    test_addr_ok_rejects_out_of_range();
    test_addr_ok_rejects_foreign_apertures();
    test_aperture_and_offset_math();

    if (failures == 0) {
        printf("unit-imx-rt700: all tests passed\n");
        return 0;
    }
    printf("unit-imx-rt700: %d failure(s)\n", failures);
    return 1;
}
