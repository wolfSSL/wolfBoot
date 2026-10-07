/* unit-imx95-disk.c
 *
 * Host test for the i.MX95 byte-addressed disk_read() shim.
 *
 * disk_read() is extracted verbatim from hal/imx95_a55.c into
 * imx95_disk_extract.h, so this exercises the shipped code. The card below it
 * is modelled: sd_read_blocks() serves blocks from a synthetic image whose
 * every byte is derived from its own offset, so a misplaced copy shows up as
 * wrong data rather than merely the wrong length.
 *
 * The destination sits inside a canaried buffer, because the interesting
 * failure for a bounce-buffer path is a write past the requested range.
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define SD_BLOCK_SIZE   512
#define SD_MAX_BLOCKS   1024

static int card_ready;
static int sd_read_blocks(uint32_t lba, uint32_t blocks, uint8_t *buf);

#include "imx95_disk_extract.h"

/* ---- the card ----------------------------------------------------------- */

#define CARD_BLOCKS     (SD_MAX_BLOCKS + 16U)

static struct {
    uint32_t calls;
    uint32_t max_blocks;    /* largest 'blocks' any one call asked for */
    uint32_t fail_after;    /* 0 = never fail */
    int      unaligned;     /* a destination sd_read_blocks would fault on */
} card;

/* Distinct per absolute byte offset, so a block placed at the wrong offset or
 * read from the wrong LBA cannot coincidentally match. */
static uint8_t card_byte(uint64_t off)
{
    return (uint8_t)((off * 31U) ^ (off >> 8) ^ 0xA5U);
}

static int sd_read_blocks(uint32_t lba, uint32_t blocks, uint8_t *buf)
{
    uint32_t i;

    card.calls++;
    if (blocks > card.max_blocks)
        card.max_blocks = blocks;
    if (blocks == 0U || blocks > SD_MAX_BLOCKS)
        return -1;
    if (((uintptr_t)buf & 3U) != 0U)
        card.unaligned = 1;    /* would fault on the target's -mstrict-align */
    if (card.fail_after != 0U && card.calls >= card.fail_after)
        return -1;
    if ((uint64_t)lba + blocks > CARD_BLOCKS)
        return -1;
    for (i = 0; i < blocks * SD_BLOCK_SIZE; i++)
        buf[i] = card_byte((uint64_t)lba * SD_BLOCK_SIZE + i);
    return 0;
}

static void card_reset(void)
{
    memset(&card, 0, sizeof(card));
    card_ready = 1;
}

/* ---- destination, canaried both sides ----------------------------------- */

#define PAD     64U
#define DSTMAX  (CARD_BLOCKS * SD_BLOCK_SIZE)

static uint8_t arena[PAD + DSTMAX + PAD];

static uint8_t *dst_at(uint32_t skew)
{
    memset(arena, 0xCC, sizeof(arena));
    return arena + PAD + skew;
}

static int canaries_intact(uint8_t *dst, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < sizeof(arena); i++) {
        uint8_t *p = arena + i;
        if (p >= dst && p < dst + len)
            continue;
        if (*p != 0xCC)
            return 0;
    }
    return 1;
}

/* ---- helpers ------------------------------------------------------------ */

static int fails;

static void check(int cond, const char *what)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond)
        fails++;
}

/* One read, fully verified: return value, every byte, and the canaries. */
static void read_case(const char *what, uint64_t start, uint32_t count,
                      uint32_t skew)
{
    uint8_t *dst = dst_at(skew);
    uint32_t i;
    int ret, bad = -1;

    card_reset();
    ret = disk_read(0, start, count, dst);
    printf("%s (start=%llu count=%u skew=%u)\n", what,
           (unsigned long long)start, count, skew);
    check(ret == (int)count, "returns the byte count requested");
    for (i = 0; i < count; i++) {
        if (dst[i] != card_byte(start + i)) {
            bad = (int)i;
            break;
        }
    }
    if (bad >= 0)
        printf("    first wrong byte at +%d\n", bad);
    check(bad < 0, "every byte comes from the right card offset");
    check(canaries_intact(dst, count), "nothing written outside the range");
    check(!card.unaligned, "sd_read_blocks never gets an unaligned buffer");
    check(card.max_blocks <= SD_MAX_BLOCKS, "no request exceeds SD_MAX_BLOCKS");
}

/* ---- tests -------------------------------------------------------------- */

