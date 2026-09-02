/* fdt.c
 *
 * Flattened device tree (DTB) parser, written from the Devicetree
 * Specification v0.4 section 5.
 *
 * fdt_open() validates a blob once, in full: header layout inside the
 * caller's capacity, then a walk of the whole structure block proving
 * balanced nesting, one root, in-bounds NUL-terminated names, in-bounds
 * property lengths and a terminating FDT_END. Everything after relies on
 * those invariants rather than re-deriving them from attacker-controlled
 * header fields on every access.
 *
 * Only the mutators can disturb the invariants, so each updates the
 * context and the on-disk header in the same step that moves bytes, and
 * bounds every move against ctx->capacity - never against a size read
 * back out of the blob.
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

#if (defined(MMU) || defined(WOLFBOOT_FDT)) && !defined(BUILD_LOADER_STAGE1)

#include "fdt.h"
#include "hal.h"
#include "printf.h"
#include "string.h"
#include <stdint.h>

#ifdef WOLFBOOT_GZIP
#include "gzip.h"
#endif

/* Coarse upper bound on a single FIT subimage's decompressed size.
 * This is a sanity ceiling, not a per-destination memory-safety
 * bound. Authenticity of the FIT bytes is provided by the outer
 * wolfBoot signature; this cap is a belt-and-suspenders limit so a
 * malformed-but-signed gzip stream cannot inflate without bound.
 * Callers that need a tighter, RAM-window-aware bound should use
 * fit_load_image_ex() (FIT-`load` destination + explicit out_max)
 * or fit_load_image_to() (caller-supplied destination + dst_max).
 * Override per target via:
 *   CFLAGS+=-DWOLFBOOT_FIT_MAX_DECOMP=...
 */
#ifndef WOLFBOOT_FIT_MAX_DECOMP
#define WOLFBOOT_FIT_MAX_DECOMP (256U * 1024U * 1024U)
#endif

/* Start of wolfBoot's own image (linker script _start_text). Weak so hosted
 * builds (sim, unit tests) without the symbol resolve it to NULL and skip the
 * bound derived from it. */
extern char _start_text[] __attribute__((weak));
#define wolfboot_start_text ((void*)_start_text)

/* ------------------------------------------------------------------ */
/* Byte order                                                          */
/* ------------------------------------------------------------------ */

uint32_t cpu_to_fdt32(uint32_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint32_t)__builtin_bswap32(x);
#endif
}
uint64_t cpu_to_fdt64(uint64_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint64_t)__builtin_bswap64(x);
#endif
}
uint32_t fdt32_to_cpu(uint32_t x)
{
    return cpu_to_fdt32(x);
}
uint64_t fdt64_to_cpu(uint64_t x)
{
    return cpu_to_fdt64(x);
}

/* ------------------------------------------------------------------ */
/* Raw accessors                                                       */
/* ------------------------------------------------------------------ */

/* fdt_open() requires a 4-byte aligned base, so aligned loads are safe.
 * Left as plain `static`: some targets build at -O0 (OPTIMIZATION_LEVEL
 * in arch.mk) where inlining these at ~40 sites grows the image. */
static uint32_t fdt_rd32(const void* p)
{
    return fdt32_to_cpu(*(const uint32_t*)p);
}
static void fdt_wr32(void* p, uint32_t v)
{
    *(uint32_t*)p = cpu_to_fdt32(v);
}

/* Reservation entries are 64-bit but the blob base is only guaranteed
 * 4-byte aligned, so they are handled as two 32-bit halves. */
static uint64_t fdt_rd64u(const uint8_t* p)
{
    return ((uint64_t)fdt_rd32(p) << 32) | (uint64_t)fdt_rd32(p + 4);
}
static void fdt_wr64u(uint8_t* p, uint64_t v)
{
    fdt_wr32(p, (uint32_t)(v >> 32));
    fdt_wr32(p + 4, (uint32_t)v);
}

static uint32_t fdt_hdr_get(const uint8_t* b, uint32_t field)
{
    return fdt_rd32(b + field);
}
static void fdt_hdr_put(uint8_t* b, uint32_t field, uint32_t v)
{
    fdt_wr32(b + field, v);
}

/* Total bytes the tree currently occupies (the strings block is last). */
static uint32_t fdt_data_end(const fdt_ctx* ctx)
{
    return ctx->off_strings + ctx->size_strings;
}

/* Write the context's layout back to the header. totalsize never drops
 * below the content; only fdt_shrink() pulls it down. Not inlined: every
 * mutator ends with this and duplicating it costs more than the call. */
static void __attribute__((noinline)) fdt_hdr_sync(fdt_ctx* ctx)
{
    uint32_t end = fdt_data_end(ctx);

    if (end > ctx->totalsize) {
        ctx->totalsize = end;
    }
    fdt_hdr_put(ctx->blob, FDT_H_TOTALSIZE, ctx->totalsize);
    fdt_hdr_put(ctx->blob, FDT_H_OFF_STRUCT, ctx->off_struct);
    fdt_hdr_put(ctx->blob, FDT_H_SIZE_STRUCT, ctx->size_struct);
    fdt_hdr_put(ctx->blob, FDT_H_OFF_STRINGS, ctx->off_strings);
    fdt_hdr_put(ctx->blob, FDT_H_SIZE_STRINGS, ctx->size_strings);
}

static int fdt_ctx_ok(const fdt_ctx* ctx)
{
    return (ctx != NULL && ctx->blob != NULL);
}

/* ------------------------------------------------------------------ */
/* Token stream                                                        */
/* ------------------------------------------------------------------ */

/* Read the token at `off` (structure-block relative), report where the
 * next starts. Safe on an unvalidated blob (every read bounded by
 * size_struct) because fdt_open()'s validation pass uses it too. */
static int fdt_tag_walk(const fdt_ctx* ctx, int off, uint32_t* tagp,
    int* nextp)
{
    const uint8_t* sb = ctx->blob + ctx->off_struct;
    uint32_t cur = (uint32_t)off;
    uint32_t avail, tag, adv;
    const uint8_t* nul;

    /* size_struct >= FDT_TAGSIZE (fdt_open), so this cannot wrap.
     * Tracking bytes-remaining keeps every bound a 32-bit compare; as
     * additions they would need 64-bit maths to stay wrap-safe. */
    if (off < 0 || (cur & (FDT_TAGSIZE - 1U)) != 0
            || cur > ctx->size_struct - FDT_TAGSIZE) {
        return -FDT_ERR_BADOFFSET;
    }
    tag = fdt_rd32(sb + cur);
    cur += FDT_TAGSIZE;
    avail = ctx->size_struct - cur;

    switch (tag) {
    case FDT_BEGIN_NODE:
        /* the name must terminate before the block does */
        nul = (const uint8_t*)memchr(sb + cur, '\0', (size_t)avail);
        if (nul == NULL) {
            return -FDT_ERR_BADSTRUCTURE;
        }
        adv = FDT_TAGALIGN((uint32_t)(nul - (sb + cur)) + 1U);
        break;
    case FDT_PROP:
        if (avail < 2U * FDT_TAGSIZE) {
            return -FDT_ERR_BADSTRUCTURE;
        }
        adv = fdt_rd32(sb + cur);
        avail -= 2U * FDT_TAGSIZE;
        cur += 2U * FDT_TAGSIZE;
        /* Reject an oversized length BEFORE aligning it: FDT_TAGALIGN
         * wraps to a small value for a length near UINT32_MAX, which
         * would then pass the bound below. */
        if (adv > avail) {
            return -FDT_ERR_BADSTRUCTURE;
        }
        adv = FDT_TAGALIGN(adv);
        break;
    case FDT_END_NODE:
    case FDT_NOP:
    case FDT_END:
        adv = 0;
        break;
    default:
        return -FDT_ERR_BADSTRUCTURE;
    }
    /* aligning up can push past the end by as much as 3 bytes */
    if (adv > avail) {
        return -FDT_ERR_BADSTRUCTURE;
    }
    *tagp = tag;
    *nextp = (int)(cur + adv);
    return 0;
}

/* Offset of the first token inside a node, i.e. just past its opening
 * token and name. */
static int fdt_node_body(const fdt_ctx* ctx, int nodeoff)
{
    uint32_t tag;
    int next, rc;

    rc = fdt_tag_walk(ctx, nodeoff, &tag, &next);
    if (rc != 0) {
        return rc;
    }
    if (tag != FDT_BEGIN_NODE) {
        return -FDT_ERR_BADOFFSET;
    }
    return next;
}

/* Offset just past a node's closing token, i.e. the end of its subtree. */
static int fdt_node_end(const fdt_ctx* ctx, int nodeoff)
{
    uint32_t tag;
    int cur = nodeoff;
    int next, rc, depth = 0;

    for (;;) {
        rc = fdt_tag_walk(ctx, cur, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag == FDT_BEGIN_NODE) {
            depth++;
        }
        else if (tag == FDT_END_NODE) {
            depth--;
            if (depth == 0) {
                return next;
            }
            if (depth < 0) {
                return -FDT_ERR_BADSTRUCTURE;
            }
        }
        else if (tag == FDT_END) {
            return -FDT_ERR_BADSTRUCTURE;
        }
        cur = next;
    }
}

/* From `off`, skip NOP tokens and return the offset of the property
 * token that follows, or -FDT_ERR_NOTFOUND once the node's property
 * list has ended. */
static int fdt_prop_scan(const fdt_ctx* ctx, int off)
{
    uint32_t tag;
    int next, rc;

    for (;;) {
        rc = fdt_tag_walk(ctx, off, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag == FDT_PROP) {
            return off;
        }
        if (tag != FDT_NOP) {
            return -FDT_ERR_NOTFOUND;
        }
        off = next;
    }
}

/* ------------------------------------------------------------------ */
/* Strings block                                                       */
/* ------------------------------------------------------------------ */

static const char* fdt_strtab_at(const fdt_ctx* ctx, uint32_t stroff)
{
    if (stroff >= ctx->size_strings) {
        return NULL;
    }
    return (const char*)(ctx->blob + ctx->off_strings + stroff);
}

/* Find `s` in the strings block. Any position matching `s` plus its
 * terminator works, so a name that is a tail of an entry shares it. */
