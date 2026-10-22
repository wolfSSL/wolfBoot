/* unit-nvm-journal.c
 *
 * Unit tests for the NVM_FLASH_JOURNAL trailer journal in src/libwolfboot.c,
 * against a mock of ECC flash that programs each unit once. Each boot runs in
 * a forked child, so it starts without journal RAM state but keeps the flash.
 * The file builds once without and once with NVM_FLASH_JOURNAL_DUAL.
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
#include <sys/wait.h>
#include <unistd.h>

#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "image.h"

#ifndef NVM_FLASH_JOURNAL
#error "build with NVM_FLASH_JOURNAL"
#endif

#define UNIT       WOLFBOOT_FLASH_WRITE_UNIT
#define PART_SIZE  WOLFBOOT_PARTITION_SIZE
#define SECT_SIZE  WOLFBOOT_SECTOR_SIZE
#define PART_UNITS (PART_SIZE / UNIT)
#define NSECT      (PART_SIZE / SECT_SIZE)

/* On-flash format, as in src/libwolfboot.c */
#define SLOT_SIZE    ((UNIT > 16) ? UNIT : 16)
#define TRAILER_SIZE (((4 + 1 + (NSECT + 1) / 2) + 3) & ~3)
#define SLOTS        ((SECT_SIZE - TRAILER_SIZE) / SLOT_SIZE)
#ifndef NVM_FLASH_JOURNAL_DUAL
/* The last slot holds the erase marker */
#define LOG_SLOTS    (SLOTS - 1)
#define MARK_SLOT    (SLOTS - 1)
#endif

#define EXIT_CUT   100

/* Torn modes for a cut program */
#define TORN_NONE   0 /* the program never happens */
#define TORN_JUNK   1 /* partly done, the unit reads as programmed */
#define TORN_ERASED 2 /* partly done, the unit still reads as erased */

/* How much a write that returns an error still writes */
#define FAIL_WRITES_ALL   0 /* all of the data, as a TC3xx verify error can */
#define FAIL_WRITES_PART  1 /* only the start, so it reads as a bad record */
#define FAIL_WRITES_NONE  2 /* nothing, so the unit still reads as erased */

struct mock_flash {
    int     ops;        /* programs and erases in this boot */
    int     cut_at;     /* reset at this op, 0 for none */
    int     torn;       /* TORN_* for a cut program */
    uint32_t fail_ops;  /* ops that return an error, bit n for op n.
                         * fail_writes says how much a program writes, and
                         * an erase does nothing. */
    int     fail_writes; /* FAIL_WRITES_* for a failing write */
    uintptr_t check_fail; /* blank checks that start here fail, 0 for none */
    int     violations; /* writes the hardware would reject */
    int     erases;     /* sector erases */
    uint8_t programmed[2][PART_UNITS];
    uint8_t weak[2][PART_UNITS]; /* cut program that reads as erased */
    /* results reported by a child */
    int     boot_ret, update_ret;
    uint8_t boot_st, update_st;
    uint8_t flags[NSECT];
    uint8_t flag;       /* read by child_get_flag */
    int     ret;
};
static struct mock_flash *mf;

/* Arguments for the child, inherited through fork() */
static int arg_n;
static uint8_t arg_val;

static int part_of(uintptr_t addr, uint32_t *unit)
{
    if ((addr >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (addr < WOLFBOOT_PARTITION_BOOT_ADDRESS + PART_SIZE)) {
        *unit = (addr - WOLFBOOT_PARTITION_BOOT_ADDRESS) / UNIT;
        return 0;
    }
    if ((addr >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (addr < WOLFBOOT_PARTITION_UPDATE_ADDRESS + PART_SIZE)) {
        *unit = (addr - WOLFBOOT_PARTITION_UPDATE_ADDRESS) / UNIT;
        return 1;
    }
    return -1;
}

static int mock_write(uintptr_t addr, const uint8_t *data, int len)
{
    uint8_t *dst = (uint8_t *)addr;
    uint32_t u, n;
    int p = part_of(addr, &u);
    int i;

    if ((p < 0) || (len <= 0) || ((addr % UNIT) != 0) || ((len % UNIT) != 0)) {
        mf->violations++;
        return -1;
    }
    for (n = 0; n < (uint32_t)len / UNIT; n++) {
        if (mf->programmed[p][u + n]) {
            mf->violations++;
            return -1;
        }
    }
    mf->ops++;
    if (mf->cut_at == mf->ops) {
        if (mf->torn != TORN_NONE) {
            /* Only the start of the record lands */
            memcpy(dst, data, len / 4);
            for (n = 0; n < (uint32_t)len / UNIT; n++) {
                if (mf->torn == TORN_JUNK)
                    mf->programmed[p][u + n] = 1;
                else
                    mf->weak[p][u + n] = 1;
            }
        }
        _exit(EXIT_CUT);
    }
    if ((mf->fail_writes != FAIL_WRITES_ALL) && (mf->ops < 32) &&
            (((mf->fail_ops >> mf->ops) & 1U) != 0)) {
        if (mf->fail_writes == FAIL_WRITES_PART) {
            memcpy(dst, data, len / 4);
            for (n = 0; n < (uint32_t)len / UNIT; n++)
                mf->programmed[p][u + n] = 1;
        }
        return -1;
    }
    for (i = 0; i < len; i++) {
        /* Programming over a cut program only works for the same data */
        if (mf->weak[p][u + i / UNIT] && (dst[i] != FLASH_BYTE_ERASED) &&
                (dst[i] != data[i]))
            dst[i] = (uint8_t)~data[i];
        else
            dst[i] = data[i];
    }
    for (n = 0; n < (uint32_t)len / UNIT; n++) {
        mf->programmed[p][u + n] = 1;
        mf->weak[p][u + n] = 0;
    }
    if ((mf->ops < 32) && (((mf->fail_ops >> mf->ops) & 1U) != 0)) {
        return -1;
    }
    return 0;
}

static int mock_erase(uintptr_t addr, int len)
{
    uint32_t u;
    int p = part_of(addr, &u);

    if ((p < 0) || (len <= 0) || ((addr % SECT_SIZE) != 0) ||
            ((len % SECT_SIZE) != 0)) {
        mf->violations++;
        return -1;
    }
    while (len > 0) {
        mf->ops++;
        if (mf->cut_at == mf->ops)
            _exit(EXIT_CUT);
        if ((mf->ops < 32) && (((mf->fail_ops >> mf->ops) & 1U) != 0))
            return -1;
        memset((void *)addr, FLASH_BYTE_ERASED, SECT_SIZE);
        memset(&mf->programmed[p][u], 0, SECT_SIZE / UNIT);
        memset(&mf->weak[p][u], 0, SECT_SIZE / UNIT);
        mf->erases++;
        addr += SECT_SIZE;
        u += SECT_SIZE / UNIT;
        len -= SECT_SIZE;
    }
    return 0;
}

static int mock_is_erased(uintptr_t addr, int len)
{
    uint32_t u, end;
    int p = part_of(addr, &u);

    if (p < 0)
        return -1;
    if ((mf->check_fail != 0) && (addr == mf->check_fail))
        return -1;
    end = (addr + len - 1 - (p ? WOLFBOOT_PARTITION_UPDATE_ADDRESS :
        WOLFBOOT_PARTITION_BOOT_ADDRESS)) / UNIT;
    for (; u <= end; u++) {
        if (mf->programmed[p][u])
            return 0;
    }
    return 1;
}

int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    return mock_write(address, data, len);
}
int hal_flash_erase(haladdr_t address, int len)
{
    return mock_erase(address, len);
}
int hal_flash_is_erased(haladdr_t address, int len)
{
    return mock_is_erased(address, len);
}
void hal_flash_unlock(void) {}
void hal_flash_lock(void) {}
#ifdef EXT_FLASH
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return mock_write(address, data, len);
}
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    memcpy(data, (void *)address, len);
    return len;
}
int ext_flash_erase(uintptr_t address, int len)
{
    return mock_erase(address, len);
}
int ext_flash_is_erased(uintptr_t address, int len)
{
    return mock_is_erased(address, len);
}
void ext_flash_unlock(void) {}
void ext_flash_lock(void) {}
#endif

