/* ftpm_callable.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFBOOT_TZ_FTPM

#include <stdint.h>
#include <string.h>

#include "store_sbrk.h"
#include "wolfboot/wcs_ftpm.h"
#include "wolftpm/fwtpm/fwtpm.h"
#include "wolftpm/fwtpm/fwtpm_command.h"
#include "wolftpm/fwtpm/fwtpm_nv.h"
#include "wolftpm/tpm2_types.h"

static FWTPM_CTX ftpm_ctx;
static int ftpm_ready;

extern unsigned int _start_heap;
extern unsigned int _heap_size;

void *_sbrk(unsigned int incr)
{
    static uint8_t *heap;
    return wolfboot_store_sbrk(incr, &heap, (uint8_t *)&_start_heap,
        (uint32_t)(&_heap_size));
}

#ifndef FWTPM_NO_NV
#define WCS_FTPM_NV_SIZE (16U * 1024U)
static uint8_t ftpm_nv[WCS_FTPM_NV_SIZE];

static int ftpm_nv_read(void *ctx, word32 offset, byte *buf, word32 size)
{
    uint8_t *nv = (uint8_t *)ctx;

    if (nv == NULL || buf == NULL || offset > WCS_FTPM_NV_SIZE ||
            size > (WCS_FTPM_NV_SIZE - offset)) {
        return BAD_FUNC_ARG;
    }

    XMEMCPY(buf, nv + offset, size);
    return TPM_RC_SUCCESS;
}

static int ftpm_nv_write(void *ctx, word32 offset, const byte *buf,
    word32 size)
{
    uint8_t *nv = (uint8_t *)ctx;

    if (nv == NULL || buf == NULL || offset > WCS_FTPM_NV_SIZE ||
            size > (WCS_FTPM_NV_SIZE - offset)) {
        return BAD_FUNC_ARG;
    }

    XMEMCPY(nv + offset, buf, size);
    return TPM_RC_SUCCESS;
}

static int ftpm_nv_erase(void *ctx, word32 offset, word32 size)
{
    uint8_t *nv = (uint8_t *)ctx;

    if (nv == NULL || offset > WCS_FTPM_NV_SIZE ||
            size > (WCS_FTPM_NV_SIZE - offset)) {
        return BAD_FUNC_ARG;
    }

    XMEMSET(nv + offset, 0xFF, size);
    return TPM_RC_SUCCESS;
}

static FWTPM_NV_HAL ftpm_nv_hal = {
    ftpm_nv_read,
    ftpm_nv_write,
    ftpm_nv_erase,
    ftpm_nv,
    WCS_FTPM_NV_SIZE
};
#endif /* !FWTPM_NO_NV */

static uint32_t ftpm_rsp_size(const uint8_t *rsp, int rspLen)
{
    if (rsp == NULL || rspLen < TPM2_HEADER_SIZE) {
        return 0;
    }

    return ((uint32_t)rsp[2] << 24) |
           ((uint32_t)rsp[3] << 16) |
           ((uint32_t)rsp[4] << 8) |
           (uint32_t)rsp[5];
}

void wcs_ftpm_init(void)
{
    int rc;

#ifndef FWTPM_NO_NV
    XMEMSET(ftpm_nv, 0xFF, sizeof(ftpm_nv));
#endif
    XMEMSET(&ftpm_ctx, 0, sizeof(ftpm_ctx));
#ifndef FWTPM_NO_NV
    (void)FWTPM_NV_SetHAL(&ftpm_ctx, &ftpm_nv_hal);
#endif

    rc = FWTPM_Init(&ftpm_ctx);
    if (rc == 0) {
        ftpm_ctx.wasStarted = 1;
        ftpm_ready = 1;
    }
}

int CSME_NSE_API wcs_ftpm_transmit(const uint8_t *cmd, uint32_t cmdSz,
        uint8_t *rsp, uint32_t *rspSz)
{
    int rspLen;

    if (!ftpm_ready) {
        return TPM_RC_INITIALIZE;
    }
    if (cmd == NULL || rsp == NULL || rspSz == NULL || cmdSz == 0U ||
            cmdSz > WCS_FTPM_MAX_COMMAND_SIZE || *rspSz == 0U ||
            *rspSz > WCS_FTPM_MAX_COMMAND_SIZE) {
        return BAD_FUNC_ARG;
    }

    rspLen = (int)*rspSz;
    int rc = FWTPM_ProcessCommand(&ftpm_ctx, cmd, (int)cmdSz, rsp, &rspLen, 0);
    if (rc >= 0) {
        uint32_t wireSz = ftpm_rsp_size(rsp, rspLen);
        if (wireSz > 0U && wireSz <= *rspSz) {
            *rspSz = wireSz;
            rc = TPM_RC_SUCCESS;
        }
        else if (rspLen >= 0) {
            *rspSz = (uint32_t)rspLen;
        }
    }
    return rc;
}

#endif /* WOLFBOOT_TZ_FTPM */
