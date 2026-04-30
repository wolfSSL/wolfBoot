/* wcs_wolfhsm.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifndef WOLFBOOT_WCS_WOLFHSM_H
#define WOLFBOOT_WCS_WOLFHSM_H

#include <stdint.h>
#include "wolfboot/wc_secure.h"

#ifdef WOLFCRYPT_TZ_WOLFHSM

/* Match wolfHSM's WH_COMM_MTU; bridge buffers are sized to this. */
#ifndef WCS_WOLFHSM_MAX_REQ_SIZE
#define WCS_WOLFHSM_MAX_REQ_SIZE 1288U
#endif

#ifndef WCS_WOLFHSM_MAX_RSP_SIZE
#define WCS_WOLFHSM_MAX_RSP_SIZE 1288U
#endif

int CSME_NSE_API wcs_wolfhsm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz);

void wcs_wolfhsm_init(void);

#endif /* WOLFCRYPT_TZ_WOLFHSM */

#endif /* WOLFBOOT_WCS_WOLFHSM_H */
