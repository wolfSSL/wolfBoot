/* gzip.c
 *
 * Clean-room implementation of RFC 1951 (DEFLATE) and RFC 1952 (gzip)
 * decompression for wolfBoot. Written from the RFC text only; no derivative
 * work from zlib, miniz, or other implementations.
 *
 * Design notes:
 *  - Single-pass inflate. The output buffer doubles as the LZ77 sliding
 *    window, so back-references read from out[out_pos - distance].
 *  - Canonical Huffman decode using counts[] / symbols[] tables. Slightly
 *    slower than a fast lookup table but ~10x smaller in code size, which
 *    matters for the bootloader.
 *  - No dynamic allocation; state lives on the caller's stack (~6 KB peak).
 *  - CRC32 IEEE 802.3 polynomial computed on-the-fly during output.
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

typedef struct gz_state {
    /* input bit stream */
    const uint8_t *in;
    uint32_t       in_len;
    uint32_t       in_pos;
    uint32_t       bit_buf;
    int            bit_count;

    /* output buffer (doubles as sliding window) */
    uint8_t       *out;
    uint32_t       out_max;
    uint32_t       out_pos;

    /* running CRC32 of decompressed bytes */
    uint32_t       crc32;
} gz_state_t;

typedef struct gz_huff {
    int16_t counts[GZIP_MAX_HUFF_BITS + 1];
    int16_t symbols[GZIP_LITLEN_CODES];
} gz_huff_t;

/* ------------------------------------------------------------------------- */
/* CRC32                                                                     */
/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/* Bit stream reader (LSB-first within bytes per RFC 1951 Sec. 3.1.1)        */
/* ------------------------------------------------------------------------- */

static int gz_need_bits(gz_state_t *s, int n)
{
    int ret = 0;

    while ((ret == 0) && (s->bit_count < n)) {
        if (s->in_pos >= s->in_len) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
        }
        else {
            s->bit_buf |= ((uint32_t)s->in[s->in_pos]) << s->bit_count;
            s->in_pos++;
            s->bit_count += 8;
        }
    }
    return ret;
}

static int gz_get_bits(gz_state_t *s, int n, uint32_t *val)
{
    int ret = gz_need_bits(s, n);
    if (ret == 0) {
        *val = s->bit_buf & (((uint32_t)1 << n) - 1);
        s->bit_buf >>= n;
        s->bit_count -= n;
    }
    return ret;
}

static void gz_align_byte(gz_state_t *s)
{
    int drop = s->bit_count & 7;
    s->bit_buf >>= drop;
    s->bit_count -= drop;
}

/* ------------------------------------------------------------------------- */
/* Output writer (writes byte; updates CRC32; back-ref reads from same buf)  */
/* ------------------------------------------------------------------------- */

