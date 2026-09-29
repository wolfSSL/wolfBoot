/* unit-tpm-i2c-zeroize.c
 *
 * Regression test for the WOLFBOOT_TPM_I2C variant of TPM2_IoCb() in
 * src/tpm.c leaving the TPM command frame resident in the staging buffer
 * tpm_i2c_write() builds.  The I2C transport cannot send the register byte
 * and the payload as two transfers, so it copies both into a local buffer;
 * a TPM command carrying a plaintext password authorization therefore stays
 * readable in bootloader stack SRAM unless that buffer is wiped.  The SPI
 * advanced-IO path is covered by unit-tpm-advio-zeroize.c.
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

#include "wolfboot/wolfboot.h"
#include "tpm.h"
#include "wolftpm/tpm2_tis.h"
#include "i2c_drv.h"

#define STAGE_BUF_SZ (MAX_SPI_FRAMESIZE + 1)

/* Plaintext authValue as it would appear inside a TPM2_NV_Write /
 * TPM2_Load authorization area handed down to the HAL callback. */
static const uint8_t test_auth[] = {
    'u', 'n', 'i', 't', '-', 'i', '2', 'c', '-', 'a', 'u', 't', 'h'
};

static uint8_t* captured_buf;
static int      i2c_writes;
static int      i2c_fail;

static uint8_t snapshot_buf[STAGE_BUF_SZ];

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

void i2c_init(void)
{
}

/* The staging buffer is the one argument the driver sees, so capture it
 * here; after tpm_i2c_write() returns its frame is dead but intact. */
int i2c_write(uint8_t addr, const uint8_t* buf, uint32_t len, int stop)
{
    (void)addr;
    (void)len;
    (void)stop;
    i2c_writes++;
    captured_buf = (uint8_t*)buf;
    return i2c_fail ? I2C_ERR_NACK : I2C_OK;
}

int i2c_read(uint8_t addr, uint8_t* buf, uint32_t len, int stop)
{
    uint32_t i;

    (void)addr;
    (void)stop;
    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(0xA0 + (i & 0x0F));
    return i2c_fail ? I2C_ERR_NACK : I2C_OK;
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
        volatile const uint8_t* _b =                                      \
            (volatile const uint8_t*)captured_buf;                        \
        unsigned _i;                                                      \
        for (_i = 0; _i < STAGE_BUF_SZ; _i++)                             \
            snapshot_buf[_i] = _b[_i];                                    \
    } while (0)

static void assert_no_residue(const uint8_t* snap, const char* which)
{
    unsigned i, j;

    for (i = 0; i + sizeof(test_auth) <= STAGE_BUF_SZ; i++) {
        for (j = 0; j < sizeof(test_auth); j++) {
            if (snap[i + j] != test_auth[j])
                break;
        }
        ck_assert_msg(j != sizeof(test_auth),
            "%s still holds the plaintext TPM authValue at offset %u", which,
            i);
    }
}

static void build_cmd(uint8_t* out, int len)
{
    int i;

    for (i = 0; i < len; i++)
        out[i] = (uint8_t)i;
    memcpy(out + 8, test_auth, sizeof(test_auth));
}

START_TEST(test_i2c_write_wipes_staging_buffer)
{
    uint8_t cmd[32];
    int rc;

    i2c_writes = 0;
    i2c_fail = 0;
    captured_buf = NULL;
    build_cmd(cmd, (int)sizeof(cmd));

    /* Snapshot before anything else is called: an intervening call pushes
     * its own frame over the very bytes under test. */
    rc = TPM2_IoCb(NULL, 0, TPM_TIS_DATA_FIFO_OFFSET, cmd, sizeof(cmd), NULL);
    SNAPSHOT_FRAME();
    ck_assert_int_eq(rc, TPM_RC_SUCCESS);
    ck_assert_ptr_nonnull(captured_buf);
    assert_no_residue(snapshot_buf, "the I2C staging buffer");
}
END_TEST

/* A failed transfer must not be the path that leaks: the wipe has to happen
 * on the error return as well. */
START_TEST(test_i2c_write_wipes_on_failure)
{
    uint8_t cmd[32];
    int rc;

    i2c_writes = 0;
    i2c_fail = 1;
    captured_buf = NULL;
    build_cmd(cmd, (int)sizeof(cmd));

    rc = TPM2_IoCb(NULL, 0, TPM_TIS_DATA_FIFO_OFFSET, cmd, sizeof(cmd), NULL);
    SNAPSHOT_FRAME();
    ck_assert_int_eq(rc, TPM_RC_FAILURE);
    ck_assert_ptr_nonnull(captured_buf);
    assert_no_residue(snapshot_buf, "the I2C staging buffer after a failure");
}
END_TEST

/* An oversize transfer must be refused rather than overrun the buffer. */
START_TEST(test_i2c_write_rejects_oversize)
{
    static uint8_t big[MAX_SPI_FRAMESIZE + 16];

    i2c_fail = 0;
    memset(big, 0x5A, sizeof(big));
    ck_assert_int_eq(tpm_i2c_write(TPM_TIS_DATA_FIFO_OFFSET, big, (int)sizeof(big)),
        BAD_FUNC_ARG);
    ck_assert_int_eq(tpm_i2c_read(TPM_TIS_DATA_FIFO_OFFSET, big, (int)sizeof(big)),
        BAD_FUNC_ARG);
}
END_TEST

static Suite* suite(void)
{
    Suite* s = suite_create("tpm-i2c-zeroize");
    TCase* tc = tcase_create("wipe");

    tcase_add_test(tc, test_i2c_write_wipes_staging_buffer);
    tcase_add_test(tc, test_i2c_write_wipes_on_failure);
    tcase_add_test(tc, test_i2c_write_rejects_oversize);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    SRunner* sr = srunner_create(suite());

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
