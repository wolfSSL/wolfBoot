/* suit.h
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
 * @file suit.h
 * @brief SUIT manifest (draft-ietf-suit-manifest-34) processing for wolfBoot.
 *
 * Compiled only when WOLFBOOT_SUIT is defined. Uses wolfCOSE for CBOR decode and
 * COSE_Sign1 verification. Zero dynamic allocation: all state is caller-provided.
 */
#ifndef SUIT_H
#define SUIT_H

#ifdef WOLFBOOT_SUIT

#include <stdint.h>
#include <stddef.h>

/* Return codes. 0 is success; negatives are errors. */
#define SUIT_SUCCESS             0
#define SUIT_E_NOT_IMPLEMENTED   (-1)
#define SUIT_E_INVALID_ARG       (-2)
#define SUIT_E_PARSE             (-3)
#define SUIT_E_AUTH              (-4)
#define SUIT_E_DIGEST_MISMATCH   (-5)
#define SUIT_E_CONDITION         (-6)
#define SUIT_E_UNSUPPORTED       (-7)
#define SUIT_E_CRYPTO            (-8)
#define SUIT_E_INSTALL           (-9)
#define SUIT_E_ROLLBACK          (-10)
#define SUIT_E_BOUNDS            (-11)
#define SUIT_E_FETCH             (-12)

/* SUIT_Envelope map keys (IANA SUIT Envelope Elements). */
enum suit_envelope_key {
    SUIT_ENV_AUTHENTICATION_WRAPPER = 2,
    SUIT_ENV_MANIFEST               = 3,
    SUIT_ENV_PAYLOAD_FETCH          = 16,
    SUIT_ENV_INSTALL                = 20,
    SUIT_ENV_TEXT                   = 23
};

/* SUIT_Manifest map keys (IANA SUIT Manifest Elements). */
enum suit_manifest_key {
    SUIT_MAN_VERSION         = 1,
    SUIT_MAN_SEQUENCE_NUMBER = 2,
    SUIT_MAN_COMMON          = 3,
    SUIT_MAN_REFERENCE_URI   = 4,
    SUIT_MAN_VALIDATE        = 7,
    SUIT_MAN_LOAD            = 8,
    SUIT_MAN_INVOKE          = 9,
    SUIT_MAN_PAYLOAD_FETCH   = 16,
    SUIT_MAN_INSTALL         = 20,
    SUIT_MAN_TEXT            = 23
};

/* SUIT_Common map keys (IANA SUIT Common Elements). */
enum suit_common_key {
    SUIT_COMMON_DEPENDENCIES    = 1,
    SUIT_COMMON_COMPONENTS      = 2,
    SUIT_COMMON_SHARED_SEQUENCE = 4
};

/* SUIT_Condition codes (IANA SUIT Commands). */
enum suit_condition {
    SUIT_COND_VENDOR_IDENTIFIER = 1,
    SUIT_COND_CLASS_IDENTIFIER  = 2,
    SUIT_COND_IMAGE_MATCH       = 3,
    SUIT_COND_COMPONENT_SLOT    = 5,
    SUIT_COND_CHECK_CONTENT     = 6,
    SUIT_COND_ABORT             = 14,
    SUIT_COND_DEVICE_IDENTIFIER = 24
};

/* SUIT_Directive codes (IANA SUIT Commands). */
enum suit_directive {
    SUIT_DIR_SET_COMPONENT_INDEX  = 12,
    SUIT_DIR_TRY_EACH             = 15,
    SUIT_DIR_WRITE                = 18,
    SUIT_DIR_OVERRIDE_PARAMETERS  = 20,
    SUIT_DIR_FETCH                = 21,
    SUIT_DIR_COPY                 = 22,
    SUIT_DIR_INVOKE               = 23,
    SUIT_DIR_SWAP                 = 31,
    SUIT_DIR_RUN_SEQUENCE         = 32
};

/* SUIT_Parameters map keys (IANA SUIT Parameters). */
enum suit_parameter {
    SUIT_PARAM_VENDOR_IDENTIFIER = 1,
    SUIT_PARAM_CLASS_IDENTIFIER  = 2,
    SUIT_PARAM_IMAGE_DIGEST      = 3,
    SUIT_PARAM_COMPONENT_SLOT    = 5,
    SUIT_PARAM_STRICT_ORDER      = 12,
    SUIT_PARAM_SOFT_FAILURE      = 13,
    SUIT_PARAM_IMAGE_SIZE        = 14,
    SUIT_PARAM_CONTENT           = 18,
    SUIT_PARAM_URI               = 21,
    SUIT_PARAM_SOURCE_COMPONENT  = 22,
    SUIT_PARAM_INVOKE_ARGS       = 23,
    SUIT_PARAM_DEVICE_IDENTIFIER = 24,
    SUIT_PARAM_FETCH_ARGUMENTS   = 25
};

/* COSE algorithm ids used by SUIT_Digest (COSE Algorithms registry). */
enum suit_cose_digest_alg {
    SUIT_COSE_ALG_SHA_256  = -16,
    SUIT_COSE_ALG_SHAKE128 = -18,
    SUIT_COSE_ALG_SHA_384  = -43,
    SUIT_COSE_ALG_SHA_512  = -44
};

#ifndef SUIT_MAX_COMPONENTS
#define SUIT_MAX_COMPONENTS 1
#endif

