/* wolfhsm_stub.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

/*
 * Non-secure side static buffers + transport context for the wolfHSM TZ
 * NSC bridge. The transport callback table itself lives in the wolfHSM
 * port file (port/stmicro/stm32-tz/wh_transport_nsc.c); this stub just
 * provides the singleton context it operates on.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>

#include "wh_transport_nsc.h"

/* Static .bss singleton. The wolfHSM client passes a pointer to this in
 * whCommClientConfig.transport_context; the transport callbacks stash the
 * inbound/outbound packets in cmd_buf/rsp_buf. */
whTransportNscClientContext g_wolfhsm_nsc_client_ctx;

#endif /* WOLFCRYPT_TZ_WOLFHSM */
