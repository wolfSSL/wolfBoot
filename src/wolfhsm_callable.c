/* wolfhsm_callable.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>
#include <string.h>

#include "store_sbrk.h"
#include "wolfboot/wcs_wolfhsm.h"

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/random.h"

#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_server.h"

#include "wh_transport_nsc.h"

extern unsigned int _start_heap;
extern unsigned int _heap_size;

void *_sbrk(unsigned int incr)
{
    static uint8_t *heap;
    return wolfboot_store_sbrk(incr, &heap, (uint8_t *)&_start_heap,
        (uint32_t)(&_heap_size));
}

/* Phase 1b uses a 32 KiB ramsim partition pair for the NVM backend; Phase 3
 * swaps this for a flash-backed adapter over wolfBoot's hal_flash_*.
 * pageSize must match WHFU_BYTES_PER_UNIT (8) — wolfHSM programs the flash
 * one unit at a time, so a larger pageSize causes the modulo check in
 * whFlashRamsim_Program to fail. */
#define WCS_WOLFHSM_RAMSIM_SIZE      (32U * 1024U)
#define WCS_WOLFHSM_RAMSIM_SECTOR    (8U * 1024U)
#define WCS_WOLFHSM_RAMSIM_PAGE      8U

static uint8_t                   g_ramsim_buf[WCS_WOLFHSM_RAMSIM_SIZE];
static whFlashRamsimCtx          g_ramsim_ctx;
static whFlashRamsimCfg          g_ramsim_cfg = {
    .memory     = g_ramsim_buf,
    .size       = WCS_WOLFHSM_RAMSIM_SIZE,
    .sectorSize = WCS_WOLFHSM_RAMSIM_SECTOR,
    .pageSize   = WCS_WOLFHSM_RAMSIM_PAGE,
    .erasedByte = 0xFFU,
    .initData   = NULL,
};
static whFlashCb                 g_flash_cb = WH_FLASH_RAMSIM_CB;

static whNvmFlashContext         g_nvm_flash_ctx;
static whNvmFlashConfig          g_nvm_flash_cfg = {
    .cb      = &g_flash_cb,
    .context = &g_ramsim_ctx,
    .config  = &g_ramsim_cfg,
};
static whNvmCb                   g_nvm_flash_cb = WH_NVM_FLASH_CB;
static whNvmContext              g_nvm_ctx;
static whNvmConfig               g_nvm_cfg = {
    .cb      = &g_nvm_flash_cb,
    .context = &g_nvm_flash_ctx,
    .config  = &g_nvm_flash_cfg,
};

static whServerCryptoContext     g_crypto_ctx;
static whTransportNscServerContext g_srv_tx_ctx;
static whTransportNscServerConfig g_srv_tx_cfg = { { 0 } };
static whCommServerConfig        g_comm_cfg = {
    .transport_context = &g_srv_tx_ctx,
    .transport_cb      = &whTransportNscServer_Cb,
    .transport_config  = &g_srv_tx_cfg,
    .server_id         = 56, /* server identifier; NS client uses client_id=1 */
};
static whServerConfig            g_server_cfg = {
    .comm_config = &g_comm_cfg,
    .nvm         = &g_nvm_ctx,
    .crypto      = &g_crypto_ctx,
#if defined WOLF_CRYPTO_CB
    .devId       = INVALID_DEVID,
#endif
};

static whServerContext           g_server;
static int                       g_wolfhsm_ready;

void wcs_wolfhsm_init(void)
{
    int rc;

    rc = wc_InitRng(g_crypto_ctx.rng);
    if (rc != 0) {
        return;
    }
    rc = wh_Nvm_Init(&g_nvm_ctx, &g_nvm_cfg);
    if (rc != WH_ERROR_OK) {
        return;
    }
    rc = wh_Server_Init(&g_server, &g_server_cfg);
    if (rc != WH_ERROR_OK) {
        return;
    }
    (void)wh_Server_SetConnected(&g_server, WH_COMM_CONNECTED);
    g_wolfhsm_ready = 1;
}

/* Single NSC veneer. Per call: validate the NS pointers/sizes (single-fetch
 * defeats TOCTOU on *rspSz), park the buffers in the secure-side transport
 * context, run wh_Server_HandleRequestMessage exactly once, write back the
 * captured response size. */
int CSME_NSE_API wcs_wolfhsm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz)
{
    uint32_t rsp_capacity;
    int rc;

    if (cmd == NULL || rsp == NULL || rspSz == NULL) {
        return WH_ERROR_BADARGS;
    }
    /* Single fetch of the caller-supplied capacity; subsequent code uses
     * only this local copy. The NS caller cannot mutate it under us. */
    rsp_capacity = *rspSz;

    if (cmdSz == 0U || cmdSz > WCS_WOLFHSM_MAX_REQ_SIZE) {
        return WH_ERROR_BADARGS;
    }
    if (rsp_capacity == 0U || rsp_capacity > WCS_WOLFHSM_MAX_RSP_SIZE) {
        return WH_ERROR_BADARGS;
    }
    if (!g_wolfhsm_ready) {
        return WH_ERROR_NOTREADY;
    }

    g_srv_tx_ctx.req_buf         = cmd;
    g_srv_tx_ctx.req_size        = (uint16_t)cmdSz;
    g_srv_tx_ctx.rsp_buf         = rsp;
    g_srv_tx_ctx.rsp_capacity    = (uint16_t)rsp_capacity;
    g_srv_tx_ctx.rsp_size        = 0;
    g_srv_tx_ctx.request_pending = 1;

    rc = wh_Server_HandleRequestMessage(&g_server);

    if (rc == WH_ERROR_OK) {
        *rspSz = (uint32_t)g_srv_tx_ctx.rsp_size;
    } else {
        *rspSz = 0;
    }
    return rc;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