static uintptr_t trailer_sector(int p)
{
    return (p ? WOLFBOOT_PARTITION_UPDATE_ADDRESS :
        WOLFBOOT_PARTITION_BOOT_ADDRESS) + PART_SIZE - SECT_SIZE;
}

static uintptr_t slot_addr(int p, int slot)
{
    return trailer_sector(p) + (uintptr_t)slot * SLOT_SIZE;
}

static int slot_programmed(int p, int slot)
{
    return !mock_is_erased(slot_addr(p, slot), SLOT_SIZE);
}

/* Program raw bytes, as a factory image or a stray write would */
static void raw_program(uintptr_t addr, const void *data, int len)
{
    uint32_t u, n;
    int p = part_of(addr, &u);

    ck_assert_int_ge(p, 0);
    memcpy((void *)addr, data, len);
    for (n = 0; n < (uint32_t)(len + UNIT - 1) / UNIT; n++)
        mf->programmed[p][u + n] = 1;
}

static void flash_erase_all(void)
{
    memset((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS, FLASH_BYTE_ERASED,
        PART_SIZE);
    memset((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS, FLASH_BYTE_ERASED,
        PART_SIZE);
    memset(mf->programmed, 0, sizeof(mf->programmed));
    memset(mf->weak, 0, sizeof(mf->weak));
    mf->fail_ops = 0;
    mf->fail_writes = FAIL_WRITES_ALL;
    mf->check_fail = 0;
    mf->violations = 0;
    mf->erases = 0;
}

typedef void (*boot_fn)(void);

/* Run fn as one boot with fresh RAM and shared flash. Return its exit code. */
static int boot(boot_fn fn, int cut_at, int torn)
{
    pid_t pid;
    int st = 0;

    fflush(stdout);
    fflush(stderr);
    mf->cut_at = cut_at;
    mf->torn = torn;
    pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        mf->ops = 0;
        fn();
        _exit(0);
    }
    waitpid(pid, &st, 0);
    mf->cut_at = 0;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void child_snapshot(void)
{
    int i;
    mf->boot_ret = wolfBoot_get_partition_state(PART_BOOT, &mf->boot_st);
    mf->update_ret = wolfBoot_get_partition_state(PART_UPDATE, &mf->update_st);
    for (i = 0; i < NSECT; i++) {
        mf->flags[i] = 0xEE;
        wolfBoot_get_update_sector_flag(i, &mf->flags[i]);
    }
}

static void snapshot(void)
{
    ck_assert_int_eq(boot(child_snapshot, 0, TORN_NONE), 0);
}

static void child_trigger(void)
{
    wolfBoot_update_trigger();
}

static void child_success(void)
{
    wolfBoot_success();
}

static void child_set_boot_state(void)
{
    mf->ret = wolfBoot_set_partition_state(PART_BOOT, arg_val);
}

static void child_set_update_state(void)
{
    mf->ret = wolfBoot_set_partition_state(PART_UPDATE, arg_val);
}

static void child_set_flag(void)
{
    mf->ret = wolfBoot_set_update_sector_flag(arg_n, arg_val);
}

static void child_clear_update(void)
{
    mf->ret = wolfBoot_clear_trailer(PART_UPDATE);
}

/* Flag writes in one boot, enough to fill the log */
static void child_many_writes(void)
{
    int i;

    for (i = 0; i < arg_n; i++) {
        wolfBoot_set_update_sector_flag(1,
            (i & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
    }
}

static void set_flag(int sector, uint8_t flag)
{
    arg_n = sector;
    arg_val = flag;
    ck_assert_int_eq(boot(child_set_flag, 0, TORN_NONE), 0);
}

/* One swap step per sector, as wolfBoot_update() logs them */
static void log_swap(int sectors)
{
    int s;
    for (s = 0; s < sectors; s++) {
        set_flag(s, SECT_FLAG_SWAPPING);
        set_flag(s, SECT_FLAG_BACKUP);
        set_flag(s, SECT_FLAG_UPDATED);
    }
}

static void setup(void)
{
    flash_erase_all();
}

static void teardown(void)
{
}

#ifndef NVM_FLASH_JOURNAL_DUAL
/* An erased sector reads as no trailer. Writes after a reset leave the
 * first slot unused, and every record takes its own slot. */
START_TEST(test_empty_log)
{
    snapshot();
    ck_assert_int_eq(mf->boot_ret, -1);
    ck_assert_int_eq(mf->update_ret, -1);

    ck_assert_int_eq(boot(child_success, 0, TORN_NONE), 0);
    snapshot();
    ck_assert_int_eq(mf->boot_ret, 0);
    ck_assert_uint_eq(mf->boot_st, IMG_STATE_SUCCESS);
    ck_assert_int_eq(mf->update_ret, -1);

    ck_assert(!slot_programmed(0, 0));
    ck_assert(slot_programmed(0, 1)); /* magic */
    ck_assert(slot_programmed(0, 2)); /* state */
    ck_assert(!slot_programmed(0, 3));
    ck_assert_int_eq(mf->erases, 0);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* The update trigger erases the sector, then logs magic and UPDATING with
 * no skipped slot. Sector flags replay after each reset. */
START_TEST(test_trigger_and_flags)
{
    int s;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->ops, 3); /* no marker on a blank sector */
    ck_assert(slot_programmed(1, 0));
    ck_assert(slot_programmed(1, 1));

    log_swap(3);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++) {
        ck_assert_uint_eq(mf->flags[s],
            (s < 3) ? SECT_FLAG_UPDATED : SECT_FLAG_NEW);
    }
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);

    /* A new trigger drops the flags of the previous update. It programs the
     * marker before the erase. */
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->ops, 4);
    ck_assert(!slot_programmed(1, MARK_SLOT));
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++)
        ck_assert_uint_eq(mf->flags[s], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST
#endif /* !NVM_FLASH_JOURNAL_DUAL */

/* A reset during a record program loses that write only, for each way a
 * program can be cut. The next write lands in a later slot. */
static void cut_flag_write(int torn)
{
    int ret;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);

    arg_n = 0;
    arg_val = SECT_FLAG_BACKUP;
    ret = boot(child_set_flag, 1, torn);
    ck_assert_int_eq(ret, EXIT_CUT);

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_SWAPPING);

    /* Resume with the same write, then the rest */
    set_flag(0, SECT_FLAG_BACKUP);
    set_flag(0, SECT_FLAG_UPDATED);
    set_flag(1, SECT_FLAG_SWAPPING);
    snapshot();
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);
}

