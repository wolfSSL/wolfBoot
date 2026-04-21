/* wcs_ftpm.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifndef WOLFBOOT_WCS_FTPM_H
#define WOLFBOOT_WCS_FTPM_H

#include <stdint.h>
#include "wolfboot/wc_secure.h"

#ifdef WOLFBOOT_TZ_FTPM

#ifndef WCS_FTPM_MAX_COMMAND_SIZE
#define WCS_FTPM_MAX_COMMAND_SIZE 4096U
#endif

int CSME_NSE_API wcs_ftpm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz);

void wcs_ftpm_init(void);

#endif /* WOLFBOOT_TZ_FTPM */

#endif /* WOLFBOOT_WCS_FTPM_H */
