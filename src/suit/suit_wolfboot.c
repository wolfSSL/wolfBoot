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

#endif /* WOLFBOOT_SUIT */