START_TEST(test_cut_write_clean)
{
    cut_flag_write(TORN_NONE);
}
END_TEST

START_TEST(test_cut_write_junk)
{
    cut_flag_write(TORN_JUNK);
}
END_TEST

START_TEST(test_cut_write_reads_erased)
{
    cut_flag_write(TORN_ERASED);
}
END_TEST

/* The first write after a reset is cut but reads as erased, and the next
 * boot writes other data there. That data fails the read-back check, so the
 * record moves to the next slot. */
START_TEST(test_cut_write_reused_slot)
{
    int s;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);

    arg_n = 0;
    arg_val = SECT_FLAG_BACKUP;
    ck_assert_int_eq(boot(child_set_flag, 1, TORN_ERASED), EXIT_CUT);

    /* Different write into the same slot */
    set_flag(5, SECT_FLAG_SWAPPING);
    snapshot();
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_SWAPPING);
    ck_assert_uint_eq(mf->flags[5], SECT_FLAG_SWAPPING);

    set_flag(0, SECT_FLAG_BACKUP);
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++) {
        ck_assert_uint_eq(mf->flags[s], (s == 0) ? SECT_FLAG_BACKUP :
            (s == 5) ? SECT_FLAG_SWAPPING : SECT_FLAG_NEW);
    }
}
END_TEST

/* A program can land and still return an error, as a TC3xx program verify
 * error can. The retry logs the record again, and both copies replay. */
START_TEST(test_write_error_lands)
{
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);

    arg_n = 0;
    arg_val = SECT_FLAG_BACKUP;
    mf->fail_ops = 1U << 1;
    ck_assert_int_eq(boot(child_set_flag, 0, TORN_NONE), 0);
    mf->fail_ops = 0;
    ck_assert_int_eq(mf->ops, 2); /* the program and its retry */

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_BACKUP);

    /* Records after the repeat replay too */
    set_flag(0, SECT_FLAG_UPDATED);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

static void child_set_two_flags(void)
{
    wolfBoot_set_update_sector_flag(0, SECT_FLAG_BACKUP);
    wolfBoot_set_update_sector_flag(2, SECT_FLAG_SWAPPING);
}

/* Every try of an append lands but returns an error, so the append fails and
 * the next write reuses its seq number. All of them replay in order. */
START_TEST(test_write_error_reused_seq)
{
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);

    mf->fail_ops = (1U << 1) | (1U << 2) | (1U << 3);
    ck_assert_int_eq(boot(child_set_two_flags, 0, TORN_NONE), 0);
    mf->fail_ops = 0;
    ck_assert_int_eq(mf->ops, 4); /* three tries, then the next write */

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_BACKUP); /* a try landed */
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* Every try of an append returns an error and writes only part of the
 * record, then a reset. The bad slots are skipped, and the log keeps its
 * state. */
START_TEST(test_write_error_junk)
{
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);

    arg_n = 0;
    arg_val = SECT_FLAG_BACKUP;
    mf->fail_ops = (1U << 1) | (1U << 2) | (1U << 3);
    mf->fail_writes = FAIL_WRITES_PART;
    ck_assert_int_eq(boot(child_set_flag, 0, TORN_NONE), 0);
    mf->fail_ops = 0;
    mf->fail_writes = FAIL_WRITES_ALL;
    ck_assert_int_eq(mf->ops, 3);

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_SWAPPING);

    set_flag(0, SECT_FLAG_BACKUP);
    snapshot();
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_BACKUP);
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* The same record write cut on several boots in a row, torn as torn1 and
 * torn2 in turn. The log keeps its state through every cut, and the write
 * lands once a boot completes it. */