static int fdt_strtab_find(const fdt_ctx* ctx, const char* s, uint32_t* stroff)
{
    const char* tab = (const char*)(ctx->blob + ctx->off_strings);
    uint32_t need = (uint32_t)strlen(s) + 1U;
    uint32_t i;

    if (need > ctx->size_strings) {
        return -FDT_ERR_NOTFOUND;
    }
    for (i = 0; i <= ctx->size_strings - need; i++) {
        if (memcmp(tab + i, s, need) == 0) {
            *stroff = i;
            return 0;
        }
    }
    return -FDT_ERR_NOTFOUND;
}

/* ------------------------------------------------------------------ */
/* Open / validate                                                     */
/* ------------------------------------------------------------------ */

/* One pass over the structure block. fdt_tag_walk() proved each token's
 * own bounds, so this adds only tree shape: balanced nesting, one root,
 * properties inside a node, names in range, terminating FDT_END. */
static int fdt_validate_struct(const fdt_ctx* ctx)
{
    const uint8_t* sb = ctx->blob + ctx->off_struct;
    uint32_t tag, nameoff;
    int cur = 0;
    int next, rc, depth = 0, roots = 0;

    for (;;) {
        rc = fdt_tag_walk(ctx, cur, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        switch (tag) {
        case FDT_BEGIN_NODE:
            if (depth == 0 && ++roots > 1) {
                return -FDT_ERR_BADSTRUCTURE;
            }
            if (++depth > FDT_MAX_DEPTH) {
                return -FDT_ERR_BADSTRUCTURE;
            }
            break;

        case FDT_PROP:
            if (depth == 0) {
                return -FDT_ERR_BADSTRUCTURE; /* property outside any node */
            }
            /* In-range is enough: fdt_open() proved the block ends in a
             * NUL, so every in-range offset terminates. */
            nameoff = fdt_rd32(sb + (uint32_t)cur + 2U * FDT_TAGSIZE);
            if (nameoff >= ctx->size_strings) {
                return -FDT_ERR_BADSTRUCTURE;
            }
            break;

        case FDT_END_NODE:
            if (depth == 0) {
                return -FDT_ERR_BADSTRUCTURE;
            }
            depth--;
            break;

        case FDT_END:
            if (depth != 0 || roots != 1) {
                return -FDT_ERR_BADSTRUCTURE;
            }
            return 0;

        default: /* FDT_NOP */
            break;
        }
        cur = next;
    }
}

/* Header checks shared by fdt_open() and fdt_peek_size(). `avail` is
 * what the caller can read now (a peek may hold only the header);
 * `maxtotal` bounds the size the blob may declare. */
static int fdt_hdr_check(const uint8_t* b, uint32_t avail, uint32_t maxtotal,
    uint32_t* totalp)
{
    uint32_t total;

    /* 32-bit loads are used throughout the header and structure block,
     * so the base has to be aligned for them. */
    if (b == NULL
            || ((uintptr_t)b & (uintptr_t)(FDT_TAGSIZE - 1U)) != 0
            || avail < (uint32_t)FDT_HEADER_SIZE) {
        return -FDT_ERR_BADARG;
    }
    if (fdt_hdr_get(b, FDT_H_MAGIC) != (uint32_t)FDT_MAGIC) {
        return -FDT_ERR_BADMAGIC;
    }
    /* A blob may declare a newer version so long as it stays readable as
     * v17; anything that predates v17 is rejected outright. */
    if (fdt_hdr_get(b, FDT_H_VERSION) < (uint32_t)FDT_SUPPORTED_VERSION
            || fdt_hdr_get(b, FDT_H_LAST_COMP)
                > (uint32_t)FDT_SUPPORTED_VERSION) {
        return -FDT_ERR_BADVERSION;
    }
    total = fdt_hdr_get(b, FDT_H_TOTALSIZE);
    if (total < (uint32_t)FDT_HEADER_SIZE || total > maxtotal) {
        return -FDT_ERR_BADLAYOUT;
    }
    *totalp = total;
    return 0;
}

int fdt_open(fdt_ctx* ctx, void* blob, uint32_t capacity)
{
    uint8_t* b = (uint8_t*)blob;
    uint32_t total, off_rsv, off_struct, size_struct, off_strings;
    uint32_t size_strings, i;
    int rc;

    if (ctx == NULL) {
        return -FDT_ERR_BADARG;
    }
    /* Closed until proven good. Every entry point gates on ctx->blob, so
     * the other fields need no scrubbing while it is NULL. */
    ctx->blob = NULL;

    /* The bound that makes everything below safe: the blob does not get
     * to declare a size larger than the buffer it actually lives in. */
    rc = fdt_hdr_check(b, capacity, capacity, &total);
    if (rc != 0) {
        return rc;
    }

    off_rsv = fdt_hdr_get(b, FDT_H_OFF_RSVMAP);
    off_struct = fdt_hdr_get(b, FDT_H_OFF_STRUCT);
    size_struct = fdt_hdr_get(b, FDT_H_SIZE_STRUCT);
    off_strings = fdt_hdr_get(b, FDT_H_OFF_STRINGS);
    size_strings = fdt_hdr_get(b, FDT_H_SIZE_STRINGS);

    /* Blocks aligned, in order, non-overlapping, inside totalsize. Each
     * 32-bit compare is kept wrap-safe by the one before it: an offset is
     * proved in range before anything is subtracted from it. */
    if (off_rsv < (uint32_t)FDT_HEADER_SIZE
            || (off_rsv & 7U) != 0
            || (off_struct & (FDT_TAGSIZE - 1U)) != 0
            || size_struct < FDT_TAGSIZE
            || off_strings > total
            || off_struct > off_strings
            || off_struct < off_rsv
            || off_struct - off_rsv < (uint32_t)FDT_RSV_ENTRY_SIZE
            || off_strings - off_struct < size_struct
            || total - off_strings < size_strings) {
        return -FDT_ERR_BADLAYOUT;
    }
    /* A terminated block lets string offsets be handed out without a
     * scan. An empty one is legal (no properties, no names) and needs no
     * terminator - every offset into it is then out of range. */
    if (size_strings > 0 && b[off_strings + size_strings - 1U] != '\0') {
        return -FDT_ERR_BADSTRUCTURE;
    }

    /* The reservation block runs to an all-zero (0, 0) entry, which must
     * appear before the structure block starts. */
    for (i = off_rsv;; i += (uint32_t)FDT_RSV_ENTRY_SIZE) {
        uint32_t j;

        if (i > off_struct - (uint32_t)FDT_RSV_ENTRY_SIZE) {
            return -FDT_ERR_BADLAYOUT;
        }
        for (j = 0; j < (uint32_t)FDT_RSV_ENTRY_SIZE; j++) {
            if (b[i + j] != 0) {
                break;
            }
        }
        if (j == (uint32_t)FDT_RSV_ENTRY_SIZE) {
            break;
        }
    }

    ctx->blob = b;
    ctx->capacity = capacity;
    ctx->totalsize = total;
    ctx->off_rsv = off_rsv;
    ctx->off_struct = off_struct;
    ctx->size_struct = size_struct;
    ctx->off_strings = off_strings;
    ctx->size_strings = size_strings;

    rc = fdt_validate_struct(ctx);
    if (rc != 0) {
        ctx->blob = NULL;
        return rc;
    }
    return 0;
}

int fdt_peek_size(const void* hdr, uint32_t hdr_len, uint32_t* totalsize)
{
    if (totalsize == NULL) {
        return -FDT_ERR_BADARG;
    }
    /* Only the header is in hand, so the declared size is bounded by the
     * generic staging maximum rather than by anything measured. */
    return fdt_hdr_check((const uint8_t*)hdr, hdr_len, WOLFBOOT_DTS_MAX_SIZE,
        totalsize);
}

uint32_t fdt_size(const fdt_ctx* ctx)
{
    if (!fdt_ctx_ok(ctx)) {
        return 0;
    }
    return ctx->totalsize;
}

int fdt_grow(fdt_ctx* ctx, uint32_t extra)
{
    uint32_t end;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    end = fdt_data_end(ctx);
    if (extra > ctx->capacity - end) {
        return -FDT_ERR_NOSPACE; /* callers report this */
    }
    ctx->totalsize = end + extra;
    fdt_hdr_sync(ctx);
    return 0;
}

int fdt_shrink(fdt_ctx* ctx)
{
    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    ctx->totalsize = fdt_data_end(ctx);
    fdt_hdr_sync(ctx);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

int fdt_next_node(const fdt_ctx* ctx, int offset, int* depth)
{
    uint32_t tag;
    int cur, next, rc;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    if (offset < 0) {
        next = 0;
    }
    else {
        rc = fdt_tag_walk(ctx, offset, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag != FDT_BEGIN_NODE) {
            return -FDT_ERR_BADOFFSET;
        }
    }
    for (;;) {
        cur = next;
        rc = fdt_tag_walk(ctx, cur, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag == FDT_BEGIN_NODE) {
            if (depth != NULL) {
                (*depth)++;
            }
            return cur;
        }
        if (tag == FDT_END_NODE) {
            if (depth != NULL) {
                (*depth)--;
                if (*depth < 0) {
                    /* walked back out of the subtree we started in */
                    return -FDT_ERR_NOTFOUND;
                }
            }
        }
        else if (tag == FDT_END) {
            return -FDT_ERR_NOTFOUND;
        }
    }
}

int fdt_first_property_offset(const fdt_ctx* ctx, int nodeoffset)
{
    int body;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    body = fdt_node_body(ctx, nodeoffset);
    if (body < 0) {
        return body;
    }
    return fdt_prop_scan(ctx, body);
}

int fdt_next_property_offset(const fdt_ctx* ctx, int propoffset)
{
    uint32_t tag;
    int next, rc;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    rc = fdt_tag_walk(ctx, propoffset, &tag, &next);
    if (rc != 0) {
        return rc;
    }
    if (tag != FDT_PROP) {
        return -FDT_ERR_BADOFFSET;
    }
    return fdt_prop_scan(ctx, next);
}

const void* fdt_getprop_by_offset(const fdt_ctx* ctx, int propoffset,
    const char** namep, int* lenp)
{
    const uint8_t* rec;
    uint32_t tag;
    int next, rc;

    if (!fdt_ctx_ok(ctx)) {
        rc = -FDT_ERR_BADARG;
    }
    else {
        rc = fdt_tag_walk(ctx, propoffset, &tag, &next);
        if (rc == 0 && tag != FDT_PROP) {
            rc = -FDT_ERR_BADOFFSET;
        }
    }
    if (rc != 0) {
        if (lenp != NULL) {
            *lenp = rc;
        }
        return NULL;
    }

    rec = ctx->blob + ctx->off_struct + (uint32_t)propoffset;
    if (lenp != NULL) {
        *lenp = (int)fdt_rd32(rec + FDT_TAGSIZE);
    }
    if (namep != NULL) {
        *namep = fdt_strtab_at(ctx, fdt_rd32(rec + 2U * FDT_TAGSIZE));
    }
    return rec + 3U * FDT_TAGSIZE;
}

const char* fdt_get_name(const fdt_ctx* ctx, int nodeoffset, int* len)
{
    const char* name;
    uint32_t tag;
    int next, rc;

    if (!fdt_ctx_ok(ctx)) {
        rc = -FDT_ERR_BADARG;
    }
    else {
        rc = fdt_tag_walk(ctx, nodeoffset, &tag, &next);
        if (rc == 0 && tag != FDT_BEGIN_NODE) {
            rc = -FDT_ERR_BADOFFSET;
        }
    }
    if (rc != 0) {
        if (len != NULL) {
            *len = rc;
        }
        return NULL;
    }
    name = (const char*)(ctx->blob + ctx->off_struct + (uint32_t)nodeoffset
        + FDT_TAGSIZE);
    if (len != NULL) {
        *len = (int)strlen(name);
    }
    return name;
}

const char* fdt_get_string(const fdt_ctx* ctx, int stroffset, int* lenp)
{
    const char* s;

    if (!fdt_ctx_ok(ctx)) {
        if (lenp != NULL) {
            *lenp = -FDT_ERR_BADARG;
        }
        return NULL;
    }
    if (stroffset < 0) {
        if (lenp != NULL) {
            *lenp = -FDT_ERR_BADOFFSET;
        }
        return NULL;
    }
    s = fdt_strtab_at(ctx, (uint32_t)stroffset);
    if (s == NULL) {
        if (lenp != NULL) {
            *lenp = -FDT_ERR_BADOFFSET;
        }
        return NULL;
    }
    /* the block ends in a NUL, so this cannot run past it */
    if (lenp != NULL) {
        *lenp = (int)strlen(s);
    }
    return s;
}

/* Offset of a named property within a node, or a negative FDT_ERR_*. */
static int fdt_prop_find(const fdt_ctx* ctx, int nodeoffset, const char* name,
    int* lenp)
{
    uint32_t namelen = (uint32_t)strlen(name);
    uint32_t tag;
    int cur, next, rc;

    /* Walks tokens directly: the public property iterators are used only
     * by the host tools, so keeping them off this path lets the linker
     * drop them from firmware. */
    cur = fdt_node_body(ctx, nodeoffset);
    if (cur < 0) {
        return cur;
    }
    for (;;) {
        rc = fdt_tag_walk(ctx, cur, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag == FDT_PROP) {
            const uint8_t* rec = ctx->blob + ctx->off_struct + (uint32_t)cur;
            const char* pname = fdt_strtab_at(ctx,
                fdt_rd32(rec + 2U * FDT_TAGSIZE));

            if (pname != NULL && strlen(pname) == namelen
                    && memcmp(pname, name, (size_t)namelen) == 0) {
                if (lenp != NULL) {
                    *lenp = (int)fdt_rd32(rec + FDT_TAGSIZE);
                }
                return cur;
            }
        }
        else if (tag != FDT_NOP) {
            return -FDT_ERR_NOTFOUND; /* end of this node's properties */
        }
        cur = next;
    }
}

const void* fdt_getprop(const fdt_ctx* ctx, int nodeoffset, const char* name,
    int* lenp)
{
    int off, len = 0;

    if (!fdt_ctx_ok(ctx) || name == NULL) {
        if (lenp != NULL) {
            *lenp = -FDT_ERR_BADARG;
        }
        return NULL;
    }
    off = fdt_prop_find(ctx, nodeoffset, name, &len);
    if (off < 0) {
        if (lenp != NULL) {
            *lenp = off;
        }
        return NULL;
    }
    if (lenp != NULL) {
        *lenp = len;
    }
    return ctx->blob + ctx->off_struct + (uint32_t)off + 3U * FDT_TAGSIZE;
}

void* fdt_getprop_address(const fdt_ctx* ctx, int nodeoffset, const char* name)
{
    const uint8_t* val;
    int len = 0;

    val = (const uint8_t*)fdt_getprop(ctx, nodeoffset, name, &len);
    if (val == NULL) {
        return NULL;
    }
    if (len == 8) {
        return (void*)(uintptr_t)fdt_rd64u(val);
    }
    if (len == 4) {
        return (void*)(uintptr_t)fdt_rd32(val);
    }
    return NULL;
}

/* Does the node at `off` carry this name? A search term with no unit
 * address ("serial") also matches a node that has one ("serial@21c0500"),
 * which is how devicetree names are conventionally written. */
static int fdt_name_eq(const fdt_ctx* ctx, int off, const char* name,
    uint32_t namelen)
{
    const char* p = (const char*)(ctx->blob + ctx->off_struct + (uint32_t)off
        + FDT_TAGSIZE);
    /* Length first keeps the memcmp inside the name: `namelen` is the
     * caller's and can exceed anything in the tree (a FIT `default`
     * string, say). The name itself is NUL-terminated per fdt_open(). */
    uint32_t plen = (uint32_t)strlen(p);

    if (plen < namelen || memcmp(p, name, (size_t)namelen) != 0) {
        return 0;
    }
    if (p[namelen] == '\0') {
        return 1;
    }
    if (p[namelen] == '@' && memchr(name, '@', (size_t)namelen) == NULL) {
        return 1;
    }
    return 0;
}

/* Direct child of `parentoff` by name, via fdt_next_node()'s depth
 * tracking: from the parent at depth 0 the direct children are the nodes
 * at depth 1, and its closing token ends the walk. */
static int fdt_subnode_find(const fdt_ctx* ctx, int parentoff,
    const char* name, uint32_t namelen)
{
    int off, depth = 0;

    for (off = fdt_next_node(ctx, parentoff, &depth);
         off >= 0;
         off = fdt_next_node(ctx, off, &depth))
    {
        if (depth == 1 && fdt_name_eq(ctx, off, name, namelen)) {
            return off;
        }
    }
    return -FDT_ERR_NOTFOUND;
}

int fdt_subnode_offset(const fdt_ctx* ctx, int parentoff, const char* name)
{
    if (!fdt_ctx_ok(ctx) || name == NULL) {
        return -FDT_ERR_BADARG;
    }
    return fdt_subnode_find(ctx, parentoff, name, (uint32_t)strlen(name));
}

int fdt_path_offset(const fdt_ctx* ctx, const char* path)
{
    const char* p;
    int off = 0; /* the root node is always at offset 0 */

    if (!fdt_ctx_ok(ctx) || path == NULL || *path != '/') {
        return -FDT_ERR_BADARG;
    }
    p = path + 1;
    while (*p != '\0') {
        const char* seg = p;
        uint32_t seglen;

        while (*p != '\0' && *p != '/') {
            p++;
        }
        seglen = (uint32_t)(p - seg);
        if (seglen > 0) {
            off = fdt_subnode_find(ctx, off, seg, seglen);
            if (off < 0) {
                return off;
            }
        }
        while (*p == '/') {
            p++;
        }
    }
    return off;
}

/* Shared walk for the tree-wide searches. `propname` NULL matches the
 * node name; otherwise the named property must equal `needle` whole. The
 * compatible search keeps its own loop so a target that never does one
 * does not link the string-list matcher.
 *
 * Always returns exactly -FDT_ERR_NOTFOUND on no match: HAL callers test
 * for that value and would read any other error as a usable offset. */
static int fdt_seek(const fdt_ctx* ctx, int startoff, const char* propname,
    const char* needle)
{
    const void* val;
    uint32_t nlen;
    int off, len;

    if (!fdt_ctx_ok(ctx) || needle == NULL) {
        return -FDT_ERR_BADARG;
    }
    nlen = (uint32_t)strlen(needle);

    for (off = fdt_next_node(ctx, startoff, NULL);
         off >= 0;
         off = fdt_next_node(ctx, off, NULL))
    {
        if (propname == NULL) {
            /* off came from fdt_next_node(), so it is a checked
             * BEGIN_NODE and its name is NUL-terminated in bounds.
             * Comparing the length first keeps the memcmp inside it. */
            const char* nm = (const char*)(ctx->blob + ctx->off_struct
                + (uint32_t)off + FDT_TAGSIZE);
            if (strlen(nm) == nlen && memcmp(nm, needle, (size_t)nlen) == 0) {
                return off;
            }
            continue;
        }
        val = fdt_getprop(ctx, off, propname, &len);
        if (val == NULL || len <= 0) {
            continue;
        }
        if ((uint32_t)len == nlen + 1U
                && memcmp(val, needle, (size_t)(nlen + 1U)) == 0) {
            return off;
        }
    }
    return -FDT_ERR_NOTFOUND;
}

int fdt_find_node_offset(const fdt_ctx* ctx, int startoff, const char* nodename)
{
    return fdt_seek(ctx, startoff, NULL, nodename);
}

int fdt_find_prop_offset(const fdt_ctx* ctx, int startoff, const char* propname,
    const char* propval)
{
    if (propname == NULL) {
        return -FDT_ERR_BADARG;
    }
    return fdt_seek(ctx, startoff, propname, propval);
}

int fdt_find_devtype(const fdt_ctx* ctx, int startoff, const char* devtype)
{
    return fdt_seek(ctx, startoff, "device_type", devtype);
}

/* Does a NUL-separated string list of `len` bytes contain `str` as a
 * whole entry? An unterminated trailing entry is ignored. Internal so it
 * folds into its one caller and drops out of images without one. */
static int fdt_stringlist_contains(const void* strlist, int len, const char* str)
{
    const char* p = (const char*)strlist;
    uint32_t want;

    if (p == NULL || str == NULL || len <= 0) {
        return 0;
    }
    want = (uint32_t)strlen(str);
    while (len > 0) {
        const char* nul = (const char*)memchr(p, '\0', (size_t)len);
        uint32_t entrylen;

        if (nul == NULL) {
            /* trailing bytes with no terminator: not a usable entry */
            break;
        }
        entrylen = (uint32_t)(nul - p);
        if (entrylen == want && memcmp(p, str, (size_t)want) == 0) {
            return 1;
        }
        len -= (int)entrylen + 1;
        p = nul + 1;
    }
    return 0;
}

int fdt_node_offset_by_compatible(const fdt_ctx* ctx, int startoff,
    const char* compatible)
{
    const void* val;
    int off, len;

    if (!fdt_ctx_ok(ctx) || compatible == NULL) {
        return -FDT_ERR_BADARG;
    }
    for (off = fdt_next_node(ctx, startoff, NULL);
         off >= 0;
         off = fdt_next_node(ctx, off, NULL))
    {
        val = fdt_getprop(ctx, off, "compatible", &len);
        if (val != NULL && fdt_stringlist_contains(val, len, compatible)) {
            return off;
        }
    }
    return -FDT_ERR_NOTFOUND; /* see fdt_find_node_offset() */
}



/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

/* Replace `oldlen` bytes at absolute blob offset `at` with `newlen`
 * bytes, shifting everything up to the end of the tree. Bounded by the
 * caller-supplied capacity, never by a value read out of the blob. */
static int fdt_block_splice(fdt_ctx* ctx, uint32_t at, uint32_t oldlen,
    uint32_t newlen)
{
    uint32_t end = fdt_data_end(ctx);
    uint32_t tail;

    if (at > end || oldlen > end - at) {
        return -FDT_ERR_BADOFFSET;
    }
    /* end <= totalsize <= capacity holds for an open context, so the
     * subtraction cannot wrap. */
    if (newlen > oldlen && newlen - oldlen > ctx->capacity - end) {
        return -FDT_ERR_NOSPACE;
    }
    tail = end - at - oldlen;
    if (tail > 0) {
        memmove(ctx->blob + at + newlen, ctx->blob + at + oldlen,
            (size_t)tail);
    }
    return 0;
}

int fdt_setprop(fdt_ctx* ctx, int nodeoffset, const char* name,
    const void* val, int len)
{
    uint32_t padlen, oldpad, stroff = 0, namelen, reclen, at, need;
    int poff, oldlen = 0, body, rc, interned;
    uint8_t* rec;

    if (!fdt_ctx_ok(ctx) || name == NULL) {
        return -FDT_ERR_BADARG;
    }
    if (len < 0 || (val == NULL && len != 0)) {
        return -FDT_ERR_BADARG;
    }
    if ((uint32_t)len > ctx->capacity) {
        return -FDT_ERR_NOSPACE;
    }
    padlen = FDT_TAGALIGN((uint32_t)len);

    poff = fdt_prop_find(ctx, nodeoffset, name, &oldlen);
    if (poff >= 0) {
        /* resize in place */
        oldpad = FDT_TAGALIGN((uint32_t)oldlen);
        at = ctx->off_struct + (uint32_t)poff + 3U * FDT_TAGSIZE;
        rc = fdt_block_splice(ctx, at, oldpad, padlen);
        if (rc != 0) {
            wolfBoot_printf("FDT: set prop %s failed! %d\n", name, rc);
            return rc;
        }
        ctx->size_struct = ctx->size_struct - oldpad + padlen;
        ctx->off_strings = ctx->off_strings - oldpad + padlen;
    }
    else {
        if (poff != -FDT_ERR_NOTFOUND) {
            return poff;
        }
        /* Work out where the record goes before touching anything, so a
         * bad node offset cannot leave a half-applied change behind. */
        body = fdt_node_body(ctx, nodeoffset);
        if (body < 0) {
            return body;
        }
        namelen = (uint32_t)strlen(name) + 1U;
        interned = (fdt_strtab_find(ctx, name, &stroff) == 0);
        reclen = 3U * FDT_TAGSIZE + padlen;
        need = reclen + (interned ? 0U : namelen);
        if (need > ctx->capacity - fdt_data_end(ctx)) {
            wolfBoot_printf("FDT: no space for prop %s\n", name);
            return -FDT_ERR_NOSPACE;
        }
        /* Space is now guaranteed for both steps, so neither can fail
         * partway and leave the tree inconsistent. */
        if (!interned) {
            stroff = ctx->size_strings;
            memcpy(ctx->blob + ctx->off_strings + ctx->size_strings, name,
                (size_t)namelen);
            ctx->size_strings += namelen;
        }
        at = ctx->off_struct + (uint32_t)body;
        rc = fdt_block_splice(ctx, at, 0, reclen);
        if (rc != 0) {
            return -FDT_ERR_INTERNAL; /* pre-checked; cannot happen */
        }
        ctx->size_struct += reclen;
        ctx->off_strings += reclen;

        rec = ctx->blob + at;
        fdt_wr32(rec, FDT_PROP);
        fdt_wr32(rec + 2U * FDT_TAGSIZE, stroff);
        poff = body;
    }

    rec = ctx->blob + ctx->off_struct + (uint32_t)poff;
    fdt_wr32(rec + FDT_TAGSIZE, (uint32_t)len);
    if (len > 0) {
        memcpy(rec + 3U * FDT_TAGSIZE, val, (size_t)len);
    }
    if (padlen > (uint32_t)len) {
        memset(rec + 3U * FDT_TAGSIZE + (uint32_t)len, 0,
            (size_t)(padlen - (uint32_t)len));
    }
    fdt_hdr_sync(ctx);
    return 0;
}

int fdt_add_subnode(fdt_ctx* ctx, int parentoff, const char* name)
{
    uint32_t namelen, nodelen, at;
    uint32_t tag;
    int cur, next, rc;
    uint8_t* rec;

    if (!fdt_ctx_ok(ctx) || name == NULL) {
        return -FDT_ERR_BADARG;
    }
    namelen = (uint32_t)strlen(name);
    rc = fdt_subnode_find(ctx, parentoff, name, namelen);
    if (rc >= 0) {
        return -FDT_ERR_EXISTS;
    }
    if (rc != -FDT_ERR_NOTFOUND) {
        return rc;
    }

    /* new children go after the parent's own properties */
    cur = fdt_node_body(ctx, parentoff);
    if (cur < 0) {
        return cur;
    }
    for (;;) {
        rc = fdt_tag_walk(ctx, cur, &tag, &next);
        if (rc != 0) {
            return rc;
        }
        if (tag != FDT_PROP && tag != FDT_NOP) {
            break;
        }
        cur = next;
    }

    nodelen = 2U * FDT_TAGSIZE + FDT_TAGALIGN(namelen + 1U);
    at = ctx->off_struct + (uint32_t)cur;
    rc = fdt_block_splice(ctx, at, 0, nodelen);
    if (rc != 0) {
        wolfBoot_printf("FDT: add subnode %s failed! %d\n", name, rc);
        return rc;
    }
    ctx->size_struct += nodelen;
    ctx->off_strings += nodelen;

    rec = ctx->blob + at;
    fdt_wr32(rec, FDT_BEGIN_NODE);
    memset(rec + FDT_TAGSIZE, 0, (size_t)FDT_TAGALIGN(namelen + 1U));
    memcpy(rec + FDT_TAGSIZE, name, (size_t)namelen);
    fdt_wr32(rec + nodelen - FDT_TAGSIZE, FDT_END_NODE);

    fdt_hdr_sync(ctx);
    return cur;
}

int fdt_del_node(fdt_ctx* ctx, int nodeoffset)
{
    uint32_t span;
    int end, rc;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    end = fdt_node_end(ctx, nodeoffset);
    if (end < 0) {
        return end;
    }
    span = (uint32_t)end - (uint32_t)nodeoffset;
    rc = fdt_block_splice(ctx, ctx->off_struct + (uint32_t)nodeoffset, span, 0);
    if (rc != 0) {
        return rc;
    }
    ctx->size_struct -= span;
    ctx->off_strings -= span;
    fdt_hdr_sync(ctx);
    return 0;
}

int fdt_add_mem_rsv(fdt_ctx* ctx, uint64_t address, uint64_t size)
{
    uint32_t shift = (uint32_t)FDT_RSV_ENTRY_SIZE;
    uint32_t end, slot;
    uint8_t* base;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    base = ctx->blob;
    end = fdt_data_end(ctx);
    if (shift > ctx->capacity - end) {
        return -FDT_ERR_NOSPACE;
    }

    /* Find the (0, 0) terminator; it must lie wholly inside the
     * reservation block. Room for the new one comes from the shift. */
    for (slot = ctx->off_rsv;; slot += shift) {
        uint32_t j;

        if (slot > ctx->off_struct - shift) {
            return -FDT_ERR_BADSTRUCTURE;
        }
        for (j = 0; j < shift; j++) {
            if (base[slot + j] != 0) {
                break;
            }
        }
        if (j == shift) {
            break;
        }
    }

    /* push the structure and strings blocks down to make room */
    memmove(base + ctx->off_struct + shift, base + ctx->off_struct,
        (size_t)(end - ctx->off_struct));
    ctx->off_struct += shift;
    ctx->off_strings += shift;

    fdt_wr64u(base + slot, address);
    fdt_wr64u(base + slot + 8U, size);
    fdt_wr64u(base + slot + shift, 0ULL);
    fdt_wr64u(base + slot + shift + 8U, 0ULL);

    fdt_hdr_put(base, FDT_H_OFF_RSVMAP, ctx->off_rsv);
    fdt_hdr_sync(ctx);

    wolfBoot_printf("FDT: /memreserve/ +0x%llx +0x%llx\n",
        (unsigned long long)address, (unsigned long long)size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fixup helpers                                                       */
/* ------------------------------------------------------------------ */

int fdt_fixup_str(fdt_ctx* ctx, int off, const char* node, const char* name,
    const char* str)
{
    wolfBoot_printf("FDT: Set %s (%d), %s=%s\n", node, off, name, str);
    return fdt_setprop(ctx, off, name, str, (int)strlen(str) + 1);
}

int fdt_fixup_val(fdt_ctx* ctx, int off, const char* node, const char* name,
    uint32_t val)
{
    uint32_t be;

    wolfBoot_printf("FDT: Set %s (%d), %s=%u\n", node, off, name, val);
    be = cpu_to_fdt32(val);
    return fdt_setprop(ctx, off, name, &be, (int)sizeof(be));
}

int fdt_fixup_val64(fdt_ctx* ctx, int off, const char* node, const char* name,
    uint64_t val)
{
    uint64_t be;

    wolfBoot_printf("FDT: Set %s (%d), %s=%llu\n",
        node, off, name, (unsigned long long)val);
    be = cpu_to_fdt64(val);
    return fdt_setprop(ctx, off, name, &be, (int)sizeof(be));
}

int fdt_fixup_initrd(fdt_ctx* ctx, uint64_t start, uint64_t size)
{
    int off, ret;

    if (!fdt_ctx_ok(ctx)) {
        return -FDT_ERR_BADARG;
    }
    off = fdt_subnode_find(ctx, 0, "chosen", sizeof("chosen") - 1);
    if (off == -FDT_ERR_NOTFOUND) {
        off = fdt_add_subnode(ctx, 0, "chosen");
    }
    if (off < 0) {
        return off;
    }
    ret = fdt_fixup_val64(ctx, off, "chosen", "linux,initrd-start", start);
    if (ret < 0) {
        return ret;
    }
    if (start + size < start) {
        /* Wrapped initrd end: linux,initrd-end would precede -start. */
        return -FDT_ERR_BADARG;
    }
    ret = fdt_fixup_val64(ctx, off, "chosen", "linux,initrd-end",
        start + size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Flattened uImage Tree (FIT)                                         */
/* ------------------------------------------------------------------ */

/* Returns the property value only when it is a NUL-terminated C string
 * within its declared length, else NULL: property values are opaque
 * byte arrays and the names taken from them are passed to node lookups
 * and strcmp(), which strlen() them. */
static const char* fit_getprop_string(const fdt_ctx* ctx, int offset,
    const char* name)
{
    int len = 0;
    const char* val = (const char*)fdt_getprop(ctx, offset, name, &len);

    if (val == NULL || len <= 0 || memchr(val, '\0', (size_t)len) == NULL) {
        return NULL;
    }
    return val;
}

/* Resolve a sub-image node by name. A well-formed FIT keeps them under
 * /images, so look there first and do not fall back when that node
 * exists - a tree-wide search by bare name would let a node planted
 * elsewhere stand in for the real sub-image. Only a FIT with no /images
 * node at all falls back to the old behavior. */
static int fit_image_offset(const fdt_ctx* ctx, const char* image)
{
    int images;

    if (image == NULL) {
        return -FDT_ERR_BADARG;
    }
    images = fdt_subnode_find(ctx, 0, "images", sizeof("images") - 1);
    if (images >= 0) {
        return fdt_subnode_find(ctx, images, image, (uint32_t)strlen(image));
    }
    return fdt_find_node_offset(ctx, -1, image);
}

/* Fall back to locating a sub-image by its "type" property. */
static const char* fit_name_by_type(const fdt_ctx* ctx, const char* type)
{
    const char* name;
    int off, len = 0;

    off = fdt_find_prop_offset(ctx, -1, "type", type);
    if (off < 0) {
        return NULL;
    }
    name = fdt_get_name(ctx, off, &len);
    if (name == NULL || len <= 0) {
        return NULL;
    }
    return name;
}

const char* fit_find_images(fdt_ctx* ctx, const char** pkernel,
    const char** pflat_dt, const char** pramdisk, const char** pfpga)
{
    const char *conf = NULL, *kernel = NULL, *flat_dt = NULL;
    const char *ramdisk = NULL, *fpga = NULL;
    int confs, off;

    if (!fdt_ctx_ok(ctx)) {
        return NULL;
    }

    /* Find the configuration to boot (optional). A target may override the
     * FIT's own `default` with a per-board selection (hal_fit_config_name). */
    confs = fdt_subnode_find(ctx, 0, "configurations",
        sizeof("configurations") - 1);
    if (confs >= 0) {
#ifdef WOLFBOOT_FIT_CONFIG_SELECT
        conf = hal_fit_config_name();
        /* If the target selected a config that is not present in this FIT,
         * fall back to the default rather than silently mis-selecting
         * images via the type-based search below. */
        if (conf != NULL
                && fdt_subnode_find(ctx, confs, conf,
                    (uint32_t)strlen(conf)) < 0) {
            wolfBoot_printf("FIT: configuration '%s' not found, "
                "using default\n", conf);
            conf = NULL;
        }
        if (conf != NULL) {
            wolfBoot_printf("FIT: selected configuration '%s'\n", conf);
        }
        if (conf == NULL)
#endif
        {
            conf = fit_getprop_string(ctx, confs, "default");
        }
    }
    if (conf != NULL && confs >= 0) {
        off = fdt_subnode_find(ctx, confs, conf, (uint32_t)strlen(conf));
        if (off >= 0) {
            kernel = fit_getprop_string(ctx, off, "kernel");
            flat_dt = fit_getprop_string(ctx, off, "fdt");
            ramdisk = fit_getprop_string(ctx, off, "ramdisk");
            fpga = fit_getprop_string(ctx, off, "fpga");
        }
    }
    if (kernel == NULL) {
        kernel = fit_name_by_type(ctx, "kernel");
    }
    if (flat_dt == NULL) {
        flat_dt = fit_name_by_type(ctx, "flat_dt");
    }
    if (ramdisk == NULL) {
        ramdisk = fit_name_by_type(ctx, "ramdisk");
    }
    if (fpga == NULL) {
        fpga = fit_name_by_type(ctx, "fpga");
    }

    if (pkernel)
        *pkernel = kernel;
    if (pflat_dt)
        *pflat_dt = flat_dt;
    if (pramdisk)
        *pramdisk = ramdisk;
    if (pfpga)
        *pfpga = fpga;

    return conf;
}

const char* fit_get_compatible(fdt_ctx* ctx, const char* image)
{
    const char* val;
    int off, len = 0;

    if (!fdt_ctx_ok(ctx) || image == NULL) {
        return NULL;
    }
    off = fit_image_offset(ctx, image);
    if (off < 0) {
        return NULL;
    }
    val = (const char*)fdt_getprop(ctx, off, "compatible", &len);
    if (val != NULL && len > 0) {
        return val;
    }
    return NULL;
}

#ifdef WOLFBOOT_FIT_RAMDISK
/* Defensive fallback: targets without a fixed relocation address
 * leave WOLFBOOT_LOAD_RAMDISK_ADDRESS at 0, in which case the
 * ramdisk is used in place. */
#ifndef WOLFBOOT_LOAD_RAMDISK_ADDRESS
#define WOLFBOOT_LOAD_RAMDISK_ADDRESS 0
#endif

/* Upper bound on the (decompressed) ramdisk size. Defaults to the
 * generic FIT decompression cap. Targets with a tighter known-safe
 * RAM window for the ramdisk should override this. */
#ifndef WOLFBOOT_FIT_MAX_RAMDISK
#define WOLFBOOT_FIT_MAX_RAMDISK WOLFBOOT_FIT_MAX_DECOMP
#endif

/* Load a FIT ramdisk subimage and patch the DTB's /chosen
 * linux,initrd-{start,end} to point at it. If
 * WOLFBOOT_LOAD_RAMDISK_ADDRESS is nonzero, the ramdisk is loaded
 * directly to that fixed address - bypassing the FIT's `load`
 * property entirely - so a gzip-compressed ramdisk is decompressed
 * straight into the override (with the override capacity acting as
 * the safety bound). Otherwise the address fit_load_image returned
 * (FIT-specified or in-FIT pointer) is used as-is. Caller passes the
 * DTB context for the initrd fixup, or NULL to skip the fixup.
 *
 * Returns 0 on success, -1 if the ramdisk node was found but the
 * load failed. The current callers ignore the return value
 * (log-and-continue), so a missing/failed ramdisk does not abort
 * the boot. */
int fit_load_ramdisk(fdt_ctx* ctx, const char* ramdisk_node, fdt_ctx* dts)
{
    int rd_size = 0;
    uint8_t* rd_ptr;
    uint8_t* rd_dst;

    if (!fdt_ctx_ok(ctx) || ramdisk_node == NULL) {
        return -1;
    }

    if (WOLFBOOT_LOAD_RAMDISK_ADDRESS != 0) {
        rd_dst = (uint8_t*)WOLFBOOT_LOAD_RAMDISK_ADDRESS;
        rd_ptr = (uint8_t*)fit_load_image_to(ctx, ramdisk_node,
            rd_dst, (uint32_t)WOLFBOOT_FIT_MAX_RAMDISK, &rd_size);
        if (rd_ptr == NULL || rd_size <= 0) {
            wolfBoot_printf("FIT: ramdisk node present but load failed\n");
            return -1;
        }
        wolfBoot_printf("Loaded ramdisk: %p (%d bytes)\n", rd_dst, rd_size);
    }
    else {
        rd_ptr = (uint8_t*)fit_load_image(ctx, ramdisk_node, &rd_size);
        if (rd_ptr == NULL || rd_size <= 0) {
            wolfBoot_printf("FIT: ramdisk node present but load failed\n");
            return -1;
        }
        rd_dst = rd_ptr;
        wolfBoot_printf("Loaded ramdisk: %p (%d bytes)\n", rd_dst, rd_size);
    }

    if (dts != NULL) {
        int frc = fdt_fixup_initrd(dts, (uint64_t)(uintptr_t)rd_dst,
            (uint64_t)rd_size);
        if (frc != 0) {
            wolfBoot_printf("FIT: fdt_fixup_initrd failed (rc=%d); "
                "kernel will not see initrd\n", frc);
            return -1;
        }
    }

    return 0;
}
#endif /* WOLFBOOT_FIT_RAMDISK */

/* Weak copy hook for FIT subimages (kernel/dtb).  Default is a plain memcpy;
 * boards where CPU writes to the load destination do not land (e.g. the
 * PolarFire MPFS250 DDR, where cached writes thrash L2 Scratch) override this
 * with a DMA-based copy (see hal/mpfs250.c).  Returns 0 on success or a
 * negative value if the copy failed, so callers can fail closed rather than
 * run on a partially written destination. */
int __attribute__((weak)) wolfBoot_fit_memcpy(void *dst, const void *src,
    uint32_t len)
{
    memcpy(dst, src, len);
    return 0;
}

/* Inner implementation shared by fit_load_image_ex and fit_load_image_to.
 * When dst_override is non-NULL it replaces the FIT image's `load`
 * property as the destination, so a compressed (gzip) payload is
 * decompressed directly into the caller's buffer rather than being
 * routed through the FIT-declared address. The `entry` property is
 * also ignored when dst_override is in effect, since the caller wants
 * the override address back.
 */
static void* fit_load_image_inner(fdt_ctx* ctx, const char* image, int* lenp,
    uint32_t out_max, void* dst_override)
{
    void *load, *entry, *data = NULL;
    int off, len = 0;
    const char *comp;
    int complen = 0;
#ifdef WOLFBOOT_GZIP
    BENCHMARK_DECLARE();
#endif

    if (!fdt_ctx_ok(ctx)) {
        if (lenp != NULL) {
            *lenp = 0;
        }
        return NULL;
    }

    off = fit_image_offset(ctx, image);
    if (off >= 0) {
        /* get load and entry */
        data = (void*)fdt_getprop(ctx, off, "data", &len);
        load = fdt_getprop_address(ctx, off, "load");
        entry = fdt_getprop_address(ctx, off, "entry");
        if (dst_override != NULL) {
            /* Caller-supplied destination replaces the FIT load
             * property and disables `entry` resolution. */
            load = dst_override;
            entry = NULL;
        }
        if (data != NULL) {
            int is_gzip = 0;
            int is_unknown_comp = 0;
            /* Detect compression unconditionally (independent of whether
             * a valid distinct load destination is available) so we can
             * fail closed when the build lacks support, when the scheme
             * is unknown, or when there is no place to decompress to -
             * instead of silently passing compressed bytes through as
             * raw. */
            comp = (const char*)fdt_getprop(ctx, off, "compression",
                &complen);
            /* Compare within the declared property length: the value
             * must be exactly "gzip" or "none" (NUL-terminated). Any
             * other shape - including an unterminated value - fails
             * closed instead of being strncmp()'d past the property. */
            if (comp != NULL) {
                if (complen == 5 && comp[4] == '\0' &&
                    memcmp(comp, "gzip", 4) == 0) {
                    is_gzip = 1;
                }
                else if (complen == 5 && comp[4] == '\0' &&
                    memcmp(comp, "none", 4) == 0) {
                    /* uncompressed */
                }
                else {
                    is_unknown_comp = 1;
                }
            }
            if (load != NULL && data != load) {
                if (is_gzip) {
#ifdef WOLFBOOT_GZIP
                    uint32_t out_len = 0;
                    uint32_t gz_max = out_max;
                    uintptr_t gap;
                    int rc;
                    /* out_max is only a sanity ceiling. Cap the output below
                     * anything above the destination that must survive the
                     * inflate: the staged compressed input, and wolfBoot's own
                     * image (a corrupted stream can emit garbage at full match
                     * speed, and without this cap the window reaches straight
                     * through the bootloader before the decoder trips on an
                     * invalid code). Conservative lower bounds. */
                    if ((uintptr_t)data > (uintptr_t)load) {
                        gap = (uintptr_t)data - (uintptr_t)load;
                        if (gap < (uintptr_t)gz_max) {
                            gz_max = (uint32_t)gap;
                        }
                    }
                    if ((uintptr_t)wolfboot_start_text > (uintptr_t)load) {
                        gap = (uintptr_t)wolfboot_start_text - (uintptr_t)load;
                        if (gap < (uintptr_t)gz_max) {
                            gz_max = (uint32_t)gap;
                        }
                    }
                    wolfBoot_printf("Decompressing Image %s (gzip): "
                        "%p -> %p (%d bytes)\n", image, data, load, len);
                    BENCHMARK_START();
                    rc = wolfBoot_gunzip((const uint8_t*)data,
                        (uint32_t)len, (uint8_t*)load, gz_max, &out_len);
                    if (rc != 0) {
                        wolfBoot_printf("FIT gunzip failed for %s: rc=%d "
                            "(wrote %u bytes)\n", image, rc, out_len);
                        return NULL;
                    }
                    len = (int)out_len;
                    /* No trailing newline: BENCHMARK_END("") appends
                     * " (<ms> ms)\r\n" under BOOT_BENCHMARK, or just "\r\n"
                     * otherwise. */
                    wolfBoot_printf("Decompressed %s: %u bytes", image,
                        out_len);
                    BENCHMARK_END("");
#else
                    wolfBoot_printf("FIT: subimage '%s' has compression="
                        "\"gzip\" but WOLFBOOT_GZIP is not enabled in "
                        "this build (rebuild with GZIP=1)\n", image);
                    return NULL;
#endif
                }
                else if (is_unknown_comp) {
                    /* Unknown compression scheme; fail closed rather
                     * than silently memcpy compressed bytes as raw. */
                    wolfBoot_printf("FIT: subimage '%s' has unsupported "
                        "compression=\"%s\"\n", image, comp);
                    return NULL;
                }
                else {
                    if (len < 0 || (uint32_t)len > out_max) {
                        wolfBoot_printf("FIT: subimage '%s' size %d exceeds "
                            "staging bound %u\n", image, len, out_max);
                        return NULL;
                    }
                    wolfBoot_printf("Loading Image %s: %p -> %p "
                        "(%d bytes)\n", image, data, load, len);
                    if (wolfBoot_fit_memcpy(load, data, (uint32_t)len) != 0) {
                        wolfBoot_printf("FIT: copy of %s to %p failed\n",
                            image, load);
                        return NULL;
                    }
                }

                /* No per-image hash-1 re-verification here. Per the
                 * FIT spec (and U-Boot's reference implementation), a
                 * hash-N subnode's value is computed over the image
                 * node's `data` property bytes verbatim - which means
                 * the compressed bytes when compression="gzip". The
                 * outer wolfBoot signature
                 * (wolfBoot_verify_authenticity) already authenticates
                 * the entire FIT, including those data bytes, so a
                 * runtime per-image hash check would be redundant.
                 * Inflater bugs on the decompressed payload are
                 * caught by gzip's own CRC32 + ISIZE trailer inside
                 * wolfBoot_gunzip. */

                /* load should always have entry, but if not use load
                 * address */
                data = (entry != NULL) ? entry : load;
            }
            else if (is_gzip || is_unknown_comp) {
                /* Compression declared but no distinct destination to
                 * decompress into. Refuse rather than hand the caller
                 * a pointer to still-compressed bytes. */
                wolfBoot_printf("FIT: subimage '%s' declares "
                    "compression=\"%s\" but has no distinct load "
                    "destination (load=%p, data=%p); refusing to pass "
                    "compressed bytes through as raw\n",
                    image, comp, load, data);
                return NULL;
            }
        }
        wolfBoot_printf("Image %s: %p (%d bytes)\n", image, data, len);
    }
    else {
        wolfBoot_printf("Image %s: Not found!\n", image);
    }
    if (lenp != NULL) {
        *lenp = len;
    }
    return data;
}

void* fit_load_image_ex(fdt_ctx* ctx, const char* image, int* lenp,
    uint32_t out_max)
{
    return fit_load_image_inner(ctx, image, lenp, out_max, NULL);
}

void* fit_load_image(fdt_ctx* ctx, const char* image, int* lenp)
{
    return fit_load_image_ex(ctx, image, lenp, WOLFBOOT_FIT_MAX_DECOMP);
}

void* fit_load_image_to(fdt_ctx* ctx, const char* image, void* dst,
    uint32_t dst_max, int* lenp)
{
    if (dst == NULL) {
        return NULL;
    }
    return fit_load_image_inner(ctx, image, lenp, dst_max, dst);
}

#ifdef WOLFBOOT_FPGA_BITSTREAM
/* Length-bounded substring search (strstr is not provided by wolfBoot's
 * freestanding string.c). Searches the first hlen bytes of haystack for
 * needle. hlen is an explicit length so this works over a DT "compatible"
 * property, which is a list of NUL-separated strings: a needle with no
 * embedded NUL (e.g. "partial") matches within any one entry - which is
 * what is wanted here, since the entry is typically a vendor-prefixed
 * string such as "xlnx,fpga-partial" - and a NUL separator can never be
 * part of the match. Returns 1 if found. */
static int fit_str_contains(const char* haystack, int hlen, const char* needle)
{
    int nlen, i;

    if (haystack == NULL || needle == NULL || hlen <= 0) {
        return 0;
    }
    nlen = (int)strlen(needle);
    if (nlen == 0 || nlen > hlen) {
        return 0;
    }
    for (i = 0; i + nlen <= hlen; i++) {
        if (memcmp(haystack + i, needle, (size_t)nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Upper bound on the (decompressed) FPGA bitstream size. Defaults to the
 * generic FIT decompression cap. The staging region at
 * WOLFBOOT_LOAD_FPGA_ADDRESS must be at least this large. */
#ifndef WOLFBOOT_FIT_MAX_FPGA
#define WOLFBOOT_FIT_MAX_FPGA WOLFBOOT_FIT_MAX_DECOMP
#endif

int fit_load_fpga(fdt_ctx* ctx, const char* fpga_node)
{
    void* data;
    const char* comp;
    uint32_t flags = HAL_FPGA_FULL;
    int len = 0;
    int ret;
    int coff;
    int clen = 0;
    BENCHMARK_DECLARE();

    if (fpga_node == NULL) {
        /* No fpga subimage present - nothing to do. */
        return 0;
    }

    /* Stage the bitstream into DDR. In the common U-Boot convention the
     * fpga sub-image carries no `load` property and is gzip-compressed
     * (the bootloader decompresses it into a scratch buffer before
     * programming the PL), so when WOLFBOOT_LOAD_FPGA_ADDRESS is set we
     * decompress/copy straight to that dedicated staging address. If it
     * is 0 we honor the FIT's own `load` property instead (fit_load_image
     * fails closed for a compressed sub-image that has no destination). */
#if defined(WOLFBOOT_LOAD_FPGA_ADDRESS) && (WOLFBOOT_LOAD_FPGA_ADDRESS != 0)
    data = fit_load_image_to(ctx, fpga_node,
        (void*)(uintptr_t)(WOLFBOOT_LOAD_FPGA_ADDRESS),
        WOLFBOOT_FIT_MAX_FPGA, &len);
#else
    data = fit_load_image(ctx, fpga_node, &len);
#endif
    if (data == NULL || len <= 0) {
        wolfBoot_printf("FIT: failed to load fpga '%s'\n", fpga_node);
        return -1;
    }

    /* Select full vs partial reconfiguration from the U-Boot-style
     * "compatible" string (e.g. "...fpga-partial"). compatible is a DT
     * string list (one or more NUL-separated entries), so scan the whole
     * property rather than only its first string. Default is full. */
    comp = NULL;
    coff = fit_image_offset(ctx, fpga_node);
    if (coff >= 0) {
        comp = (const char*)fdt_getprop(ctx, coff, "compatible", &clen);
    }
    if (fit_str_contains(comp, clen, "partial")) {
        flags = HAL_FPGA_PARTIAL;
    }

    wolfBoot_printf("FIT: programming FPGA '%s' (%d bytes, %s)\n",
        fpga_node, len, (flags == HAL_FPGA_PARTIAL) ? "partial" : "full");

    BENCHMARK_START();
    ret = hal_fpga_load(flags, (uintptr_t)data, (size_t)len);
    if (ret != 0) {
        wolfBoot_printf("FIT: hal_fpga_load failed: %d\n", ret);
#ifdef WOLFBOOT_FPGA_NONFATAL
        wolfBoot_printf("FIT: continuing without FPGA (non-fatal)\n");
        return 0;
#else
        return -1;
#endif
    }
    BENCHMARK_END("FIT: FPGA programmed");
    return 0;
}
#endif /* WOLFBOOT_FPGA_BITSTREAM */


/* ------------------------------------------------------------------ */
/* Malformed-blob corpus (WOLFBOOT_FDT_CORPUS)                         */
/* ------------------------------------------------------------------ */

/* Test-only. Derives deliberately-invalid blobs from a known-good one so
 * the parser can be shown to reject each rather than read out of bounds.
 * Driven by "fdt-parser -f"; never compiled into firmware. Deliberately
 * writes headers with its own byte-wise helpers so a bug in the parser's
 * accessors cannot mask a bug in the parser. */
#ifdef WOLFBOOT_FDT_CORPUS

#include <stdlib.h>

static uint32_t corpus_rd32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void corpus_wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t corpus_hdr_get(const uint8_t* b, uint32_t field)
{
    return corpus_rd32(b + field);
}

static void corpus_hdr_set(uint8_t* b, uint32_t field, uint32_t v)
{
    corpus_wr32(b + field, v);
}

/* Locate the offset (absolute, within the blob) of the first tag of the
 * given kind inside the structure block. Returns 0 if not found. The
 * walk is intentionally naive - it is scanning a known-good blob. */
static uint32_t corpus_find_tag(const uint8_t* b, uint32_t len, uint32_t want,
    int skip)
{
    uint32_t off = corpus_hdr_get(b, FDT_H_OFF_STRUCT);
    uint32_t end = off + corpus_hdr_get(b, FDT_H_SIZE_STRUCT);
    uint32_t tag;

    if (end > len)
        end = len;

    while (off + 4 <= end) {
        tag = corpus_rd32(b + off);
        if (tag == want) {
            if (skip == 0)
                return off;
            skip--;
        }
        switch (tag) {
        case FDT_BEGIN_NODE:
            off += 4;
            while (off < end && b[off] != '\0')
                off++;
            off = (off + 4) & ~3U; /* past the NUL, realigned */
            break;
        case FDT_PROP:
            if (off + 12 > end)
                return 0;
            off += 12 + ((corpus_rd32(b + off + 4) + 3U) & ~3U);
            break;
        case FDT_END:
            return 0;
        default:
            off += 4;
            break;
        }
    }
    return 0;
}

/* ---- mutators ------------------------------------------------------ */
/* Each receives a private copy of the good blob and may shrink *len.
 * The buffer is allocated at exactly the original length, so growing is
 * not permitted. */

static void m_magic_bad(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_MAGIC, 0xDEADBEEFU);
}

static void m_magic_off_by_one(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_MAGIC, (uint32_t)FDT_MAGIC + 1U);
}

static void m_version_zero(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_VERSION, 0);
}

static void m_version_16(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_VERSION, 16);
}

static void m_lastcomp_future(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_LAST_COMP, 0x20);
}

static void m_totalsize_huge(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_TOTALSIZE, 0x10000000U);
}

static void m_totalsize_max(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_TOTALSIZE, 0xFFFFFFFFU);
}

static void m_totalsize_tiny(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_TOTALSIZE, 8);
}

static void m_totalsize_zero(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_TOTALSIZE, 0);
}

static void m_off_struct_past_end(uint8_t* b, uint32_t* len)
{
    corpus_hdr_set(b, FDT_H_OFF_STRUCT, *len + 0x1000U);
}

static void m_off_strings_past_end(uint8_t* b, uint32_t* len)
{
    corpus_hdr_set(b, FDT_H_OFF_STRINGS, *len + 0x1000U);
}

static void m_blocks_swapped(uint8_t* b, uint32_t* len)
{
    uint32_t s = corpus_hdr_get(b, FDT_H_OFF_STRUCT);
    (void)len;
    corpus_hdr_set(b, FDT_H_OFF_STRUCT, corpus_hdr_get(b, FDT_H_OFF_STRINGS));
    corpus_hdr_set(b, FDT_H_OFF_STRINGS, s);
}

static void m_blocks_overlap(uint8_t* b, uint32_t* len)
{
    (void)len;
    /* Pull the string block back into the middle of the struct block. */
    corpus_hdr_set(b, FDT_H_OFF_STRINGS,
        corpus_hdr_get(b, FDT_H_OFF_STRUCT) + (corpus_hdr_get(b, FDT_H_SIZE_STRUCT) / 2U));
}

static void m_size_struct_huge(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_SIZE_STRUCT, 0x0FFFFFFFU);
}

