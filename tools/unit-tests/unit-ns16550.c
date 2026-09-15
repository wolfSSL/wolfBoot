/* unit-ns16550.c
 *
 * Unit tests for the instance-based NS16550 UART driver.
 *
 * The driver is pointed at a plain malloc'd buffer standing in for the
 * register block, so the register offsets, the reg-shift/reg-offset
 * arithmetic, the divisor rounding and the timeout bound are all checked on
 * the host. A "port" is emulated by pre-setting the LSR byte.
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
#include <stdlib.h>
#include <string.h>

#include "../../include/ns16550.h"
#include "../../hal/uart/ns16550.c"

/* A register window big enough for the Xilinx layout (reg-offset 0x1000,
 * reg-shift 2, eight registers). */
#define REGS_SIZE 0x1100

static uint8_t *regs;

/* Address of register `r`, derived from the device the same way the driver
 * derives it, so the test tracks base + reg_off + (r << reg_shift). */
static volatile uint32_t *reg32(const struct ns16550_dev *d, uint32_t r)
{
    return (volatile uint32_t *)((uint8_t *)d->base + d->reg_off +
        (r << d->reg_shift));
}
static volatile uint8_t *reg8(const struct ns16550_dev *d, uint32_t r)
{
    return (volatile uint8_t *)((uint8_t *)d->base + d->reg_off +
        (r << d->reg_shift));
}

static void setup(void)
{
    regs = calloc(1, REGS_SIZE);
    ck_assert_ptr_nonnull(regs);
}
static void teardown(void)
{
    free(regs);
    regs = NULL;
}

/* The customer's node: xlnx,xps-uart16550-2.00.a, reg-offset 0x1000,
 * reg-shift 2, clock-frequency 0x5f5dd19 (99,999,001 Hz). */
static void dev_xilinx(struct ns16550_dev *d)
{
    memset(d, 0, sizeof(*d));
    d->base = (uintptr_t)regs;
    d->reg_off = 0x1000;
    d->reg_shift = 2;
    d->clk_hz = 0x5f5dd19;
    d->io_width = 0; /* let the driver derive 32-bit from reg_shift */
}

/* A byte-spaced legacy port. */
static void dev_legacy(struct ns16550_dev *d)
{
    memset(d, 0, sizeof(*d));
    d->base = (uintptr_t)regs;
    d->reg_off = 0;
    d->reg_shift = 0;
    d->clk_hz = 1843200; /* the classic 1.8432 MHz crystal */
    d->io_width = 0;
}

START_TEST(test_ns16550_init_programs_xilinx_divisor)
{
    struct ns16550_dev d;
    uint32_t div;

    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_OK);

    /* reg_shift 2 must have selected 32-bit accesses */
    ck_assert_uint_eq(d.io_width, 4);

    /* 99999001 / (16 * 115200) = 54.25 -> rounds to 54 */
    div = (*reg32(&d, NS16550_DLM) << 8) | *reg32(&d, NS16550_DLL);
    ck_assert_uint_eq(div, 54);

    /* DLAB must be back down so THR is addressable, and the format 8N1 */
    ck_assert_uint_eq(*reg32(&d, NS16550_LCR), NS16550_LCR_8N1);
    /* interrupts off, FIFOs enabled and reset, DTR/RTS asserted */
    ck_assert_uint_eq(*reg32(&d, NS16550_IER), 0);
    ck_assert_uint_eq(*reg32(&d, NS16550_FCR),
        NS16550_FCR_ENABLE | NS16550_FCR_RXRST | NS16550_FCR_TXRST);
    ck_assert_uint_eq(*reg32(&d, NS16550_MCR),
        NS16550_MCR_DTR | NS16550_MCR_RTS);
}
END_TEST

