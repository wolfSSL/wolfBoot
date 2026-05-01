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

int CSME_NSE_API wcs_wolfhsm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz);

void wcs_wolfhsm_init(void);

#endif /* WOLFCRYPT_TZ_WOLFHSM */

#endif /* WOLFBOOT_WCS_WOLFHSM_H */
