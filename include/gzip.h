/* gzip.h
 *
 * Native gzip decompression for wolfBoot FIT subimages.
 *
 * Clean-room implementation of RFC 1951 (DEFLATE) and RFC 1952 (gzip).
 *
 * Compile with GZIP=1.
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
#ifndef WOLFBOOT_GZIP_H
#define WOLFBOOT_GZIP_H

#include <stdint.h>

/* error codes */
#define WOLFBOOT_GZIP_E_FORMAT     -1  /* bad magic / method / reserved bits */
#define WOLFBOOT_GZIP_E_TRUNCATED  -2  /* input ended mid-stream */
#define WOLFBOOT_GZIP_E_OUTPUT     -3  /* output would exceed out_max */
#define WOLFBOOT_GZIP_E_HUFFMAN    -4  /* invalid Huffman tree / code */
#define WOLFBOOT_GZIP_E_DISTANCE   -5  /* back-ref distance > bytes written */
#define WOLFBOOT_GZIP_E_CRC32      -6  /* trailer CRC32 mismatch */
#define WOLFBOOT_GZIP_E_ISIZE      -7  /* trailer ISIZE mismatch */
#define WOLFBOOT_GZIP_E_PARAM      -8  /* invalid parameter */

/* RFC 1952 gzip wrapper constants */
#define GZIP_MAGIC_ID1            0x1FU      /* first magic byte  */
#define GZIP_MAGIC_ID2            0x8BU      /* second magic byte */
#define GZIP_CM_DEFLATE           8          /* CM = DEFLATE      */
#define GZIP_HEADER_MIN_SIZE      10         /* magic+CM+FLG+MTIME+XFL+OS */
#define GZIP_TRAILER_SIZE         8          /* CRC32 + ISIZE     */
#define GZIP_CRC32_INIT           0xFFFFFFFFU
#define GZIP_CRC32_FINAL_XOR      0xFFFFFFFFU
#define GZIP_CRC32_POLY           0xEDB88320U /* IEEE 802.3 reflected */

/* RFC 1952 Sec. 2.3.1 header flag bits (FLG byte) */
#define GZIP_FLG_FTEXT            0x01
#define GZIP_FLG_FHCRC            0x02
#define GZIP_FLG_FEXTRA           0x04
#define GZIP_FLG_FNAME            0x08
#define GZIP_FLG_FCOMMENT         0x10
#define GZIP_FLG_RESERVED         0xE0

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

/* RFC 1951 DEFLATE - run-length repeat symbols (Sec. 3.2.7).
 *   sym 16: 2 extra bits, repeat previous length 3..6 times
 *   sym 17: 3 extra bits, repeat zero        3..10 times
 *   sym 18: 7 extra bits, repeat zero       11..138 times
 */
#define GZIP_REPEAT_PREV_EXTRA    2
#define GZIP_REPEAT_PREV_BASE     3
#define GZIP_REPEAT_Z3_EXTRA      3
#define GZIP_REPEAT_Z3_BASE       3
#define GZIP_REPEAT_Z7_EXTRA      7
#define GZIP_REPEAT_Z7_BASE       11

/* Decompress a gzip stream.
 *
 * in       - pointer to gzip stream (RFC 1952 wrapper around RFC 1951 DEFLATE)
 * in_len   - length of gzip stream in bytes
 * out      - destination buffer; also used as the DEFLATE sliding window,
 *            so the output region must be readable as well as writable.
 * out_max  - maximum bytes that may be written to out
 * out_len  - on success, set to the number of bytes written
 *
 * Returns 0 on success, negative WOLFBOOT_GZIP_E_* on error.
 */
int wolfBoot_gunzip(const uint8_t *in, uint32_t in_len,
                    uint8_t *out, uint32_t out_max,
                    uint32_t *out_len);

#endif /* WOLFBOOT_GZIP_H */
