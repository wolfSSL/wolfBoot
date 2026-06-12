/* suit_parse.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
/**
 * @file suit_parse.c
 * @brief Parse a SUIT_Envelope (draft-ietf-suit-manifest-34) and locate its
 * authentication-wrapper and manifest members. Zero-copy: records offsets into
 * the caller's buffer using the wolfCOSE CBOR decoder.
 */
#include "suit.h"

#ifdef WOLFBOOT_SUIT

#include <wolfcose/wolfcose.h>

#define SUIT_CBOR_MAJOR_TAG 6u

/* Parse SUIT_Common to locate suit-shared-sequence (a bstr-wrapped sequence). */
static int suit_parse_common(struct suit_manifest* m)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;
    size_t i;
    int64_t key = 0;
    const uint8_t* data = NULL;
    size_t dataLen = 0;

    ctx.buf = NULL;
    ctx.cbuf = m->common;
    ctx.bufSz = m->commonLen;
    ctx.idx = 0;

    if (wc_CBOR_DecodeMapStart(&ctx, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    for (i = 0; (i < count) && (ret == SUIT_SUCCESS); i++) {
        if (wc_CBOR_DecodeInt(&ctx, &key) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (key == (int64_t)SUIT_COMMON_SHARED_SEQUENCE) {
            if (wc_CBOR_DecodeBstr(&ctx, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->sharedSeq = data;
                m->sharedSeqLen = dataLen;
            }
        }
        else {
            if (wc_CBOR_Skip(&ctx) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
        }
    }
    return ret;
}

/* Parse the SUIT_Manifest map to locate common, validate and install. The
 * command sequences and common are bstr-wrapped per draft-34. */
static int suit_parse_manifest(struct suit_manifest* m)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;
    size_t i;
    int64_t key = 0;
    const uint8_t* data = NULL;
    size_t dataLen = 0;

    ctx.buf = NULL;
    ctx.cbuf = m->manifest;
    ctx.bufSz = m->manifestLen;
    ctx.idx = 0;

    if (wc_CBOR_DecodeMapStart(&ctx, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    for (i = 0; (i < count) && (ret == SUIT_SUCCESS); i++) {
        if (wc_CBOR_DecodeInt(&ctx, &key) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (key == (int64_t)SUIT_MAN_COMMON) {
            if (wc_CBOR_DecodeBstr(&ctx, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->common = data;
                m->commonLen = dataLen;
            }
        }
        else if (key == (int64_t)SUIT_MAN_VALIDATE) {
            if (wc_CBOR_DecodeBstr(&ctx, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->validate = data;
                m->validateLen = dataLen;
            }
        }
        else if (key == (int64_t)SUIT_MAN_INSTALL) {
            if (wc_CBOR_DecodeBstr(&ctx, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->install = data;
                m->installLen = dataLen;
            }
        }
        else {
            if (wc_CBOR_Skip(&ctx) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
        }
    }
    if ((ret == SUIT_SUCCESS) && (m->common != NULL)) {
        ret = suit_parse_common(m);
    }
    return ret;
}

int suit_open(struct suit_manifest* m, const uint8_t* env, size_t len)
{
    int ret = SUIT_SUCCESS;
    int cret = WOLFCOSE_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;
    size_t i;
    int64_t key = 0;
    const uint8_t* data = NULL;
    size_t dataLen = 0;
    uint64_t tag = 0;

    if ((m == NULL) || (env == NULL) || (len == 0u)) {
        return SUIT_E_INVALID_ARG;
    }

    m->env = env;
    m->envLen = len;
    m->envEncodedLen = 0;
    m->manifest = NULL;
    m->manifestLen = 0;
    m->authWrapper = NULL;
    m->authWrapperLen = 0;
    m->common = NULL;
    m->commonLen = 0;
    m->sharedSeq = NULL;
    m->sharedSeqLen = 0;
    m->validate = NULL;
    m->validateLen = 0;
    m->install = NULL;
    m->installLen = 0;

    ctx.buf = NULL;
    ctx.cbuf = env;
    ctx.bufSz = len;
    ctx.idx = 0;

    /* SUIT_Envelope may carry an optional CBOR tag (#6.107). Consume it. */
    if (wc_CBOR_PeekType(&ctx) == SUIT_CBOR_MAJOR_TAG) {
        cret = wc_CBOR_DecodeTag(&ctx, &tag);
        if (cret != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }

    if (ret == SUIT_SUCCESS) {
        cret = wc_CBOR_DecodeMapStart(&ctx, &count);
        if (cret != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }

    for (i = 0; (i < count) && (ret == SUIT_SUCCESS); i++) {
        cret = wc_CBOR_DecodeInt(&ctx, &key);
        if (cret != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (key == (int64_t)SUIT_ENV_AUTHENTICATION_WRAPPER) {
            cret = wc_CBOR_DecodeBstr(&ctx, &data, &dataLen);
            if (cret != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->authWrapper = data;
                m->authWrapperLen = dataLen;
            }
        }
        else if (key == (int64_t)SUIT_ENV_MANIFEST) {
            cret = wc_CBOR_DecodeBstr(&ctx, &data, &dataLen);
            if (cret != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                m->manifest = data;
                m->manifestLen = dataLen;
            }
        }
        else {
            /* Severable members (text, payload-fetch, install) not needed here. */
            cret = wc_CBOR_Skip(&ctx);
            if (cret != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
        }
    }

    if (ret == SUIT_SUCCESS) {
        m->envEncodedLen = ctx.idx;
        if ((m->manifest == NULL) || (m->authWrapper == NULL)) {
            ret = SUIT_E_PARSE;
        }
    }

    if (ret == SUIT_SUCCESS) {
        ret = suit_parse_manifest(m);
    }

    return ret;
}

#endif /* WOLFBOOT_SUIT */