/* Scratch for COSE_Sign1 Sig_structure reconstruction during verify. 512 bytes
 * covers ES256/384/512 and EdDSA; raise for ML-DSA manifests. */
#ifndef SUIT_SCRATCH_SZ
#define SUIT_SCRATCH_SZ 512
#endif

/* Trust-anchor keystore slot used to verify the manifest signature. */
#ifndef SUIT_KEY_SLOT
#define SUIT_KEY_SLOT 0
#endif

/* Parsed SUIT envelope/manifest. Holds zero-copy offsets into the caller's
 * input buffer; no copies are made. */
struct suit_manifest {
    const uint8_t* env;         /* envelope buffer (caller-owned) */
    size_t         envLen;
    size_t         envEncodedLen; /* bytes the envelope CBOR actually occupies */
    const uint8_t* manifest;    /* bstr-wrapped SUIT_Manifest */
    size_t         manifestLen;
    uint64_t       sequenceNumber; /* suit-manifest-sequence-number (anti-rollback) */
    const uint8_t* authWrapper; /* suit-authentication-wrapper contents */
    size_t         authWrapperLen;
    const uint8_t* common;      /* suit-common (bstr-wrapped SUIT_Common) */
    size_t         commonLen;
    const uint8_t* sharedSeq;   /* suit-shared-sequence (command sequence) */
    size_t         sharedSeqLen;
    const uint8_t* validate;    /* suit-validate command sequence */
    size_t         validateLen;
    const uint8_t* install;     /* suit-install command sequence */
    size_t         installLen;
};

/* Parameter store: set by directive-override-parameters, consumed by the
 * conditions/directives that follow. Zero-copy into the manifest buffer. */
struct suit_params {
    const uint8_t* imageDigest;    /* SUIT_Digest CBOR [alg, bytes] */
    size_t         imageDigestLen;
    const uint8_t* vendorId;       /* RFC 4122 UUID bytes */
    size_t         vendorIdLen;
    const uint8_t* classId;
    size_t         classIdLen;
    const uint8_t* content;        /* directive-write content */
    size_t         contentLen;
    const uint8_t* uri;            /* directive-fetch source (tstr bytes) */
    size_t         uriLen;
    uint64_t       imageSize;
    size_t         sourceComponent;
    int            componentSlot;
};

/* Host-provided component I/O. The interpreter never touches flash/storage
 * directly, which keeps it reusable outside wolfBoot (the host supplies these).
 * All return 0 on success, negative on error. */
struct suit_component_ops {
    void* ctx;
    int (*hash)(void* ctx, size_t idx, uint8_t* out, size_t outLen);
    int (*write)(void* ctx, size_t idx, const uint8_t* src, size_t len);
    int (*copy)(void* ctx, size_t idx, size_t srcIdx);
    /* Retrieve the payload at uri into component idx (SUIT_HAVE_FETCH). The host
     * (e.g. wolfUpdate transport) owns the network/storage; NULL if unsupported. */
    int (*fetch)(void* ctx, size_t idx, const uint8_t* uri, size_t uriLen);
};

/* Command-sequence interpreter state. Fixed-size, no heap. */
struct suit_context {
    struct suit_manifest*            m;
    const struct suit_component_ops* ops;
    struct suit_params               params;
    size_t                           componentIndex;
    uint64_t                         minSequence;   /* reject seq < this (anti-rollback) */
    size_t                           maxImageSize;  /* reject image/content larger (0 = no cap) */
    const uint8_t*                   deviceVendorId; /* this device's identity */
    size_t                           deviceVendorIdLen;
    const uint8_t*                   deviceClassId;
    size_t                           deviceClassIdLen;
    /* Content-decryption key (SUIT_HAVE_ENCRYPTION). When set, directive-write
     * treats the content parameter as a COSE_Encrypt0 message and decrypts it
     * into decBuf before handing the plaintext to ops->write. */
    const uint8_t*                   cek;
    size_t                           cekLen;
    uint8_t*                         decBuf;
    size_t                           decBufLen;
};

int suit_open(struct suit_manifest* m, const uint8_t* env, size_t len);
int suit_verify_auth(struct suit_manifest* m);
int suit_process(struct suit_context* ctx, struct suit_manifest* m);

#ifdef SUIT_HAVE_REPORT
/* Minimal SUIT status report keys: a compact outcome record for an update server
 * (e.g. wolfUpdate), not the full draft-suit-report COSE attestation. */
enum suit_report_key {
    SUIT_REPORT_RESULT          = 1, /* int: 0 on success, else the SUIT_E_* code */
    SUIT_REPORT_SEQUENCE_NUMBER = 2  /* uint: the manifest sequence number */
};

/* Encode a status report for a finished suit_process into the caller's buffer as
 * CBOR. result is the suit_process return code. Writes the encoded length to
 * written. Zero allocation: out is caller-owned. */
int suit_report_encode(const struct suit_context* ctx,
    const struct suit_manifest* m, int result,
    uint8_t* out, size_t outLen, size_t* written);
#endif /* SUIT_HAVE_REPORT */

/* wolfBoot entry point: open + authenticate + process a staged SUIT envelope.
 * The caller supplies the component I/O ops and this device's identity. */
int wolfBoot_suit_verify(const uint8_t* env, size_t envLen,
    const struct suit_component_ops* ops,
    const uint8_t* vendorId, size_t vendorIdLen,
    const uint8_t* classId, size_t classIdLen);

#endif /* WOLFBOOT_SUIT */

#endif /* SUIT_H */