static void test_shapes(void)
{
    read_case("aligned, one whole block", 0, SD_BLOCK_SIZE, 0);
    read_case("aligned, several whole blocks", 0, 4U * SD_BLOCK_SIZE, 0);
    read_case("head straddles a block boundary", 3, SD_BLOCK_SIZE, 0);
    read_case("tail shorter than a block", 0, 100, 0);
    read_case("head and tail both partial", 511, SD_BLOCK_SIZE + 2U, 0);
    read_case("a single byte", 1234, 1, 0);
    read_case("misaligned destination", 0, 4U * SD_BLOCK_SIZE, 1);
    read_case("misaligned destination and offset start", 7, 2000, 3);
}

/* More blocks than one command carries, so disk_read() must split the transfer.
 * The assertions on call count and request size are what keep this case honest:
 * a count that quietly fits in one command would exercise nothing. */
static void test_chunking(void)
{
    uint32_t count = (SD_MAX_BLOCKS + 8U) * SD_BLOCK_SIZE;

    read_case("spans more blocks than one command carries", 0, count, 0);
    check(card.calls >= 2, "the transfer was split across commands");
    check(card.max_blocks == SD_MAX_BLOCKS,
          "the largest request is exactly SD_MAX_BLOCKS");
    printf("    %u commands, largest %u blocks\n", card.calls, card.max_blocks);
}

static void test_zero_count(void)
{
    uint8_t *dst = dst_at(0);

    puts("a zero-length read touches nothing");
    card_reset();
    check(disk_read(0, 0, 0, dst) == 0, "returns 0");
    check(card.calls == 0, "issues no card command");
    check(canaries_intact(dst, 0), "writes nothing");
}

static void test_no_card(void)
{
    uint8_t *dst = dst_at(0);

    puts("a read before the card is ready fails");
    card_reset();
    card_ready = 0;
    check(disk_read(0, 0, SD_BLOCK_SIZE, dst) == -1, "returns -1");
    check(card.calls == 0, "issues no card command");
}

static void test_error_propagates(void)
{
    uint8_t *dst = dst_at(0);

    puts("a card error is propagated, not partially reported");
    card_reset();
    card.fail_after = 1;
    check(disk_read(0, 0, 4U * SD_BLOCK_SIZE, dst) == -1, "returns -1");

    card_reset();
    card.fail_after = 2;
    check(disk_read(0, 3, 4U * SD_BLOCK_SIZE, dst) == -1,
          "returns -1 when the failure is mid-transfer");
}

/* The card command argument is 32-bit. An offset whose block number does not
 * fit must be refused rather than wrapped into an unrelated sector. */
static void test_lba_limit(void)
{
    uint8_t *dst = dst_at(0);
    uint64_t past = (0xFFFFFFFFULL + 1ULL) * SD_BLOCK_SIZE;

    puts("an offset past the 32-bit block limit is refused");
    card_reset();
    check(disk_read(0, past, SD_BLOCK_SIZE, dst) == -1, "returns -1");
    check(card.calls == 0, "issues no card command");

    card_reset();
    check(disk_read(0, past - 1ULL, 8, dst) == -1,
          "refused rather than wrapped when the range straddles the limit");
}

/* The count is reported through an int return, so one that cannot be expressed
 * must be refused rather than answered with what looks like an error code. */
static void test_unreportable_count(void)
{
    uint8_t *dst = dst_at(0);

    puts("a count too large to report is refused");
    card_reset();
    check(disk_read(0, 0, 0x80000000U, dst) == -1, "returns -1");
    check(card.calls == 0, "issues no card command");
    check(canaries_intact(dst, 0), "writes nothing");
}

/* start + done is formed inside the loop, so a range whose inclusive end is not
 * representable would wrap there and read some unrelated low sector. */
static void test_end_overflow(void)
{
    uint8_t *dst = dst_at(0);

    puts("a byte range whose end is not representable is refused");
    card_reset();
    check(disk_read(0, UINT64_MAX - 4ULL, 64, dst) == -1,
          "returns -1 when start + count wraps");
    check(card.calls == 0, "issues no card command");
    card_reset();
    check(disk_read(0, UINT64_MAX, 1, dst) == -1,
          "returns -1 for the last representable byte plus one");
    check(card.calls == 0, "still issues no card command");
}

int main(void)
{
    test_shapes();
    test_chunking();
    test_zero_count();
    test_no_card();
    test_error_propagates();
    test_lba_limit();
    test_unreportable_count();
    test_end_overflow();

    printf("\n%s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}
