/* suit_test.c
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
 * @file suit_test.c
 * @brief Host unit test for the SUIT verify + process + install paths. Authors a
 * full signed SUIT envelope with wolfCOSE and exercises suit_open +
 * suit_verify_auth + suit_process (identity validate, payload install via
 * directive-write, image-match), with tamper cases. Build with
 * -DSUIT_INSTALL_DIRECTIVES.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfcose/wolfcose.h>

#include "suit.h"

/* Test trust anchor: keystore_get_buffer() returns the P-256 public key X||Y. */
static uint8_t g_pub[64];
uint8_t* keystore_get_buffer(int id) { (void)id; return g_pub; }

/* COSE key id and a keyslot_id_by_sha() stub that records the kid the verifier
 * extracted (proving kid-based key selection) and maps it to the test slot. */
static const uint8_t KID[8] = { 0xde,0xad,0xbe,0xef,0x01,0x02,0x03,0x04 };
static uint8_t g_seen_kid[16];
static size_t g_seen_kidLen;
int keyslot_id_by_sha(const uint8_t* hint)
{
    if (hint != NULL) {
        memcpy(g_seen_kid, hint, sizeof(KID));
        g_seen_kidLen = sizeof(KID);
    }
    return 0;
}

/* A RAM "component" standing in for a flash partition: directive-write stores
 * the installed payload here, and condition-image-match hashes it. */
static uint8_t g_flash[256];
static size_t g_flashLen;
static int g_corrupt_write; /* simulate a faulty/hostile write for testing */

static int comp_hash(void* c, size_t idx, uint8_t* out, size_t outLen)
{
    (void)c; (void)idx;
    if (outLen < 32) { return -1; }
    return wc_Hash(WC_HASH_TYPE_SHA256, g_flash, (word32)g_flashLen, out,
        (word32)outLen);
}

static int comp_write(void* c, size_t idx, const uint8_t* src, size_t len)
{
    (void)c; (void)idx;
    if (len > sizeof(g_flash)) { return -1; }
    memcpy(g_flash, src, len);
    g_flashLen = len;
    if (g_corrupt_write) { g_flash[0] ^= 0xFF; }
    return 0;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); return -1; } } while (0)

/* The firmware payload the manifest installs. */
static const uint8_t FW[] = "wolfUpdate firmware payload v2 installed via SUIT";
static const uint8_t VENDOR[16] = {
    0xfa,0x6b,0x4a,0x53,0xd5,0xad,0x5f,0xdf,0xbe,0x9d,0xe6,0x63,0xe4,0xd4,0x1f,0xfe };
static const uint8_t CLASSID[16] = {
    0x14,0x92,0xaf,0x14,0x25,0x69,0x5e,0x48,0xbf,0x42,0x9b,0x2d,0x51,0xf2,0xab,0x45 };

/* Content-encryption key + IV for the encrypted-install case (A128GCM). */
static const uint8_t CEK[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
static const uint8_t ENC_IV[12] = {
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab };

static const uint8_t TEST_D[32] = {
    0xC9,0xAF,0xA9,0xD8,0x45,0xBA,0x75,0x16,0x6B,0x5C,0x21,0x57,0x67,0xB1,0xD6,0x93,
    0x4E,0x50,0xC3,0xDB,0x36,0xE8,0x9B,0x12,0x7B,0x8A,0x62,0x2B,0x12,0x0F,0x67,0x21 };
static const uint8_t TEST_QX[32] = {
    0x60,0xFE,0xD4,0xBA,0x25,0x5A,0x9D,0x31,0xC9,0x61,0xEB,0x74,0xC6,0x35,0x6D,0x68,
    0xC0,0x49,0xB8,0x92,0x3B,0x61,0xFA,0x6C,0xE6,0x69,0x62,0x2E,0x60,0xF2,0x9F,0xB6 };
static const uint8_t TEST_QY[32] = {
    0x79,0x03,0xFE,0x10,0x08,0xB8,0xBC,0x99,0xA4,0x1A,0xE9,0xE9,0x56,0x28,0xBC,0x64,
    0xF2,0xF1,0xB2,0x0C,0x2D,0x7E,0x9F,0x51,0x77,0xA3,0xC2,0x94,0xD4,0x46,0x22,0x99 };

#ifdef SUIT_HAVE_FETCH
/* directive-fetch source, and a fetch op standing in for wolfUpdate transport:
 * it records the uri it was handed and stages the payload into the component. */
static const char URI[] = "coaps://wolfupdate.example/fw";
static int g_fetch_called;
static uint8_t g_fetch_uri[64];
static size_t g_fetch_uriLen;
static int comp_fetch(void* c, size_t idx, const uint8_t* uri, size_t uriLen)
{
    (void)c; (void)idx;
    g_fetch_called = 1;
    if (uriLen > sizeof(g_fetch_uri)) { return -1; }
    memcpy(g_fetch_uri, uri, uriLen);
    g_fetch_uriLen = uriLen;
    if (sizeof(FW) > sizeof(g_flash)) { return -1; }
    memcpy(g_flash, FW, sizeof(FW));
    g_flashLen = sizeof(FW);
    return 0;
}
#endif

#define E(call) do { if ((call) != WOLFCOSE_SUCCESS) { return -1; } } while (0)

static void cbor_init(WOLFCOSE_CBOR_CTX* c, uint8_t* out, size_t sz)
{
    c->buf = out; c->cbuf = out; c->bufSz = sz; c->idx = 0;
}

static int enc_suit_digest(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* digest, size_t digestLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_COSE_ALG_SHA_256));
    E(wc_CBOR_EncodeBstr(&c, digest, digestLen));
    *outLen = c.idx;
    return 0;
}

