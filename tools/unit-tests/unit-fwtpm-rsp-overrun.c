/* unit-fwtpm-rsp-overrun.c
 *
 * Regression test for F-11043: wcs_fwtpm_transmit() validated the
 * caller-supplied response capacity, then passed that (possibly short)
 * buffer straight to FWTPM_ProcessCommand. The fwTPM processor emits a
 * 10-byte error response for a malformed command regardless of the
 * offered capacity, so a non-secure caller offering less than 10
 * bytes got an out-of-range write before the wrapper compared rspLen
 * against the capacity.
 *
 * The fix processes into a max-sized staging buffer inside the veneer
 * and copies to the caller's buffer only after verifying the produced
 * length fits the snapshotted capacity.
 *
 * The real fwtpm_callable.c is included; FWTPM_ProcessCommand is
 * mocked to emulate the processor's behavior of writing the full
 * 10-byte error response no matter what *rspSize was on entry.
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

#define WOLFBOOT_TZ_FWTPM

/* Minimal types/macros that fwtpm_callable.c uses from wolftpm headers.
 * Poison the include guards of the heavy fwtpm headers so they become
 * no-ops; we define the handful of types/symbols we actually need
 * ourselves (same pattern as unit-fwtpm-nv-oob). */
#define _FWTPM_H_
#define _FWTPM_COMMAND_H_
#define _FWTPM_NV_H_

#define WOLFTPM2_NO_WOLFCRYPT

typedef uint8_t  byte;
typedef uint32_t word32;

#define BAD_FUNC_ARG    (-173)
#define TPM_RC_SUCCESS   0x000
#define TPM_RC_FAILURE   0x101
#define TPM_RC_INITIALIZE 0x100

#define XMEMCPY(d,s,l)  memcpy((d),(s),(l))
#define XMEMSET(b,c,l)  memset((b),(c),(l))

#define TPM2_HEADER_SIZE 10

typedef struct FWTPM_NV_HAL_S {
    int (*read)(void *ctx, word32 offset, byte *buf, word32 size);
    int (*write)(void *ctx, word32 offset, const byte *buf, word32 size);
    int (*erase)(void *ctx, word32 offset, word32 size);
    void *ctx;
    word32 maxSize;
} FWTPM_NV_HAL;

typedef struct FWTPM_CTX_S {
    int wasStarted;
} FWTPM_CTX;

/* Stub symbols referenced by fwtpm_callable.c */
unsigned int _start_heap;
unsigned int _heap_size;

void *wolfboot_store_sbrk(unsigned int incr, uint8_t **heap,
    uint8_t *start, uint32_t size)
{
    (void)incr; (void)heap; (void)start; (void)size;
    return NULL;
}

int FWTPM_Init(FWTPM_CTX *ctx) { (void)ctx; return 0; }
int FWTPM_NV_SetHAL(FWTPM_CTX *ctx, FWTPM_NV_HAL *hal)
    { (void)ctx; (void)hal; return 0; }

/* The 10-byte error response the fwTPM processor emits for a malformed
 * command: tag | size(=10) | rc, no handle area. */
static const uint8_t g_err_rsp[TPM2_HEADER_SIZE] = {
    0x80, 0xC1, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x09
};

/* Emulates the real processor: writes the full 10-byte error response
 * into rspBuf even when the offered *rspSize is smaller. */
int FWTPM_ProcessCommand(FWTPM_CTX *ctx, const byte *cmdBuf, int cmdSize,
    byte *rspBuf, int *rspSize, int locality)
{
    (void)ctx; (void)cmdBuf; (void)cmdSize; (void)locality;

    memcpy(rspBuf, g_err_rsp, TPM2_HEADER_SIZE);
    *rspSize = TPM2_HEADER_SIZE;
    return TPM_RC_SUCCESS;
}

/* Bring in the code under test as part of this translation unit. */
#include "../../src/fwtpm_callable.c"

