/* wolfhsm_callable.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>
#include <string.h>
#include <arm_cmse.h>

#include "loader.h"
#include "store_sbrk.h"
#include "wolfboot/wcs_wolfhsm.h"
#include "wolfboot/wolfhsm_flash_hal.h"

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/random.h"

#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_flash.h"
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

#define WCS_WOLFHSM_SERVER_ID            56U

/* Two 32 KiB partitions in the wolfBoot keyvault region: 64 KiB used out of
 * 112 KiB, leaving headroom. Per-partition layout = 24 B state header +
 * 32 directory entries * 56 B (= ~1.8 KiB) + ~30 KiB usable payload. */
#define WCS_WOLFHSM_PARTITION_SIZE       (32U * 1024U)

/* Linker-provided symbols for the FLASH_KEYVAULT region defined in
 * hal/stm32h5.ld; matches the PSA / PKCS11 stores' pattern. */
extern uint32_t _flash_keyvault;
extern uint32_t _flash_keyvault_size;

static whFlashH5Ctx              g_flash_ctx;
/* Immutable flash config referenced by g_nvm_flash_cfg.config. Held at file
 * scope so the pointer stays valid after wcs_wolfhsm_init returns. */
static whFlashH5Ctx              g_flash_h5_cfg;

static whNvmFlashContext         g_nvm_flash_ctx;
static whNvmFlashConfig          g_nvm_flash_cfg = {
    .cb      = &whFlashH5_Cb,
    .context = &g_flash_ctx,
    /* .config is set at runtime in wcs_wolfhsm_init */
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
static whTransportNscServerConfig g_srv_tx_cfg = { 0 };
static const whTransportServerCb g_srv_tx_cb = WH_TRANSPORT_NSC_SERVER_CB;
static whCommServerConfig        g_comm_cfg = {
    .transport_context = &g_srv_tx_ctx,
    .transport_cb      = &g_srv_tx_cb,
    .transport_config  = &g_srv_tx_cfg,
    .server_id         = WCS_WOLFHSM_SERVER_ID,
};
static whServerConfig            g_server_cfg = {
    .comm_config = &g_comm_cfg,
    .nvm         = &g_nvm_ctx,
    .crypto      = &g_crypto_ctx,
#ifdef WOLF_CRYPTO_CB
    .devId       = INVALID_DEVID,
#endif
};

static whServerContext           g_server;
static volatile int              g_wolfhsm_ready;

void wcs_wolfhsm_init(void)
{
    int rc;

    if (g_wolfhsm_ready) {
        return;
    }

    memset(&g_srv_tx_ctx, 0, sizeof(g_srv_tx_ctx));

    g_flash_h5_cfg.base           = (uint32_t)&_flash_keyvault;
    g_flash_h5_cfg.size           = (uint32_t)&_flash_keyvault_size;
    g_flash_h5_cfg.partition_size = WCS_WOLFHSM_PARTITION_SIZE;
    g_nvm_flash_cfg.config        = &g_flash_h5_cfg;

    /* g_crypto_ctx.rng is an embedded WC_RNG[1] (see wh_server.h) */
    rc = wc_InitRng(&g_crypto_ctx.rng[0]);
    if (rc != 0) {
        wolfBoot_panic();
    }
    rc = wh_Nvm_Init(&g_nvm_ctx, &g_nvm_cfg);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }
    rc = wh_Server_Init(&g_server, &g_server_cfg);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }
    rc = wh_Server_SetConnected(&g_server, WH_COMM_CONNECTED);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }
    g_wolfhsm_ready = 1;
}

int CSME_NSE_API wcs_wolfhsm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz)
{
    uint32_t rsp_capacity;
    int rc;

    if (cmd == NULL || rsp == NULL || rspSz == NULL) {
        return WH_ERROR_BADARGS;
    }
    /* The caller is the non-secure world. Validate every caller-supplied
     * pointer references non-secure memory before dereferencing it, so a
     * compromised NS app cannot trick the secure side into reading or
     * writing through a secure-world pointer. Use CMSE_NONSECURE to match the
     * sibling veneers (fwtpm_callable.c, pkcs11_callable.c, wc_callable.c):
     * the relevant property is the Secure/Non-secure attribution (SAU/IDAU);
     * the MPU read/write bits read back as 0 when NO_MPU is set and do not
     * constrain Secure accesses to NS memory anyway. rspSz is checked first
     * since it is dereferenced below to obtain the response capacity. */
    if (cmse_check_address_range((void *)rspSz, sizeof(*rspSz),
            CMSE_NONSECURE) == NULL) {
        return WH_ERROR_BADARGS;
    }
    /* single-fetch *rspSz so it cannot be re-read after validation */
    rsp_capacity = *(volatile const uint32_t *)rspSz;

    if (cmdSz == 0U || cmdSz > WH_COMM_MTU) {
        *rspSz = 0;
        return WH_ERROR_BADARGS;
    }
    if (rsp_capacity == 0U || rsp_capacity > WH_COMM_MTU) {
        *rspSz = 0;
        return WH_ERROR_BADARGS;
    }
    if (cmse_check_address_range((void *)cmd, cmdSz,
            CMSE_NONSECURE) == NULL) {
        *rspSz = 0;
        return WH_ERROR_BADARGS;
    }
    if (cmse_check_address_range(rsp, rsp_capacity,
            CMSE_NONSECURE) == NULL) {
        *rspSz = 0;
        return WH_ERROR_BADARGS;
    }
    if (!g_wolfhsm_ready) {
        *rspSz = 0;
        return WH_ERROR_NOTREADY;
    }

    g_srv_tx_ctx.req_buf         = cmd;
    g_srv_tx_ctx.req_size        = (uint16_t)cmdSz;
    g_srv_tx_ctx.rsp_buf         = rsp;
    g_srv_tx_ctx.rsp_capacity    = (uint16_t)rsp_capacity;
    g_srv_tx_ctx.rsp_size        = 0;
    g_srv_tx_ctx.request_pending = 1;

    /* The NSC bridge is synchronous and always ready once linked. A
     * client CommClose marks the server disconnected; re-assert the
     * transport-ready state on every veneer entry so a following
     * CommInit is accepted. This is the per-transport ready signal,
     * which for NSC is the veneer call itself. */
    rc = wh_Server_SetConnected(&g_server, WH_COMM_CONNECTED);
    if (rc != WH_ERROR_OK) {
        *rspSz = 0;
        return rc;
    }

    rc = wh_Server_HandleRequestMessage(&g_server);

    if (rc == WH_ERROR_OK) {
        /* clamp: dispatcher must honor rsp_capacity; defend against regression */
        if ((uint32_t)g_srv_tx_ctx.rsp_size > rsp_capacity) {
            *rspSz = 0;
            rc     = WH_ERROR_ABORTED;
        }
        else {
            *rspSz = (uint32_t)g_srv_tx_ctx.rsp_size;
        }
    }
    else {
        *rspSz = 0;
    }

    g_srv_tx_ctx.req_buf         = NULL;
    g_srv_tx_ctx.req_size        = 0;
    g_srv_tx_ctx.rsp_buf         = NULL;
    g_srv_tx_ctx.rsp_capacity    = 0;
    g_srv_tx_ctx.request_pending = 0;

    return rc;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
