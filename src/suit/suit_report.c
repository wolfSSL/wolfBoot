/* suit_report.c
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
 * @file suit_report.c
 * @brief Minimal SUIT status report: a compact CBOR { result, sequence-number }
 * record an update server (e.g. wolfUpdate) reads to learn an update's outcome
 * without a second boot. Not the full draft-suit-report COSE attestation.
 */
#include "suit.h"

#if defined(WOLFBOOT_SUIT) && defined(SUIT_HAVE_REPORT)

#include <wolfcose/wolfcose.h>

int suit_report_encode(const struct suit_context* ctx,
    const struct suit_manifest* m, int result,
    uint8_t* out, size_t outLen, size_t* written)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX c;

    (void)ctx;
    if ((m == NULL) || (out == NULL) || (written == NULL)) {
        return SUIT_E_INVALID_ARG;
    }

    c.buf = out;
    c.cbuf = NULL;
    c.bufSz = outLen;
    c.idx = 0;

    if (wc_CBOR_EncodeMapStart(&c, 2) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_BOUNDS;
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_EncodeInt(&c, (int64_t)SUIT_REPORT_RESULT)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_BOUNDS;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_EncodeInt(&c, (int64_t)result) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_BOUNDS;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_EncodeInt(&c, (int64_t)SUIT_REPORT_SEQUENCE_NUMBER)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_BOUNDS;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_EncodeUint(&c, m->sequenceNumber) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_BOUNDS;
        }
    }
    if (ret == SUIT_SUCCESS) {
        *written = c.idx;
    }
    return ret;
}

#endif /* WOLFBOOT_SUIT && SUIT_HAVE_REPORT */