static void m_size_strings_huge(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_SIZE_STRINGS, 0x0FFFFFFFU);
}

static void m_size_struct_wrap(uint8_t* b, uint32_t* len)
{
    (void)len;
    /* off + size wraps 32 bits. */
    corpus_hdr_set(b, FDT_H_SIZE_STRUCT, 0xFFFFFFF0U);
}

static void m_off_rsv_unaligned(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_OFF_RSVMAP, FDT_HEADER_SIZE + 1U);
}

static void m_off_rsv_in_header(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_OFF_RSVMAP, 4);
}

static void m_off_rsv_past_struct(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_OFF_RSVMAP, corpus_hdr_get(b, FDT_H_OFF_STRUCT) + 0x100U);
}

static void m_off_struct_unaligned(uint8_t* b, uint32_t* len)
{
    (void)len;
    corpus_hdr_set(b, FDT_H_OFF_STRUCT, corpus_hdr_get(b, FDT_H_OFF_STRUCT) + 1U);
}

static void m_rsv_no_terminator(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_hdr_get(b, FDT_H_OFF_RSVMAP);
    uint32_t end = corpus_hdr_get(b, FDT_H_OFF_STRUCT);
    if (end > *len)
        end = *len;
    while (off < end)
        b[off++] = 0xAA;
}

