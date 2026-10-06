/* gzip.c
 *
 * Clean-room implementation of RFC 1951 (DEFLATE) and RFC 1952 (gzip)
 * decompression for wolfBoot. Written from the RFC text only; no derivative
 * work from zlib, miniz, or other implementations.
 *
 * Design notes:
 *  - Single-pass inflate; the output buffer is the LZ77 window.
 *  - Machine-word bit buffer, refilled a byte at a time, kept in locals in
 *    the block decoder. Past the end of input it shifts in zero bits, which
 *    is reported as truncation once they are consumed.
 *  - Two Huffman decoders behind one wrapper, driver and header parser:
 *      default             - table lookup on the next GZIP_ROOT_BITS bits
 *                            with a second level for longer codes, word
 *                            copies where alignment allows, table CRC32
 *                            over the output afterwards. A few KB of
 *                            static tables, several times the rate.
 *      WOLFBOOT_GZIP_SMALL - canonical counts[]/symbols[] decode one bit
 *                            at a time, CRC32 folded in per byte. Smallest
 *                            code, no tables beyond the trees.
 *  - No dynamic allocation: the caller's stack (~6 KB peak) plus, for the
 *    default decoder, the static tables.
 *
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
#ifdef WOLFBOOT_GZIP

#include "gzip.h"
#include <stddef.h>
#include <stdint.h>

/* RFC 1951/1952 constants internal to this module. */

/* RFC 1952 CRC32 (IEEE 802.3 reflected) */
#define GZIP_CRC32_INIT           0xFFFFFFFFU
#define GZIP_CRC32_FINAL_XOR      0xFFFFFFFFU
#define GZIP_CRC32_POLY           0xEDB88320U

/* RFC 1951 DEFLATE - alphabet sizes */
#define GZIP_MAX_HUFF_BITS        15    /* max Huffman code length */
#define GZIP_CL_CODES             19    /* code-length alphabet    */
#define GZIP_LITLEN_CODES         288   /* literal/length alphabet */
#define GZIP_DIST_CODES           32    /* distance alphabet       */

/* RFC 1951 DEFLATE - fixed Huffman boundaries (Sec. 3.2.6) */
#define GZIP_FIXED_LIT_END_8BIT   144   /* 0..143    -> 8 bits */
#define GZIP_FIXED_LIT_END_9BIT   256   /* 144..255  -> 9 bits */
#define GZIP_FIXED_LIT_END_7BIT   280   /* 256..279  -> 7 bits */
#define GZIP_FIXED_LIT_END        288   /* 280..287  -> 8 bits */
#define GZIP_FIXED_DIST_COUNT     30    /* 0..29     -> 5 bits */

/* RFC 1951 DEFLATE - alphabet bounds (Sec. 3.2.4 / 3.2.5) */
#define GZIP_EOB_SYMBOL           256   /* end-of-block marker */
#define GZIP_LENGTH_CODE_BASE     257   /* first length code   */
#define GZIP_LENGTH_CODE_COUNT    29    /* 257..285            */
#define GZIP_DIST_CODE_COUNT      30    /* 0..29               */

/* RFC 1951 DEFLATE - dynamic block header (Sec. 3.2.7) */
#define GZIP_HLIT_BITS            5     /* HLIT field width    */
#define GZIP_HDIST_BITS           5     /* HDIST field width   */
#define GZIP_HCLEN_BITS           4     /* HCLEN field width   */
#define GZIP_HLIT_BASE            257   /* HLIT + 257          */
#define GZIP_HDIST_BASE           1     /* HDIST + 1           */
#define GZIP_HCLEN_BASE           4     /* HCLEN + 4           */
#define GZIP_CL_LEN_BITS          3     /* code-length code is 3 bits */
#define GZIP_CL_MAX_BITS          7     /* code-length codes are 0..7 bits */

/* RFC 1951 Sec. 3.2.7 repeat symbols: 16 = previous length 3..6 times,
 * 17 = zero 3..10 times, 18 = zero 11..138 times */
#define GZIP_REPEAT_PREV_EXTRA    2
#define GZIP_REPEAT_PREV_BASE     3
#define GZIP_REPEAT_Z3_EXTRA      3
#define GZIP_REPEAT_Z3_BASE       3
#define GZIP_REPEAT_Z7_EXTRA      7
#define GZIP_REPEAT_Z7_BASE       11

/* RFC 1951 Sec. 3.2.5: length codes 257..285 base values and extra bits */
static const uint16_t gz_len_base[29] = {
    3,   4,   5,   6,   7,   8,   9,  10,
    11,  13,  15,  17,  19,  23,  27,  31,
    35,  43,  51,  59,  67,  83,  99, 115,
    131, 163, 195, 227, 258
};
static const uint8_t gz_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};

/* RFC 1951 Sec. 3.2.5: distance codes 0..29 base values and extra bits */
static const uint16_t gz_dist_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,
    17,   25,   33,   49,   65,   97,   129,  193,
    257,  385,  513,  769,  1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t gz_dist_extra[30] = {
    0, 0, 0, 0,  1,  1,  2,  2,
    3, 3, 4, 4,  5,  5,  6,  6,
    7, 7, 8, 8,  9,  9, 10, 10,
    11, 11, 12, 12, 13, 13
};