static void repeated_cuts(int torn1, int torn2)
{
    int i;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);
    for (i = 0; i < 6; i++) {
        arg_n = 0;
        arg_val = SECT_FLAG_BACKUP;
        ck_assert_int_eq(boot(child_set_flag, 1, (i & 1) ? torn2 : torn1),
            EXIT_CUT);
        snapshot();
        ck_assert_int_eq(mf->update_ret, 0);
        ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
        ck_assert_uint_eq(mf->flags[0], SECT_FLAG_SWAPPING);
    }
    set_flag(0, SECT_FLAG_BACKUP);
    snapshot();
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_BACKUP);
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);
}

START_TEST(test_repeated_cuts_junk)
{
    repeated_cuts(TORN_JUNK, TORN_JUNK);
}
END_TEST

START_TEST(test_repeated_cuts_erased)
{
    repeated_cuts(TORN_ERASED, TORN_ERASED);
}
END_TEST

START_TEST(test_repeated_cuts_mixed)
{
    repeated_cuts(TORN_ERASED, TORN_JUNK);
}
END_TEST

/* A blank check that fails while the log loads drops the replay, so the
 * trailer reads as missing, not as the older state replayed before the
 * error. Nothing is written until a load succeeds. */
START_TEST(test_load_check_fails)
{
    uintptr_t skipped;

    arg_val = IMG_STATE_TESTING;
    ck_assert_int_eq(boot(child_set_boot_state, 0, TORN_NONE), 0);
    ck_assert_int_eq(boot(child_success, 0, TORN_NONE), 0);
    snapshot();
    ck_assert_int_eq(mf->boot_ret, 0);
    ck_assert_uint_eq(mf->boot_st, IMG_STATE_SUCCESS);

    /* The slot skipped after a reset, between TESTING and SUCCESS */
#ifdef NVM_FLASH_JOURNAL_DUAL
    skipped = trailer_sector(0) - SECT_SIZE + 4 * SLOT_SIZE;
#else
    skipped = slot_addr(0, 3);
#endif
    ck_assert_int_eq(mock_is_erased(skipped, SLOT_SIZE), 1);
    ck_assert_int_eq(mock_is_erased(skipped + SLOT_SIZE, SLOT_SIZE), 0);

    mf->check_fail = skipped;
    snapshot();
    ck_assert_int_eq(mf->boot_ret, -1);
    ck_assert_int_eq(boot(child_success, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->ops, 0);
    mf->check_fail = 0;

    snapshot();
    ck_assert_int_eq(mf->boot_ret, 0);
    ck_assert_uint_eq(mf->boot_st, IMG_STATE_SUCCESS);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* A trigger whose clear fails does not set UPDATING, so the next boot cannot
 * resume the old swap steps over the new image. The old log is from a swap
 * whose final clear failed. */
START_TEST(test_trigger_clear_fails)
{
    int s, erase_op;

#ifdef NVM_FLASH_JOURNAL_DUAL
    erase_op = 1;
#else
    erase_op = 2; /* after the marker */
#endif
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    arg_val = IMG_STATE_FINAL_FLAGS;
    ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);

    mf->fail_ops = 1U << erase_op;
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    mf->fail_ops = 0;
    ck_assert_int_eq(mf->ops, erase_op); /* nothing written after it */

    snapshot();
#ifdef NVM_FLASH_JOURNAL_DUAL
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_FINAL_FLAGS);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
#else
    /* The marker was written, so the old log reads as erased */
    ck_assert_int_eq(mf->update_ret, -1);
#endif

    /* A retry that clears sets UPDATING with no flags */
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++)
        ck_assert_uint_eq(mf->flags[s], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

#ifndef NVM_FLASH_JOURNAL_DUAL
/* Last programmed slot of the log, or -1 */
static int last_slot(int p)
{
    int slot;

    for (slot = LOG_SLOTS - 1; slot >= 0; slot--) {
        if (slot_programmed(p, slot))
            break;
    }
    return slot;
}

/* Clear one slot, as a cut erase can clear some slots and not others */
static void erase_slot(int p, int slot)
{
    uint32_t u;

    ck_assert_int_eq(part_of(slot_addr(p, slot), &u), p);
    memset((void *)slot_addr(p, slot), FLASH_BYTE_ERASED, SLOT_SIZE);
    memset(&mf->programmed[p][u], 0, SLOT_SIZE / UNIT);
    memset(&mf->weak[p][u], 0, SLOT_SIZE / UNIT);
}

/* Bad slots after the last record come from cut writes, so any number of
 * them is skipped */
START_TEST(test_junk_tolerated)
{
    uint8_t junk[SLOT_SIZE];
    int last, slot;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    last = last_slot(1);
    memset(junk, 0x5A, sizeof(junk));
    for (slot = last + 1; slot <= last + 4; slot++)
        raw_program(slot_addr(1, slot), junk, SLOT_SIZE);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_UPDATED);

    set_flag(2, SECT_FLAG_SWAPPING);
    snapshot();
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_UPDATED);
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* A clear programs the erase marker, then erases the sector. A reset before
 * the marker is written keeps the old log. After it, the log reads as erased,
 * even though the cut erase left the old records. */
START_TEST(test_cut_clear)
{
    int cut, torn;

    for (torn = TORN_NONE; torn <= TORN_ERASED; torn++) {
        for (cut = 1; cut <= 3; cut++) {
            flash_erase_all();
            ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
            log_swap(2);
            arg_val = IMG_STATE_FINAL_FLAGS;
            ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);

            ck_assert_int_eq(boot(child_clear_update, cut, torn),
                (cut <= 2) ? EXIT_CUT : 0);
            snapshot();
            if ((cut == 1) && (torn != TORN_JUNK)) {
                ck_assert_int_eq(mf->update_ret, 0);
                ck_assert_uint_eq(mf->update_st, IMG_STATE_FINAL_FLAGS);
                ck_assert_uint_eq(mf->flags[1], SECT_FLAG_UPDATED);
            }
            else {
                ck_assert_int_eq(mf->update_ret, -1);
            }

            /* The clear completes on the next try */
            ck_assert_int_eq(boot(child_clear_update, 0, TORN_NONE), 0);
            ck_assert_int_eq(mf->ret, 0);
            ck_assert(!slot_programmed(1, MARK_SLOT));
            snapshot();
            ck_assert_int_eq(mf->update_ret, -1);
            ck_assert_int_eq(mf->violations, 0);
        }
    }
}
END_TEST