static void m_truncated_half(uint8_t* b, uint32_t* len)
{
    (void)b;
    /* Keep the header's totalsize, but hand the parser half the bytes. */
    *len = *len / 2U;
}

static void m_truncated_header(uint8_t* b, uint32_t* len)
{
    (void)b;
    *len = FDT_HEADER_SIZE - 4U;
}

static void m_struct_no_end(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_find_tag(b, *len, FDT_END, 0);
    if (off != 0)
        corpus_wr32(b + off, FDT_NOP);
}

static void m_struct_bad_tag(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_hdr_get(b, FDT_H_OFF_STRUCT);
    if (off + 4 <= *len)
        corpus_wr32(b + off, 0x42424242U);
}

static void m_unbalanced_end_node(uint8_t* b, uint32_t* len)
{
    /* Turn the second node's opening tag into a close, unbalancing the
     * nesting for the rest of the stream. */
    uint32_t off = corpus_find_tag(b, *len, FDT_BEGIN_NODE, 1);
    if (off != 0)
        corpus_wr32(b + off, FDT_END_NODE);
}

static void m_prop_len_max(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_find_tag(b, *len, FDT_PROP, 0);
    if (off != 0)
        corpus_wr32(b + off + 4, 0xFFFFFFFFU);
}

static void m_prop_len_totalsize(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_find_tag(b, *len, FDT_PROP, 0);
    if (off != 0)
        corpus_wr32(b + off + 4, corpus_hdr_get(b, FDT_H_TOTALSIZE));
}