START_TEST(test_ns16550_init_legacy_byte_spaced)
{
    struct ns16550_dev d;
    uint32_t div;

    dev_legacy(&d);
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_OK);
    ck_assert_uint_eq(d.io_width, 1);

    /* 1843200 / (16 * 115200) = exactly 1 */
    div = ((uint32_t)*reg8(&d, NS16550_DLM) << 8) | *reg8(&d, NS16550_DLL);
    ck_assert_uint_eq(div, 1);
    ck_assert_uint_eq(*reg8(&d, NS16550_LCR), NS16550_LCR_8N1);

    /* 9600 on the same clock: 1843200 / 153600 = 12 */
    ck_assert_int_eq(ns16550_init(&d, 9600), NS16550_OK);
    div = ((uint32_t)*reg8(&d, NS16550_DLM) << 8) | *reg8(&d, NS16550_DLL);
    ck_assert_uint_eq(div, 12);

    /* a divisor above 8 bits must land in both latches */
    ck_assert_int_eq(ns16550_init(&d, 110), NS16550_OK);
    div = ((uint32_t)*reg8(&d, NS16550_DLM) << 8) | *reg8(&d, NS16550_DLL);
    ck_assert_uint_eq(div, 1047);
    ck_assert_uint_gt(*reg8(&d, NS16550_DLM), 0);
}
END_TEST

START_TEST(test_ns16550_init_rejects_bad_arguments)
{
    struct ns16550_dev d;

    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(NULL, 115200), NS16550_ERR_ARG);
    ck_assert_int_eq(ns16550_init(&d, 0), NS16550_ERR_ARG);

    dev_xilinx(&d);
    d.base = 0;
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_ERR_ARG);

    dev_xilinx(&d);
    d.reg_shift = 4;
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_ERR_ARG);

    /* baud far above the clock rounds the divisor to zero */
    dev_xilinx(&d);
    d.clk_hz = 100000;
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_ERR_CLK);

    /* and a clock far above the baud overflows the 16-bit latch */
    dev_xilinx(&d);
    d.clk_hz = 0xF0000000U;
    ck_assert_int_eq(ns16550_init(&d, 110), NS16550_ERR_CLK);

    /* A baud whose * 16 wraps 32 bits is refused up front. 0x10000000 is
     * the first such value and would otherwise divide by zero. */
    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 0x10000000U), NS16550_ERR_ARG);
    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 0xFFFFFFFFU), NS16550_ERR_ARG);
}
END_TEST

START_TEST(test_ns16550_write_emits_bytes_when_thre_set)
{
    struct ns16550_dev d;
    const char msg[] = "WOLFBOOT READY\r\n";

    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_OK);

    /* emulate a port that is always ready and always fully drained */
    *reg32(&d, NS16550_LSR) = NS16550_LSR_THRE | NS16550_LSR_TEMT;

    ck_assert_int_eq(ns16550_write(&d, msg, sizeof(msg) - 1), NS16550_OK);
    /* THR is write-only and not a FIFO here, so only the last byte lands */
    ck_assert_uint_eq(*reg32(&d, NS16550_THR), (uint32_t)(uint8_t)'\n');

    /* a zero-length write is legal and still drains */
    ck_assert_int_eq(ns16550_write(&d, msg, 0), NS16550_OK);
    ck_assert_int_eq(ns16550_write(&d, NULL, 0), NS16550_OK);
}
END_TEST

START_TEST(test_ns16550_write_times_out_on_absent_port)
{
    struct ns16550_dev d;

    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_OK);

    /* LSR stuck at 0: an unprogrammed PL region. The write must give up
     * rather than spin forever - this driver runs on the boot path. */
    *reg32(&d, NS16550_LSR) = 0;
    ck_assert_int_eq(ns16550_write(&d, "x", 1), NS16550_ERR_TMO);

    /* THRE set but TEMT never: the bytes go out, the final drain times out */
    *reg32(&d, NS16550_LSR) = NS16550_LSR_THRE;
    ck_assert_int_eq(ns16550_write(&d, "x", 1), NS16550_ERR_TMO);

    ck_assert_int_eq(ns16550_write(NULL, "x", 1), NS16550_ERR_ARG);
    ck_assert_int_eq(ns16550_write(&d, NULL, 1), NS16550_ERR_ARG);
}
END_TEST

