/* unit-ti-hercules-write.c
 *
 * Regression test for F-9736: the short-write path of hal_flash_write()
 * in hal/ti_hercules.c checked only len < WRITE_BLOCK_SIZE, not whether
 * (address % WRITE_BLOCK_SIZE) + len stayed inside the block. A short
 * write starting near the end of a block copied past the end of the
 * WRITE_BLOCK_SIZE stack staging buffer (memcpy(temp + off, data, len))
 * and programmed only the first block, losing the bytes intended for
 * the next block.
 *
 * hal/ti_hercules.c needs the TI FAPI vendor headers (board build tree)
 * and cannot be compiled on the host, but the bug is entirely in the
 * staging logic of hal_flash_write(), so the Makefile extracts
 * hal_flash_unlock_helper() and hal_flash_write() verbatim from
 * hal/ti_hercules.c (ti_hercules_write_extract.h) and runs them here
 * against emulated FAPI calls; Fapi_issueProgrammingCommand() performs
 * the program into a host flash array, so the test asserts on the
 * flash contents.
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
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* The extracted functions are RAMFUNCTION; on the host that is nothing. */
#define RAMFUNCTION
#define FLASHBUFFER_SIZE 4096

/* Hercules flash is memory-mapped: the short-write path reads the block
 * back with a plain memcpy from the flash address itself. Map the host
 * flash array at a fixed low address so those raw reads land in it. */
#define G_FLASH_BASE 0x10000UL

/* FAPI stubs: banks trivial, one auto-ECC mode, and an FSM that stays
 * busy for a few polls after each program. The programmed data only
 * lands in the array once the FSM reports ready, so code that issues a
 * command or reads the array back while a program is in flight sees
 * exactly what the hardware would give it: a rejected command and
 * stale data. */
typedef int Fapi_FlashBankType;
#define Fapi_Status_FsmReady        0
#define Fapi_AutoEccGeneration      1
#define FSM_BUSY_POLLS              3
#define FAPI_CHECK_FSM_READY_BUSY   fapi_check_fsm()

/* Emulated flash: 16 KB, erased (0xFF) initially. */
#define G_FLASH_SZ (4 * FLASHBUFFER_SIZE)
static uint8_t *g_flash; /* mmap'ed at G_FLASH_BASE in setup */

/* Programmed commands (address, size) for the assertions. */
#define MAX_PROGS 8
struct prog_rec {
    uint32_t addr;
    uint32_t size;
};
static struct prog_rec g_progs[MAX_PROGS];
static int g_progs_n;
static int g_prog_fail; /* nonzero = Fapi_issueProgrammingCommand fails */

/* FSM state: polls remaining, the program staged behind them, and a
 * sticky flag set if a command was issued while the FSM was busy. */
static int g_fsm_busy;
static int g_prog_while_busy;
static int g_pending;
static uint32_t g_pending_addr;
static uint32_t g_pending_size;
static uint8_t g_pending_data[FLASHBUFFER_SIZE];

static int fapi_check_fsm(void)
{
    if (g_fsm_busy > 0) {
        if (--g_fsm_busy == 0 && g_pending) {
            memcpy(g_flash + (g_pending_addr - G_FLASH_BASE),
                g_pending_data, g_pending_size);
            g_pending = 0;
        }
        return 1; /* not Fapi_Status_FsmReady */
    }
    return Fapi_Status_FsmReady;
}

static Fapi_FlashBankType f021_lookup_bank(uint32_t address)
{
    (void)address;
    return 0;
}

static int Fapi_setActiveFlashBank(Fapi_FlashBankType bank)
{
    (void)bank;
    return 0;
}

static int Fapi_enableMainBankSectors(uint16_t en)
{
    (void)en;
    return 0;
}

static int Fapi_issueProgrammingCommand(void *addr, uint8_t *data, int size,
    void *x, int y, int ecc)
{
    uint32_t a = (uint32_t)(uintptr_t)addr;

    (void)x; (void)y; (void)ecc;
    if (g_progs_n < MAX_PROGS) {
        g_progs[g_progs_n].addr = a;
        g_progs[g_progs_n].size = (uint32_t)size;
        g_progs_n++;
    }
    if (g_fsm_busy > 0)
        g_prog_while_busy = 1;
    if (g_prog_fail)
        return -1;
    if (a < G_FLASH_BASE ||
        a + (uint32_t)size > G_FLASH_BASE + G_FLASH_SZ)
        return -1;
    /* staged until the FSM drains (see fapi_check_fsm) */
    g_pending_addr = a;
    g_pending_size = (uint32_t)size;
    memcpy(g_pending_data, data, (size_t)size);
    g_pending = 1;
    g_fsm_busy = FSM_BUSY_POLLS;
    return 0;
}

void wolfBoot_printf(const char *format, ...)
{
    (void)format;
}

/* The real functions from hal/ti_hercules.c (extracted by the Makefile). */
#include "ti_hercules_write_extract.h"

