/* suit_process.c
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
 * @file suit_process.c
 * @brief SUIT command-sequence interpreter (draft-ietf-suit-manifest-34).
 * Executes the shared + validate sequences: parameter store, identity checks,
 * and image-match against the staged component (hashed via the host ops).
 */
#include "suit.h"

#ifdef WOLFBOOT_SUIT

#include <string.h>
#include <wolfcose/wolfcose.h>

#define SUIT_SHA256_SZ 32

/* Compare a SUIT_Digest = [ alg, bstr digest ] against a computed hash. */
static int suit_digest_eq(const uint8_t* suitDigest, size_t suitDigestLen,
    const uint8_t* hash, size_t hashLen)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;
    int64_t alg = 0;
    const uint8_t* digBytes = NULL;
    size_t digBytesLen = 0;

    ctx.buf = NULL;
    ctx.cbuf = suitDigest;
    ctx.bufSz = suitDigestLen;
    ctx.idx = 0;

    if (wc_CBOR_DecodeArrayStart(&ctx, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    else if (count < 2u) {
        ret = SUIT_E_PARSE;
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeInt(&ctx, &alg) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (alg != (int64_t)SUIT_COSE_ALG_SHA_256) {
            ret = SUIT_E_UNSUPPORTED;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeBstr(&ctx, &digBytes, &digBytesLen)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if ((digBytesLen != hashLen) ||
            (XMEMCMP(digBytes, hash, hashLen) != 0)) {
            ret = SUIT_E_DIGEST_MISMATCH;
        }
    }
    return ret;
}

/* directive-override-parameters: store the { param-key: value } map. */
static int suit_override(struct suit_context* ctx, WOLFCOSE_CBOR_CTX* c)
{
    int ret = SUIT_SUCCESS;
    size_t count = 0;
    size_t i;
    int64_t pkey = 0;
    const uint8_t* data = NULL;
    size_t dataLen = 0;
    uint64_t uval = 0;

    if (wc_CBOR_DecodeMapStart(c, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    for (i = 0; (i < count) && (ret == SUIT_SUCCESS); i++) {
        if (wc_CBOR_DecodeInt(c, &pkey) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (pkey == (int64_t)SUIT_PARAM_IMAGE_DIGEST) {
            if (wc_CBOR_DecodeBstr(c, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.imageDigest = data;
                ctx->params.imageDigestLen = dataLen;
            }
        }
        else if (pkey == (int64_t)SUIT_PARAM_VENDOR_IDENTIFIER) {
            if (wc_CBOR_DecodeBstr(c, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.vendorId = data;
                ctx->params.vendorIdLen = dataLen;
            }
        }
        else if (pkey == (int64_t)SUIT_PARAM_CLASS_IDENTIFIER) {
            if (wc_CBOR_DecodeBstr(c, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.classId = data;
                ctx->params.classIdLen = dataLen;
            }
        }
        else if (pkey == (int64_t)SUIT_PARAM_IMAGE_SIZE) {
            if (wc_CBOR_DecodeUint(c, &uval) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.imageSize = uval;
            }
        }
        else if (pkey == (int64_t)SUIT_PARAM_CONTENT) {
            if (wc_CBOR_DecodeBstr(c, &data, &dataLen) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.content = data;
                ctx->params.contentLen = dataLen;
            }
        }
        else if (pkey == (int64_t)SUIT_PARAM_SOURCE_COMPONENT) {
            if (wc_CBOR_DecodeUint(c, &uval) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->params.sourceComponent = (size_t)uval;
            }
        }
        else {
            if (wc_CBOR_Skip(c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
        }
    }
    return ret;
}

/* condition-image-match: hash the current component, compare to image-digest. */
static int suit_image_match(struct suit_context* ctx)
{
    int ret = SUIT_SUCCESS;
    uint8_t hash[SUIT_SHA256_SZ];

    if ((ctx->ops == NULL) || (ctx->ops->hash == NULL)) {
        ret = SUIT_E_UNSUPPORTED;
    }
    else if (ctx->params.imageDigest == NULL) {
        ret = SUIT_E_CONDITION;
    }
    else if (ctx->ops->hash(ctx->ops->ctx, ctx->componentIndex, hash,
            sizeof(hash)) != 0) {
        ret = SUIT_E_CRYPTO;
    }
    else {
        ret = suit_digest_eq(ctx->params.imageDigest,
            ctx->params.imageDigestLen, hash, sizeof(hash));
    }
    return ret;
}

static int suit_id_match(const uint8_t* paramId, size_t paramLen,
    const uint8_t* devId, size_t devLen)
{
    int ret = SUIT_SUCCESS;

    if ((paramId == NULL) || (devId == NULL) || (paramLen != devLen) ||
        (XMEMCMP(paramId, devId, paramLen) != 0)) {
        ret = SUIT_E_CONDITION;
    }
    return ret;
}

/* Walk one SUIT_Command_Sequence: a flat array of (command, argument) pairs. */
static int suit_run_sequence(struct suit_context* ctx, const uint8_t* seq,
    size_t seqLen)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX c;
    size_t count = 0;
    size_t i = 0;
    int64_t cmd = 0;
    uint64_t uval = 0;

    c.buf = NULL;
    c.cbuf = seq;
    c.bufSz = seqLen;
    c.idx = 0;

    if (wc_CBOR_DecodeArrayStart(&c, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }

    while ((i + 1u < count) && (ret == SUIT_SUCCESS)) {
        if (wc_CBOR_DecodeInt(&c, &cmd) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
        else if (cmd == (int64_t)SUIT_DIR_SET_COMPONENT_INDEX) {
            if (wc_CBOR_DecodeUint(&c, &uval) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ctx->componentIndex = (size_t)uval;
            }
        }
        else if (cmd == (int64_t)SUIT_DIR_OVERRIDE_PARAMETERS) {
            ret = suit_override(ctx, &c);
        }
        else if (cmd == (int64_t)SUIT_DIR_WRITE) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else if ((ctx->ops == NULL) || (ctx->ops->write == NULL)) {
                ret = SUIT_E_UNSUPPORTED;
            }
            else if (ctx->params.content == NULL) {
                ret = SUIT_E_INSTALL;
            }
            else if (ctx->ops->write(ctx->ops->ctx, ctx->componentIndex,
                    ctx->params.content, ctx->params.contentLen) != 0) {
                ret = SUIT_E_INSTALL;
            }
        }
        else if (cmd == (int64_t)SUIT_DIR_COPY) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else if ((ctx->ops == NULL) || (ctx->ops->copy == NULL)) {
                ret = SUIT_E_UNSUPPORTED;
            }
            else if (ctx->ops->copy(ctx->ops->ctx, ctx->componentIndex,
                    ctx->params.sourceComponent) != 0) {
                ret = SUIT_E_INSTALL;
            }
        }
        else if (cmd == (int64_t)SUIT_COND_IMAGE_MATCH) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {  /* reporting policy */
                ret = SUIT_E_PARSE;
            }
            else {
                ret = suit_image_match(ctx);
            }
        }
        else if (cmd == (int64_t)SUIT_COND_VENDOR_IDENTIFIER) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ret = suit_id_match(ctx->params.vendorId,
                    ctx->params.vendorIdLen, ctx->deviceVendorId,
                    ctx->deviceVendorIdLen);
            }
        }
        else if (cmd == (int64_t)SUIT_COND_CLASS_IDENTIFIER) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ret = suit_id_match(ctx->params.classId,
                    ctx->params.classIdLen, ctx->deviceClassId,
                    ctx->deviceClassIdLen);
            }
        }
        else if (cmd == (int64_t)SUIT_COND_ABORT) {
            if (wc_CBOR_Skip(&c) != WOLFCOSE_SUCCESS) {
                ret = SUIT_E_PARSE;
            }
            else {
                ret = SUIT_E_CONDITION;
            }
        }
        else {
            /* Default-deny: a SUIT processor MUST abort on a command it does not
             * implement rather than silently skip it. A skipped command could
             * carry security-relevant intent, so an unrecognized (or known but
             * unsupported) command fails the whole sequence. */
            ret = SUIT_E_UNSUPPORTED;
        }
        i += 2u;
    }
    return ret;
}

int suit_process(struct suit_context* ctx, struct suit_manifest* m)
{
    int ret = SUIT_SUCCESS;

    if ((ctx == NULL) || (m == NULL)) {
        return SUIT_E_INVALID_ARG;
    }
    ctx->m = m;

    /* suit-shared-sequence runs first (sets up parameters and component). */
    if ((ret == SUIT_SUCCESS) && (m->sharedSeq != NULL)) {
        ret = suit_run_sequence(ctx, m->sharedSeq, m->sharedSeqLen);
    }
    /* suit-validate then asserts the staged image is the authorized one. */
    if ((ret == SUIT_SUCCESS) && (m->validate != NULL)) {
        ret = suit_run_sequence(ctx, m->validate, m->validateLen);
    }

    /* In SUIT-driven install mode, the install sequence (write/copy directives)
     * places the image. In the default handoff mode the caller drives the
     * existing A/B swap after this returns success, and install is not run. */
#ifdef SUIT_INSTALL_DIRECTIVES
    if ((ret == SUIT_SUCCESS) && (m->install != NULL)) {
        ret = suit_run_sequence(ctx, m->install, m->installLen);
    }
#endif
    return ret;
}

#endif /* WOLFBOOT_SUIT */
