/* boot_state_tpm.c
 *
 * TPM NV backend for the anti-rollback reference and boot audit counters.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

/* TPM NV storage backend for src/boot_state.c; another root of trust can
 * replace this file without touching the policy or src/tpm.c. */

#include "user_settings.h"

#ifdef WOLFBOOT_ANTI_ROLLBACK

#include "boot_state.h"
#include "tpm.h"
#include <wolftpm/tpm2_wrap.h>
#include <string.h>

#define BOOT_STATE_NV_WRITE_MASK (TPMA_NV_PLATFORMCREATE | TPMA_NV_PPWRITE | \
    TPMA_NV_OWNERWRITE | TPMA_NV_AUTHWRITE | TPMA_NV_POLICYWRITE | \
    TPMA_NV_TPM_NT)
#define BOOT_STATE_NV_WRITE_ATTR (TPMA_NV_PLATFORMCREATE | TPMA_NV_PPWRITE)

int boot_state_backend_read(uint8_t *buf, uint32_t len)
{
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    WOLFTPM2_NV nv;
    word32 readSz;
    int rc;

    memset(&nv, 0, sizeof(nv));
    nv.handle.hndl = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;

    /* Only an undefined index (format-1 TPM_RC_HANDLE) is unprovisioned; any
     * other error must not reset the floor through trust-on-first-use. */
    rc = wolfTPM2_NVOpen(dev, &nv, WOLFBOOT_TPM_BOOT_STATE_NV_BASE, NULL, 0);
    if (rc != 0) {
        if ((rc & RC_FMT1) && ((rc & RC_MAX_FMT1) == TPM_RC_HANDLE)) {
            return BOOT_STATE_UNPROVISIONED;
        }
        return BOOT_STATE_UNAVAILABLE;
    }
    /* Only an index this backend created can hold the floor: platform-created
     * and writable by the platform hierarchy alone. */
    if ((nv.attributes & BOOT_STATE_NV_WRITE_MASK) != BOOT_STATE_NV_WRITE_ATTR) {
        return BOOT_STATE_CORRUPT;
    }

    wolfTPM2_SetAuthHandle(dev, 0, &nv.handle);
    readSz = len;
    rc = wolfTPM2_NVReadAuth(dev, &nv, WOLFBOOT_TPM_BOOT_STATE_NV_BASE,
                             buf, &readSz, 0);
    /* Clear the auth slot the read set, so it does not carry into later ops. */
    wolfTPM2_UnsetAuth(dev, 0);
    /* An index smaller than the record (TPM_RC_NV_RANGE) is malformed:
     * corrupt, which halts, not unavailable. */
    if (rc == TPM_RC_NV_RANGE) {
        return BOOT_STATE_CORRUPT;
    }
    if (rc != 0) {
        return BOOT_STATE_UNAVAILABLE;
    }
    if (readSz != len) {
        return BOOT_STATE_CORRUPT;
    }
    return 0;
}

int boot_state_backend_write(const uint8_t *buf, uint32_t len)
{
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    WOLFTPM2_HANDLE parent;
    WOLFTPM2_NV nv;
    NV_Write_In in;
    word32 nvAttributes = 0;
    int rc;

    if (len > sizeof(in.data.buffer)) {
        return -1;
    }
    memset(&parent, 0, sizeof(parent));
    memset(&nv, 0, sizeof(nv));
    parent.hndl = TPM_RH_PLATFORM;
    nv.handle.hndl = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;

    wolfTPM2_GetNvAttributesTemplate(TPM_RH_PLATFORM, &nvAttributes);
    nvAttributes &= ~TPMA_NV_AUTHWRITE;

    /* Created on first write (trust-on-first-use). No TPMA_NV_WRITEDEFINE:
     * the record is rewritten as versions change. */
    rc = wolfTPM2_NVCreateAuth(dev, &parent, &nv,
                               WOLFBOOT_TPM_BOOT_STATE_NV_BASE,
                               nvAttributes, len, NULL, 0);
    if (rc == TPM_RC_NV_DEFINED) {
        rc = 0;
    }
    wolfTPM2_UnsetAuth(dev, 1);
    if (rc != 0) {
        return -1;
    }

    /* TPMA_NV_PPWRITE: the platform authorizes the write. Its auth is empty
     * until boot_state_backend_lock() disables the hierarchy. */
    memset(&in, 0, sizeof(in));
    in.authHandle = TPM_RH_PLATFORM;
    in.nvIndex = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;
    in.data.size = (UINT16)len;
    memcpy(in.data.buffer, buf, len);
    wolfTPM2_SetAuthPassword(dev, 0, NULL);
    rc = TPM2_NV_Write(&in);
    wolfTPM2_UnsetAuth(dev, 0);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

int boot_state_backend_lock(void)
{
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    HierarchyControl_In in;
    int rc;

    /* Disable the platform hierarchy until the next Startup(CLEAR) (TCG PC
     * Client Platform spec, section 10): the OS cannot write or undefine. */
    memset(&in, 0, sizeof(in));
    in.authHandle = TPM_RH_PLATFORM;
    in.enable = TPM_RH_PLATFORM;
    in.state = 0;
    wolfTPM2_SetAuthPassword(dev, 0, NULL);
    rc = TPM2_HierarchyControl(&in);
    wolfTPM2_UnsetAuth(dev, 0);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

#endif /* WOLFBOOT_ANTI_ROLLBACK */