static void m_prop_len_signed(uint8_t* b, uint32_t* len)
{
    /* Lands negative when the parser narrows the length to int. */
    uint32_t off = corpus_find_tag(b, *len, FDT_PROP, 0);
    if (off != 0)
        corpus_wr32(b + off + 4, 0x80000004U);
}

static void m_prop_nameoff_past(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_find_tag(b, *len, FDT_PROP, 0);
    if (off != 0)
        corpus_wr32(b + off + 8, corpus_hdr_get(b, FDT_H_SIZE_STRINGS) + 0x1000U);
}

static void m_prop_nameoff_last(uint8_t* b, uint32_t* len)
{
    /* Exactly one past the end of the string block. */
    uint32_t off = corpus_find_tag(b, *len, FDT_PROP, 0);
    if (off != 0)
        corpus_wr32(b + off + 8, corpus_hdr_get(b, FDT_H_SIZE_STRINGS));
}

static void m_node_name_unterminated(uint8_t* b, uint32_t* len)
{
    /* Fill from the second node's name to the end of the struct block
     * with non-NUL bytes so the name never terminates. */
    uint32_t off = corpus_find_tag(b, *len, FDT_BEGIN_NODE, 1);
    uint32_t end = corpus_hdr_get(b, FDT_H_OFF_STRUCT) + corpus_hdr_get(b, FDT_H_SIZE_STRUCT);
    if (off == 0)
        return;
    if (end > *len)
        end = *len;
    for (off += 4; off < end; off++)
        b[off] = 'A';
}