static void setup(void)
{
    if (g_flash == NULL) {
        g_flash = mmap((void *)G_FLASH_BASE, G_FLASH_SZ,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (g_flash == MAP_FAILED) {
            fprintf(stderr, "mmap of flash region failed\n");
            exit(2);
        }
    }
    memset(g_flash, 0xFF, G_FLASH_SZ);
    memset(g_progs, 0, sizeof(g_progs));
    g_progs_n = 0;
    g_prog_fail = 0;
    g_fsm_busy = 0;
    g_prog_while_busy = 0;
    g_pending = 0;
}

static void teardown(void)
{
}

/* A short write inside one block: single read-modify-write, only the
 * written bytes change. */
START_TEST(test_short_write_fits_block)
{
    uint8_t data[100];
    uint32_t addr = G_FLASH_BASE + FLASHBUFFER_SIZE + 100;
    int i;

    for (i = 0; i < 100; i++)
        data[i] = (uint8_t)(0x10 + i);

    ck_assert_int_eq(hal_flash_write(addr, data, 100), 0);
    ck_assert_int_eq(g_progs_n, 1);
    ck_assert_uint_eq(g_progs[0].addr, G_FLASH_BASE + FLASHBUFFER_SIZE);
    ck_assert_uint_eq(g_progs[0].size, FLASHBUFFER_SIZE);
    for (i = 0; i < 100; i++)
        ck_assert_uint_eq(g_flash[addr + i - G_FLASH_BASE], data[i]);
    /* the rest of the block is untouched */
    ck_assert_uint_eq(g_flash[addr - 1 - G_FLASH_BASE], 0xFF);
    ck_assert_uint_eq(g_flash[addr + 100 - G_FLASH_BASE], 0xFF);
}
END_TEST

/* A short write crossing a block boundary (the F-9736 case): the first
 * partial block and the remainder in the next block must both be
 * programmed, and nothing may be copied past the staging buffer. The
 * second program must also wait for the FSM: issued back-to-back it is
 * rejected by the hardware, and the read-modify-write that stages the
 * next block would read the array while a program is in flight. */
START_TEST(test_short_write_crosses_block)
{
    uint8_t data[200];
    uint32_t addr = G_FLASH_BASE + 2 * FLASHBUFFER_SIZE - 100;
    int i;

    for (i = 0; i < 200; i++)
        data[i] = (uint8_t)(0x40 + i);

    ck_assert_int_eq(hal_flash_write(addr, data, 200), 0);
    ck_assert_int_eq(g_prog_while_busy, 0);

    /* first block: bytes 0-99 of data at the tail; next block: 100-199 */
    for (i = 0; i < 200; i++)
        ck_assert_uint_eq(g_flash[addr + i - G_FLASH_BASE], data[i]);
    ck_assert_int_eq(g_progs_n, 2);
    ck_assert_uint_eq(g_progs[0].addr, G_FLASH_BASE + FLASHBUFFER_SIZE);
    ck_assert_uint_eq(g_progs[1].addr, G_FLASH_BASE + 2 * FLASHBUFFER_SIZE);
}
END_TEST

/* A one-byte short write exactly at the last byte of a block: still a
 * single in-block RMW. */
START_TEST(test_short_write_at_block_end)
{
    uint8_t data[1] = { 0xAB };
    uint32_t addr = G_FLASH_BASE + 2 * FLASHBUFFER_SIZE - 1;

    ck_assert_int_eq(hal_flash_write(addr, data, 1), 0);
    ck_assert_int_eq(g_progs_n, 1);
    ck_assert_uint_eq(g_flash[addr - G_FLASH_BASE], 0xAB);
    ck_assert_uint_eq(g_flash[addr + 1 - G_FLASH_BASE], 0xFF);
}
END_TEST

/* A write of at least one full block keeps programming directly from
 * data in block-sized chunks (existing behavior). */
START_TEST(test_long_write_direct_chunks)
{
    uint8_t data[FLASHBUFFER_SIZE + 904];
    uint32_t addr = G_FLASH_BASE;
    int i;

    for (i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)(i & 0xFF);

    ck_assert_int_eq(hal_flash_write(addr, data, sizeof(data)), 0);
    ck_assert_int_eq(g_progs_n, 2);
    ck_assert_uint_eq(g_progs[0].size, FLASHBUFFER_SIZE);
    ck_assert_uint_eq(g_progs[1].addr, G_FLASH_BASE + FLASHBUFFER_SIZE);
    ck_assert_uint_eq(g_progs[1].size, 904);
    for (i = 0; i < sizeof(data); i++)
        ck_assert_uint_eq(g_flash[addr + i - G_FLASH_BASE], data[i]);
}
END_TEST

Suite *ti_hercules_write_suite(void)
{
    Suite *s = suite_create("ti-hercules-write");
    TCase *tc = tcase_create("ti-hercules-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_short_write_fits_block);
    tcase_add_test(tc, test_short_write_crosses_block);
    tcase_add_test(tc, test_short_write_at_block_end);
    tcase_add_test(tc, test_long_write_direct_chunks);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = ti_hercules_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