/* shared-sequence: select component 0, set device identity parameters. */
static int enc_shared_seq(uint8_t* out, size_t outSz, size_t* outLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 4));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_SET_COMPONENT_INDEX));
    E(wc_CBOR_EncodeUint(&c, 0));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_OVERRIDE_PARAMETERS));
    E(wc_CBOR_EncodeMapStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_VENDOR_IDENTIFIER));
    E(wc_CBOR_EncodeBstr(&c, VENDOR, sizeof(VENDOR)));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_CLASS_IDENTIFIER));
    E(wc_CBOR_EncodeBstr(&c, CLASSID, sizeof(CLASSID)));
    *outLen = c.idx;
    return 0;
}

/* validate: assert this update is for this device. */
static int enc_validate_seq(uint8_t* out, size_t outSz, size_t* outLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 4));
    E(wc_CBOR_EncodeInt(&c, SUIT_COND_VENDOR_IDENTIFIER));
    E(wc_CBOR_EncodeUint(&c, 15));
    E(wc_CBOR_EncodeInt(&c, SUIT_COND_CLASS_IDENTIFIER));
    E(wc_CBOR_EncodeUint(&c, 15));
    *outLen = c.idx;
    return 0;
}

/* install: set the image digest + content, write it, then image-match. */
static int enc_install_seq(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* sd, size_t sdLen, const uint8_t* content, size_t contentLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 6));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_OVERRIDE_PARAMETERS));
    E(wc_CBOR_EncodeMapStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_IMAGE_DIGEST));
    E(wc_CBOR_EncodeBstr(&c, sd, sdLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_CONTENT));
    E(wc_CBOR_EncodeBstr(&c, content, contentLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_WRITE));
    E(wc_CBOR_EncodeUint(&c, 15));
    E(wc_CBOR_EncodeInt(&c, SUIT_COND_IMAGE_MATCH));
    E(wc_CBOR_EncodeUint(&c, 15));
    *outLen = c.idx;
    return 0;
}

#ifdef SUIT_HAVE_FETCH
/* install via fetch: set the image digest + uri, fetch the payload, image-match. */
static int enc_install_fetch_seq(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* sd, size_t sdLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 6));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_OVERRIDE_PARAMETERS));
    E(wc_CBOR_EncodeMapStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_IMAGE_DIGEST));
    E(wc_CBOR_EncodeBstr(&c, sd, sdLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_PARAM_URI));
    E(wc_CBOR_EncodeTstr(&c, (const uint8_t*)URI, strlen(URI)));
    E(wc_CBOR_EncodeInt(&c, SUIT_DIR_FETCH));
    E(wc_CBOR_EncodeUint(&c, 15));
    E(wc_CBOR_EncodeInt(&c, SUIT_COND_IMAGE_MATCH));
    E(wc_CBOR_EncodeUint(&c, 15));
    *outLen = c.idx;
    return 0;
}
#endif