static void m_strings_unterminated(uint8_t* b, uint32_t* len)
{
    uint32_t off = corpus_hdr_get(b, FDT_H_OFF_STRINGS);
    uint32_t sz = corpus_hdr_get(b, FDT_H_SIZE_STRINGS);
    if (sz == 0 || off + sz > *len)
        return;
    b[off + sz - 1U] = 'A';
}

static void m_prop_before_node(uint8_t* b, uint32_t* len)
{
    /* A property tag at depth 0 - properties must belong to a node. */
    uint32_t off = corpus_hdr_get(b, FDT_H_OFF_STRUCT);
    if (off + 4 <= *len)
        corpus_wr32(b + off, FDT_PROP);
}

/* ---- synthesized blobs --------------------------------------------- */

/* Build a minimal blob whose structure block is supplied by the caller.
 * The string block is a single NUL so any nameoff of 0 resolves. */
static uint8_t* corpus_synth(const uint8_t* structblk, uint32_t structlen,
    uint32_t* outlen)
{
    uint32_t off_rsv = FDT_HEADER_SIZE;
    uint32_t off_struct = off_rsv + 16U; /* one (0,0) terminator entry */
    uint32_t off_strings = off_struct + structlen;
    uint32_t total = off_strings + 1U;
    uint8_t* b;

    total = (total + 3U) & ~3U;
    b = calloc(1, total);
    if (b == NULL)
        return NULL;

    corpus_hdr_set(b, FDT_H_MAGIC, (uint32_t)FDT_MAGIC);
    corpus_hdr_set(b, FDT_H_TOTALSIZE, total);
    corpus_hdr_set(b, FDT_H_OFF_STRUCT, off_struct);
    corpus_hdr_set(b, FDT_H_OFF_STRINGS, off_strings);
    corpus_hdr_set(b, FDT_H_OFF_RSVMAP, off_rsv);
    corpus_hdr_set(b, FDT_H_VERSION, 17);
    corpus_hdr_set(b, FDT_H_LAST_COMP, 16);
    corpus_hdr_set(b, FDT_H_SIZE_STRINGS, 1);
    corpus_hdr_set(b, FDT_H_SIZE_STRUCT, structlen);
    memcpy(b + off_struct, structblk, structlen);
    /* string block is the trailing NUL already zeroed by calloc */

    *outlen = total;
    return b;
}

