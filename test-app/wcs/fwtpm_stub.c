/* fwtpm_stub.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFBOOT_TZ_FWTPM

#include <stdint.h>
#include <string.h>

#include "wolfboot/wcs_fwtpm.h"
#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_tis.h"

#define FWTPM_TIS_BASE          0xD40000U
#define FWTPM_TIS_ACCESS        0x0000U
#define FWTPM_TIS_INTF_CAPS     0x0014U
#define FWTPM_TIS_STS           0x0018U
#define FWTPM_TIS_BURST_COUNT   0x0019U
#define FWTPM_TIS_DATA_FIFO     0x0024U
#define FWTPM_TIS_DID_VID       0x0F00U
#define FWTPM_TIS_RID           0x0F04U

#define FWTPM_ACCESS_VALID            0x80U
#define FWTPM_ACCESS_ACTIVE_LOCALITY  0x20U
#define FWTPM_ACCESS_REQUEST_USE      0x02U

#define FWTPM_STS_VALID          0x80U
#define FWTPM_STS_COMMAND_READY  0x40U
#define FWTPM_STS_GO             0x20U
#define FWTPM_STS_DATA_AVAIL     0x10U
#define FWTPM_STS_DATA_EXPECT    0x08U

#define FWTPM_BURST_COUNT 64U

struct fwtpm_tis_state {
    uint8_t cmd[WCS_FWTPM_MAX_COMMAND_SIZE];
    uint8_t rsp[WCS_FWTPM_MAX_COMMAND_SIZE];
    uint32_t cmdSz;
    uint32_t rspSz;
    uint32_t rspPos;
    uint8_t locality;
    uint8_t status;
};

static struct fwtpm_tis_state fwtpm_tis = {
    {0}, {0}, 0, 0, 0, 0,
    FWTPM_STS_VALID | FWTPM_STS_COMMAND_READY
};

static int fwtpm_reg_offset(uint32_t addr, uint32_t *off)
{
    if (off == NULL || addr < FWTPM_TIS_BASE ||
            addr >= (FWTPM_TIS_BASE + 0x1000U)) {
        return BAD_FUNC_ARG;
    }

    *off = addr - FWTPM_TIS_BASE;
    return TPM_RC_SUCCESS;
}

static void fwtpm_store_le(uint8_t *buf, uint16_t size, uint32_t val)
{
    uint16_t i;

    for (i = 0; i < size; i++) {
        buf[i] = (uint8_t)(val >> (8U * i));
    }
}

static uint32_t fwtpm_cmd_size(void)
{
    if (fwtpm_tis.cmdSz < TPM2_HEADER_SIZE) {
        return TPM2_HEADER_SIZE;
    }

    return ((uint32_t)fwtpm_tis.cmd[2] << 24) |
           ((uint32_t)fwtpm_tis.cmd[3] << 16) |
           ((uint32_t)fwtpm_tis.cmd[4] << 8) |
           (uint32_t)fwtpm_tis.cmd[5];
}

static int fwtpm_execute(void)
{
    int rc;

    fwtpm_tis.rspSz = sizeof(fwtpm_tis.rsp);
    fwtpm_tis.rspPos = 0;

    rc = wcs_fwtpm_transmit(fwtpm_tis.cmd, fwtpm_tis.cmdSz, fwtpm_tis.rsp,
        &fwtpm_tis.rspSz);
    fwtpm_tis.cmdSz = 0;

    if (rc == TPM_RC_SUCCESS) {
        fwtpm_tis.status = FWTPM_STS_VALID | FWTPM_STS_DATA_AVAIL;
    }
    else {
        fwtpm_tis.rspSz = 0;
        fwtpm_tis.status = FWTPM_STS_VALID | FWTPM_STS_COMMAND_READY;
    }

    return rc;
}

int TPM2_IoCb_FwtpmNsc(TPM2_CTX *ctx, INT32 isRead, UINT32 addr,
    BYTE *buf, UINT16 size, void *userCtx)
{
    uint32_t off;
    uint16_t burst;

    (void)ctx;
    (void)userCtx;

    if (buf == NULL || size == 0U) {
        return BAD_FUNC_ARG;
    }

    if (fwtpm_reg_offset(addr, &off) != TPM_RC_SUCCESS) {
        return BAD_FUNC_ARG;
    }

    burst = FWTPM_BURST_COUNT;

    if (isRead) {
        switch (off) {
            case FWTPM_TIS_ACCESS:
                buf[0] = FWTPM_ACCESS_VALID | FWTPM_ACCESS_ACTIVE_LOCALITY;
                break;
            case FWTPM_TIS_STS:
                XMEMSET(buf, 0, size);
                buf[0] = fwtpm_tis.status;
                if (size > 1U) {
                    buf[1] = (uint8_t)burst;
                }
                if (size > 2U) {
                    buf[2] = (uint8_t)(burst >> 8);
                }
                break;
            case FWTPM_TIS_BURST_COUNT:
                fwtpm_store_le(buf, size, burst);
                break;
            case FWTPM_TIS_BURST_COUNT + 1U:
                fwtpm_store_le(buf, size, burst >> 8);
                break;
            case FWTPM_TIS_DATA_FIFO:
                if (fwtpm_tis.rspPos + size > fwtpm_tis.rspSz) {
                    return TPM_RC_FAILURE;
                }
                XMEMCPY(buf, fwtpm_tis.rsp + fwtpm_tis.rspPos, size);
                fwtpm_tis.rspPos += size;
                if (fwtpm_tis.rspPos >= fwtpm_tis.rspSz) {
                    fwtpm_tis.status = FWTPM_STS_VALID | FWTPM_STS_COMMAND_READY;
                }
                break;
            case FWTPM_TIS_INTF_CAPS:
                fwtpm_store_le(buf, size, 0x00000183U);
                break;
            case FWTPM_TIS_DID_VID:
                fwtpm_store_le(buf, size, 0x00011B4EU);
                break;
            case FWTPM_TIS_RID:
                buf[0] = 0x01U;
                break;
            default:
                XMEMSET(buf, 0, size);
                break;
        }
    }
    else {
        switch (off) {
            case FWTPM_TIS_ACCESS:
                if ((buf[0] & FWTPM_ACCESS_REQUEST_USE) != 0U) {
                    fwtpm_tis.locality = 0;
                }
                break;
            case FWTPM_TIS_STS:
                if ((buf[0] & FWTPM_STS_COMMAND_READY) != 0U) {
                    fwtpm_tis.cmdSz = 0;
                    fwtpm_tis.rspSz = 0;
                    fwtpm_tis.rspPos = 0;
                    fwtpm_tis.status = FWTPM_STS_VALID |
                        FWTPM_STS_COMMAND_READY;
                }
                else if ((buf[0] & FWTPM_STS_GO) != 0U) {
                    return fwtpm_execute();
                }
                break;
            case FWTPM_TIS_DATA_FIFO:
                if (fwtpm_tis.cmdSz + size > sizeof(fwtpm_tis.cmd)) {
                    return TPM_RC_FAILURE;
                }
                XMEMCPY(fwtpm_tis.cmd + fwtpm_tis.cmdSz, buf, size);
                fwtpm_tis.cmdSz += size;
                if (fwtpm_tis.cmdSz < fwtpm_cmd_size()) {
                    fwtpm_tis.status = FWTPM_STS_VALID | FWTPM_STS_DATA_EXPECT;
                }
                else {
                    fwtpm_tis.status = FWTPM_STS_VALID;
                }
                break;
            default:
                break;
        }
    }

    return TPM_RC_SUCCESS;
}

int TPM2_IoCb(TPM2_CTX *ctx, INT32 isRead, UINT32 addr, BYTE *buf,
    UINT16 size, void *userCtx)
{
    return TPM2_IoCb_FwtpmNsc(ctx, isRead, addr, buf, size, userCtx);
}

#endif /* WOLFBOOT_TZ_FWTPM */
