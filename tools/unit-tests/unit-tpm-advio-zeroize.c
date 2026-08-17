/* unit-tpm-advio-zeroize.c
 *
 * Regression test for the WOLFTPM_ADV_IO variant of TPM2_IoCb() in src/tpm.c
 * leaving the TPM command/response frame resident in its stack staging
 * buffers (txBuf/rxBuf) when it returns.  With advanced IO the TIS layer in
 * wolfTPM hands the raw payload straight to the HAL callback, so the wipe
 * that TPM2_TIS_Read()/TPM2_TIS_Write() perform on their own txBuf/rxBuf
 * (lib/wolfTPM/src/tpm2_tis.c) is only done here.  A TPM command carrying a
 * plaintext password authorization therefore stays readable in bootloader
 * stack SRAM after the transfer completes.
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif

#include "wolfboot/wolfboot.h"
#include "tpm.h"
#include "wolftpm/tpm2_tis.h"

#define ADV_BUF_SZ (MAX_SPI_FRAMESIZE + TPM_TIS_HEADER_SZ)

/* Plaintext authValue as it would appear inside a TPM2_NV_Write /
 * TPM2_Load authorization area handed down to the HAL callback. */
static const uint8_t test_auth[] = {
    'u', 'n', 'i', 't', '-', 't', 'p', 'm', '-', 'a', 'u', 't', 'h'
};

static uint8_t*       captured_tx;
static uint8_t*       captured_rx;
static int            spi_calls;
static int            spi_fail_payload;
static int            spi_never_ready;

/* Snapshots of the (now dead) TPM2_IoCb() frame, taken by the tests with an
 * inline volatile copy loop so that no intervening call can reuse the stack
 * before the contents are inspected. */
static uint8_t snapshot_tx[ADV_BUF_SZ];
static uint8_t snapshot_rx[ADV_BUF_SZ];

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

void spi_init(int polarity, int phase)
{
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
}

/* Minimal TIS-speaking SPI slave: acknowledges the header (LSB of the last
 * header byte set) and answers a payload read with a recognizable pattern. */
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;

    (void)cs;
    (void)flags;
    spi_calls++;

    if (sz == 0) /* de-assert only */
        return 0;

    if (spi_calls == 1) {
        captured_tx = (uint8_t*)tx;
        captured_rx = rx;
        for (i = 0; i < sz; i++)
            rx[i] = 0;
        if (!spi_never_ready)
            rx[sz - 1] = TPM_TIS_READY_MASK;
        return 0;
    }

    if (spi_fail_payload)
        return -1;

    for (i = 0; i < sz; i++)
        rx[i] = (uint8_t)(0xA0 + (i & 0x0F));
    return 0;
}

void TPM2_ForceZero(void* mem, word32 len)
{
    volatile uint8_t* p = (volatile uint8_t*)mem;
    word32 i;

    for (i = 0; i < len; i++)
        p[i] = 0;
}

#include "../../src/tpm.c"

/* Copy the dead frame without calling anything (a memcpy() or a helper
 * function would push its own frame over the bytes under test). */
#define SNAPSHOT_FRAME()                                                  \
    do {                                                                  \
        volatile const uint8_t* _t = (volatile const uint8_t*)captured_tx;\
        volatile const uint8_t* _r = (volatile const uint8_t*)captured_rx;\
        unsigned _i;                                                      \
        for (_i = 0; _i < ADV_BUF_SZ; _i++) {                             \
            snapshot_tx[_i] = _t[_i];                                     \
            snapshot_rx[_i] = _r[_i];                                     \
        }                                                                 \
    } while (0)

static void assert_no_residue(const uint8_t* snap, const char* which)
{
    unsigned i, j;

    for (i = 0; i + sizeof(test_auth) <= ADV_BUF_SZ; i++) {
        for (j = 0; j < sizeof(test_auth); j++) {
            if (snap[i + j] != test_auth[j])
                break;
        }
        ck_assert_msg(j != sizeof(test_auth),
            "%s still holds the plaintext TPM authValue at offset %u", which,
            i);
    }
}