static int gz_emit_byte(gz_state_t *s, uint8_t b)
{
    int ret = 0;

    if (s->out_pos >= s->out_max) {
        ret = WOLFBOOT_GZIP_E_OUTPUT;
    }
    else {
        s->out[s->out_pos] = b;
        s->out_pos++;
        s->crc32 = gz_crc32_byte(s->crc32, b);
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* Canonical Huffman build / decode                                          */
/* ------------------------------------------------------------------------- */

/* Build canonical Huffman decode tables from per-symbol code lengths.
 * lengths[i] is the bit length of symbol i (0 = absent).
 * Returns 0 on success, WOLFBOOT_GZIP_E_HUFFMAN on malformed (over-subscribed)
 * trees. Empty alphabets and single-symbol trees are accepted (a common
 * DEFLATE idiom for distance trees with one or zero codes). */
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

    /* Empty alphabet (all symbols absent) is permitted. */
    all_zero = (ret == 0) && (h->counts[0] == n);

    /* Kraft inequality: sum 2^(MAX-len) * counts[len] should be <= 2^MAX.
     * Detect over-subscribed (left < 0). Under-subscribed trees (left > 0)
     * with one or zero codes are accepted. */
    if ((ret == 0) && !all_zero) {
        left = 1;
        for (len = 1; (len <= GZIP_MAX_HUFF_BITS) && (ret == 0); len++) {
            left <<= 1;
            left -= (int)h->counts[len];
            if (left < 0) {
                ret = WOLFBOOT_GZIP_E_HUFFMAN;
            }
        }
    }

    if ((ret == 0) && !all_zero) {
        /* Compute starting offset of each length-bucket in symbols[] */
        offs[1] = 0;
        for (len = 1; len < GZIP_MAX_HUFF_BITS; len++) {
            offs[len + 1] = (int16_t)(offs[len] + h->counts[len]);
        }
        /* Sort symbols by code length, then symbol number (canonical order) */
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

/* Decode one symbol using canonical Huffman tables. Returns the symbol on
 * success (always >= 0), or negative WOLFBOOT_GZIP_E_* on error. */
static int gz_huff_decode(gz_state_t *s, const gz_huff_t *h)
{
    int ret = WOLFBOOT_GZIP_E_HUFFMAN; /* updated to symbol or i/o error */
    int code = 0;
    int first = 0;
    int index = 0;
    int len, count, br_ret;
    uint32_t bit;

    for (len = 1; (len <= GZIP_MAX_HUFF_BITS) &&
                  (ret == WOLFBOOT_GZIP_E_HUFFMAN); len++) {
        br_ret = gz_get_bits(s, 1, &bit);
        if (br_ret != 0) {
            ret = br_ret;
        }
        else {
            code = (code << 1) | (int)bit;
            count = h->counts[len];
            if (code - count < first) {
                ret = h->symbols[index + (code - first)];
            }
            else {
                index += count;
                first = (first + count) << 1;
            }
        }
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* Block decoders                                                            */
/* ------------------------------------------------------------------------- */

/* RFC 1951 Sec. 3.2.4: stored (uncompressed) block */
static int gz_inflate_stored(gz_state_t *s)
{
    int ret = 0;
    uint32_t len = 0, nlen;

    /* Discard remaining bits in current partial byte */
    gz_align_byte(s);

    /* LEN and NLEN are little-endian 16-bit words */
    if (s->in_pos + 4 > s->in_len) {
        ret = WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if (ret == 0) {
        len  = (uint32_t)s->in[s->in_pos] |
               ((uint32_t)s->in[s->in_pos + 1] << 8);
        nlen = (uint32_t)s->in[s->in_pos + 2] |
               ((uint32_t)s->in[s->in_pos + 3] << 8);
        s->in_pos += 4;

        if ((len ^ 0xFFFFU) != nlen) {
            ret = WOLFBOOT_GZIP_E_FORMAT;
        }
        else if (s->in_pos + len > s->in_len) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
        }
        else {
            /* No buffered bits remain after align_byte; clear defensively */
            s->bit_buf = 0;
            s->bit_count = 0;
        }
    }

    while ((ret == 0) && (len > 0)) {
        ret = gz_emit_byte(s, s->in[s->in_pos]);
        if (ret == 0) {
            s->in_pos++;
            len--;
        }
    }
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

/* Inflate the body of a Huffman-coded block (fixed or dynamic) until the
 * end-of-block symbol (256) is decoded. */
static int gz_inflate_huffman(gz_state_t *s,
                              const gz_huff_t *litlen, const gz_huff_t *dist)
{
    int ret = 0;
    int done = 0;
    int sym, li;
    uint32_t length, distance, extra, copy_pos;

    while ((ret == 0) && !done) {
        sym = gz_huff_decode(s, litlen);
        if (sym < 0) {
            ret = sym;
        }
        else if (sym < GZIP_EOB_SYMBOL) {
            ret = gz_emit_byte(s, (uint8_t)sym);
        }
        else if (sym == GZIP_EOB_SYMBOL) {
            done = 1;
        }
        else {
            /* length code 257..285 -> length 3..258 */
            li = sym - GZIP_LENGTH_CODE_BASE;
            if (li >= GZIP_LENGTH_CODE_COUNT) {
                ret = WOLFBOOT_GZIP_E_HUFFMAN;
            }
            length = 0;
            if (ret == 0) {
                length = gz_len_base[li];
                if (gz_len_extra[li] > 0) {
                    ret = gz_get_bits(s, gz_len_extra[li], &extra);
                    if (ret == 0) {
                        length += extra;
                    }
                }
            }

            distance = 0;
            if (ret == 0) {
                sym = gz_huff_decode(s, dist);
                if (sym < 0) {
                    ret = sym;
                }
                else if (sym >= GZIP_DIST_CODE_COUNT) {
                    ret = WOLFBOOT_GZIP_E_HUFFMAN;
                }
                else {
                    distance = gz_dist_base[sym];
                    if (gz_dist_extra[sym] > 0) {
                        ret = gz_get_bits(s, gz_dist_extra[sym], &extra);
                        if (ret == 0) {
                            distance += extra;
                        }
                    }
                }
            }

            if (ret == 0) {
                if ((distance == 0) || (distance > s->out_pos)) {
                    ret = WOLFBOOT_GZIP_E_DISTANCE;
                }
                else if (s->out_pos + length > s->out_max) {
                    ret = WOLFBOOT_GZIP_E_OUTPUT;
                }
            }

            /* LZ77 copy. Output buffer doubles as the window. Copy must be
             * byte-by-byte to support overlapping runs (length > distance). */
            if (ret == 0) {
                copy_pos = s->out_pos - distance;
                while ((ret == 0) && (length > 0)) {
                    ret = gz_emit_byte(s, s->out[copy_pos]);
                    if (ret == 0) {
                        copy_pos++;
                        length--;
                    }
                }
            }
        }
    }
    return ret;
}

/* RFC 1951 Sec. 3.2.7: dynamic Huffman block.
 * Decodes the code-length code, expands it into the literal/length and
 * distance trees, then runs gz_inflate_huffman() on the block body. */
static int gz_inflate_dynamic(gz_state_t *s)
{
    int ret;
    uint8_t cl_lens[GZIP_CL_CODES];
    uint8_t code_lens[GZIP_LITLEN_CODES + GZIP_DIST_CODES];
    gz_huff_t cl_huff;
    gz_huff_t litlen_huff;
    gz_huff_t dist_huff;
    uint32_t hlit = 0, hdist = 0, hclen = 0, val;
    int i, total, idx, sym;
    uint8_t prev = 0;

    ret = gz_get_bits(s, GZIP_HLIT_BITS, &hlit);
    if (ret == 0) {
        hlit += GZIP_HLIT_BASE;
        ret = gz_get_bits(s, GZIP_HDIST_BITS, &hdist);
    }
    if (ret == 0) {
        hdist += GZIP_HDIST_BASE;
        ret = gz_get_bits(s, GZIP_HCLEN_BITS, &hclen);
    }
    if (ret == 0) {
        hclen += GZIP_HCLEN_BASE;
        if ((hlit > GZIP_LITLEN_CODES) || (hdist > GZIP_DIST_CODES) ||
            (hclen > GZIP_CL_CODES)) {
            ret = WOLFBOOT_GZIP_E_FORMAT;
        }
    }

    /* Read code-length code lengths in the permuted order */
    if (ret == 0) {
        for (i = 0; i < GZIP_CL_CODES; i++) {
            cl_lens[i] = 0;
        }
        for (i = 0; (i < (int)hclen) && (ret == 0); i++) {
            ret = gz_get_bits(s, GZIP_CL_LEN_BITS, &val);
            if (ret == 0) {
                cl_lens[gz_cl_order[i]] = (uint8_t)val;
            }
        }
    }
    if (ret == 0) {
        ret = gz_huff_build(&cl_huff, cl_lens, GZIP_CL_CODES);
    }

    /* Decode the litlen + dist code-length sequence using the CL tree */
    if (ret == 0) {
        total = (int)hlit + (int)hdist;
        idx = 0;
        while ((ret == 0) && (idx < total)) {
            sym = gz_huff_decode(s, &cl_huff);
            if (sym < 0) {
                ret = sym;
            }
            else if (sym < 16) {
                code_lens[idx++] = (uint8_t)sym;
                prev = (uint8_t)sym;
            }
            else if (sym == 16) {
                /* repeat previous length 3..6 times (2 extra bits) */
                if (idx == 0) {
                    ret = WOLFBOOT_GZIP_E_FORMAT;
                }
                else {
                    ret = gz_get_bits(s, GZIP_REPEAT_PREV_EXTRA, &val);
                    if (ret == 0) {
                        val += GZIP_REPEAT_PREV_BASE;
                        if (idx + (int)val > total) {
                            ret = WOLFBOOT_GZIP_E_FORMAT;
                        }
                        else {
                            while (val--) code_lens[idx++] = prev;
                        }
                    }
                }
            }
            else if (sym == 17) {
                /* repeat zero 3..10 times (3 extra bits) */
                ret = gz_get_bits(s, GZIP_REPEAT_Z3_EXTRA, &val);
                if (ret == 0) {
                    val += GZIP_REPEAT_Z3_BASE;
                    if (idx + (int)val > total) {
                        ret = WOLFBOOT_GZIP_E_FORMAT;
                    }
                    else {
                        while (val--) code_lens[idx++] = 0;
                        prev = 0;
                    }
                }
            }
            else if (sym == 18) {
                /* repeat zero 11..138 times (7 extra bits) */
                ret = gz_get_bits(s, GZIP_REPEAT_Z7_EXTRA, &val);
                if (ret == 0) {
                    val += GZIP_REPEAT_Z7_BASE;
                    if (idx + (int)val > total) {
                        ret = WOLFBOOT_GZIP_E_FORMAT;
                    }
                    else {
                        while (val--) code_lens[idx++] = 0;
                        prev = 0;
                    }
                }
            }
            else {
                ret = WOLFBOOT_GZIP_E_FORMAT;
            }
        }
    }

    /* End-of-block symbol (256) must have a code */
    if ((ret == 0) && (code_lens[GZIP_EOB_SYMBOL] == 0)) {
        ret = WOLFBOOT_GZIP_E_HUFFMAN;
    }

    if (ret == 0) {
        ret = gz_huff_build(&litlen_huff, code_lens, (int)hlit);
    }
    if (ret == 0) {
        ret = gz_huff_build(&dist_huff, code_lens + hlit, (int)hdist);
    }
    if (ret == 0) {
        ret = gz_inflate_huffman(s, &litlen_huff, &dist_huff);
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* DEFLATE driver                                                            */
/* ------------------------------------------------------------------------- */

static int gz_inflate(gz_state_t *s)
{
    int ret = 0;
    uint32_t bfinal = 0, btype = 0;
    gz_huff_t fixed_litlen;
    gz_huff_t fixed_dist;
    int fixed_built = 0;

    while ((ret == 0) && !bfinal) {
        ret = gz_get_bits(s, 1, &bfinal);
        if (ret == 0) {
            ret = gz_get_bits(s, 2, &btype);
        }
        if (ret == 0) {
            if (btype == 0) {
                ret = gz_inflate_stored(s);
            }
            else if (btype == 1) {
                if (!fixed_built) {
                    ret = gz_build_fixed(&fixed_litlen, &fixed_dist);
                    if (ret == 0) {
                        fixed_built = 1;
                    }
                }
                if (ret == 0) {
                    ret = gz_inflate_huffman(s, &fixed_litlen, &fixed_dist);
                }
            }
            else if (btype == 2) {
                ret = gz_inflate_dynamic(s);
            }
            else {
                ret = WOLFBOOT_GZIP_E_FORMAT;
            }
        }
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* RFC 1952 wrapper                                                          */
/* ------------------------------------------------------------------------- */

static int gz_skip_zstring(gz_state_t *s)
{
    int ret = WOLFBOOT_GZIP_E_TRUNCATED;
    int done = 0;

    while ((ret != 0) && !done) {
        if (s->in_pos >= s->in_len) {
            done = 1; /* ret stays at TRUNCATED */
        }
        else if (s->in[s->in_pos++] == 0) {
            ret = 0;
            done = 1;
        }
    }
    return ret;
}

static int gz_parse_header(gz_state_t *s)
{
    int ret = 0;
    uint8_t flg = 0;
    uint32_t xlen;

    if (s->in_len < GZIP_HEADER_MIN_SIZE) {
        ret = WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if (ret == 0) {
        /* Magic 1F 8B, CM = 8 (DEFLATE) */
        if ((s->in[0] != GZIP_MAGIC_ID1) || (s->in[1] != GZIP_MAGIC_ID2) ||
            (s->in[2] != GZIP_CM_DEFLATE)) {
            ret = WOLFBOOT_GZIP_E_FORMAT;
        }
        else {
            flg = s->in[3];
            if (flg & GZIP_FLG_RESERVED) {
                ret = WOLFBOOT_GZIP_E_FORMAT;
            }
            else {
                /* Skip MTIME(4) + XFL(1) + OS(1) */
                s->in_pos = GZIP_HEADER_MIN_SIZE;
            }
        }
    }

    if ((ret == 0) && (flg & GZIP_FLG_FEXTRA)) {
        if (s->in_pos + 2 > s->in_len) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
        }
        else {
            xlen = (uint32_t)s->in[s->in_pos] |
                   ((uint32_t)s->in[s->in_pos + 1] << 8);
            s->in_pos += 2;
            if (s->in_pos + xlen > s->in_len) {
                ret = WOLFBOOT_GZIP_E_TRUNCATED;
            }
            else {
                s->in_pos += xlen;
            }
        }
    }
    if ((ret == 0) && (flg & GZIP_FLG_FNAME)) {
        ret = gz_skip_zstring(s);
    }
    if ((ret == 0) && (flg & GZIP_FLG_FCOMMENT)) {
        ret = gz_skip_zstring(s);
    }
    if ((ret == 0) && (flg & GZIP_FLG_FHCRC)) {
        if (s->in_pos + 2 > s->in_len) {
            ret = WOLFBOOT_GZIP_E_TRUNCATED;
        }
        else {
            s->in_pos += 2; /* header CRC; not validated */
        }
    }
    return ret;
}

static int gz_parse_trailer(gz_state_t *s, uint32_t computed_crc,
                            uint32_t bytes_out)
{
    int ret = 0;
    uint32_t got_crc, got_isize;

    /* Discard partial byte from final block, then read 8-byte trailer */
    gz_align_byte(s);
    s->bit_buf = 0;
    s->bit_count = 0;

    if (s->in_pos + GZIP_TRAILER_SIZE > s->in_len) {
        ret = WOLFBOOT_GZIP_E_TRUNCATED;
    }
    if (ret == 0) {
        got_crc = (uint32_t)s->in[s->in_pos] |
                  ((uint32_t)s->in[s->in_pos + 1] << 8) |
                  ((uint32_t)s->in[s->in_pos + 2] << 16) |
                  ((uint32_t)s->in[s->in_pos + 3] << 24);
        got_isize = (uint32_t)s->in[s->in_pos + 4] |
                    ((uint32_t)s->in[s->in_pos + 5] << 8) |
                    ((uint32_t)s->in[s->in_pos + 6] << 16) |
                    ((uint32_t)s->in[s->in_pos + 7] << 24);
        s->in_pos += GZIP_TRAILER_SIZE;

        if (got_crc != computed_crc) {
            ret = WOLFBOOT_GZIP_E_CRC32;
        }
        else if (got_isize != bytes_out) {
            ret = WOLFBOOT_GZIP_E_ISIZE;
        }
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/* Public entry point                                                        */
/* ------------------------------------------------------------------------- */

int wolfBoot_gunzip(const uint8_t *in, uint32_t in_len,
                    uint8_t *out, uint32_t out_max,
                    uint32_t *out_len)
{
    int ret = 0;
    gz_state_t s;

    if ((in == NULL) || (out == NULL) || (out_len == NULL)) {
        ret = WOLFBOOT_GZIP_E_PARAM;
    }
    else {
        s.in = in;
        s.in_len = in_len;
        s.in_pos = 0;
        s.bit_buf = 0;
        s.bit_count = 0;
        s.out = out;
        s.out_max = out_max;
        s.out_pos = 0;
        s.crc32 = GZIP_CRC32_INIT;

        ret = gz_parse_header(&s);
        if (ret == 0) {
            ret = gz_inflate(&s);
        }
        if (ret == 0) {
            /* Final CRC32 is the running register XOR'd with the final mask */
            s.crc32 ^= GZIP_CRC32_FINAL_XOR;
            ret = gz_parse_trailer(&s, s.crc32, s.out_pos);
        }
        *out_len = s.out_pos;
    }
    return ret;
}

#endif /* WOLFBOOT_GZIP */
