/* czhsm_client.c -- CI compile-only stub of the PIC32CZ wolfHSM client port.
 *
 * Binds the public wolfHSM shared-memory transport to the PIC32CZ_CFG_SHARED_MEM_*
 * window that arch.mk passes on the command line, so wolfBoot's
 * WOLFBOOT_ENABLE_WOLFHSM_CLIENT path compiles and links against the same public
 * wolfHSM client/transport code as the real port. Nothing here boots or talks
 * to the HSM core. NOT a runnable implementation; see
 * tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#include <stdint.h>
#include <string.h>

#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_transport_mem.h"

#include "port/client/czhsm_client.h"
#include "port/mailbox.h"

#ifndef PIC32CZ_CFG_SHARED_MEM_PHYS_BASE
#error "PIC32CZ_CFG_SHARED_MEM_PHYS_BASE must be defined (see arch.mk)"
#endif
#ifndef WOLFHSM_CFG_CLIENT_ID
#error "WOLFHSM_CFG_CLIENT_ID must be defined (see arch.mk)"
#endif

static const whTransportMemConfig tmcCfg[1] = {{
    .req       = (void*)(uintptr_t)PIC32CZ_CFG_SHARED_MEM_PHYS_BASE,
    .req_size  = PIC32CZ_CFG_SHARED_MEM_REQ_SIZE,
    .resp      = (void*)(uintptr_t)(PIC32CZ_CFG_SHARED_MEM_PHYS_BASE +
                                    PIC32CZ_CFG_SHARED_MEM_REQ_SIZE),
    .resp_size = PIC32CZ_CFG_SHARED_MEM_RESP_SIZE,
}};
static const whTransportClientCb  tmcCb[1]  = {WH_TRANSPORT_MEM_CLIENT_CB};
static whTransportMemClientContext tmcCtx[1] = {0};

static whCommClientConfig ccCfg[1] = {{
    .transport_cb      = tmcCb,
    .transport_context = (void*)tmcCtx,
    .transport_config  = (const void*)tmcCfg,
    .client_id         = WOLFHSM_CFG_CLIENT_ID,
}};

int czhsm_setup(whClientConfig* c_conf)
{
    if (c_conf == NULL) {
        return -1;
    }
    memset(c_conf, 0, sizeof(*c_conf));
    c_conf->comm = ccCfg;
    return 0;
}

int czhsm_connect(void)
{
    if (mb_send_connect() != 0) {
        return -1;
    }
    return (mb_read_connection() == MAILBOX_STATUS_OK) ? 0 : -1;
}