static void setup(void)
{
    captured_tx = NULL;
    captured_rx = NULL;
    spi_calls = 0;
    spi_fail_payload = 0;
    spi_never_ready = 0;
    memset(snapshot_tx, 0xFF, sizeof(snapshot_tx));
    memset(snapshot_rx, 0xFF, sizeof(snapshot_rx));
}

/* Normal return: the command (including its authorization area) was copied
 * into txBuf and must not survive the call. */
START_TEST(test_advio_write_wipes_txbuf)
{
    int rc;

    rc = TPM2_IoCb(&wolftpm_dev.ctx, 0 /* write */, 0x24, (uint8_t*)test_auth,
        (word16)sizeof(test_auth), NULL);
    SNAPSHOT_FRAME();

    ck_assert_int_eq(rc, 0);
    ck_assert_ptr_ne(captured_tx, NULL);
    assert_no_residue(snapshot_tx, "txBuf");
}
END_TEST

/* Wait-state error return: spi_xfer() fails on the payload transfer, so the
 * function returns early - the copy of the command is still in txBuf. */
START_TEST(test_advio_write_wipes_txbuf_on_error)
{
    int rc;

    spi_fail_payload = 1;
    rc = TPM2_IoCb(&wolftpm_dev.ctx, 0 /* write */, 0x24, (uint8_t*)test_auth,
        (word16)sizeof(test_auth), NULL);
    SNAPSHOT_FRAME();

    ck_assert_int_ne(rc, 0);
    ck_assert_ptr_ne(captured_tx, NULL);
    assert_no_residue(snapshot_tx, "txBuf");
}
END_TEST

/* Timeout error return: the wait-state loop never sees the ready bit, so
 * TPM2_IoCb() bails out through the de-assert path. */
START_TEST(test_advio_write_wipes_txbuf_on_timeout)
{
    int rc;

    spi_never_ready = 1;
    rc = TPM2_IoCb(&wolftpm_dev.ctx, 0 /* write */, 0x24, (uint8_t*)test_auth,
        (word16)sizeof(test_auth), NULL);
    SNAPSHOT_FRAME();

    ck_assert_int_ne(rc, 0);
    ck_assert_ptr_ne(captured_tx, NULL);
    assert_no_residue(snapshot_tx, "txBuf");
}
END_TEST

/* Read: the TPM response lands in rxBuf and is copied out to the caller;
 * the staging copy must not be left behind. */
START_TEST(test_advio_read_wipes_rxbuf)
{
    uint8_t out[sizeof(test_auth)];
    unsigned i;
    int rc;

    rc = TPM2_IoCb(&wolftpm_dev.ctx, 1 /* read */, 0x24, out,
        (word16)sizeof(out), NULL);
    SNAPSHOT_FRAME();

    ck_assert_int_eq(rc, 0);
    ck_assert_ptr_ne(captured_rx, NULL);
    /* the response really was delivered to the caller ... */
    for (i = 0; i < sizeof(out); i++)
        ck_assert_uint_eq(out[i], (uint8_t)(0xA0 + (i & 0x0F)));
    /* ... and no copy of it remains in the staging buffer */
    for (i = 0; i < ADV_BUF_SZ; i++) {
        ck_assert_msg(snapshot_rx[i] == 0,
            "rxBuf still holds TPM response byte 0x%02x at offset %u",
            snapshot_rx[i], i);
    }
}
END_TEST

static Suite *tpm_advio_zeroize_suite(void)
{
    Suite *s = suite_create("tpm_advio_zeroize");
    TCase *tc = tcase_create("zeroize");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_advio_write_wipes_txbuf);
    tcase_add_test(tc, test_advio_write_wipes_txbuf_on_error);
    tcase_add_test(tc, test_advio_write_wipes_txbuf_on_timeout);
    tcase_add_test(tc, test_advio_read_wipes_rxbuf);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = tpm_advio_zeroize_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