/* reg-shift/reg-offset arithmetic must not alias two registers together. */
START_TEST(test_ns16550_register_spacing_is_distinct)
{
    struct ns16550_dev d;

    dev_xilinx(&d);
    ck_assert_int_eq(ns16550_init(&d, 115200), NS16550_OK);

    /* LCR (0x03) at reg-offset 0x1000, reg-shift 2 lands at 0x100C */
    ck_assert_ptr_eq((void *)reg32(&d, NS16550_LCR), (void *)(regs + 0x100C));
    /* and the block starts at reg-offset, not at base */
    ck_assert_ptr_eq((void *)reg32(&d, NS16550_THR), (void *)(regs + 0x1000));
    /* nothing was written below reg-offset */
    {
        size_t i;
        for (i = 0; i < 0x1000; i++) {
            ck_assert_uint_eq(regs[i], 0);
        }
    }
}
END_TEST

/* can_read reflects LSR.DR, and read() returns the byte in RBR. */
START_TEST(test_ns16550_read_returns_byte_when_dr_set)
{
    struct ns16550_dev d;
    uint8_t c = 0;
    int ret;

    memset(&d, 0, sizeof(d));
    d.base = (uintptr_t)regs;
    d.io_width = 1;
    d.clk_hz = 100000000;
    (void)ns16550_init(&d, 115200);

    /* No data yet. */
    *reg8(&d, NS16550_LSR) = 0;
    ck_assert_int_eq(ns16550_can_read(&d), 0);

    /* Port presents a byte. */
    *reg8(&d, NS16550_RBR) = 0x5A;
    *reg8(&d, NS16550_LSR) = NS16550_LSR_DR;
    ck_assert_int_eq(ns16550_can_read(&d), 1);

    ret = ns16550_read(&d, &c);
    ck_assert_int_eq(ret, NS16550_OK);
    ck_assert_uint_eq(c, 0x5A);
}
END_TEST

/* read() must bound its wait rather than spin forever on a dead port, and
 * both entry points must reject bad arguments instead of dereferencing. */
START_TEST(test_ns16550_read_times_out_and_checks_arguments)
{
    struct ns16550_dev d;
    uint8_t c = 0;

    memset(&d, 0, sizeof(d));
    d.base = (uintptr_t)regs;
    d.io_width = 1;
    d.clk_hz = 100000000;
    (void)ns16550_init(&d, 115200);

    /* DR never asserts: the call returns rather than hanging. */
    *reg8(&d, NS16550_LSR) = 0;
    ck_assert_int_eq(ns16550_read(&d, &c), NS16550_ERR_TMO);

    ck_assert_int_eq(ns16550_read(NULL, &c), NS16550_ERR_ARG);
    ck_assert_int_eq(ns16550_read(&d, NULL), NS16550_ERR_ARG);
    ck_assert_int_eq(ns16550_can_read(NULL), 0);

    /* A device with no base is not a port. */
    memset(&d, 0, sizeof(d));
    ck_assert_int_eq(ns16550_can_read(&d), 0);
    ck_assert_int_eq(ns16550_read(&d, &c), NS16550_ERR_ARG);
}
END_TEST

static Suite *ns16550_suite(void)
{
    Suite *s = suite_create("ns16550");
    TCase *tc = tcase_create("ns16550");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_ns16550_init_programs_xilinx_divisor);
    tcase_add_test(tc, test_ns16550_init_legacy_byte_spaced);
    tcase_add_test(tc, test_ns16550_init_rejects_bad_arguments);
    tcase_add_test(tc, test_ns16550_write_emits_bytes_when_thre_set);
    tcase_add_test(tc, test_ns16550_write_times_out_on_absent_port);
    tcase_add_test(tc, test_ns16550_register_spacing_is_distinct);
    tcase_add_test(tc, test_ns16550_read_returns_byte_when_dr_set);
    tcase_add_test(tc, test_ns16550_read_times_out_and_checks_arguments);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = ns16550_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
