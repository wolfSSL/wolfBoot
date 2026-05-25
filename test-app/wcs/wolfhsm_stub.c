/* wolfhsm_stub.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

/*
 * Non-secure side static buffers + transport context for the wolfHSM TZ
 * NSC bridge. The transport callback table itself lives in the wolfHSM
 * port file (port/armv8m-tz/wh_transport_nsc.c); this stub just
 * provides the singleton context it operates on.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>

#include "wolfhsm/wh_error.h"
#include "wh_transport_nsc.h"
#include "wh_test_common.h"

/* Static .bss singleton. The wolfHSM client passes a pointer to this in
 * whCommClientConfig.transport_context; the transport callbacks stash the
 * inbound/outbound packets in cmd_buf/rsp_buf. */
whTransportNscClientContext g_wolfhsm_nsc_client_ctx;

/* Stub for the in-process NVM-backend helper. wh_test_clientserver.c and
 * wh_test_crypto.c reference this from server-side test paths
 * (whTest_ClientServerSequential and friends). Those paths spawn a server
 * alongside the client in the same process; we never reach them because
 * the secure-world wolfBoot image hosts the real server. Returning
 * WH_ERROR_BADARGS keeps the linker happy and would surface a clear
 * failure if anything did try to call it. */
int whTest_NvmCfgBackend(whTestNvmBackendType   type,
                         whTestNvmBackendUnion* nvmSetup, whNvmConfig* nvmCfg,
                         whFlashRamsimCfg* fCfg, whFlashRamsimCtx* fCtx,
                         const whFlashCb* fCb)
{
    (void)type;
    (void)nvmSetup;
    (void)nvmCfg;
    (void)fCfg;
    (void)fCtx;
    (void)fCb;
    return WH_ERROR_BADARGS;
}

#endif /* WOLFCRYPT_TZ_WOLFHSM */