/* RFC 1951 Sec. 3.2.7: code-length code permutation */
static const uint8_t gz_cl_order[GZIP_CL_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* Bit buffer width; WOLFBOOT_GZIP_BITS_32 forces the narrow one for tests. */
#if (UINTPTR_MAX > 0xFFFFFFFFU) && !defined(WOLFBOOT_GZIP_BITS_32)
typedef uint64_t gz_bits_t;
#define GZIP_BITS_WIDTH           64
#else
typedef uint32_t gz_bits_t;
#define GZIP_BITS_WIDTH           32
#endif
#define GZIP_BITS_FULL            (GZIP_BITS_WIDTH - 8)

typedef struct gz_state {
    const uint8_t *in;        /* next input byte */
    const uint8_t *in_end;
    gz_bits_t      buf;       /* bits not yet consumed, LSB first */
    uint32_t       nbits;     /* valid bits in buf */
    uint32_t       over;      /* zero bits appended past the end of input */

    uint8_t       *out;       /* output buffer (doubles as sliding window) */
    uint8_t       *out_end;
    uint8_t       *op;        /* next output byte */
#ifdef WOLFBOOT_GZIP_SMALL
    uint32_t       crc32;     /* running CRC32 of decompressed bytes */
#endif
} gz_state_t;

#if defined(__GNUC__)
typedef uint64_t gz_word_t __attribute__((__may_alias__));
#define GZIP_WORD_ACCESS 1
#endif

/* ------------------------------------------------------------------------- */
/* Bit stream reader (LSB-first within bytes per RFC 1951 Sec. 3.1.1)        */
/* ------------------------------------------------------------------------- */

/* Macros over named scalars so the block decoder keeps the reader in
 * registers; the struct forms serve the slow paths. */

/* Refill to more than GZIP_BITS_FULL bits; past the end of input, zero
 * bytes are appended and counted in `over`. */
#define GZ_REFILL_V(buf, nbits, over, in, in_end) \
    do { \
        while ((nbits) <= GZIP_BITS_FULL) { \
            if ((in) < (in_end)) { \
                (buf) |= (gz_bits_t)(*(in)++) << (nbits); \
            } \
            else { \
                (over) += 8; \
            } \
            (nbits) += 8; \
        } \
    } while (0)

/* Bits that were never in the input have been consumed. */
#define GZ_OVERRUN_V(nbits, over)     ((over) > (nbits))

#define GZ_PEEK_V(buf, n)             ((uint32_t)(buf) & ((1U << (n)) - 1U))
#define GZ_DROP_V(buf, nbits, n) \
    do { (buf) >>= (n); (nbits) -= (n); } while (0)

#define GZ_REFILL(s)   GZ_REFILL_V((s)->buf, (s)->nbits, (s)->over, \
                                   (s)->in, (s)->in_end)
#define GZ_OVERRUN(s)  GZ_OVERRUN_V((s)->nbits, (s)->over)
#define GZ_PEEK(s, n)  GZ_PEEK_V((s)->buf, (n))
#define GZ_DROP(s, n)  GZ_DROP_V((s)->buf, (s)->nbits, (n))

static uint32_t gz_get_bits(gz_state_t *s, uint32_t n)
{
    uint32_t v;

    GZ_REFILL(s);
    v = GZ_PEEK(s, n);
    GZ_DROP(s, n);
    return v;
}

