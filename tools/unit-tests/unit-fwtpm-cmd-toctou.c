/* unit-fwtpm-cmd-toctou.c
 *
 * Regression test for F-11044: wcs_fwtpm_transmit() validated that cmd
 * identifies non-secure memory and then passed that mutable buffer
 * directly to FWTPM_ProcessCommand. The processor parses the packet
 * more than once (authentication, then execution), so a DMA-capable
 * non-secure attacker who rewrites the command buffer in between can
 * make the authenticated command differ from the executed command.
 *
 * The fix copies the command into secure staging memory after the
 * range validation and invokes the processor only on that copy, which
 * an NS DMA master cannot touch.
 *
 * The real fwtpm_callable.c is included; FWTPM_ProcessCommand is
 * mocked with two parse points (authentication, execution) and the
 * test plays the attacker, rewriting the NS command buffer at the
 * window between them. The mock may only rewrite the caller's buffer,
 * never the secure staging, mirroring the hardware boundary.
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

static const uint8_t g_err_rsp[TPM2_HEADER_SIZE] = {
    0x80, 0xC1, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x09
};

#define CMD_SZ 24
static uint8_t g_ns_cmd[CMD_SZ]; /* the non-secure world's command */

/* 1 when the attacker fires in the parse window of this call. */
static int g_attack;

/* What the processor saw at each parse point. */
static uint8_t g_authed[CMD_SZ];
static uint8_t g_executed[CMD_SZ];

int FWTPM_ProcessCommand(FWTPM_CTX *ctx, const byte *cmdBuf, int cmdSize,
    byte *rspBuf, int *rspSize, int locality)
{
    (void)ctx; (void)locality;

    /* Authentication parse. */
    memcpy(g_authed, cmdBuf, (size_t)cmdSize);

    if (g_attack) {
        /* The attacker's DMA rewrite reaches only NS memory - the
         * caller's command buffer - never secure staging. */
        memset(g_ns_cmd, 0x5A, (size_t)cmdSize);
    }

    /* Execution parse. */
    memcpy(g_executed, cmdBuf, (size_t)cmdSize);

    memcpy(rspBuf, g_err_rsp, TPM2_HEADER_SIZE);
    *rspSize = TPM2_HEADER_SIZE;
    return TPM_RC_SUCCESS;
}

/* Bring in the code under test as part of this translation unit. */
#include "../../src/fwtpm_callable.c"

/* With the attacker firing mid-processing, the command the processor
 * authenticates must be byte-identical to the command it executes:
 * pre-fix the processor parses the caller's mutable buffer, so the
 * rewrite changes the executed command. */
START_TEST(cmd_immune_to_midflight_rewrite)
{
    uint8_t rsp[16];
    uint32_t rspSz = sizeof(rsp);
    int i;
    int rc;

    for (i = 0; i < CMD_SZ; i++)
        g_ns_cmd[i] = (uint8_t)(0x10 + i);

    wcs_fwtpm_init();
    g_attack = 1;

    rc = wcs_fwtpm_transmit(g_ns_cmd, CMD_SZ, rsp, &rspSz);
    ck_assert_int_eq(rc, TPM_RC_SUCCESS);
    ck_assert_uint_eq(rspSz, TPM2_HEADER_SIZE);

    ck_assert_mem_eq(g_authed, g_executed, CMD_SZ);
    /* The attacker did rewrite the NS buffer (the attack model fired);
     * the processor just did not act on it. */
    ck_assert_uint_eq(g_ns_cmd[0], 0x5A);
    for (i = 0; i < CMD_SZ; i++)
        ck_assert_uint_eq(g_executed[i], (uint8_t)(0x10 + i));
}
END_TEST

/* No attacker: an ordinary transmission is unaffected by the staging. */
START_TEST(cmd_plain_transmission_intact)
{
    uint8_t rsp[16];
    uint32_t rspSz = sizeof(rsp);
    int i;
    int rc;

    for (i = 0; i < CMD_SZ; i++)
        g_ns_cmd[i] = (uint8_t)(0x40 + i);

    wcs_fwtpm_init();
    g_attack = 0;

    rc = wcs_fwtpm_transmit(g_ns_cmd, CMD_SZ, rsp, &rspSz);
    ck_assert_int_eq(rc, TPM_RC_SUCCESS);
    ck_assert_uint_eq(rspSz, TPM2_HEADER_SIZE);
    ck_assert_mem_eq(g_authed, g_executed, CMD_SZ);
    for (i = 0; i < CMD_SZ; i++)
        ck_assert_uint_eq(g_executed[i], (uint8_t)(0x40 + i));
}
END_TEST

static Suite *fwtpm_cmd_toctou_suite(void)
{
    Suite *s = suite_create("fwtpm_cmd_toctou");
    TCase *tc = tcase_create("midflight-rewrite");

    tcase_add_test(tc, cmd_immune_to_midflight_rewrite);
    tcase_add_test(tc, cmd_plain_transmission_intact);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fwtpm_cmd_toctou_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails == 0 ? 0 : 1;
}