#define MEM_SZ   64
#define GUARD_B  0x9C
static uint8_t g_mem[MEM_SZ];

static void mem_reset(void)
{
    int i;

    for (i = 0; i < MEM_SZ; i++)
        g_mem[i] = GUARD_B;
}

/* rsp is placed mid-array so any write past rspCapacity lands in the
 * guard tail. */
#define RSP_OFF  (MEM_SZ / 2 - 6)

/* A response capacity below the 10-byte minimum response must not
 * produce an out-of-range write; the veneer must refuse (the produced
 * response does not fit) instead of letting the processor overrun the
 * buffer. */
START_TEST(rsp_short_capacity_no_overrun)
{
    uint8_t *rsp = g_mem + RSP_OFF;
    uint32_t rspSz = 6; /* < 10: below any possible response */
    uint8_t cmd[4] = { 0x80, 0x01, 0x00, 0x04 };
    int i;
    int rc;

    mem_reset();
    wcs_fwtpm_init();

    rc = wcs_fwtpm_transmit(cmd, sizeof(cmd), rsp, &rspSz);
    ck_assert_int_eq(rc, TPM_RC_FAILURE);

    /* Nothing may have been written past the offered capacity. */
    for (i = (int)rspSz; i < MEM_SZ - RSP_OFF; i++) {
        ck_assert_msg(g_mem[RSP_OFF + i] == GUARD_B,
            "write past the offered response capacity (offset %d)", i);
    }
}
END_TEST

/* A capacity that fits the response gets the full response copied. */
START_TEST(rsp_fitting_capacity_gets_response)
{
    uint8_t *rsp = g_mem + RSP_OFF;
    uint32_t rspSz = 16;
    uint8_t cmd[4] = { 0x80, 0x01, 0x00, 0x04 };
    int i;
    int rc;

    mem_reset();
    wcs_fwtpm_init();

    rc = wcs_fwtpm_transmit(cmd, sizeof(cmd), rsp, &rspSz);
    ck_assert_int_eq(rc, TPM_RC_SUCCESS);
    ck_assert_uint_eq(rspSz, TPM2_HEADER_SIZE);
    for (i = 0; i < TPM2_HEADER_SIZE; i++)
        ck_assert_uint_eq(rsp[i], g_err_rsp[i]);

    /* Nothing past the produced response. */
    for (i = TPM2_HEADER_SIZE; i < (int)rspSz; i++)
        ck_assert_uint_eq(g_mem[RSP_OFF + i], GUARD_B);
}
END_TEST

/* Boundary: a capacity of exactly 10 fits the minimum response. */
START_TEST(rsp_exact_capacity_fits)
{
    uint8_t *rsp = g_mem + RSP_OFF;
    uint32_t rspSz = TPM2_HEADER_SIZE;
    uint8_t cmd[4] = { 0x80, 0x01, 0x00, 0x04 };
    int i;
    int rc;

    mem_reset();
    wcs_fwtpm_init();

    rc = wcs_fwtpm_transmit(cmd, sizeof(cmd), rsp, &rspSz);
    ck_assert_int_eq(rc, TPM_RC_SUCCESS);
    ck_assert_uint_eq(rspSz, TPM2_HEADER_SIZE);
    for (i = 0; i < TPM2_HEADER_SIZE; i++)
        ck_assert_uint_eq(rsp[i], g_err_rsp[i]);
    ck_assert_uint_eq(g_mem[RSP_OFF + TPM2_HEADER_SIZE], GUARD_B);
}
END_TEST

static Suite *fwtpm_rsp_overrun_suite(void)
{
    Suite *s = suite_create("fwtpm_rsp_overrun");
    TCase *tc = tcase_create("rsp-capacity-guard");

    tcase_add_test(tc, rsp_short_capacity_no_overrun);
    tcase_add_test(tc, rsp_fitting_capacity_gets_response);
    tcase_add_test(tc, rsp_exact_capacity_fits);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fwtpm_rsp_overrun_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails == 0 ? 0 : 1;
}
