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

/*
 * This file is the whole of the storage dependency for anti-rollback. The
 * policy in src/boot_state.c reaches storage only through the two functions
 * below, so moving the version reference onto a root-of-trust device later is
 * a matter of building a different backend file instead of this one.
 *
 * It is kept out of src/tpm.c deliberately, so the TPM NV backend is a
 * self-contained translation unit that a different root-of-trust backend can
 * replace without touching the rest of the TPM code.
 */

#include "user_settings.h"

#ifdef WOLFBOOT_ANTI_ROLLBACK

#include "boot_state.h"
#include "tpm.h"
#include <wolftpm/tpm2_wrap.h>
#include <string.h>

int boot_state_backend_read(uint8_t *buf, uint32_t len)
{
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    WOLFTPM2_NV nv;
    word32 readSz;
    int rc;

    memset(&nv, 0, sizeof(nv));
    nv.handle.hndl = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;

    /* A nonexistent index (normal first boot) must be told apart from a TPM
     * error: treating an error as unprovisioned would reset the rollback floor
     * through trust-on-first-use. An undefined index is TPM_RC_HANDLE in
     * format-1 encoding, so mask with RC_MAX_FMT1 before comparing. */
    rc = wolfTPM2_NVOpen(dev, &nv, WOLFBOOT_TPM_BOOT_STATE_NV_BASE, NULL, 0);
    if (rc != 0) {
        if ((rc & RC_FMT1) && ((rc & RC_MAX_FMT1) == TPM_RC_HANDLE)) {
            return BOOT_STATE_UNPROVISIONED;
        }
        return BOOT_STATE_UNAVAILABLE;
    }

    wolfTPM2_SetAuthHandle(dev, 0, &nv.handle);
    readSz = len;
    rc = wolfTPM2_NVReadAuth(dev, &nv, WOLFBOOT_TPM_BOOT_STATE_NV_BASE,
                             buf, &readSz, 0);
    /* Clear the auth slot the read set, so it does not carry into later ops. */
    wolfTPM2_UnsetAuth(dev, 0);
    /* The index exists (NVOpen succeeded) but is smaller than the record: the
     * TPM refuses the read with TPM_RC_NV_RANGE. That is a malformed reference,
     * so report it as corrupt and let the policy halt, rather than unavailable,
     * which non-strict mode would wave through. */
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
    word32 nvAttributes = 0;
    int rc;

    memset(&parent, 0, sizeof(parent));
    memset(&nv, 0, sizeof(nv));
    parent.hndl = TPM_RH_PLATFORM;
    nv.handle.hndl = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;

    wolfTPM2_GetNvAttributesTemplate(TPM_RH_PLATFORM, &nvAttributes);
    /* Lockable by boot_state_backend_lock() until the next TPM reset. */
    nvAttributes |= TPMA_NV_WRITE_STCLEAR;

    /* Created on first write, which is what makes the trust-on-first-use
     * anchor work. TPMA_NV_WRITEDEFINE is deliberately not set: unlike a
     * sealed blob this record is rewritten as versions change. */
    rc = wolfTPM2_NVCreateAuth(dev, &parent, &nv,
                               WOLFBOOT_TPM_BOOT_STATE_NV_BASE,
                               nvAttributes, len, NULL, 0);
    if (rc == TPM_RC_NV_DEFINED) {
        rc = 0;
    }
    if (rc != 0) {
        return -1;
    }

    wolfTPM2_UnsetAuth(dev, 1);
    rc = wolfTPM2_NVWriteAuth(dev, &nv, WOLFBOOT_TPM_BOOT_STATE_NV_BASE,
                              (uint8_t *)buf, len, 0);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

int boot_state_backend_lock(void)
{
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    WOLFTPM2_NV nv;
    int rc;

    memset(&nv, 0, sizeof(nv));
    nv.handle.hndl = WOLFBOOT_TPM_BOOT_STATE_NV_BASE;
    /* The read path leaves no session in slot 0; the index's empty auth
     * authorizes the lock (TPMA_NV_AUTHWRITE). */
    wolfTPM2_SetAuthPassword(dev, 0, NULL);
    rc = wolfTPM2_NVWriteLock(dev, &nv);
    wolfTPM2_UnsetAuth(dev, 0);
    wolfTPM2_UnsetAuth(dev, 1);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

#endif /* WOLFBOOT_ANTI_ROLLBACK */
