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

/* RFC 1952 gzip wrapper constants (format-defining; useful to callers
 * that pre-validate the gzip magic/method before invoking the inflater).
 * All other DEFLATE-internal constants live in src/gzip.c. */
#define GZIP_MAGIC_ID1            0x1FU      /* first magic byte  */
#define GZIP_MAGIC_ID2            0x8BU      /* second magic byte */
#define GZIP_CM_DEFLATE           8          /* CM = DEFLATE      */
#define GZIP_HEADER_MIN_SIZE      10         /* magic+CM+FLG+MTIME+XFL+OS */
#define GZIP_TRAILER_SIZE         8          /* CRC32 + ISIZE     */

/* RFC 1952 Sec. 2.3.1 header flag bits (FLG byte) */
#define GZIP_FLG_FTEXT            0x01
#define GZIP_FLG_FHCRC            0x02
#define GZIP_FLG_FEXTRA           0x04
#define GZIP_FLG_FNAME            0x08
#define GZIP_FLG_FCOMMENT         0x10
#define GZIP_FLG_RESERVED         0xE0

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

#ifdef WOLFBOOT_GZIP_SINK

/* Window the stream was compressed with, as a power of two. RFC 1951 caps
 * it at 32KB; below 4KB the ratio costs more than the RAM it saves. */
#define GZIP_SINK_WINDOW_MIN_BITS 12
#define GZIP_SINK_WINDOW_MAX_BITS 15

/* Scratch wolfBoot_gunzip_sink() needs for a given window: twice the window,
 * so a back-reference is always resident in a flat buffer. */
#define GZIP_SINK_BUF_SIZE(bits)  (2U << (bits))

/* Called with each run of decompressed bytes, in order and exactly once.
 * `off` is the offset within the decompressed stream. Returns 0 on success;
 * anything else aborts the inflate. */
typedef int (*wolfBoot_gzip_write_cb)(void *ctx, uint32_t off,
                                      const uint8_t *buf, uint32_t len);

/* Called to fetch a run of the compressed stream, which the decoder reads
 * in order but a piece at a time. `off` is the offset within the stream.
 * Returns 0 on success; anything else aborts the inflate. Lets the stream
 * live somewhere that cannot be addressed directly, or be transformed on
 * the way in. */
typedef int (*wolfBoot_gzip_read_cb)(void *ctx, uint32_t off,
                                     uint8_t *buf, uint32_t len);

/* Decompress a gzip stream to a sink rather than to a buffer, so the output
 * need not be addressable and only GZIP_SINK_BUF_SIZE(window_bits) of RAM is
 * used regardless of how large the stream decompresses to.
 *
 * The stream is read in place from `in`, or pulled in through `read_cb` when
 * it is not addressable; pass exactly one of the two, and with `read_cb`,
 * `in_len` is the length of the whole compressed stream.
 *
 * The stream must have been compressed with a window no larger than
 * 1 << window_bits; a back-reference beyond it is rejected with
 * WOLFBOOT_GZIP_E_DISTANCE rather than silently accepted.
 *
 * Returns 0 on success, negative WOLFBOOT_GZIP_E_* on error.
 */
int wolfBoot_gunzip_sink(const uint8_t *in, uint32_t in_len,
                         wolfBoot_gzip_read_cb read_cb, void *read_ctx,
                         uint8_t *scratch, uint32_t window_bits,
                         wolfBoot_gzip_write_cb write_cb, void *ctx,
                         uint32_t out_max, uint32_t *out_len);

#endif /* WOLFBOOT_GZIP_SINK */

#endif /* WOLFBOOT_GZIP_H */