/* A cut erase that cleared only the newest record, FINAL_FLAGS. The records
 * left would replay as a swap that has not finished, so the marker makes the
 * log read as erased instead. */
START_TEST(test_cut_clear_partial)
{
    int last;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    arg_val = IMG_STATE_FINAL_FLAGS;
    ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);
    last = last_slot(1);

    ck_assert_int_eq(boot(child_clear_update, 2, TORN_NONE), EXIT_CUT);
    erase_slot(1, last);
    snapshot();
    ck_assert_int_eq(mf->update_ret, -1);

    /* The next write erases the sector first */
    arg_val = IMG_STATE_UPDATING;
    ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->erases, 2);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* A marker program that fails and leaves the slot blank. The clear fails
 * without erasing, so the old log stays, and the next clear works. */
START_TEST(test_clear_marker_fails)
{
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    arg_val = IMG_STATE_FINAL_FLAGS;
    ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);

    mf->fail_ops = 1U << 1; /* the marker */
    mf->fail_writes = FAIL_WRITES_NONE;
    ck_assert_int_eq(boot(child_clear_update, 0, TORN_NONE), 0);
    mf->fail_ops = 0;
    mf->fail_writes = FAIL_WRITES_ALL;
    ck_assert_int_eq(mf->ret, -1);
    ck_assert_int_eq(mf->ops, 1); /* no erase */

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_FINAL_FLAGS);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_UPDATED);

    ck_assert_int_eq(boot(child_clear_update, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->ret, 0);
    snapshot();
    ck_assert_int_eq(mf->update_ret, -1);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* Records never use the marker slot. The write after the last free slot
 * rewrites the log in a fresh sector, in the same boot or after a reset. */
START_TEST(test_full_log_marker_slot)
{
    int n = LOG_SLOTS - 3;

    /* The trigger uses slots 0 and 1, and the next boot skips slot 2. The
     * last write of this boot finds no free slot. */
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    arg_n = n + 1;
    ck_assert_int_eq(boot(child_many_writes, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->erases, 2);
    ck_assert(!slot_programmed(1, MARK_SLOT));
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[1],
        (n & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->violations, 0);

    /* Fill the same slots, then write once more after a reset */
    flash_erase_all();
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    arg_n = n;
    ck_assert_int_eq(boot(child_many_writes, 0, TORN_NONE), 0);
    ck_assert(slot_programmed(1, LOG_SLOTS - 1));
    ck_assert(!slot_programmed(1, MARK_SLOT));
    ck_assert_int_eq(mf->erases, 1);

    set_flag(2, SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->erases, 2);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[1],
        ((n - 1) & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* A flag write that compacts the full log, then one more */
static void child_compact_writes(void)
{
    uint8_t st = 0;

    wolfBoot_set_update_sector_flag(2, SECT_FLAG_SWAPPING);
    mf->update_ret = wolfBoot_get_partition_state(PART_UPDATE, &st);
    mf->update_st = st;
    wolfBoot_set_update_sector_flag(3, SECT_FLAG_SWAPPING);
}

/* A compaction that fails at its erase, or at a record after it, leaves a log
 * that reads as erased. The trailer stays in RAM, so the same boot still reads
 * it, and the next write logs it again. */
START_TEST(test_compact_fails)
{
    static const uint32_t fail[2] = {
        1U << 2, /* the erase, after the marker */
        (1U << 3) | (1U << 4) | (1U << 5) /* every try of the first record */
    };
    int i, n = LOG_SLOTS - 3;

    for (i = 0; i < 2; i++) {
        flash_erase_all();
        ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
        arg_n = n;
        ck_assert_int_eq(boot(child_many_writes, 0, TORN_NONE), 0);

        mf->fail_ops = fail[i];
        mf->fail_writes = FAIL_WRITES_PART;
        ck_assert_int_eq(boot(child_compact_writes, 0, TORN_NONE), 0);
        mf->fail_ops = 0;
        mf->fail_writes = FAIL_WRITES_ALL;
        ck_assert_int_eq(mf->update_ret, 0);
        ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);

        snapshot();
        ck_assert_int_eq(mf->update_ret, 0);
        ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
        ck_assert_uint_eq(mf->flags[1],
            ((n - 1) & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
        ck_assert_uint_eq(mf->flags[3], SECT_FLAG_SWAPPING);
        ck_assert_int_eq(mf->violations, 0);
    }
}
END_TEST

/* A record missing in the middle of the log means a cut erase */
START_TEST(test_partial_erase_gap)
{
    uint8_t rec[SLOT_SIZE];
    uintptr_t a;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);

    /* Damage one record so that its CRC fails */
    ck_assert(slot_programmed(1, 3));
    a = slot_addr(1, 3);
    memcpy(rec, (void *)a, SLOT_SIZE);
    rec[8] ^= 0x01;
    memcpy((void *)a, rec, SLOT_SIZE);
    snapshot();
    ck_assert_int_eq(mf->update_ret, -1);
}
END_TEST

/* A log with junk and no records reads as erased */
START_TEST(test_junk_only)
{
    uint8_t junk[SLOT_SIZE];

    memset(junk, 0x33, sizeof(junk));
    raw_program(slot_addr(0, 4), junk, SLOT_SIZE);
    snapshot();
    ck_assert_int_eq(mf->boot_ret, -1);
    ck_assert_int_eq(boot(child_success, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->erases, 1);
    snapshot();
    ck_assert_int_eq(mf->boot_ret, 0);
    ck_assert_uint_eq(mf->boot_st, IMG_STATE_SUCCESS);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST
#endif /* !NVM_FLASH_JOURNAL_DUAL */

/* A trailer in the in-place format, as an older wolfBoot left it with every
 * unit of the sector programmed. It reads as the starting state, and the
 * first write rewrites it as records. */
static void program_old_trailer(void)
{
    static uint8_t sector[SECT_SIZE];
    uint32_t magic = WOLFBOOT_MAGIC_TRAIL;
    uint8_t *end = sector + SECT_SIZE;

    memset(sector, FLASH_BYTE_ERASED, sizeof(sector));
    memcpy(end - 4, &magic, 4);
    end[-5] = IMG_STATE_UPDATING;
    /* sectors 0 and 1 in the flags byte at position 0 */
    end[-6] = (uint8_t)(SECT_FLAG_UPDATED | (SECT_FLAG_BACKUP << 4));
    end[-7] = (uint8_t)(SECT_FLAG_SWAPPING | (SECT_FLAG_NEW << 4));
    raw_program(trailer_sector(1), sector, SECT_SIZE);
}

START_TEST(test_old_format_import)
{
    program_old_trailer();
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_BACKUP);
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
    ck_assert_uint_eq(mf->flags[3], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->erases, 0);

    set_flag(2, SECT_FLAG_BACKUP);
    ck_assert_int_eq(mf->erases, 1);
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_BACKUP);
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_BACKUP);
    ck_assert_uint_eq(mf->flags[3], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

#ifndef NVM_FLASH_JOURNAL_DUAL
/* A reset during the rewrite leaves no trailer, never a partial one, because
 * the magic is logged last. The ops are the erase, the state and flags, the
 * magic, then the new flag. */
START_TEST(test_old_format_cut_compaction)
{
    int cut;

    for (cut = 1; cut <= 4; cut++) {
        flash_erase_all();
        program_old_trailer();
        arg_n = 2;
        arg_val = SECT_FLAG_BACKUP;
        ck_assert_int_eq(boot(child_set_flag, cut, TORN_NONE), EXIT_CUT);
        snapshot();
        if ((cut == 2) || (cut == 3)) {
            ck_assert_int_eq(mf->update_ret, -1);
        }
        else {
            ck_assert_int_eq(mf->update_ret, 0);
            ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
            ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
            ck_assert_uint_eq(mf->flags[1], SECT_FLAG_BACKUP);
            ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
        }
        ck_assert_int_eq(mf->violations, 0);
    }
}
END_TEST
#endif /* !NVM_FLASH_JOURNAL_DUAL */

/* A full log is rewritten as records in a fresh sector */
START_TEST(test_full_log_compacts)
{
    int i, n = SLOTS + 4;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    /* Alternate one flag so that every call logs a record */
    for (i = 0; i < n; i++) {
        set_flag(1, (i & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
    }
    ck_assert_int_ge(mf->erases, 2);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[1],
        ((n - 1) & 1) ? SECT_FLAG_BACKUP : SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* Erase the partition between two trailer writes. arg_n 1 erases it outside
 * the journal, which drops the RAM copy. */
static void child_erase_between(void)
{
    uint8_t st;
    mf->update_ret = wolfBoot_get_partition_state(PART_UPDATE, &st);
    mf->update_st = st;
    if (arg_n)
        mock_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, PART_SIZE);
    else
        wolfBoot_erase_partition(PART_UPDATE);
    mf->ret = wolfBoot_get_partition_state(PART_UPDATE, &st);
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
}

static void check_erase_between(int outside)
{
    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(1);
    arg_n = outside;
    ck_assert_int_eq(boot(child_erase_between, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_int_eq(mf->ret, -1);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_NEW);
    ck_assert_int_eq(mf->violations, 0);
}

START_TEST(test_erase_partition)
{
    check_erase_between(0);
}
END_TEST

START_TEST(test_external_erase_reloads)
{
    check_erase_between(1);
}
END_TEST

/* Two sectors of swap steps in one boot */
static void child_swap_two(void)
{
    int s;
    for (s = 0; s < 2; s++) {
        wolfBoot_set_update_sector_flag(s, SECT_FLAG_SWAPPING);
        wolfBoot_set_update_sector_flag(s, SECT_FLAG_BACKUP);
        wolfBoot_set_update_sector_flag(s, SECT_FLAG_UPDATED);
    }
}

/* A reset at any record write, cut in any way, loses that write and the
 * writes after it. Repeating the steps completes the log. */
START_TEST(test_cut_everywhere)
{
    int ret, cut, s, torn;

    for (torn = TORN_NONE; torn <= TORN_ERASED; torn++) {
        for (cut = 1; cut <= 6; cut++) {
            flash_erase_all();
            ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
            ret = boot(child_swap_two, cut, torn);
            ck_assert_int_eq(ret, EXIT_CUT);
            snapshot();
            ck_assert_int_eq(mf->update_ret, 0);
            ck_assert_uint_eq(mf->flags[(cut - 1) / 3],
                ((cut - 1) % 3 == 0) ? SECT_FLAG_NEW :
                ((cut - 1) % 3 == 1) ? SECT_FLAG_SWAPPING : SECT_FLAG_BACKUP);
            ck_assert_int_eq(boot(child_swap_two, 0, TORN_NONE), 0);
            snapshot();
            ck_assert_int_eq(mf->update_ret, 0);
            ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
            for (s = 0; s < NSECT; s++) {
                ck_assert_uint_eq(mf->flags[s],
                    (s < 2) ? SECT_FLAG_UPDATED : SECT_FLAG_NEW);
            }
            ck_assert_int_eq(mf->violations, 0);
        }
    }
}
END_TEST

static void child_get_flag(void)
{
    mf->flag = 0xEE;
    mf->ret = wolfBoot_get_update_sector_flag(arg_n, &mf->flag);
}

/* A write to sector arg_n, then a valid one in the same boot */
static void child_bad_then_good(void)
{
    wolfBoot_set_update_sector_flag(arg_n, SECT_FLAG_SWAPPING);
    wolfBoot_set_update_sector_flag(1, SECT_FLAG_SWAPPING);
}

/* A sector past the flag bytes of the trailer reads as erased and is never
 * written, so a later write in the same boot keeps the log. The last sector
 * that fits still works. */
START_TEST(test_sector_out_of_range)
{
    /* Two flags per byte, in the bytes before the state and the magic */
    int first_bad = 2 * (TRAILER_SIZE - 5);
    int s;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    set_flag(0, SECT_FLAG_SWAPPING);
    set_flag(first_bad - 1, SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->ops, 1);
    arg_n = first_bad - 1;
    ck_assert_int_eq(boot(child_get_flag, 0, TORN_NONE), 0);
    ck_assert_uint_eq(mf->flag, SECT_FLAG_SWAPPING);

    arg_n = first_bad;
    ck_assert_int_eq(boot(child_bad_then_good, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->ops, 1); /* only the valid write */
    ck_assert_int_eq(boot(child_get_flag, 0, TORN_NONE), 0);
    ck_assert_uint_eq(mf->flag, SECT_FLAG_NEW);

    arg_n = 0xFFFF;
    arg_val = SECT_FLAG_SWAPPING;
    ck_assert_int_eq(boot(child_set_flag, 0, TORN_NONE), 0);
    ck_assert_int_eq(mf->ops, 0);
    ck_assert_int_eq(boot(child_get_flag, 0, TORN_NONE), 0);
    ck_assert_uint_eq(mf->flag, SECT_FLAG_NEW);

    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++) {
        ck_assert_uint_eq(mf->flags[s],
            (s < 2) ? SECT_FLAG_SWAPPING : SECT_FLAG_NEW);
    }
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

#ifdef NVM_FLASH_JOURNAL_DUAL
/* Trailer sector s of partition p. Sector 0 is the last sector. */
static uintptr_t dual_sector(int p, int s)
{
    return trailer_sector(p) - (uintptr_t)s * SECT_SIZE;
}

static int dual_slot_programmed(int p, int s, int slot)
{
    return !mock_is_erased(dual_sector(p, s) + (uintptr_t)slot * SLOT_SIZE,
        SLOT_SIZE);
}

/* Generation in the header of a sector, or 0 if it has none */
static uint32_t dual_gen(int p, int s)
{
    uint32_t gen;

    if (!dual_slot_programmed(p, s, 0))
        return 0;
    memcpy(&gen, (void *)(dual_sector(p, s) + 8), sizeof(gen));
    return gen;
}

/* The first write to an empty log starts a log in the sector before the last
 * one. The header and commit come first, and the last sector stays blank. */
START_TEST(test_dual_empty_log)
{
    snapshot();
    ck_assert_int_eq(mf->boot_ret, -1);

    ck_assert_int_eq(boot(child_success, 0, TORN_NONE), 0);
    snapshot();
    ck_assert_int_eq(mf->boot_ret, 0);
    ck_assert_uint_eq(mf->boot_st, IMG_STATE_SUCCESS);
    ck_assert_uint_eq(dual_gen(0, 1), 1);
    ck_assert(dual_slot_programmed(0, 1, 1)); /* commit */
    ck_assert(dual_slot_programmed(0, 1, 2)); /* magic */
    ck_assert(dual_slot_programmed(0, 1, 3)); /* state */
    ck_assert(!dual_slot_programmed(0, 1, 4));
    ck_assert(!dual_slot_programmed(0, 0, 0));
    ck_assert_int_eq(mf->erases, 1);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* Each update trigger starts a new log in the other sector. The old log stays
 * in flash until its sector is used again. */
START_TEST(test_dual_trigger_alternates)
{
    int s;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    ck_assert_uint_eq(dual_gen(1, 1), 1);
    log_swap(3);
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[2], SECT_FLAG_UPDATED);

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    ck_assert_uint_eq(dual_gen(1, 0), 2);
    ck_assert(dual_slot_programmed(1, 1, 5)); /* first flag of the old log */
    snapshot();
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    for (s = 0; s < NSECT; s++)
        ck_assert_uint_eq(mf->flags[s], SECT_FLAG_NEW);

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    ck_assert_uint_eq(dual_gen(1, 1), 3);
    ck_assert_int_eq(mf->erases, 3);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* The log in use is never erased, so bad slots come only from cut writes.
 * Any number of them is skipped. */
START_TEST(test_dual_junk_tolerated)
{
    uint8_t junk[SLOT_SIZE];
    int slot;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(1);
    memset(junk, 0x5A, sizeof(junk));
    for (slot = 20; slot < 26; slot++) {
        raw_program(dual_sector(1, 1) + (uintptr_t)slot * SLOT_SIZE, junk,
            SLOT_SIZE);
    }
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);

    set_flag(1, SECT_FLAG_SWAPPING);
    snapshot();
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
    ck_assert_uint_eq(mf->flags[1], SECT_FLAG_SWAPPING);
    ck_assert_int_eq(mf->violations, 0);
}
END_TEST

/* A record missing in the middle of the log reads as a damaged log */
START_TEST(test_dual_gap)
{
    uint8_t rec[SLOT_SIZE];
    uintptr_t a = dual_sector(1, 1) + 5 * SLOT_SIZE;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(2);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);

    /* Damage the first flag record so that its CRC fails */
    ck_assert(dual_slot_programmed(1, 1, 5));
    memcpy(rec, (void *)a, SLOT_SIZE);
    rec[8] ^= 0x01;
    memcpy((void *)a, rec, SLOT_SIZE);
    snapshot();
    ck_assert_int_eq(mf->update_ret, -1);
}
END_TEST

/* A clear writes an empty log into the other sector. A reset before its
 * commit record keeps the old log. The ops are the erase, the header and the
 * commit. */
START_TEST(test_dual_cut_clear)
{
    int cut, torn;

    for (torn = TORN_NONE; torn <= TORN_ERASED; torn++) {
        for (cut = 1; cut <= 4; cut++) {
            flash_erase_all();
            ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
            log_swap(2);
            arg_val = IMG_STATE_FINAL_FLAGS;
            ck_assert_int_eq(boot(child_set_update_state, 0, TORN_NONE), 0);

            ck_assert_int_eq(boot(child_clear_update, cut, torn),
                (cut <= 3) ? EXIT_CUT : 0);
            snapshot();
            if (cut <= 3) {
                ck_assert_int_eq(mf->update_ret, 0);
                ck_assert_uint_eq(mf->update_st, IMG_STATE_FINAL_FLAGS);
                ck_assert_uint_eq(mf->flags[1], SECT_FLAG_UPDATED);
            }
            else {
                ck_assert_int_eq(mf->ret, 0);
                ck_assert_int_eq(mf->update_ret, -1);
            }
            ck_assert_int_eq(mf->violations, 0);
        }
    }
}
END_TEST

static void child_erase_update(void)
{
    wolfBoot_erase_partition(PART_UPDATE);
}

/* The sector before the last one holds the newest log, and the last sector an
 * older log with swap flags. A reset at any step of a partition erase never
 * brings back the older log. */
START_TEST(test_dual_cut_erase_partition)
{
    int cut, ret;

    for (cut = 1; ; cut++) {
        flash_erase_all();
        ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
        ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
        log_swap(1);
        ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
        ck_assert_uint_eq(dual_gen(1, 0), 2);
        ck_assert_uint_eq(dual_gen(1, 1), 3);

        ret = boot(child_erase_update, cut, TORN_NONE);
        snapshot();
        if (mf->update_ret == 0) {
            ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
            ck_assert_uint_eq(mf->flags[0], SECT_FLAG_NEW);
        }
        ck_assert_int_eq(mf->violations, 0);
        if (ret == 0)
            break;
        ck_assert_int_eq(ret, EXIT_CUT);
    }
    ck_assert_int_eq(mf->update_ret, -1);
}
END_TEST

/* A full log moves to the other sector. A reset at any step of a boot that
 * fills the log leaves a trailer holding one of the values written. */
START_TEST(test_dual_cut_compaction)
{
    int cut, ret;

    for (cut = 1; ; cut++) {
        flash_erase_all();
        ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
        arg_n = SLOTS + 8;
        ret = boot(child_many_writes, cut, TORN_JUNK);
        snapshot();
        ck_assert_int_eq(mf->update_ret, 0);
        ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
        if (cut == 1)
            ck_assert_uint_eq(mf->flags[1], SECT_FLAG_NEW);
        else
            ck_assert((mf->flags[1] == SECT_FLAG_SWAPPING) ||
                (mf->flags[1] == SECT_FLAG_BACKUP));
        ck_assert_int_eq(mf->violations, 0);
        if (ret == 0)
            break;
        ck_assert_int_eq(ret, EXIT_CUT);
    }
    ck_assert_int_ge(mf->erases, 2);
}
END_TEST

/* Records after a commit were written only after the commit landed, so the
 * log stays valid when the commit later reads back wrong */
START_TEST(test_dual_commit_lost_after_records)
{
    uint8_t rec[SLOT_SIZE];
    uintptr_t a = dual_sector(1, 1) + SLOT_SIZE;

    ck_assert_int_eq(boot(child_trigger, 0, TORN_NONE), 0);
    log_swap(1);
    memcpy(rec, (void *)a, SLOT_SIZE);
    rec[12] ^= 0xFF;
    memcpy((void *)a, rec, SLOT_SIZE);
    snapshot();
    ck_assert_int_eq(mf->update_ret, 0);
    ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
}
END_TEST

/* An in-place trailer is rewritten in the other sector, so a reset at any
 * step keeps it readable. The ops are the erase, the header, two copy
 * records, the commit, then the new flag. */
START_TEST(test_dual_old_format_cut)
{
    int cut;

    for (cut = 1; cut <= 6; cut++) {
        flash_erase_all();
        program_old_trailer();
        arg_n = 2;
        arg_val = SECT_FLAG_BACKUP;
        ck_assert_int_eq(boot(child_set_flag, cut, TORN_NONE), EXIT_CUT);
        snapshot();
        ck_assert_int_eq(mf->update_ret, 0);
        ck_assert_uint_eq(mf->update_st, IMG_STATE_UPDATING);
        ck_assert_uint_eq(mf->flags[0], SECT_FLAG_UPDATED);
        ck_assert_uint_eq(mf->flags[1], SECT_FLAG_BACKUP);
        ck_assert_uint_eq(mf->flags[2], SECT_FLAG_SWAPPING);
        ck_assert_int_eq(mf->violations, 0);
    }
}
END_TEST
#endif /* NVM_FLASH_JOURNAL_DUAL */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-nvm-journal");
    TCase *tc = tcase_create("journal");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_cut_write_clean);
    tcase_add_test(tc, test_cut_write_junk);
    tcase_add_test(tc, test_cut_write_reads_erased);
    tcase_add_test(tc, test_cut_write_reused_slot);
    tcase_add_test(tc, test_write_error_lands);
    tcase_add_test(tc, test_write_error_reused_seq);
    tcase_add_test(tc, test_write_error_junk);
    tcase_add_test(tc, test_repeated_cuts_junk);
    tcase_add_test(tc, test_repeated_cuts_erased);
    tcase_add_test(tc, test_repeated_cuts_mixed);
    tcase_add_test(tc, test_load_check_fails);
    tcase_add_test(tc, test_trigger_clear_fails);
    tcase_add_test(tc, test_old_format_import);
    tcase_add_test(tc, test_full_log_compacts);
    tcase_add_test(tc, test_erase_partition);
    tcase_add_test(tc, test_external_erase_reloads);
    tcase_add_test(tc, test_cut_everywhere);
    tcase_add_test(tc, test_sector_out_of_range);
#ifdef NVM_FLASH_JOURNAL_DUAL
    tcase_add_test(tc, test_dual_empty_log);
    tcase_add_test(tc, test_dual_trigger_alternates);
    tcase_add_test(tc, test_dual_junk_tolerated);
    tcase_add_test(tc, test_dual_gap);
    tcase_add_test(tc, test_dual_cut_clear);
    tcase_add_test(tc, test_dual_cut_erase_partition);
    tcase_add_test(tc, test_dual_cut_compaction);
    tcase_add_test(tc, test_dual_commit_lost_after_records);
    tcase_add_test(tc, test_dual_old_format_cut);
#else
    tcase_add_test(tc, test_empty_log);
    tcase_add_test(tc, test_trigger_and_flags);
    tcase_add_test(tc, test_junk_tolerated);
    tcase_add_test(tc, test_cut_clear);
    tcase_add_test(tc, test_cut_clear_partial);
    tcase_add_test(tc, test_clear_marker_fails);
    tcase_add_test(tc, test_full_log_marker_slot);
    tcase_add_test(tc, test_compact_fails);
    tcase_add_test(tc, test_partial_erase_gap);
    tcase_add_test(tc, test_junk_only);
    tcase_add_test(tc, test_old_format_cut_compaction);
#endif
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;
    void *p;

    mf = mmap(NULL, sizeof(*mf), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    p = mmap((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS, PART_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if ((mf == MAP_FAILED) || (p == MAP_FAILED))
        return 1;
    p = mmap((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS, PART_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED)
        return 1;

    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
