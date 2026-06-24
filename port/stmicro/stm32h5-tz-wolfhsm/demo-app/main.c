/* main.c - reference non-secure entry point for the wolfHSM TrustZone
 *           demo on STM32H5.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/*
 * This file is a reference implementation of the minimal non-secure
 * entry point that a wolfHSM TrustZone client needs on ARMv8-M.
 *
 * It is NOT the file wolfBoot links into the default test app for
 * stm32h5-tz-wolfhsm.config. That role is filled by
 * test-app/app_stm32h5.c, which calls cmd_wolfhsm_test() from
 * test-app/wcs/wolfhsm_test.c. This file documents the canonical
 * pattern in one place so an integrator porting the wolfHSM ARMv8-M
 * NSC transport to a different ARMv8-M part can read a single short
 * source file and see exactly what the client side has to do.
 *
 * The single-app TrustZone model used here is:
 *
 *   - The secure-world wolfBoot image hosts the wolfHSM server. It
 *     exposes the server to the non-secure world through one
 *     cmse_nonsecure_entry veneer, wcs_wolfhsm_transmit(), provided by
 *     wolfBoot under src/wolfhsm_callable.c.
 *   - The non-secure-world test application initialises a wolfHSM
 *     client whose transport is the generic ARMv8-M NSC bridge from
 *     wolfHSM at port/armv8m-tz/wh_transport_nsc.{c,h}. Send() calls
 *     the veneer inline; Recv() consumes the cached response.
 *   - The client calls whTest_ClientConfig() (from wolfHSM
 *     test/wh_test.h), which runs the full client-side wolfHSM test
 *     suite over the real NSC bridge: ClientServer, Crypto, KeyWrap,
 *     She, Timeout, Auth.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* wolfHSM client and transport */
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_error.h"

#include "wh_transport_nsc.h"

/* Forward-declared rather than included so this reference file does not
 * depend on the wolfHSM test sources being on the build include path. */
struct whClientConfig;
extern int whTest_ClientConfig(struct whClientConfig* clientCfg);

#define WOLFHSM_DEMO_CLIENT_ID  1

/* Non-secure-side singletons. The transport callback table itself lives
 * in the wolfHSM port at port/armv8m-tz/wh_transport_nsc.c; the context
 * is a static buffer the callbacks operate on. */
static whTransportNscClientContext  s_nsc_ctx;
static whTransportNscClientConfig   s_nsc_cfg;

/* Hook for the integrator to set up clocks, GPIO, UART. wolfBoot
 * provides equivalents (hal_init() and friends) when this code is
 * built as part of test-app/app_stm32h5.c. */
extern void wolfhsm_demo_board_init(void);
extern void wolfhsm_demo_uart_init(void);

static int wolfhsm_demo_run(void)
{
    whCommClientConfig comm;
    whClientConfig     cfg;
    int                rc;
    static const whTransportClientCb nsc_cb = WH_TRANSPORT_NSC_CLIENT_CB;

    memset(&s_nsc_ctx, 0, sizeof(s_nsc_ctx));
    memset(&s_nsc_cfg, 0, sizeof(s_nsc_cfg));
    memset(&comm,     0, sizeof(comm));
    memset(&cfg,      0, sizeof(cfg));

    comm.transport_cb      = &nsc_cb;
    comm.transport_context = &s_nsc_ctx;
    comm.transport_config  = &s_nsc_cfg;
    comm.client_id         = WOLFHSM_DEMO_CLIENT_ID;

    cfg.comm = &comm;

    rc = whTest_ClientConfig((struct whClientConfig*)&cfg);
    if (rc != WH_ERROR_OK) {
        printf("wolfHSM demo: whTest_ClientConfig failed rc=%d\r\n", rc);
        return rc;
    }

    printf("wolfHSM demo: whTest_ClientConfig rc=0\r\n");
    return 0;
}

int main(void)
{
    int rc;

    wolfhsm_demo_board_init();
    wolfhsm_demo_uart_init();

    printf("\r\nwolfHSM TrustZone demo (STM32H5)\r\n");

    rc = wolfhsm_demo_run();

    /* Mirror the BKPT contract used by wolfBoot's m33mu CI harness:
     *   bkpt #0x7d => first-boot pass
     *   bkpt #0x7f => second-boot pass
     *   bkpt #0x7e => fail
     * The single-run reference here only uses pass/fail. */
    if (rc == 0) {
        __asm__ volatile ("bkpt #0x7d");
    }
    else {
        __asm__ volatile ("bkpt #0x7e");
    }

    while (1) {
    }
}