/* Drop the partial byte and hand whole buffered bytes back to the cursor. */
static int gz_align_byte(gz_state_t *s)
{
    GZ_DROP(s, s->nbits & 7U);
    if (GZ_OVERRUN(s)) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    s->in -= (s->nbits >> 3) - (s->over >> 3);
    s->buf = 0;
    s->nbits = 0;
    s->over = 0;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* CRC32                                                                     */
/* ------------------------------------------------------------------------- */

#ifdef WOLFBOOT_GZIP_SMALL

static uint32_t gz_crc32_byte(uint32_t crc, uint8_t b)
{
    int k;
    crc ^= b;
    for (k = 0; k < 8; k++) {
        if (crc & 1U) {
            crc = (crc >> 1) ^ GZIP_CRC32_POLY;
        } else {
            crc = crc >> 1;
        }
    }
    return crc;
}

#else /* !WOLFBOOT_GZIP_SMALL */

static uint32_t gz_crc_table[256];
static int      gz_crc_table_ready;

static void gz_crc_init(void)
{
    uint32_t i, k, c;

    if (gz_crc_table_ready) {
        return;
    }
    for (i = 0; i < 256; i++) {
        c = i;
        for (k = 0; k < 8; k++) {
            c = (c & 1U) ? ((c >> 1) ^ GZIP_CRC32_POLY) : (c >> 1);
        }
        gz_crc_table[i] = c;
    }
    gz_crc_table_ready = 1;
}

#define GZ_CRC_BYTE(crc, b) \
    ((gz_crc_table[((crc) ^ (b)) & 0xFFU]) ^ ((crc) >> 8))

static uint32_t gz_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t crc = GZIP_CRC32_INIT;

#if defined(GZIP_WORD_ACCESS) && defined(__BYTE_ORDER__) && \
    (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    /* Aligned words, then the bytes of each word in stream order. */
    while ((len > 0) && (((uintptr_t)p & (sizeof(gz_word_t) - 1U)) != 0)) {
        crc = GZ_CRC_BYTE(crc, *p++);
        len--;
    }
    while (len >= sizeof(gz_word_t)) {
        gz_word_t w = *(const gz_word_t *)p;
        uint32_t k;

        for (k = 0; k < sizeof(gz_word_t); k++) {
            crc = GZ_CRC_BYTE(crc, (uint8_t)w);
            w >>= 8;
        }
        p += sizeof(gz_word_t);
        len -= (uint32_t)sizeof(gz_word_t);
    }
#endif
    while (len > 0) {
        crc = GZ_CRC_BYTE(crc, *p++);
        len--;
    }
    return crc ^ GZIP_CRC32_FINAL_XOR;
}

#endif /* WOLFBOOT_GZIP_SMALL */

/* ------------------------------------------------------------------------- */
/* Huffman decoding (RFC 1951 Sec. 3.2.2)                                    */
/* ------------------------------------------------------------------------- */

#ifdef WOLFBOOT_GZIP_SMALL

/* Canonical code described by the number of codes of each length and the
 * symbols sorted by code length, then value. */
typedef struct gz_huff {
    int16_t counts[GZIP_MAX_HUFF_BITS + 1];
    int16_t symbols[GZIP_LITLEN_CODES];
} gz_huff_t;

static int gz_huff_build(gz_huff_t *h, const uint8_t *lengths, int n)
{
    int ret = 0;
    int sym, len, left, all_zero;
    int16_t offs[GZIP_MAX_HUFF_BITS + 1];

    for (len = 0; len <= GZIP_MAX_HUFF_BITS; len++) {
        h->counts[len] = 0;
    }
    for (sym = 0; (sym < n) && (ret == 0); sym++) {
        if (lengths[sym] > GZIP_MAX_HUFF_BITS) {
            ret = WOLFBOOT_GZIP_E_HUFFMAN;
        }
        else {
            h->counts[lengths[sym]]++;
        }
    }

    /* Empty alphabet is permitted. */
    all_zero = (ret == 0) && (h->counts[0] == n);

    /* Kraft inequality: reject over-subscribed sets, accept incomplete ones. */
    if ((ret == 0) && !all_zero) {
        left = 1;
        for (len = 1; (len <= GZIP_MAX_HUFF_BITS) && (ret == 0); len++) {
            left <<= 1;
            if ((int)h->counts[len] > left) {
                ret = WOLFBOOT_GZIP_E_HUFFMAN;
            }
            else {
                left -= (int)h->counts[len];
            }
        }
    }

    if ((ret == 0) && !all_zero) {
        /* symbols[] in canonical order: by code length, then symbol */
        offs[1] = 0;
        for (len = 1; len < GZIP_MAX_HUFF_BITS; len++) {
            offs[len + 1] = (int16_t)(offs[len] + h->counts[len]);
        }
        for (sym = 0; sym < n; sym++) {
            int sl = lengths[sym];
            if (sl != 0) {
                h->symbols[offs[sl]] = (int16_t)sym;
                offs[sl]++;
            }
        }
    }
    return ret;
}

/* One symbol, one bit at a time; the reader must hold GZIP_MAX_HUFF_BITS.
 * Returns the symbol or a negative error for a pattern that is no code. */
static int gz_huff_decode(const gz_huff_t *h, gz_bits_t *buf, uint32_t *nbits)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int len, count;

    for (len = 1; len <= GZIP_MAX_HUFF_BITS; len++) {
        code |= (int)GZ_PEEK_V(*buf, 1);
        GZ_DROP_V(*buf, *nbits, 1);
        count = h->counts[len];
        if (code - count < first) {
            return h->symbols[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return WOLFBOOT_GZIP_E_HUFFMAN;
}

#define GZ_DECODE_V(h, sym, buf, nbits) \
    do { (sym) = gz_huff_decode((h), &(buf), &(nbits)); } while (0)

#else /* !WOLFBOOT_GZIP_SMALL */

/* Root index width: 9 bits resolves the fixed trees and nearly every
 * dynamic code in one lookup. At least 7, so the code-length code never
 * needs a second level. */
#ifndef WOLFBOOT_GZIP_ROOT_BITS
#define WOLFBOOT_GZIP_ROOT_BITS   9
#endif
#if (WOLFBOOT_GZIP_ROOT_BITS < GZIP_CL_MAX_BITS) || \
    (WOLFBOOT_GZIP_ROOT_BITS > GZIP_MAX_HUFF_BITS)
#error "WOLFBOOT_GZIP_ROOT_BITS must be between 7 and 15"
#endif
#define GZIP_ROOT_BITS            WOLFBOOT_GZIP_ROOT_BITS
#define GZIP_ROOT_SIZE            (1U << GZIP_ROOT_BITS)
#define GZIP_ROOT_MASK            (GZIP_ROOT_SIZE - 1U)

/* Second-level pools, well above what a valid code needs behind a 9-bit
 * root; a code that would overflow is rejected. */
#define GZIP_LITLEN_SUB_MAX       2048
#define GZIP_DIST_SUB_MAX         1024

/* Entry: bits 0..15 symbol or sub-table offset, bits 16..19 code bits
 * consumed or sub-table index width, bit 31 sub-table. Zero = no code. */
#define GZ_ENTRY_SUB              0x80000000U
#define GZ_ENTRY_LEN(e)           (((e) >> 16) & 0xFU)
#define GZ_ENTRY_SYM(e)           ((e) & 0xFFFFU)
#define GZ_LEAF(sym, len)         (((uint32_t)(len) << 16) | (uint32_t)(sym))

/* `root` is indexed by the next GZIP_ROOT_BITS of the stream; `sub` holds
 * the second level behind root slots that lead to longer codes. */
typedef struct gz_huff {
    uint32_t *root;        /* GZIP_ROOT_SIZE entries */
    uint32_t *sub;         /* second-level pool */
    uint32_t  sub_max;     /* pool capacity in entries */
} gz_huff_t;

/* Static rather than on the stack of a function deep in the boot path. */
static uint32_t gz_litlen_root[GZIP_ROOT_SIZE];
static uint32_t gz_litlen_sub[GZIP_LITLEN_SUB_MAX];
static uint32_t gz_dist_root[GZIP_ROOT_SIZE];
static uint32_t gz_dist_sub[GZIP_DIST_SUB_MAX];
static uint32_t gz_cl_root[GZIP_ROOT_SIZE];

/* Huffman codes are packed MSB first into an LSB-first stream (RFC 1951
 * Sec. 3.1.1), so table indices are the reversed codes. */
static uint32_t gz_reverse(uint32_t code, uint32_t len)
{
    uint32_t r = 0;

    while (len > 0) {
        r = (r << 1) | (code & 1U);
        code >>= 1;
        len--;
    }
    return r;
}

/* Lookup tables from canonical code lengths. Over-subscribed sets are
 * rejected; incomplete sets leave zero slots that decode as no code. */
static int gz_huff_build(gz_huff_t *t, const uint8_t *lengths, int n)
{
    uint32_t count[GZIP_MAX_HUFF_BITS + 1];
    uint32_t next_code[GZIP_MAX_HUFF_BITS + 1];
    uint8_t  sub_bits[GZIP_ROOT_SIZE];
    uint32_t sym, len, code, rev, i, left, sub_used;
    uint32_t num = (uint32_t)n;

    for (len = 0; len <= GZIP_MAX_HUFF_BITS; len++) {
        count[len] = 0;
    }
    for (sym = 0; sym < num; sym++) {
        if (lengths[sym] > GZIP_MAX_HUFF_BITS) {
            return WOLFBOOT_GZIP_E_HUFFMAN;
        }
        count[lengths[sym]]++;
    }
    for (i = 0; i < GZIP_ROOT_SIZE; i++) {
        t->root[i] = 0;
    }
    if (count[0] == num) {
        return 0; /* empty alphabet is permitted */
    }

    /* Kraft inequality: reject over-subscribed sets. */
    left = 1;
    for (len = 1; len <= GZIP_MAX_HUFF_BITS; len++) {
        left <<= 1;
        if (count[len] > left) {
            return WOLFBOOT_GZIP_E_HUFFMAN;
        }
        left -= count[len];
    }

    /* First code of each length, in canonical order. */
    code = 0;
    count[0] = 0;
    for (len = 1; len <= GZIP_MAX_HUFF_BITS; len++) {
        code = (code + count[len - 1]) << 1;
        next_code[len] = code;
    }

    /* Pass 1: size each sub-table by the longest code behind its root slot. */
    for (i = 0; i < GZIP_ROOT_SIZE; i++) {
        sub_bits[i] = 0;
    }
    for (len = GZIP_ROOT_BITS + 1; len <= GZIP_MAX_HUFF_BITS; len++) {
        code = next_code[len];
        for (sym = 0; sym < num; sym++) {
            if (lengths[sym] == len) {
                rev = gz_reverse(code, len) & GZIP_ROOT_MASK;
                if (sub_bits[rev] < len - GZIP_ROOT_BITS) {
                    sub_bits[rev] = (uint8_t)(len - GZIP_ROOT_BITS);
                }
                code++;
            }
        }
    }
    sub_used = 0;
    for (i = 0; i < GZIP_ROOT_SIZE; i++) {
        if (sub_bits[i] != 0) {
            if (sub_used + (1U << sub_bits[i]) > t->sub_max) {
                return WOLFBOOT_GZIP_E_HUFFMAN;
            }
            t->root[i] = GZ_ENTRY_SUB | ((uint32_t)sub_bits[i] << 16) |
                         sub_used;
            sub_used += 1U << sub_bits[i];
        }
    }
    for (i = 0; i < sub_used; i++) {
        t->sub[i] = 0;
    }

    /* Pass 2: fill every slot whose low bits are the reversed code; the
     * bits beyond the root index the sub-table the same way. */
    for (sym = 0; sym < num; sym++) {
        len = lengths[sym];
        if (len == 0) {
            continue;
        }
        code = next_code[len]++;
        rev = gz_reverse(code, len);
        if (len <= GZIP_ROOT_BITS) {
            for (i = rev; i < GZIP_ROOT_SIZE; i += (1U << len)) {
                t->root[i] = GZ_LEAF(sym, len);
            }
        }
        else {
            uint32_t root_e = t->root[rev & GZIP_ROOT_MASK];
            uint32_t width = GZ_ENTRY_LEN(root_e);
            uint32_t base = GZ_ENTRY_SYM(root_e);
            uint32_t rest = len - GZIP_ROOT_BITS;

            for (i = rev >> GZIP_ROOT_BITS; i < (1U << width); i += (1U << rest)) {
                t->sub[base + i] = GZ_LEAF(sym, rest);
            }
        }
    }
    return 0;
}

/* `sym` becomes the next symbol, or a negative error for a pattern that
 * is no code. */
#define GZ_DECODE_V(tab, sym, buf, nbits) \
    do { \
        uint32_t e_ = (tab)->root[GZ_PEEK_V(buf, GZIP_ROOT_BITS)]; \
        uint32_t l_; \
        if (e_ & GZ_ENTRY_SUB) { \
            GZ_DROP_V(buf, nbits, GZIP_ROOT_BITS); \
            e_ = (tab)->sub[GZ_ENTRY_SYM(e_) + \
                            GZ_PEEK_V(buf, GZ_ENTRY_LEN(e_))]; \
        } \
        l_ = GZ_ENTRY_LEN(e_); \
        if (l_ == 0) { \
            (sym) = WOLFBOOT_GZIP_E_HUFFMAN; \
        } \
        else { \
            GZ_DROP_V(buf, nbits, l_); \
            (sym) = (int)GZ_ENTRY_SYM(e_); \
        } \
    } while (0)

#endif /* WOLFBOOT_GZIP_SMALL */

static int gz_decode(gz_state_t *s, const gz_huff_t *h)
{
    int sym;

    GZ_DECODE_V(h, sym, s->buf, s->nbits);
    return sym;
}

/* ------------------------------------------------------------------------- */
/* Output                                                                    */
/* ------------------------------------------------------------------------- */

#ifdef WOLFBOOT_GZIP_SMALL

/* One byte out, CRC folded in. */
#define GZ_PUT_V(op, crc, b) \
    do { \
        uint8_t b_ = (uint8_t)(b); \
        *(op)++ = b_; \
        (crc) = gz_crc32_byte((crc), b_); \
    } while (0)

/* Back-reference byte by byte, which also serves overlapping runs. */
#define GZ_COPY_V(op, crc, src, len, dist) \
    do { \
        uint32_t n_ = (len); \
        const uint8_t *s_ = (src); \
        (void)(dist); \
        while (n_ > 0) { \
            GZ_PUT_V(op, crc, *s_++); \
            n_--; \
        } \
    } while (0)

#else /* !WOLFBOOT_GZIP_SMALL */

#define GZ_PUT_V(op, crc, b) \
    do { *(op)++ = (uint8_t)(b); (void)(crc); } while (0)

/* Back-reference, possibly overlapping its source. Words are moved only
 * when the distance is at least a word and both cursors share alignment,
 * so nothing is read unaligned or before it is written. */
static void gz_copy(uint8_t *op, const uint8_t *src, uint32_t len,
    uint32_t dist)
{
#ifdef GZIP_WORD_ACCESS
    if ((dist >= sizeof(gz_word_t)) &&
            ((((uintptr_t)op ^ (uintptr_t)src) & (sizeof(gz_word_t) - 1U)) == 0)) {
        while ((len > 0) && (((uintptr_t)op & (sizeof(gz_word_t) - 1U)) != 0)) {
            *op++ = *src++;
            len--;
        }
        while (len >= sizeof(gz_word_t)) {
            *(gz_word_t *)op = *(const gz_word_t *)src;
            op += sizeof(gz_word_t);
            src += sizeof(gz_word_t);
            len -= (uint32_t)sizeof(gz_word_t);
        }
    }
#else
    (void)dist;
#endif
    while (len > 0) {
        *op++ = *src++;
        len--;
    }
}

#define GZ_COPY_V(op, crc, src, len, dist) \
    do { gz_copy((op), (src), (len), (dist)); (op) += (len); (void)(crc); } while (0)

#endif /* WOLFBOOT_GZIP_SMALL */

/* ------------------------------------------------------------------------- */
/* Block decoders                                                            */
/* ------------------------------------------------------------------------- */

static int gz_inflate_stored(gz_state_t *s)
{
    uint32_t len, nlen;
    uint32_t crc = 0;
    int ret;

    ret = gz_align_byte(s);
    if (ret != 0) {
        return ret;
    }
    /* LEN and NLEN are little-endian 16-bit words */
    if (s->in_end - s->in < 4) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    len  = (uint32_t)s->in[0] | ((uint32_t)s->in[1] << 8);
    nlen = (uint32_t)s->in[2] | ((uint32_t)s->in[3] << 8);
    s->in += 4;
    if ((len ^ 0xFFFFU) != nlen) {
        return WOLFBOOT_GZIP_E_FORMAT;
    }
    if ((uint32_t)(s->in_end - s->in) < len) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if ((uint32_t)(s->out_end - s->op) < len) {
        return WOLFBOOT_GZIP_E_OUTPUT;
    }
#ifdef WOLFBOOT_GZIP_SMALL
    crc = s->crc32;
#endif
    while (len > 0) {
        GZ_PUT_V(s->op, crc, *s->in++);
        len--;
    }
#ifdef WOLFBOOT_GZIP_SMALL
    s->crc32 = crc;
#endif
    return 0;
}

/* Block body up to and including the end-of-block symbol, with the reader
 * state in locals. */
static int gz_inflate_huffman(gz_state_t *s, const gz_huff_t *litlen,
    const gz_huff_t *dist)
{
    const uint8_t *in = s->in;
    const uint8_t *in_end = s->in_end;
    gz_bits_t buf = s->buf;
    uint32_t nbits = s->nbits;
    uint32_t over = s->over;
    uint8_t *out = s->out;
    uint8_t *out_end = s->out_end;
    uint8_t *op = s->op;
    uint32_t crc = 0;
    uint32_t length, distance, extra;
    int sym;
    int ret = 0;

#ifdef WOLFBOOT_GZIP_SMALL
    crc = s->crc32;
#endif
    for (;;) {
        GZ_REFILL_V(buf, nbits, over, in, in_end);
        if (GZ_OVERRUN_V(nbits, over)) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
            break;
        }
        GZ_DECODE_V(litlen, sym, buf, nbits);
        if (sym < 0) {
            ret = sym;
            break;
        }
        if (sym < GZIP_EOB_SYMBOL) {
            if (op >= out_end) {
                ret = WOLFBOOT_GZIP_E_OUTPUT;
                break;
            }
            GZ_PUT_V(op, crc, sym);
            continue;
        }
        if (sym == GZIP_EOB_SYMBOL) {
            break;
        }
        /* length code 257..285 -> length 3..258 */
        sym -= GZIP_LENGTH_CODE_BASE;
        if (sym >= GZIP_LENGTH_CODE_COUNT) {
            ret = WOLFBOOT_GZIP_E_HUFFMAN;
            break;
        }
        extra = gz_len_extra[sym];
        length = gz_len_base[sym] + GZ_PEEK_V(buf, extra);
        GZ_DROP_V(buf, nbits, extra);

        GZ_REFILL_V(buf, nbits, over, in, in_end);
        GZ_DECODE_V(dist, sym, buf, nbits);
        if (sym < 0) {
            ret = sym;
            break;
        }
        if (sym >= GZIP_DIST_CODE_COUNT) {
            ret = WOLFBOOT_GZIP_E_HUFFMAN;
            break;
        }
        extra = gz_dist_extra[sym];
        GZ_REFILL_V(buf, nbits, over, in, in_end);
        distance = gz_dist_base[sym] + GZ_PEEK_V(buf, extra);
        GZ_DROP_V(buf, nbits, extra);
        if (GZ_OVERRUN_V(nbits, over)) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
            break;
        }

        if (distance > (uint32_t)(op - out)) {
            ret = WOLFBOOT_GZIP_E_DISTANCE;
            break;
        }
        if (length > (uint32_t)(out_end - op)) {
            ret = WOLFBOOT_GZIP_E_OUTPUT;
            break;
        }
        GZ_COPY_V(op, crc, op - distance, length, distance);
    }
    s->in = in;
    s->buf = buf;
    s->nbits = nbits;
    s->over = over;
    s->op = op;
#ifdef WOLFBOOT_GZIP_SMALL
    s->crc32 = crc;
#endif
    return ret;
}

/* Build the fixed Huffman trees defined in RFC 1951 Sec. 3.2.6 */
static int gz_build_fixed(gz_huff_t *litlen, gz_huff_t *dist)
{
    int ret;
    uint8_t lengths[GZIP_LITLEN_CODES];
    int i;

    for (i = 0;                        i < GZIP_FIXED_LIT_END_8BIT; i++) lengths[i] = 8;
    for (i = GZIP_FIXED_LIT_END_8BIT;  i < GZIP_FIXED_LIT_END_9BIT; i++) lengths[i] = 9;
    for (i = GZIP_FIXED_LIT_END_9BIT;  i < GZIP_FIXED_LIT_END_7BIT; i++) lengths[i] = 7;
    for (i = GZIP_FIXED_LIT_END_7BIT;  i < GZIP_FIXED_LIT_END;      i++) lengths[i] = 8;
    ret = gz_huff_build(litlen, lengths, GZIP_FIXED_LIT_END);
    if (ret == 0) {
        for (i = 0; i < GZIP_FIXED_DIST_COUNT; i++) lengths[i] = 5;
        ret = gz_huff_build(dist, lengths, GZIP_FIXED_DIST_COUNT);
    }
    return ret;
}

/* RFC 1951 Sec. 3.2.7: code-length code, then the two trees, then the body. */
static int gz_inflate_dynamic(gz_state_t *s, gz_huff_t *litlen,
    gz_huff_t *dist)
{
    uint8_t cl_lens[GZIP_CL_CODES];
    uint8_t code_lens[GZIP_LITLEN_CODES + GZIP_DIST_CODES];
    gz_huff_t cl_huff;
    uint32_t hlit, hdist, hclen, i, total, idx, val;
    uint8_t prev = 0;
    int sym, ret;

    hlit  = gz_get_bits(s, GZIP_HLIT_BITS) + GZIP_HLIT_BASE;
    hdist = gz_get_bits(s, GZIP_HDIST_BITS) + GZIP_HDIST_BASE;
    hclen = gz_get_bits(s, GZIP_HCLEN_BITS) + GZIP_HCLEN_BASE;
    if ((hlit > GZIP_LITLEN_CODES) || (hdist > GZIP_DIST_CODES) ||
            (hclen > GZIP_CL_CODES)) {
        return WOLFBOOT_GZIP_E_FORMAT;
    }

    /* Read code-length code lengths in the permuted order */
    for (i = 0; i < GZIP_CL_CODES; i++) {
        cl_lens[i] = 0;
    }
    for (i = 0; i < hclen; i++) {
        cl_lens[gz_cl_order[i]] = (uint8_t)gz_get_bits(s, GZIP_CL_LEN_BITS);
    }
    if (GZ_OVERRUN(s)) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
#ifndef WOLFBOOT_GZIP_SMALL
    /* code-length codes are at most 7 bits, so no second level */
    cl_huff.root = gz_cl_root;
    cl_huff.sub = NULL;
    cl_huff.sub_max = 0;
#endif
    ret = gz_huff_build(&cl_huff, cl_lens, GZIP_CL_CODES);
    if (ret != 0) {
        return ret;
    }

    /* Decode the litlen + dist code-length sequence using the CL tree */
    total = hlit + hdist;
    idx = 0;
    while (idx < total) {
        GZ_REFILL(s);
        if (GZ_OVERRUN(s)) {
            return WOLFBOOT_GZIP_E_TRUNCATED;
        }
        sym = gz_decode(s, &cl_huff);
        if (sym < 0) {
            return sym;
        }
        if (sym < 16) {
            code_lens[idx++] = (uint8_t)sym;
            prev = (uint8_t)sym;
        }
        else if (sym == 16) {
            /* repeat previous length 3..6 times (2 extra bits) */
            if (idx == 0) {
                return WOLFBOOT_GZIP_E_FORMAT;
            }
            val = GZ_PEEK(s, GZIP_REPEAT_PREV_EXTRA) + GZIP_REPEAT_PREV_BASE;
            GZ_DROP(s, GZIP_REPEAT_PREV_EXTRA);
            if (idx + val > total) {
                return WOLFBOOT_GZIP_E_FORMAT;
            }
            while (val--) code_lens[idx++] = prev;
        }
        else if (sym == 17) {
            /* repeat zero 3..10 times (3 extra bits) */
            val = GZ_PEEK(s, GZIP_REPEAT_Z3_EXTRA) + GZIP_REPEAT_Z3_BASE;
            GZ_DROP(s, GZIP_REPEAT_Z3_EXTRA);
            if (idx + val > total) {
                return WOLFBOOT_GZIP_E_FORMAT;
            }
            while (val--) code_lens[idx++] = 0;
            prev = 0;
        }
        else if (sym == 18) {
            /* repeat zero 11..138 times (7 extra bits) */
            val = GZ_PEEK(s, GZIP_REPEAT_Z7_EXTRA) + GZIP_REPEAT_Z7_BASE;
            GZ_DROP(s, GZIP_REPEAT_Z7_EXTRA);
            if (idx + val > total) {
                return WOLFBOOT_GZIP_E_FORMAT;
            }
            while (val--) code_lens[idx++] = 0;
            prev = 0;
        }
        else {
            return WOLFBOOT_GZIP_E_FORMAT;
        }
    }
    if (GZ_OVERRUN(s)) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }

    /* End-of-block symbol (256) must have a code */
    if (code_lens[GZIP_EOB_SYMBOL] == 0) {
        return WOLFBOOT_GZIP_E_HUFFMAN;
    }
    ret = gz_huff_build(litlen, code_lens, (int)hlit);
    if (ret == 0) {
        ret = gz_huff_build(dist, code_lens + hlit, (int)hdist);
    }
    if (ret == 0) {
        ret = gz_inflate_huffman(s, litlen, dist);
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* DEFLATE driver                                                            */
/* ------------------------------------------------------------------------- */

static int gz_inflate(gz_state_t *s)
{
    gz_huff_t litlen, dist;
    uint32_t bfinal, btype;
    int fixed_built = 0;
    int ret = 0;

#ifndef WOLFBOOT_GZIP_SMALL
    litlen.root = gz_litlen_root;
    litlen.sub = gz_litlen_sub;
    litlen.sub_max = GZIP_LITLEN_SUB_MAX;
    dist.root = gz_dist_root;
    dist.sub = gz_dist_sub;
    dist.sub_max = GZIP_DIST_SUB_MAX;
#endif

    do {
        bfinal = gz_get_bits(s, 1);
        btype = gz_get_bits(s, 2);
        if (GZ_OVERRUN(s)) {
            return WOLFBOOT_GZIP_E_TRUNCATED;
        }
        if (btype == 0) {
            ret = gz_inflate_stored(s);
        }
        else if (btype == 1) {
            /* rebuilt only after a dynamic block replaced it */
            if (!fixed_built) {
                ret = gz_build_fixed(&litlen, &dist);
                fixed_built = 1;
            }
            if (ret == 0) {
                ret = gz_inflate_huffman(s, &litlen, &dist);
            }
        }
        else if (btype == 2) {
            ret = gz_inflate_dynamic(s, &litlen, &dist);
            fixed_built = 0;
        }
        else {
            ret = WOLFBOOT_GZIP_E_FORMAT;
        }
    } while ((ret == 0) && !bfinal);
    return ret;
}

/* ------------------------------------------------------------------------- */
/* RFC 1952 wrapper                                                          */
/* ------------------------------------------------------------------------- */

static int gz_skip_zstring(gz_state_t *s)
{
    while (s->in < s->in_end) {
        if (*s->in++ == 0) {
            return 0;
        }
    }
    return WOLFBOOT_GZIP_E_TRUNCATED;
}

static int gz_parse_header(gz_state_t *s)
{
    uint8_t flg;
    uint32_t xlen;

    if (s->in_end - s->in < GZIP_HEADER_MIN_SIZE) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    /* Magic 1F 8B, CM = 8 (DEFLATE) */
    if ((s->in[0] != GZIP_MAGIC_ID1) || (s->in[1] != GZIP_MAGIC_ID2) ||
            (s->in[2] != GZIP_CM_DEFLATE)) {
        return WOLFBOOT_GZIP_E_FORMAT;
    }
    flg = s->in[3];
    if (flg & GZIP_FLG_RESERVED) {
        return WOLFBOOT_GZIP_E_FORMAT;
    }
    /* Skip MTIME(4) + XFL(1) + OS(1) */
    s->in += GZIP_HEADER_MIN_SIZE;

    if (flg & GZIP_FLG_FEXTRA) {
        if (s->in_end - s->in < 2) {
            return WOLFBOOT_GZIP_E_TRUNCATED;
        }
        xlen = (uint32_t)s->in[0] | ((uint32_t)s->in[1] << 8);
        s->in += 2;
        if ((uint32_t)(s->in_end - s->in) < xlen) {
            return WOLFBOOT_GZIP_E_TRUNCATED;
        }
        s->in += xlen;
    }
    if ((flg & GZIP_FLG_FNAME) && (gz_skip_zstring(s) != 0)) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if ((flg & GZIP_FLG_FCOMMENT) && (gz_skip_zstring(s) != 0)) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if (flg & GZIP_FLG_FHCRC) {
        if (s->in_end - s->in < 2) {
            return WOLFBOOT_GZIP_E_TRUNCATED;
        }
        s->in += 2; /* header CRC; not validated */
    }
    return 0;
}

static int gz_parse_trailer(gz_state_t *s, uint32_t computed_crc,
                            uint32_t bytes_out)
{
    uint32_t got_crc, got_isize;
    int ret;

    /* Discard partial byte from final block, then read 8-byte trailer */
    ret = gz_align_byte(s);
    if (ret != 0) {
        return ret;
    }
    if (s->in_end - s->in < GZIP_TRAILER_SIZE) {
        return WOLFBOOT_GZIP_E_TRUNCATED;
    }
    got_crc = (uint32_t)s->in[0] | ((uint32_t)s->in[1] << 8) |
              ((uint32_t)s->in[2] << 16) | ((uint32_t)s->in[3] << 24);
    got_isize = (uint32_t)s->in[4] | ((uint32_t)s->in[5] << 8) |
                ((uint32_t)s->in[6] << 16) | ((uint32_t)s->in[7] << 24);
    s->in += GZIP_TRAILER_SIZE;
    if (got_crc != computed_crc) {
        return WOLFBOOT_GZIP_E_CRC32;
    }
    if (got_isize != bytes_out) {
        return WOLFBOOT_GZIP_E_ISIZE;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Public entry point                                                        */
/* ------------------------------------------------------------------------- */

int wolfBoot_gunzip(const uint8_t *in, uint32_t in_len,
                    uint8_t *out, uint32_t out_max,
                    uint32_t *out_len)
{
    gz_state_t s;
    uint32_t produced, crc;
    int ret;

    if ((in == NULL) || (out == NULL) || (out_len == NULL)) {
        return WOLFBOOT_GZIP_E_PARAM;
    }
    s.in = in;
    s.in_end = in + in_len;
    s.buf = 0;
    s.nbits = 0;
    s.over = 0;
    s.out = out;
    s.out_end = out + out_max;
    s.op = out;
#ifdef WOLFBOOT_GZIP_SMALL
    s.crc32 = GZIP_CRC32_INIT;
#endif

    ret = gz_parse_header(&s);
    if (ret == 0) {
        ret = gz_inflate(&s);
    }
    produced = (uint32_t)(s.op - out);
    if (ret == 0) {
#ifdef WOLFBOOT_GZIP_SMALL
        /* Final CRC32 is the running register XOR'd with the final mask */
        crc = s.crc32 ^ GZIP_CRC32_FINAL_XOR;
#else
        gz_crc_init();
        crc = gz_crc32(out, produced);
#endif
        ret = gz_parse_trailer(&s, crc, produced);
    }
    *out_len = produced;
    return ret;
}

#endif /* WOLFBOOT_GZIP */
