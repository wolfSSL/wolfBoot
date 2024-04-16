/* rot.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>
#include <hal/tpm_io.h>
#include "keystore.h"
#include "tpm.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void usage(void)
{
    printf("Expected usage:\n");
    printf("./tools/tpm/rot [-nvbase] [-write] [-auth] [-sha384] [-lock]\n");
    printf("* -nvbase=[handle] (default 0x%x)\n", WOLFBOOT_TPM_KEYSTORE_NV_BASE);
    printf("* -write: Using keystore.c API's hashes each public key and stores into NV\n");
    printf("* -auth=password: Optional password for NV\n");
    printf("* -sha384: Use SHA2-384 (default is SHA2-256)\n");
    printf("* -lock: Lock the write\n");
    printf("\nExamples:\n");
    printf("\t./tools/tpm/rot\n");
    printf("\t./tools/tpm/rot -write\n");
}

static int TPM2_Boot_SecureROT_Example(TPMI_RH_NV_AUTH authHandle, word32 nvBaseIdx,
    enum wc_HashType hashType, int doWrite, int doLock,
    const char *authBuf, int authBufSz)
{
    int rc;
    WOLFTPM2_DEV dev;
    WOLFTPM2_SESSION tpmSession;
    WOLFTPM2_HANDLE parent;
    WOLFTPM2_NV nv;
    TPMS_NV_PUBLIC nvPublic;
    word32 nvAttributes;
    /* always use AES CFB parameter encryption */
    int paramEncAlg = TPM_ALG_CFB;
    byte digest[WC_MAX_DIGEST_SIZE];
    int digestSz = 0;
    int id;

    XMEMSET(&tpmSession, 0, sizeof(tpmSession));
    XMEMSET(&parent, 0, sizeof(parent));
    XMEMSET(digest, 0, sizeof(digest));

    /* setup the parent handle OWNER/PLATFORM */
    parent.hndl = authHandle;

    #ifndef WOLFTPM_ADV_IO
    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    #else
    rc = wolfTPM2_Init(&dev, NULL, NULL);
    #endif
    if (rc != TPM_RC_SUCCESS) {
        printf("\nwolfTPM2_Init failed\n");
        goto exit;
    }

    /* Start TPM session for parameter encryption */
    printf("Parameter Encryption: Enabled %s and HMAC\n\n",
        TPM2_GetAlgName(paramEncAlg));
    rc = wolfTPM2_StartSession(&dev, &tpmSession, NULL, NULL,
            TPM_SE_HMAC, paramEncAlg);
    if (rc != 0) goto exit;
    printf("TPM2_StartAuthSession: sessionHandle 0x%x\n",
        (word32)tpmSession.handle.hndl);
    /* Set TPM session attributes for parameter encryption */
    rc = wolfTPM2_SetAuthSession(&dev, 1, &tpmSession,
        (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
         TPMA_SESSION_continueSession));
    if (rc != 0) goto exit;

    printf("NV Auth (%d)\n", authBufSz);
    TPM2_PrintBin((const uint8_t*)authBuf, authBufSz);

    for (id = 0; id < keystore_num_pubkeys(); id++) {
        TPM_HANDLE handle = nvBaseIdx + id;
        uint32_t keyType = keystore_get_key_type(id);
        int bufSz = keystore_get_size(id);
        uint8_t*buf = keystore_get_buffer(id);

        (void)keyType; /* not used */

        printf("Computing keystore hash for index %d\n", id);

        printf("Public Key (%d)\n", bufSz);
        TPM2_PrintBin(buf, bufSz);

        /* hash public key */
        digestSz = wc_HashGetDigestSize(hashType);
        rc = wc_Hash(hashType, buf, (word32)bufSz, digest, digestSz);
        if (rc == 0) {
            printf("Public Key Hash (%d)\n", digestSz);
            TPM2_PrintBin(digest, digestSz);
        }
        if (rc == 0 && doWrite) {
            printf("Storing hash of keystore.c %d to NV index 0x%x\n",
                id, handle);

            /* Get NV attributes */
            rc = wolfTPM2_GetNvAttributesTemplate(parent.hndl, &nvAttributes);
            if (rc == 0) {
                /* allow this NV to be locked */
                nvAttributes |= TPMA_NV_WRITEDEFINE;

                /* Create NV - NV struct populated */
                rc = wolfTPM2_NVCreateAuth(&dev, &parent, &nv, handle,
                    nvAttributes, digestSz, (const uint8_t*)authBuf, authBufSz);
                if (rc == TPM_RC_NV_DEFINED) {
                    printf("Warning: NV Index 0x%x already exists!\n", handle);
                    rc = 0;
                }
            }
            if (rc == 0) {
                /* Write digest to NV */
                rc = wolfTPM2_NVWriteAuth(&dev, &nv, handle, digest, digestSz, 0);
            }
            if (rc == 0) {
                printf("Wrote %d bytes to NV 0x%x\n", digestSz, handle);
            }
        }

        /* Setup a read/lock structure */
        XMEMSET(&nv, 0, sizeof(nv));
        nv.handle.hndl = handle;
        nv.handle.auth.size = authBufSz;
        XMEMCPY(nv.handle.auth.buffer, authBuf, nv.handle.auth.size);

        if (rc == 0) {
            /* Read the NV Index publicArea to have up to date NV Index Name */
            rc = wolfTPM2_NVReadPublic(&dev, nv.handle.hndl, &nvPublic);
        }
        if (rc == 0) {
            digestSz = nvPublic.dataSize;

            /* Read access */
            printf("Reading NV 0x%x public key hash\n", nv.handle.hndl);
            rc = wolfTPM2_NVReadAuth(&dev, &nv, nv.handle.hndl,
                digest, (word32*)&digestSz, 0);
        }
        if (rc == 0) {
            printf("Read Public Key Hash (%d)\n", digestSz);
            TPM2_PrintBin(digest, digestSz);
        }
        else if ((rc & RC_MAX_FMT1) == TPM_RC_HANDLE) {
            printf("NV index does not exist\n");
        }

        if (rc == 0 && doLock) {
            printf("Locking NV index 0x%x\n", nv.handle.hndl);
            rc = wolfTPM2_NVWriteLock(&dev, &nv);
            if (rc == 0) {
                printf("NV 0x%x locked\n", nv.handle.hndl);
            }
        }

        if (rc != 0) goto exit;
    }