/* Two sibling root nodes - the spec allows exactly one. */
static uint8_t* s_two_roots(uint32_t* len)
{
    uint8_t sb[7 * 4];
    uint32_t i = 0;
    corpus_wr32(sb + i, FDT_BEGIN_NODE); i += 4;
    corpus_wr32(sb + i, 0);            i += 4; /* empty name */
    corpus_wr32(sb + i, FDT_END_NODE);   i += 4;
    corpus_wr32(sb + i, FDT_BEGIN_NODE); i += 4;
    corpus_wr32(sb + i, 0);            i += 4; /* second root */
    corpus_wr32(sb + i, FDT_END_NODE);   i += 4;
    corpus_wr32(sb + i, FDT_END);        i += 4;
    return corpus_synth(sb, i, len);
}

/* Deeply nested nodes - unbounded recursion or stack growth in a
 * consumer would show up here. */
static uint8_t* s_deep_nesting(uint32_t* len)
{
    const uint32_t depth = 10000;
    uint32_t sz = (depth * 3U + 1U) * 4U;
    uint8_t* sb = calloc(1, sz);
    uint8_t* out;
    uint32_t i = 0, d;

    if (sb == NULL)
        return NULL;
    for (d = 0; d < depth; d++) {
        corpus_wr32(sb + i, FDT_BEGIN_NODE); i += 4;
        corpus_wr32(sb + i, 0);            i += 4;
    }
    for (d = 0; d < depth; d++) {
        corpus_wr32(sb + i, FDT_END_NODE);   i += 4;
    }
    corpus_wr32(sb + i, FDT_END); i += 4;

    out = corpus_synth(sb, i, len);
    free(sb);
    return out;
}

