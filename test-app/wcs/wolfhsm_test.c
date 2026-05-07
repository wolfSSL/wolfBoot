/* wolfhsm_test.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wolfhsm_test.h"

#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_client_crypto.h"
#include "wolfhsm/wh_common.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_keyid.h"

#include "wolfssl/wolfcrypt/aes.h"
#include "wolfssl/wolfcrypt/misc.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfssl/wolfcrypt/sha256.h"

#include "wh_transport_nsc.h"

/* NS-side singleton transport context lives in wolfhsm_stub.c. */
extern whTransportNscClientContext g_wolfhsm_nsc_client_ctx;

#define WCS_WOLFHSM_CLIENT_ID 1

static int wolfhsm_test_rng(void)
{
    WC_RNG  rng;
    uint8_t rnd[16];
    unsigned int i;
    int rc;

    memset(&rng, 0, sizeof(rng));
    memset(rnd, 0, sizeof(rnd));

    rc = wc_InitRng_ex(&rng, NULL, WH_DEV_ID);
    if (rc != 0) {
        printf("wolfHSM RNG init failed: %d\r\n", rc);
        return rc;
    }

    rc = wc_RNG_GenerateBlock(&rng, rnd, sizeof(rnd));
    if (rc != 0) {
        printf("wolfHSM RNG generate failed: %d\r\n", rc);
        (void)wc_FreeRng(&rng);
        return rc;
    }

    printf("wolfHSM RNG ok:");
    for (i = 0; i < sizeof(rnd); i++) {
        printf(" %02x", rnd[i]);
    }
    printf("\r\n");

    (void)wc_FreeRng(&rng);
    return 0;
}

static int wolfhsm_test_sha256(void)
{
    /* SHA256("abc") — FIPS 180-2, Appendix B.1. */
    static const uint8_t expected[WC_SHA256_DIGEST_SIZE] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    wc_Sha256 sha;
    uint8_t   digest[WC_SHA256_DIGEST_SIZE];
    int       rc;

    memset(&sha, 0, sizeof(sha));
    memset(digest, 0, sizeof(digest));

    rc = wc_InitSha256_ex(&sha, NULL, WH_DEV_ID);
    if (rc != 0) {
        printf("wolfHSM SHA256 init failed: %d\r\n", rc);
        return rc;
    }

    rc = wc_Sha256Update(&sha, (const uint8_t*)"abc", 3);
    if (rc == 0) {
        rc = wc_Sha256Final(&sha, digest);
    }
    wc_Sha256Free(&sha);
    if (rc != 0) {
        printf("wolfHSM SHA256 hash failed: %d\r\n", rc);
        return rc;
    }

    if (memcmp(digest, expected, sizeof(expected)) != 0) {
        printf("wolfHSM SHA256 mismatch\r\n");
        return -1;
    }
    printf("wolfHSM SHA256 ok\r\n");
    return 0;
}

static int wolfhsm_test_aes_cached(whClientContext *client)
{
    /* FIPS 197 Appendix B AES-128 vector. CBC with IV=0 yields the same
     * first-block ciphertext as ECB, so a single block under CBC suffices
     * to verify the key+algorithm wired through correctly. */
    static const uint8_t key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t pt[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t expected[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
    };
    static const uint8_t iv[16] = { 0 };
    Aes      aes;
    uint8_t  ct[16];
    uint16_t keyId      = WH_KEYID_ERASED;
    int      aes_inited = 0;
    int      rc;

    memset(&aes, 0, sizeof(aes));
    memset(ct, 0, sizeof(ct));

    rc = wh_Client_KeyCache(client, WH_NVM_FLAGS_USAGE_ENCRYPT, NULL, 0,
                            key, (uint16_t)sizeof(key), &keyId);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM KeyCache failed: %d\r\n", rc);
        return rc;
    }

    rc = wc_AesInit(&aes, NULL, WH_DEV_ID);
    if (rc != 0) {
        printf("wolfHSM AesInit failed: %d\r\n", rc);
        goto out;
    }
    aes_inited = 1;

    rc = wh_Client_AesSetKeyId(&aes, keyId);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM AesSetKeyId failed: %d\r\n", rc);
        goto out;
    }

    rc = wc_AesSetIV(&aes, iv);
    if (rc == 0) {
        rc = wc_AesCbcEncrypt(&aes, ct, pt, (word32)sizeof(pt));
    }
    if (rc != 0) {
        printf("wolfHSM AES encrypt failed: %d\r\n", rc);
        goto out;
    }

    if (memcmp(ct, expected, sizeof(expected)) != 0) {
        printf("wolfHSM AES mismatch\r\n");
        rc = -1;
        goto out;
    }
    printf("wolfHSM AES ok\r\n");

