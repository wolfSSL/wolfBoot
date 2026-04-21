/* ftpm_stub.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFBOOT_TZ_FTPM

#include <stdint.h>
#include <string.h>

#include "wolfboot/wcs_ftpm.h"
#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_tis.h"

#define FTPM_TIS_BASE          0xD40000U
#define FTPM_TIS_ACCESS        0x0000U
#define FTPM_TIS_INTF_CAPS     0x0014U
#define FTPM_TIS_STS           0x0018U
#define FTPM_TIS_BURST_COUNT   0x0019U
#define FTPM_TIS_DATA_FIFO     0x0024U
#define FTPM_TIS_DID_VID       0x0F00U
#define FTPM_TIS_RID           0x0F04U

#define FTPM_ACCESS_VALID            0x80U
#define FTPM_ACCESS_ACTIVE_LOCALITY  0x20U
#define FTPM_ACCESS_REQUEST_USE      0x02U

#define FTPM_STS_VALID          0x80U
#define FTPM_STS_COMMAND_READY  0x40U
#define FTPM_STS_GO             0x20U
#define FTPM_STS_DATA_AVAIL     0x10U
#define FTPM_STS_DATA_EXPECT    0x08U

#define FTPM_BURST_COUNT 64U

struct ftpm_tis_state {
    uint8_t cmd[WCS_FTPM_MAX_COMMAND_SIZE];
    uint8_t rsp[WCS_FTPM_MAX_COMMAND_SIZE];
    uint32_t cmdSz;
    uint32_t rspSz;
    uint32_t rspPos;
    uint8_t locality;
    uint8_t status;
};

static struct ftpm_tis_state ftpm_tis = {
    {0}, {0}, 0, 0, 0, 0,
    FTPM_STS_VALID | FTPM_STS_COMMAND_READY
};

static uint32_t ftpm_reg_offset(uint32_t addr)
{
    return (addr - FTPM_TIS_BASE) & 0x0FFFU;
}

static void ftpm_store_le(uint8_t *buf, uint16_t size, uint32_t val)
{
    uint16_t i;

    for (i = 0; i < size; i++) {
        buf[i] = (uint8_t)(val >> (8U * i));
    }
}

static uint32_t ftpm_cmd_size(void)
{
    if (ftpm_tis.cmdSz < TPM2_HEADER_SIZE) {
        return TPM2_HEADER_SIZE;
    }

    return ((uint32_t)ftpm_tis.cmd[2] << 24) |
           ((uint32_t)ftpm_tis.cmd[3] << 16) |
           ((uint32_t)ftpm_tis.cmd[4] << 8) |
           (uint32_t)ftpm_tis.cmd[5];
}

static int ftpm_execute(void)
{
    int rc;

    ftpm_tis.rspSz = sizeof(ftpm_tis.rsp);
    ftpm_tis.rspPos = 0;

    rc = wcs_ftpm_transmit(ftpm_tis.cmd, ftpm_tis.cmdSz, ftpm_tis.rsp,
        &ftpm_tis.rspSz);
    ftpm_tis.cmdSz = 0;

    if (rc == TPM_RC_SUCCESS) {
        ftpm_tis.status = FTPM_STS_VALID | FTPM_STS_DATA_AVAIL;
    }
    else {
        ftpm_tis.rspSz = 0;
        ftpm_tis.status = FTPM_STS_VALID | FTPM_STS_COMMAND_READY;
    }

    return rc;
}

int TPM2_IoCb_FtpmNsc(TPM2_CTX *ctx, INT32 isRead, UINT32 addr,
    BYTE *buf, UINT16 size, void *userCtx)
{
    uint32_t off;
    uint16_t burst;

    (void)ctx;
    (void)userCtx;

    if (buf == NULL || size == 0U || addr < FTPM_TIS_BASE) {
        return BAD_FUNC_ARG;
    }

    off = ftpm_reg_offset(addr);
    burst = FTPM_BURST_COUNT;

    if (isRead) {
        switch (off) {
            case FTPM_TIS_ACCESS:
                buf[0] = FTPM_ACCESS_VALID | FTPM_ACCESS_ACTIVE_LOCALITY;
                break;
            case FTPM_TIS_STS:
                XMEMSET(buf, 0, size);
                buf[0] = ftpm_tis.status;
                if (size > 1U) {
                    buf[1] = (uint8_t)burst;
                }
                if (size > 2U) {
                    buf[2] = (uint8_t)(burst >> 8);
                }
                break;
            case FTPM_TIS_BURST_COUNT:
                ftpm_store_le(buf, size, burst);
                break;
            case FTPM_TIS_BURST_COUNT + 1U:
                ftpm_store_le(buf, size, burst >> 8);
                break;
            case FTPM_TIS_DATA_FIFO:
                if (ftpm_tis.rspPos + size > ftpm_tis.rspSz) {
                    return TPM_RC_FAILURE;
                }
                XMEMCPY(buf, ftpm_tis.rsp + ftpm_tis.rspPos, size);
                ftpm_tis.rspPos += size;
                if (ftpm_tis.rspPos >= ftpm_tis.rspSz) {
                    ftpm_tis.status = FTPM_STS_VALID | FTPM_STS_COMMAND_READY;
                }
                break;
            case FTPM_TIS_INTF_CAPS:
                ftpm_store_le(buf, size, 0x00000183U);
                break;
            case FTPM_TIS_DID_VID:
                ftpm_store_le(buf, size, 0x00011B4EU);
                break;
            case FTPM_TIS_RID:
                buf[0] = 0x01U;
                break;
            default:
                XMEMSET(buf, 0, size);
                break;
        }
    }
    else {
        switch (off) {
            case FTPM_TIS_ACCESS:
                if ((buf[0] & FTPM_ACCESS_REQUEST_USE) != 0U) {
                    ftpm_tis.locality = 0;
                }
                break;
            case FTPM_TIS_STS:
                if ((buf[0] & FTPM_STS_COMMAND_READY) != 0U) {
                    ftpm_tis.cmdSz = 0;
                    ftpm_tis.rspSz = 0;
                    ftpm_tis.rspPos = 0;
                    ftpm_tis.status = FTPM_STS_VALID |
                        FTPM_STS_COMMAND_READY;
                }
                else if ((buf[0] & FTPM_STS_GO) != 0U) {
                    return ftpm_execute();
                }
                break;
            case FTPM_TIS_DATA_FIFO:
                if (ftpm_tis.cmdSz + size > sizeof(ftpm_tis.cmd)) {
                    return TPM_RC_FAILURE;
                }
                XMEMCPY(ftpm_tis.cmd + ftpm_tis.cmdSz, buf, size);
                ftpm_tis.cmdSz += size;
                if (ftpm_tis.cmdSz < ftpm_cmd_size()) {
                    ftpm_tis.status = FTPM_STS_VALID | FTPM_STS_DATA_EXPECT;
                }
                else {
                    ftpm_tis.status = FTPM_STS_VALID;
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
    return TPM2_IoCb_FtpmNsc(ctx, isRead, addr, buf, size, userCtx);
}

#endif /* WOLFBOOT_TZ_FTPM */