static int enc_common(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* shared, size_t sharedLen)
{
    static const uint8_t COMP_ID = 0x00;
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeMapStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_COMMON_COMPONENTS));
    E(wc_CBOR_EncodeArrayStart(&c, 1));
    E(wc_CBOR_EncodeArrayStart(&c, 1));
    E(wc_CBOR_EncodeBstr(&c, &COMP_ID, 1));
    E(wc_CBOR_EncodeInt(&c, SUIT_COMMON_SHARED_SEQUENCE));
    E(wc_CBOR_EncodeBstr(&c, shared, sharedLen));
    *outLen = c.idx;
    return 0;
}

static int enc_manifest(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* common, size_t commonLen,
    const uint8_t* validate, size_t validateLen,
    const uint8_t* install, size_t installLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeMapStart(&c, 5));
    E(wc_CBOR_EncodeInt(&c, SUIT_MAN_VERSION));
    E(wc_CBOR_EncodeUint(&c, 1));
    E(wc_CBOR_EncodeInt(&c, SUIT_MAN_SEQUENCE_NUMBER));
    E(wc_CBOR_EncodeUint(&c, 1));
    E(wc_CBOR_EncodeInt(&c, SUIT_MAN_COMMON));
    E(wc_CBOR_EncodeBstr(&c, common, commonLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_MAN_VALIDATE));
    E(wc_CBOR_EncodeBstr(&c, validate, validateLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_MAN_INSTALL));
    E(wc_CBOR_EncodeBstr(&c, install, installLen));
    *outLen = c.idx;
    return 0;
}

static int enc_auth(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* sd, size_t sdLen, const uint8_t* cose, size_t coseLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeArrayStart(&c, 2));
    E(wc_CBOR_EncodeBstr(&c, sd, sdLen));
    E(wc_CBOR_EncodeBstr(&c, cose, coseLen));
    *outLen = c.idx;
    return 0;
}

static int enc_envelope(uint8_t* out, size_t outSz, size_t* outLen,
    const uint8_t* aw, size_t awLen, const uint8_t* man, size_t manLen)
{
    WOLFCOSE_CBOR_CTX c;
    cbor_init(&c, out, outSz);
    E(wc_CBOR_EncodeMapStart(&c, 2));
    E(wc_CBOR_EncodeInt(&c, SUIT_ENV_AUTHENTICATION_WRAPPER));
    E(wc_CBOR_EncodeBstr(&c, aw, awLen));
    E(wc_CBOR_EncodeInt(&c, SUIT_ENV_MANIFEST));
    E(wc_CBOR_EncodeBstr(&c, man, manLen));
    *outLen = c.idx;
    return 0;
}

/* Author a full signed SUIT envelope (identity + install of FW) into env. When
 * encrypt is set, the install content is a COSE_Encrypt0 of FW (decrypted on
 * install); the image-digest is always over the FW plaintext. */