out:
    if (aes_inited) {
        wc_AesFree(&aes);
    }
    (void)wh_Client_KeyEvict(client, keyId);
    return rc;
}

static int wolfhsm_test_persist(whClientContext *client, int *boot_state)
{
    static const uint8_t persist_key[16] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00
    };
    uint16_t keyId = WH_MAKE_KEYID(0, WCS_WOLFHSM_CLIENT_ID, 1);
    uint8_t  out[sizeof(persist_key)];
    uint16_t outSz = (uint16_t)sizeof(out);
    int      rc;

    memset(out, 0, sizeof(out));
    rc = wh_Client_KeyExport(client, keyId, NULL, 0, out, &outSz);
    if (rc == WH_ERROR_OK && outSz == sizeof(persist_key) &&
        memcmp(out, persist_key, sizeof(persist_key)) == 0) {
        printf("wolfHSM second boot path, restored persisted key\r\n");
        *boot_state = WOLFHSM_TEST_SECOND_BOOT_OK;
        wc_ForceZero(out, sizeof(out));
        return 0;
    }
    wc_ForceZero(out, sizeof(out));

    printf("wolfHSM first boot path, committing key to NVM\r\n");
    rc = wh_Client_KeyCache(client, WH_NVM_FLAGS_USAGE_ENCRYPT, NULL, 0,
                            persist_key, (uint16_t)sizeof(persist_key),
                            &keyId);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM persist KeyCache failed: %d\r\n", rc);
        return rc;
    }
    rc = wh_Client_KeyCommit(client, keyId);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM persist KeyCommit failed: %d\r\n", rc);
        return rc;
    }
    (void)wh_Client_KeyEvict(client, keyId);
    *boot_state = WOLFHSM_TEST_FIRST_BOOT_OK;
    return 0;
}

int cmd_wolfhsm_test(const char *args)
{
    static const whTransportNscClientConfig nsc_cfg = { { 0 } };
    whCommClientConfig comm_cfg;
    whClientConfig     cfg;
    whClientContext    client;
    uint32_t out_clientid = 0;
    uint32_t out_serverid = 0;
    int boot_state = WOLFHSM_TEST_FAIL;
    int rc;

    (void)args;

    memset(&comm_cfg, 0, sizeof(comm_cfg));
    comm_cfg.transport_cb      = &whTransportNscClient_Cb;
    comm_cfg.transport_context = &g_wolfhsm_nsc_client_ctx;
    comm_cfg.transport_config  = &nsc_cfg;
    comm_cfg.client_id         = WCS_WOLFHSM_CLIENT_ID;

    memset(&cfg, 0, sizeof(cfg));
    cfg.comm = &comm_cfg;

    memset(&client, 0, sizeof(client));

    rc = wh_Client_Init(&client, &cfg);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM Init failed: %d\r\n", rc);
        return WOLFHSM_TEST_FAIL;
    }

    rc = wh_Client_CommInit(&client, &out_clientid, &out_serverid);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM CommInit failed: %d\r\n", rc);
        (void)wh_Client_Cleanup(&client);
        return WOLFHSM_TEST_FAIL;
    }

    printf("wolfHSM CommInit ok (client=%u server=%u)\r\n",
        (unsigned)out_clientid, (unsigned)out_serverid);

    rc = wolfhsm_test_rng();
    if (rc != 0) {
        (void)wh_Client_Cleanup(&client);
        return WOLFHSM_TEST_FAIL;
    }

    rc = wolfhsm_test_sha256();
    if (rc != 0) {
        (void)wh_Client_Cleanup(&client);
        return WOLFHSM_TEST_FAIL;
    }

    rc = wolfhsm_test_aes_cached(&client);
    if (rc != 0) {
        (void)wh_Client_Cleanup(&client);
        return WOLFHSM_TEST_FAIL;
    }

    rc = wolfhsm_test_persist(&client, &boot_state);
    if (rc != 0) {
        (void)wh_Client_Cleanup(&client);
        return WOLFHSM_TEST_FAIL;
    }

    printf("wolfHSM NSC tests passed\r\n");

    (void)wh_Client_Cleanup(&client);
    return boot_state;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
