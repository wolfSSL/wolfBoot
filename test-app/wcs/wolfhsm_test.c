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
#include "wolfhsm/wh_common.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"

#include "wh_transport_nsc.h"

/* These are the two bare-metal-safe entries that whTest_ClientConfig
 * itself dispatches to in lib/wolfHSM/test/wh_test.c. Forward-declared
 * here so we do not need to pull in wh_test.c (which carries its own
 * main()) just to call them. */
extern int whTest_ClientServerClientConfig(whClientConfig* cfg);
extern int whTest_CryptoClientConfig(whClientConfig* cfg);

/* NS-side singleton transport context lives in wolfhsm_stub.c. */
extern whTransportNscClientContext g_wolfhsm_nsc_client_ctx;

#define WCS_WOLFHSM_CLIENT_ID 1

int cmd_wolfhsm_test(const char *args)
{
    static const whTransportNscClientConfig nsc_cfg = { 0 };
    static const whTransportClientCb nsc_cb = WH_TRANSPORT_NSC_CLIENT_CB;
    whCommClientConfig comm_cfg;
    whClientConfig     cfg;
    int rc;

    (void)args;

    memset(&comm_cfg, 0, sizeof(comm_cfg));
    comm_cfg.transport_cb      = &nsc_cb;
    comm_cfg.transport_context = &g_wolfhsm_nsc_client_ctx;
    comm_cfg.transport_config  = &nsc_cfg;
    comm_cfg.client_id         = WCS_WOLFHSM_CLIENT_ID;

    memset(&cfg, 0, sizeof(cfg));
    cfg.comm = &comm_cfg;

    /* Run the full wolfHSM client suite over the real NSC bridge into
     * the secure-world server. Exercises echo + NVM + cert ops then
     * RNG + SHA + AES + RSA + ECC + ECDH + HKDF. No POSIX, no pthread
     * fake; every op goes through the real ARMv8-M TrustZone NSC
     * veneer. This is the body of whTest_ClientConfig inlined for the
     * features enabled on this build. */
    rc = whTest_ClientServerClientConfig(&cfg);
    if (rc != 0) {
        printf("wolfHSM ClientServerClientConfig FAILED rc=%d\r\n", rc);
        return WOLFHSM_TEST_FAIL;
    }

#ifndef WOLFHSM_CFG_NO_CRYPTO
    rc = whTest_CryptoClientConfig(&cfg);
    if (rc != 0) {
        printf("wolfHSM CryptoClientConfig FAILED rc=%d\r\n", rc);
        return WOLFHSM_TEST_FAIL;
    }
#endif

    printf("wolfHSM whTest_ClientConfig PASSED\r\n");
    return WOLFHSM_TEST_FIRST_BOOT_OK;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