/* Structure block that is nothing but an END tag - no root node. */
static uint8_t* s_no_root(uint32_t* len)
{
    uint8_t sb[4];
    corpus_wr32(sb, FDT_END);
    return corpus_synth(sb, sizeof(sb), len);
}

/* Root node opened and never closed. */
static uint8_t* s_unclosed_root(uint32_t* len)
{
    uint8_t sb[3 * 4];
    uint32_t i = 0;
    corpus_wr32(sb + i, FDT_BEGIN_NODE); i += 4;
    corpus_wr32(sb + i, 0);            i += 4;
    corpus_wr32(sb + i, FDT_END);        i += 4;
    return corpus_synth(sb, i, len);
}

/* Empty structure block. */
static uint8_t* s_empty_struct(uint32_t* len)
{
    static const uint8_t none[1] = { 0 };
    return corpus_synth(none, 0, len);
}

/* ---- case table ---------------------------------------------------- */

typedef void (*mutate_fn)(uint8_t* b, uint32_t* len);
typedef uint8_t* (*synth_fn)(uint32_t* len);

struct corpus_case {
    const char* name;
    const char* what;
    mutate_fn   mutate;
    synth_fn    build;
};

static const struct corpus_case CASES[] = {
    { "magic_bad",            "magic replaced with 0xDEADBEEF",     m_magic_bad, NULL },
    { "magic_off_by_one",     "magic + 1",                          m_magic_off_by_one, NULL },
    { "version_zero",         "version = 0",                        m_version_zero, NULL },
    { "version_16",           "version = 16 (pre-v17)",             m_version_16, NULL },
    { "lastcomp_future",      "last_comp_version = 0x20",           m_lastcomp_future, NULL },
    { "totalsize_huge",       "totalsize 256 MiB, buffer is not",   m_totalsize_huge, NULL },
    { "totalsize_max",        "totalsize = 0xFFFFFFFF",             m_totalsize_max, NULL },
    { "totalsize_tiny",       "totalsize = 8 (below header size)",  m_totalsize_tiny, NULL },
    { "totalsize_zero",       "totalsize = 0",                      m_totalsize_zero, NULL },
    { "off_struct_past_end",  "off_dt_struct past the buffer",      m_off_struct_past_end, NULL },
    { "off_strings_past_end", "off_dt_strings past the buffer",     m_off_strings_past_end, NULL },
    { "blocks_swapped",       "struct and strings offsets swapped", m_blocks_swapped, NULL },
    { "blocks_overlap",       "strings block starts inside struct", m_blocks_overlap, NULL },
    { "size_struct_huge",     "size_dt_struct = 256 MiB",           m_size_struct_huge, NULL },
    { "size_strings_huge",    "size_dt_strings = 256 MiB",          m_size_strings_huge, NULL },
    { "size_struct_wrap",     "off + size_dt_struct wraps 32 bits", m_size_struct_wrap, NULL },
    { "off_rsv_unaligned",    "off_mem_rsvmap not 8-byte aligned",  m_off_rsv_unaligned, NULL },
    { "off_rsv_in_header",    "off_mem_rsvmap points into header",  m_off_rsv_in_header, NULL },
    { "off_rsv_past_struct",  "off_mem_rsvmap past off_dt_struct",  m_off_rsv_past_struct, NULL },
    { "off_struct_unaligned", "off_dt_struct not 4-byte aligned",   m_off_struct_unaligned, NULL },
    { "rsv_no_terminator",    "reserve map has no (0,0) entry",     m_rsv_no_terminator, NULL },
    { "truncated_half",       "buffer is half the declared size",   m_truncated_half, NULL },
    { "truncated_header",     "buffer shorter than the header",     m_truncated_header, NULL },
    { "struct_no_end",        "FDT_END replaced with FDT_NOP",      m_struct_no_end, NULL },
    { "struct_bad_tag",       "unknown tag 0x42424242",             m_struct_bad_tag, NULL },
    { "unbalanced_end_node",  "extra FDT_END_NODE, nesting broken", m_unbalanced_end_node, NULL },
    { "prop_len_max",         "property len = 0xFFFFFFFF",          m_prop_len_max, NULL },
    { "prop_len_totalsize",   "property len = totalsize",           m_prop_len_totalsize, NULL },
    { "prop_len_signed",      "property len negative as int",       m_prop_len_signed, NULL },
    { "prop_nameoff_past",    "property nameoff past string block", m_prop_nameoff_past, NULL },
    { "prop_nameoff_last",    "property nameoff one past the end",  m_prop_nameoff_last, NULL },
    { "node_name_unterm",     "node name never NUL-terminated",     m_node_name_unterminated, NULL },
    { "strings_unterm",       "string block does not end in NUL",   m_strings_unterminated, NULL },
    { "prop_before_node",     "FDT_PROP at depth 0",                m_prop_before_node, NULL },
    { "two_roots",            "two sibling root nodes",             NULL, s_two_roots },
    { "deep_nesting",         "10000 levels of nesting",            NULL, s_deep_nesting },
    { "no_root",              "structure block is only FDT_END",    NULL, s_no_root },
    { "unclosed_root",        "root node never closed",             NULL, s_unclosed_root },
    { "empty_struct",         "size_dt_struct = 0",                 NULL, s_empty_struct },
};

int fdt_corpus_count(void)
{
    return (int)(sizeof(CASES) / sizeof(CASES[0]));
}

const char* fdt_corpus_name(int idx)
{
    if (idx < 0 || idx >= fdt_corpus_count())
        return NULL;
    return CASES[idx].name;
}

const char* fdt_corpus_desc(int idx)
{
    if (idx < 0 || idx >= fdt_corpus_count())
        return NULL;
    return CASES[idx].what;
}

uint8_t* fdt_corpus_build(int idx, const uint8_t* base, uint32_t baselen,
    uint32_t* outlen)
{
    uint8_t* buf;
    uint32_t len;

    if (idx < 0 || idx >= fdt_corpus_count())
        return NULL;

    if (CASES[idx].build != NULL)
        return CASES[idx].build(outlen);

    if (base == NULL || baselen < FDT_HEADER_SIZE)
        return NULL;

    /* Mutate a scratch copy first so a shrinking mutator can be honored
     * by handing back a buffer allocated at exactly the final length -
     * that is what lets ASan catch a read past the end. */
    buf = malloc(baselen);
    if (buf == NULL)
        return NULL;
    memcpy(buf, base, baselen);
    len = baselen;
    CASES[idx].mutate(buf, &len);

    if (len < baselen) {
        uint8_t* tight = malloc(len);
        if (tight == NULL) {
            free(buf);
            return NULL;
        }
        memcpy(tight, buf, len);
        free(buf);
        buf = tight;
    }

    *outlen = len;
    return buf;
}

#endif /* WOLFBOOT_FDT_CORPUS */

#endif /* (MMU || WOLFBOOT_FDT) && !BUILD_LOADER_STAGE1 */
