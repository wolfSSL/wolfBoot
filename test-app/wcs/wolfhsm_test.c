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

#include "wolfboot/wcs_wolfhsm.h"

#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"

#include "wolfssl/wolfcrypt/random.h"

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

/*
 * Phase 1c exerciser. Initializes the wolfHSM client (which auto-registers
 * the wolfCrypt cryptocb under WH_DEV_ID), runs the CommInit handshake, then
 * exercises a real crypto op (RNG) routed through the secure-side server.
 */
int cmd_wolfhsm_test(const char *args)
{
    static const whTransportNscClientConfig nsc_cfg = { 0 };
    whCommClientConfig comm_cfg;
    whClientConfig     cfg;
    whClientContext    client;
    uint32_t out_clientid = 0;
    uint32_t out_serverid = 0;
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
        return rc;
    }

    rc = wh_Client_CommInit(&client, &out_clientid, &out_serverid);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM CommInit failed: %d\r\n", rc);
        (void)wh_Client_Cleanup(&client);
        return rc;
    }

    printf("wolfHSM CommInit ok (client=%u server=%u)\r\n",
        (unsigned)out_clientid, (unsigned)out_serverid);

    rc = wolfhsm_test_rng();
    if (rc != 0) {
        (void)wh_Client_Cleanup(&client);
        return rc;
    }

    printf("wolfHSM NSC tests passed\r\n");

    (void)wh_Client_Cleanup(&client);
    return 0;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