exit:

    if (rc != 0) {
        printf("\nFailure 0x%x: %s\n\n", rc, wolfTPM2_GetRCString(rc));
    }

    wolfTPM2_UnloadHandle(&dev, &tpmSession.handle);
    wolfTPM2_Cleanup(&dev);

    return rc;
}

int main(int argc, char *argv[])
{
    /* use platform handle to prevent TPM2_Clear from removing */
    TPMI_RH_NV_AUTH authHandle = TPM_RH_PLATFORM;
    word32 nvBaseIdx = WOLFBOOT_TPM_KEYSTORE_NV_BASE;
    int doWrite = 0, doLock = 0;
    enum wc_HashType hashType = WC_HASH_TYPE_SHA256;
    const char* authBuf = NULL;
    int authBufSz = 0;

    if (argc >= 2) {
        if (XSTRCMP(argv[1], "-?") == 0 ||
            XSTRCMP(argv[1], "-h") == 0 ||
            XSTRCMP(argv[1], "--help") == 0) {
            usage();
            return 0;
        }
    }
    while (argc > 1) {
        if (XSTRNCMP(argv[argc-1], "-nvbase=", XSTRLEN("-nvbase=")) == 0) {
            const char* nvBaseIdxStr = argv[argc-1] + XSTRLEN("-nvbase=");
            nvBaseIdx = (word32)XSTRTOL(nvBaseIdxStr, NULL, 0);
            if (!(authHandle == TPM_RH_PLATFORM && (
                    nvBaseIdx > TPM_20_PLATFORM_MFG_NV_SPACE &&
                    nvBaseIdx < TPM_20_OWNER_NV_SPACE)) &&
                !(authHandle == TPM_RH_OWNER && (
                    nvBaseIdx > TPM_20_OWNER_NV_SPACE &&
                    nvBaseIdx < TPM_20_TCG_NV_SPACE)))
            {
                fprintf(stderr, "Invalid NV Index %s\n", nvBaseIdxStr);
                fprintf(stderr, "\tPlatform Range: 0x%x -> 0x%x\n",
                    TPM_20_PLATFORM_MFG_NV_SPACE, TPM_20_OWNER_NV_SPACE);
                fprintf(stderr, "\tOwner Range: 0x%x -> 0x%x\n",
                    TPM_20_OWNER_NV_SPACE, TPM_20_TCG_NV_SPACE);
                usage();
                return -1;
            }
        }
        else if (XSTRNCMP(argv[argc-1], "-auth=", XSTRLEN("-auth=")) == 0) {
            authBuf = argv[argc-1] + XSTRLEN("-auth=");
            authBufSz = (int)XSTRLEN(authBuf);
        }
        else if (XSTRCMP(argv[argc-1], "-sha384") == 0) {
            hashType = WC_HASH_TYPE_SHA384;
        }
        else if (XSTRCMP(argv[argc-1], "-write") == 0) {
            doWrite = 1;
        }
        else if (XSTRCMP(argv[argc-1], "-lock") == 0) {
            doLock = 1;
        }
        else {
            printf("Warning: Unrecognized option: %s\n", argv[argc-1]);
        }
        argc--;
    };

    return TPM2_Boot_SecureROT_Example(
        authHandle,
        nvBaseIdx,
        hashType,
        doWrite,
        doLock,
        authBuf, authBufSz);
}
