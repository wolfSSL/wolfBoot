/* unit-boot-state.c
 *
 * Host unit tests for the anti-rollback state machine in src/boot_state.c:
 * the bitwise CRC-32, the load/validate path and its BOOT_STATE_* codes,
 * trust-on-first-use anchoring, the rollback-audit path and the
 * skip-write-when-unchanged comparison. The storage backend is isolated behind
 * boot_state_backend_{read,write}(), so a mock backend exercises the whole
 * policy on the host. Built with plain gcc; boot_state.c has no inline asm.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#include "boot_state.h"

/* Mock backend and halt stub, referenced by boot_state.c. mock_read_code, when
 * nonzero, is returned directly by the read so the backend-level codes
 * (UNPROVISIONED/CORRUPT/UNAVAILABLE) can be injected; zero means "a record is
 * present", served from mock_nv. */
static uint8_t  mock_nv[sizeof(struct boot_state)];
static int      mock_read_code;
static int      mock_write_fail;
static int      mock_write_called;
static uint8_t  mock_write_buf[sizeof(struct boot_state)];
static jmp_buf  halt_env;
static int      halted;
#if defined(WOLFBOOT_ANTI_ROLLBACK_STRICT)
static const int strict = 1;
#else
static const int strict = 0;
#endif
static int      mock_lock_called;
static int      mock_lock_fail;

int boot_state_backend_read(uint8_t *buf, uint32_t len)
{
    if (mock_read_code != 0)
        return mock_read_code;
    memcpy(buf, mock_nv, len);
    return 0;
}

int boot_state_backend_write(const uint8_t *buf, uint32_t len)
{
    mock_write_called++;
    if (len <= sizeof(mock_write_buf)) {
        memcpy(mock_write_buf, buf, len);
        memcpy(mock_nv, buf, len);
    }
    return mock_write_fail ? -1 : 0;
}

int boot_state_backend_lock(void)
{
    mock_lock_called++;
    return mock_lock_fail ? -1 : 0;
}

void hal_hold_in_reset(void)
{
    halted = 1;
    longjmp(halt_env, 1);
}

#include "../../src/boot_state.c"

static int fails;
#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; } } while (0)

static void make_valid(struct boot_state *st, uint32_t anchor, uint32_t record)
{
    memset(st, 0, sizeof(*st));
    st->magic = BOOT_STATE_MAGIC;
    st->struct_version = BOOT_STATE_VERSION;
    st->anchor = anchor;
    st->record = record;
    st->crc = boot_state_compute_crc(st);
}

static void reset_mock(void)
{
    memset(mock_nv, 0, sizeof(mock_nv));
    mock_read_code = 0;
    mock_write_fail = 0;
    mock_write_called = 0;
    memset(mock_write_buf, 0, sizeof(mock_write_buf));
    halted = 0;
    mock_lock_called = 0;
    mock_lock_fail = 0;
}