static int author(uint8_t* env, size_t envSz, size_t* envLen, size_t* sigOff,
    int encrypt, int useFetch)
{
    WC_RNG rng;
    ecc_key eccKey;
    WOLFCOSE_KEY signKey;
    uint8_t fwDigest[32], manDigest[32];
    uint8_t sdFw[64], sdMan[64];
    size_t sdFwLen = 0, sdManLen = 0;
    uint8_t shared[64], validate[32], install[320], common[128], manifest[640];
    size_t sharedLen = 0, validateLen = 0, installLen = 0, commonLen = 0;
    size_t manifestLen = 0;
    uint8_t cose[256], aw[384];
    size_t coseLen = 0, awLen = 0;
    uint8_t scratch[1024];
    const uint8_t* contentPtr = FW;
    size_t contentLen = sizeof(FW);
    size_t i;
#ifdef SUIT_HAVE_ENCRYPTION
    WOLFCOSE_KEY enckey;
    uint8_t encFW[256];
    size_t encFWLen = 0;
#endif

    CHECK(wc_Hash(WC_HASH_TYPE_SHA256, FW, (word32)sizeof(FW), fwDigest,
        sizeof(fwDigest)) == 0, "hash fw");
    CHECK(enc_suit_digest(sdFw, sizeof(sdFw), &sdFwLen, fwDigest,
        sizeof(fwDigest)) == 0, "sd fw");
    CHECK(enc_shared_seq(shared, sizeof(shared), &sharedLen) == 0, "shared");
    CHECK(enc_validate_seq(validate, sizeof(validate), &validateLen) == 0,
        "validate");

#ifdef SUIT_HAVE_ENCRYPTION
    if (encrypt) {
        CHECK(wc_CoseKey_Init(&enckey) == 0, "enc key init");
        CHECK(wc_CoseKey_SetSymmetric(&enckey, CEK, sizeof(CEK)) == 0,
            "set sym key");
        CHECK(wc_CoseEncrypt0_Encrypt(&enckey, WOLFCOSE_ALG_A128GCM,
            ENC_IV, sizeof(ENC_IV), FW, sizeof(FW), NULL, 0, NULL, NULL, 0,
            scratch, sizeof(scratch), encFW, sizeof(encFW), &encFWLen)
            == WOLFCOSE_SUCCESS, "encrypt0");
        wc_CoseKey_Free(&enckey);
        contentPtr = encFW;
        contentLen = encFWLen;
    }
#else
    (void)encrypt;
#endif

#ifdef SUIT_HAVE_FETCH
    if (useFetch) {
        CHECK(enc_install_fetch_seq(install, sizeof(install), &installLen, sdFw,
            sdFwLen) == 0, "install-fetch");
    }
    else {
        CHECK(enc_install_seq(install, sizeof(install), &installLen, sdFw,
            sdFwLen, contentPtr, contentLen) == 0, "install");
    }
#else
    (void)useFetch;
    CHECK(enc_install_seq(install, sizeof(install), &installLen, sdFw, sdFwLen,
        contentPtr, contentLen) == 0, "install");
#endif
    CHECK(enc_common(common, sizeof(common), &commonLen, shared, sharedLen) == 0,
        "common");
    CHECK(enc_manifest(manifest, sizeof(manifest), &manifestLen, common,
        commonLen, validate, validateLen, install, installLen) == 0, "manifest");

    CHECK(wc_Hash(WC_HASH_TYPE_SHA256, manifest, (word32)manifestLen, manDigest,
        sizeof(manDigest)) == 0, "hash manifest");
    CHECK(enc_suit_digest(sdMan, sizeof(sdMan), &sdManLen, manDigest,
        sizeof(manDigest)) == 0, "sd manifest");

    CHECK(wc_InitRng(&rng) == 0, "InitRng");
    CHECK(wc_ecc_init(&eccKey) == 0, "ecc_init");
    CHECK(wc_ecc_import_unsigned(&eccKey, TEST_QX, TEST_QY, TEST_D,
        ECC_SECP256R1) == 0, "import key");
    memcpy(g_pub, TEST_QX, 32);
    memcpy(g_pub + 32, TEST_QY, 32);
    CHECK(wc_CoseKey_Init(&signKey) == 0, "key init");
    CHECK(wc_CoseKey_SetEcc(&signKey, WOLFCOSE_CRV_P256, &eccKey) == 0,
        "set ecc");
    signKey.hasPrivate = 1;
    CHECK(wc_CoseSign1_Sign(&signKey, WOLFCOSE_ALG_ES256, KID, sizeof(KID),
        NULL, 0, sdMan, sdManLen, NULL, 0, scratch, sizeof(scratch), cose,
        sizeof(cose), &coseLen, &rng) == WOLFCOSE_SUCCESS, "sign");

    CHECK(enc_auth(aw, sizeof(aw), &awLen, sdMan, sdManLen, cose, coseLen) == 0,
        "auth");
    CHECK(enc_envelope(env, envSz, envLen, aw, awLen, manifest, manifestLen) == 0,
        "envelope");

    *sigOff = 0;
    for (i = 0; i + coseLen <= *envLen; i++) {
        if (memcmp(env + i, cose, coseLen) == 0) {
            *sigOff = i + coseLen - 1;
        }
    }

    wc_CoseKey_Free(&signKey);
    wc_ecc_free(&eccKey);
    wc_FreeRng(&rng);
    return 0;
}

static void ctx_init(struct suit_context* c, struct suit_manifest* m,
    const struct suit_component_ops* ops)
{
    memset(c, 0, sizeof(*c));
    c->m = m;
    c->ops = ops;
    c->deviceVendorId = VENDOR;
    c->deviceVendorIdLen = sizeof(VENDOR);
    c->deviceClassId = CLASSID;
    c->deviceClassIdLen = sizeof(CLASSID);
}

