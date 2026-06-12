/* suit_wolfboot.c
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
 * @file suit_wolfboot.c
 * @brief wolfBoot entry point for SUIT: run open + authenticate + process over a
 * staged SUIT envelope. The caller (e.g. wolfUpdate, or a boot-time dispatch)
 * supplies the envelope buffer, the component I/O ops (flash-backed on target),
 * and this device's vendor/class identity.
 */
#include "suit.h"

#ifdef WOLFBOOT_SUIT

#include <string.h>
#include <wolfssl/wolfcrypt/hash.h>
#include "image.h"

int wolfBoot_suit_verify(const uint8_t* env, size_t envLen,
    const struct suit_component_ops* ops,
    const uint8_t* vendorId, size_t vendorIdLen,
    const uint8_t* classId, size_t classIdLen)
{
    int ret;
    struct suit_manifest m;
    struct suit_context ctx;

    ret = suit_open(&m, env, envLen);
    if (ret == SUIT_SUCCESS) {
        ret = suit_verify_auth(&m);
    }
    if (ret == SUIT_SUCCESS) {
        memset(&ctx, 0, sizeof(ctx));
        ctx.m = &m;
        ctx.ops = ops;
        ctx.deviceVendorId = vendorId;
        ctx.deviceVendorIdLen = vendorIdLen;
        ctx.deviceClassId = classId;
        ctx.deviceClassIdLen = classIdLen;
        ret = suit_process(&ctx, &m);
    }
    return ret;
}

/* Detect a SUIT envelope at a partition start: CBOR major type 5 (map) or 6
 * (tag). The wolfBoot magic byte never falls in this range. */
int wolfBoot_image_is_suit(const uint8_t* image)
{
    uint8_t b = image[0];
    return (((b >= 0xA0u) && (b <= 0xDFu)) ? 1 : 0);
}

/* Component I/O for the concatenated [envelope][image] layout: component 0 is
 * the image that follows the envelope. imageSize comes from the manifest, set by
 * override-parameters before image-match runs. */
struct wb_suit_opsctx {
    const uint8_t*       imgBase;
    struct suit_context* sctx;
};

static int wb_suit_hash(void* c, size_t idx, uint8_t* out, size_t outLen)
{
    struct wb_suit_opsctx* o = (struct wb_suit_opsctx*)c;
    (void)idx;
    if (outLen < 32u) {
        return -1;
    }
    return wc_Hash(WC_HASH_TYPE_SHA256, o->imgBase,
        (word32)o->sctx->params.imageSize, out, (word32)outLen);
}

/* Boot-time dispatch: authenticate the manifest and image-match the image that
 * follows it, then expose the image as fw_base/fw_size for the A/B swap. */
int wolfBoot_suit_verify_authenticity(struct wolfBoot_image* img)
{
    int ret;
    struct suit_manifest m;
    struct suit_context ctx;
    struct suit_component_ops ops;
    struct wb_suit_opsctx opsctx;

    memset(&ctx, 0, sizeof(ctx));
    memset(&ops, 0, sizeof(ops));

    ret = suit_open(&m, img->hdr, WOLFBOOT_PARTITION_SIZE);
    if (ret == SUIT_SUCCESS) {
        ret = suit_verify_auth(&m);
    }
    if (ret == SUIT_SUCCESS) {
        opsctx.imgBase = img->hdr + m.envEncodedLen;
        opsctx.sctx = &ctx;
        ops.ctx = &opsctx;
        ops.hash = wb_suit_hash;
        ctx.m = &m;
        ctx.ops = &ops;
        ret = suit_process(&ctx, &m);
    }
    if (ret == SUIT_SUCCESS) {
        img->fw_base = (uint8_t*)(img->hdr + m.envEncodedLen);
        img->fw_size = (uint32_t)ctx.params.imageSize;
    }
    return (ret == SUIT_SUCCESS) ? 0 : -1;
}

#endif /* WOLFBOOT_SUIT */