int main(void)
{
    struct boot_state st, rec;

    /* CRC-32 known-answer: the IEEE 802.3 check value of "123456789". */
    CHECK(boot_state_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u,
          "crc32 KAT");

    /* Unprovisioned passes through unchanged. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNPROVISIONED;
    CHECK(boot_state_load(&st) == BOOT_STATE_UNPROVISIONED,
          "load unprovisioned");

    /* A well-formed record loads OK with its fields intact. */
    reset_mock();
    make_valid(&rec, 5, 5);
    memcpy(mock_nv, &rec, sizeof(rec));
    CHECK(boot_state_load(&st) == BOOT_STATE_OK, "load ok");
    CHECK(st.anchor == 5 && st.record == 5, "load ok fields");

    /* Wrong magic is corrupt (checked before the CRC). */
    reset_mock();
    make_valid(&rec, 5, 5);
    rec.magic ^= 0xFFu;
    memcpy(mock_nv, &rec, sizeof(rec));
    CHECK(boot_state_load(&st) == BOOT_STATE_CORRUPT, "load bad magic");

    /* A CRC mismatch is corrupt. */
    reset_mock();
    make_valid(&rec, 7, 7);
    rec.crc ^= 0xA5A5A5A5u;
    memcpy(mock_nv, &rec, sizeof(rec));
    CHECK(boot_state_load(&st) == BOOT_STATE_CORRUPT, "load bad crc");

    /* A present-but-malformed record (short read) is CORRUPT, not
     * UNAVAILABLE: the existing reference cannot be trusted and must halt,
     * instead of being waved through as a merely-unreachable backend. */
    reset_mock();
    mock_read_code = BOOT_STATE_CORRUPT;
    CHECK(boot_state_load(&st) == BOOT_STATE_CORRUPT, "load backend corrupt");

    /* An unreachable backend is UNAVAILABLE. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNAVAILABLE;
    CHECK(boot_state_load(&st) == BOOT_STATE_UNAVAILABLE,
          "load backend unavailable");

    /* An unreachable backend writes nothing and cannot be locked. Strict
     * mode refuses to boot without it. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNAVAILABLE;
    if (setjmp(halt_env) == 0) {
        boot_state_on_boot(7);
        CHECK(!strict, "unavailable did not halt in strict mode");
    }
    else {
        CHECK(strict, "unavailable halted in non-strict mode");
    }
    CHECK(mock_write_called == 0 && mock_lock_called == 0,
          "unavailable neither writes nor locks");

    /* A failed write warns and still locks; strict mode halts at the write. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNPROVISIONED;
    mock_write_fail = 1;
    if (setjmp(halt_env) == 0) {
        boot_state_on_boot(7);
        CHECK(!strict, "write failure did not halt in strict mode");
        CHECK(mock_lock_called == 1, "write failure still locks");
    }
    else {
        CHECK(strict, "write failure halted in non-strict mode");
        CHECK(mock_lock_called == 0, "strict write failure halts before lock");
    }

    /* A failed lock warns and boots; strict mode halts. */
    reset_mock();
    make_valid(&rec, 10, 10);
    memcpy(mock_nv, &rec, sizeof(rec));
    mock_lock_fail = 1;
    if (setjmp(halt_env) == 0) {
        boot_state_on_boot(10);
        CHECK(!strict, "lock failure did not halt in strict mode");
    }
    else {
        CHECK(strict, "lock failure halted in non-strict mode");
    }
    CHECK(mock_lock_called == 1, "lock attempted");

    /* Trust on first use: the first booted image sets anchor and record. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNPROVISIONED;
    boot_state_on_boot(42);
    CHECK(mock_write_called == 1, "tofu writes");
    memcpy(&st, mock_write_buf, sizeof(st));
    CHECK(st.anchor == 42 && st.record == 42, "tofu anchor/record");
    CHECK(mock_lock_called == 1, "tofu locks");

    /* A higher version advances the anchor. */
    reset_mock();
    make_valid(&rec, 10, 10);
    memcpy(mock_nv, &rec, sizeof(rec));
    boot_state_on_boot(20);
    CHECK(mock_write_called == 1, "advance writes");
    memcpy(&st, mock_write_buf, sizeof(st));
    CHECK(st.anchor == 20 && st.record == 20, "advance fields");

    /* Re-booting the same version changes nothing and skips the write. */
    reset_mock();
    make_valid(&rec, 10, 10);
    memcpy(mock_nv, &rec, sizeof(rec));
    boot_state_on_boot(10);
    CHECK(mock_write_called == 0, "same version skips write");
    CHECK(mock_lock_called == 1, "same version still locks");

    /* A validly signed older image still boots, keeps the anchor, and leaves
     * an audit event behind. */
    reset_mock();
    make_valid(&rec, 10, 10);
    memcpy(mock_nv, &rec, sizeof(rec));
    boot_state_on_boot(4);
    CHECK(mock_write_called == 1, "rollback writes audit");
    memcpy(&st, mock_write_buf, sizeof(st));
    CHECK(st.anchor == 10, "rollback keeps anchor");
    CHECK(st.record == 4, "rollback records booted");
    CHECK(st.rollback_count == 1, "rollback counted");
    CHECK(mock_lock_called == 1, "rollback locks");
    CHECK(st.last_rollback_from == 10 && st.last_rollback_to == 4,
          "rollback from/to");

    /* A corrupt reference halts the boot and is not overwritten. */
    reset_mock();
    make_valid(&rec, 9, 9);
    rec.crc ^= 1u;
    memcpy(mock_nv, &rec, sizeof(rec));
    if (setjmp(halt_env) == 0) {
        boot_state_on_boot(9);
        CHECK(0, "corrupt reference did not halt");
    }
    else {
        CHECK(halted == 1, "corrupt reference halted");
        CHECK(mock_write_called == 0, "corrupt reference not overwritten");
    }

    /* on_failure with a corrupt reference leaves it untouched and does not
     * halt: unlike on_boot, on_failure handles CORRUPT itself to avoid
     * replacing the corruption with a valid zeroed anchor. */
    reset_mock();
    make_valid(&rec, 9, 9);
    rec.crc ^= 1u;
    memcpy(mock_nv, &rec, sizeof(rec));
    boot_state_on_failure(BOOT_FAIL_VERIFY_BOTH);
    CHECK(mock_write_called == 0, "failure keeps corrupt reference");
    CHECK(halted == 0, "failure on corrupt does not halt");

    /* on_failure when unprovisioned starts a fresh record with the code. */
    reset_mock();
    mock_read_code = BOOT_STATE_UNPROVISIONED;
    boot_state_on_failure(BOOT_FAIL_NO_IMAGE);
    CHECK(mock_write_called == 1, "failure unprovisioned writes");
    memcpy(&st, mock_write_buf, sizeof(st));
    CHECK(st.boot_fail_count == 1, "failure count set");
    CHECK(st.last_fail_code == BOOT_FAIL_NO_IMAGE, "failure code recorded");

    /* on_failure on a valid record bumps the counter, keeps anchor/record. */
    reset_mock();
    make_valid(&rec, 10, 8);
    memcpy(mock_nv, &rec, sizeof(rec));
    boot_state_on_failure(BOOT_FAIL_VERIFY_BOTH);
    CHECK(mock_write_called == 1, "failure valid writes");
    memcpy(&st, mock_write_buf, sizeof(st));
    CHECK(st.boot_fail_count == 1, "failure valid count");
    CHECK(st.last_fail_code == BOOT_FAIL_VERIFY_BOTH, "failure valid code");
    CHECK(st.anchor == 10 && st.record == 8, "failure preserves anchor/record");

    if (fails == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("%d check(s) failed\n", fails);
    return 1;
}