int main(void)
{
    uint8_t env[1024];
    size_t envLen = 0;
    size_t sigOff = 0;
    struct suit_manifest m;
    struct suit_component_ops ops;
    struct suit_context c;
    FILE* f;
    int ret;
#ifdef SUIT_HAVE_ENCRYPTION
    uint8_t decBuf[256];
#endif
#ifdef SUIT_HAVE_REPORT
    uint8_t report[64];
    size_t reportLen = 0;
    WOLFCOSE_CBOR_CTX rc;
    size_t rcount = 0;
    int64_t rkey = 0, rresult = 0;
    uint64_t rseq = 0;
#endif

    memset(&ops, 0, sizeof(ops));
    ops.hash = comp_hash;
    ops.write = comp_write;

    if (author(env, sizeof(env), &envLen, &sigOff, 0, 0) != 0) { return 1; }
    printf("authored full SUIT envelope: %zu bytes\n", envLen);

    /* Dump the envelope for the independent cross-check (cbor2 + pycose). */
    f = fopen("/tmp/suit_envelope.cbor", "wb");
    if (f != NULL) {
        (void)fwrite(env, 1, envLen, f);
        (void)fclose(f);
    }

    CHECK(suit_open(&m, env, envLen) == SUIT_SUCCESS, "suit_open");
    CHECK(m.common != NULL && m.sharedSeq != NULL && m.validate != NULL &&
        m.install != NULL, "manifest sub-sequences located");
    CHECK(suit_verify_auth(&m) == SUIT_SUCCESS, "verify_auth");
    CHECK(g_seen_kidLen == sizeof(KID) &&
        memcmp(g_seen_kid, KID, sizeof(KID)) == 0,
        "verifier extracted + used the COSE kid");
    printf("PASS: parsed + authenticated (key selected by COSE kid)\n");

    /* Full process: identity validate, install (write FW), image-match. */
    g_flashLen = 0;
    g_corrupt_write = 0;
    ctx_init(&c, &m, &ops);
    CHECK(suit_process(&c, &m) == SUIT_SUCCESS, "process should pass");
    CHECK(g_flashLen == sizeof(FW) && memcmp(g_flash, FW, sizeof(FW)) == 0,
        "installed payload must equal FW");
    printf("PASS: validated + installed payload + image-match (%zu bytes)\n",
        g_flashLen);

    /* Faulty/hostile write -> the post-install image-match must catch it. */
    g_flashLen = 0;
    g_corrupt_write = 1;
    ctx_init(&c, &m, &ops);
    ret = suit_process(&c, &m);
    CHECK(ret == SUIT_E_DIGEST_MISMATCH, "corrupt write must fail image-match");
    printf("PASS: corrupt install rejected (image-match)\n");
    g_corrupt_write = 0;

    /* Wrong device identity -> condition-vendor-identifier must fail. */
    g_flashLen = 0;
    ctx_init(&c, &m, &ops);
    c.deviceVendorId = CLASSID;
    ret = suit_process(&c, &m);
    CHECK(ret == SUIT_E_CONDITION, "wrong vendor must fail condition");
    printf("PASS: wrong vendor rejected (condition)\n");

    /* Anti-rollback: a manifest older than the installed sequence is rejected. */
    g_flashLen = 0;
    ctx_init(&c, &m, &ops);
    c.minSequence = 5; /* manifest sequence-number is 1 */
    ret = suit_process(&c, &m);
    CHECK(ret == SUIT_E_ROLLBACK, "older sequence must be rejected");
    printf("PASS: anti-rollback (older sequence rejected)\n");

    /* Bounds: content larger than the allowed image size is rejected. */
    g_flashLen = 0;
    ctx_init(&c, &m, &ops);
    c.maxImageSize = 8; /* FW payload is larger */
    ret = suit_process(&c, &m);
    CHECK(ret == SUIT_E_BOUNDS, "oversized content must be rejected");
    printf("PASS: bounds (oversized content rejected)\n");

    /* Tampered signature -> authentication must fail. */
    env[sigOff] ^= 0xFF;
    CHECK(suit_open(&m, env, envLen) == SUIT_SUCCESS, "reopen tampered");
    ret = suit_verify_auth(&m);
    CHECK(ret == SUIT_E_AUTH, "tampered signature must fail auth");
    printf("PASS: tampered signature rejected (auth)\n");

#ifdef SUIT_HAVE_ENCRYPTION
    /* Encrypted payload: install content is a COSE_Encrypt0, decrypted with the
     * device key on write. Confidentiality end to end. */
    if (author(env, sizeof(env), &envLen, &sigOff, 1, 0) != 0) { return 1; }
    CHECK(suit_open(&m, env, envLen) == SUIT_SUCCESS, "open (encrypted)");
    CHECK(suit_verify_auth(&m) == SUIT_SUCCESS, "verify_auth (encrypted)");
    g_flashLen = 0;
    g_corrupt_write = 0;
    ctx_init(&c, &m, &ops);
    c.cek = CEK;
    c.cekLen = sizeof(CEK);
    c.decBuf = decBuf;
    c.decBufLen = sizeof(decBuf);
    CHECK(suit_process(&c, &m) == SUIT_SUCCESS, "process (encrypted) should pass");
    CHECK(g_flashLen == sizeof(FW) && memcmp(g_flash, FW, sizeof(FW)) == 0,
        "decrypted install must equal FW plaintext");
    printf("PASS: encrypted payload decrypted + installed (confidentiality)\n");
#endif

#ifdef SUIT_HAVE_FETCH
    /* directive-fetch: the host retrieves the payload by uri instead of having it
     * embedded, then image-match validates what was fetched. */
    if (author(env, sizeof(env), &envLen, &sigOff, 0, 1) != 0) { return 1; }
    CHECK(suit_open(&m, env, envLen) == SUIT_SUCCESS, "open (fetch)");
    CHECK(suit_verify_auth(&m) == SUIT_SUCCESS, "verify_auth (fetch)");
    g_flashLen = 0;
    g_fetch_called = 0;
    memset(&ops, 0, sizeof(ops));
    ops.hash = comp_hash;
    ops.fetch = comp_fetch;
    ctx_init(&c, &m, &ops);
    CHECK(suit_process(&c, &m) == SUIT_SUCCESS, "process (fetch) should pass");
    CHECK(g_fetch_called == 1, "fetch op must be invoked");
    CHECK(g_fetch_uriLen == strlen(URI) &&
        memcmp(g_fetch_uri, URI, g_fetch_uriLen) == 0, "fetch uri must match");
    CHECK(g_flashLen == sizeof(FW) && memcmp(g_flash, FW, sizeof(FW)) == 0,
        "fetched payload must equal FW");
    printf("PASS: directive-fetch retrieved + image-matched payload\n");
    memset(&ops, 0, sizeof(ops));
    ops.hash = comp_hash;
    ops.write = comp_write;
#endif

#ifdef SUIT_HAVE_REPORT
    /* status report: a finished process emits a compact { result, sequence }
     * record an update server consumes to learn the outcome. */
    if (author(env, sizeof(env), &envLen, &sigOff, 0, 0) != 0) { return 1; }
    CHECK(suit_open(&m, env, envLen) == SUIT_SUCCESS, "open (report)");
    CHECK(suit_verify_auth(&m) == SUIT_SUCCESS, "verify_auth (report)");
    g_flashLen = 0;
    g_corrupt_write = 0;
    ctx_init(&c, &m, &ops);
    ret = suit_process(&c, &m);
    CHECK(ret == SUIT_SUCCESS, "process (report) should pass");
    CHECK(suit_report_encode(&c, &m, ret, report, sizeof(report), &reportLen)
        == SUIT_SUCCESS, "report encode");
    rc.buf = NULL;
    rc.cbuf = report;
    rc.bufSz = reportLen;
    rc.idx = 0;
    CHECK(wc_CBOR_DecodeMapStart(&rc, &rcount) == WOLFCOSE_SUCCESS &&
        rcount == 2, "report is a 2-entry map");
    CHECK(wc_CBOR_DecodeInt(&rc, &rkey) == WOLFCOSE_SUCCESS &&
        rkey == SUIT_REPORT_RESULT, "report result key");
    CHECK(wc_CBOR_DecodeInt(&rc, &rresult) == WOLFCOSE_SUCCESS &&
        rresult == 0, "report result is success");
    CHECK(wc_CBOR_DecodeInt(&rc, &rkey) == WOLFCOSE_SUCCESS &&
        rkey == SUIT_REPORT_SEQUENCE_NUMBER, "report sequence key");
    CHECK(wc_CBOR_DecodeUint(&rc, &rseq) == WOLFCOSE_SUCCESS &&
        rseq == 1, "report sequence is the manifest sequence");
    printf("PASS: status report encodes result + sequence (%zu bytes)\n",
        reportLen);
#endif

    printf("ALL SUIT INSTALL TESTS PASSED\n");
    return 0;
}
