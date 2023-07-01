/* preseal.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>
#include <hal/tpm_io.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DEFAULT_PCR_INDEX 16

static int readFile(char* name, uint8_t* buf, uint32_t* bufSz)
{
    int ret;
    int fd;
    struct stat st;

    /* check size */
    ret = stat(name, &st);

    if (ret == 0) {
        if (st.st_size > *bufSz)
            return INPUT_SIZE_E;

        fd = open(name, O_RDONLY);

        if (fd < 0)
            ret = fd;
    }

    /* read the contents */
    if (ret == 0) {
        ret = read(fd, buf, st.st_size);

        if (ret >= 0) {
            *bufSz = ret;
            ret = 0;
        }

        close(fd);
    }

    return ret;
}

static signed char HexCharToByte(signed char ch)
{
    signed char ret = (signed char)ch;
    if (ret >= '0' && ret <= '9')
        ret -= '0';
    else if (ret >= 'A' && ret <= 'F')
        ret -= 'A' - 10;
    else if (ret >= 'a' && ret <= 'f')
        ret -= 'a' - 10;
    else
        ret = -1; /* error case - return code must be signed */
    return ret;
}

static int HexToByte(const char *hex, unsigned char *output, unsigned long sz)
{
    word32 i;
    for (i = 0; i < sz; i++) {
        signed char ch1, ch2;
        ch1 = HexCharToByte(hex[i * 2]);
        ch2 = HexCharToByte(hex[i * 2 + 1]);
        if ((ch1 < 0) || (ch2 < 0)) {
            return -1;
        }
        output[i] = (unsigned char)((ch1 << 4) + ch2);
    }
    return (int)sz;
}

static void usage()
{
    printf("NOTE currently policy sealing only supports ecc256 keys");
    printf("Expected usage: ./preseal pubkey policypubkey policysignature imagedigest sealNVindex digestNVindex [pcrindex]\n");
    printf("pubkey: the verification key to seal into the tpm\n");
    printf("policypubkey: the pubkey used sign the policy expiration date\n");
    printf("policysignature: the signature of the policy expiration date\n");
    printf("imagedigest: the digest of the image that this pubkey verifies\n");
    printf("sealNVindex: the NV index to seal the pubkey to\n");
    printf("digestNVindex: the NV index to seal the policyDigest to\n");
    printf("pcrindex: the pcrindex to extend with the imagedigest, defaults to 16\n");
}

int main(int argc, char** argv)
{
    int rc = -1;
    WOLFTPM2_DEV dev;
    WOLFTPM2_SESSION tpmSession;
    WOLFTPM2_KEY authKey;
    PCR_Reset_In pcrReset;
    /* default to aes since param encryption is required */
    TPM_ALG_ID paramEncAlg = TPM_ALG_CFB;
    uint8_t pcrArray[48];
    uint32_t pcrArraySz = 1;
    uint8_t pubkey[ECC_MAXSIZE];
    uint32_t pubkeySz = ECC_MAXSIZE;
    uint8_t policyPubkey[ECC_MAXSIZE];
    uint32_t policyPubkeySz = ECC_MAXSIZE;
    uint8_t policySigned[ECC_MAXSIZE];
    uint32_t policySignedSz = ECC_MAXSIZE;
    uint8_t imageDigest[WC_MAX_DIGEST_SIZE];
    uint32_t imageDigestSz = WC_MAX_DIGEST_SIZE;
    uint32_t sealNvIndex;
    uint32_t policyDigestNvIndex;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    XMEMSET(&dev, 0, sizeof(WOLFTPM2_DEV));
    XMEMSET(&tpmSession, 0, sizeof(WOLFTPM2_SESSION));
    XMEMSET(&authKey, 0, sizeof(WOLFTPM2_KEY));
    XMEMSET(&pcrReset, 0, sizeof(PCR_Reset_In));

#ifndef NO_FILESYSTEM
    if (argc < 7) {
        usage();
        return 0;
    }

    rc = readFile(argv[1], pubkey, &pubkeySz);
    if (rc != 0) {
        printf("Failed to read pubkey\n");
        return 1;
    }

    rc = readFile(argv[2], policyPubkey, &policyPubkeySz);
    if (rc != 0) {
        printf("Failed to read policypubkey\n");
        return 1;
    }

    rc = readFile(argv[3], policySigned, &policySignedSz);
    if (rc != 0) {
        printf("Failed to read policysignature\n");
        return 1;
    }

    rc = readFile(argv[4], imageDigest, &imageDigestSz);
    if (rc != 0) {
        printf("Failed to read imagedigest\n");
        return 1;
    }

    sealNvIndex = atoi(argv[5]);
    policyDigestNvIndex = atoi(argv[6]);

    /* TODO change this to a loop when multiple pcr's are supported */
    if (argc > 7 )
        pcrArray[0] = atoi(argv[7]);
    else
        pcrArray[0] = DEFAULT_PCR_INDEX;
#else
    rc = HexToByte(PUBKEY, pubkey, strlen(PUBKEY) / 2);
    if (rc < 0) {
        printf("Failed to read pubkey\n");
        return 1;
    }
    pubkeySz = strlen(PUBKEY) / 2;

    rc = HexToByte(POLICY_PUBKEY, policyPubkey, strlen(POLICY_PUBKEY) / 2);
    if (rc < 0) {
        printf("Failed to read pubkey\n");
        return 1;
    }
    policyPubkeySz = strlen(POLICY_PUBKEY) / 2;

    rc = HexToByte(POLICY_SIGNED, policySigned, strlen(POLICY_SIGNED) / 2);
    if (rc < 0) {
        printf("Failed to read pubkey\n");
        return 1;
    }
    policySignedSz = strlen(POLICY_SIGNED) / 2;

    rc = HexToByte(IMAGE_DIGEST, imageDigest, strlen(IMAGE_DIGEST) / 2);
    if (rc < 0) {
        printf("Failed to read pubkey\n");
        return 1;
    }
    imageDigestSz = strlen(IMAGE_DIGEST) / 2;

    sealNvIndex = SEAL_NV_INDEX;
    policyDigestNvIndex = POLICY_DIGEST_NV_INDEX;

    #ifdef PCR_INDEX
        pcrArray[0] = PCR_INDEX;
    #else
        pcrArray[0] = DEFAULT_PCR_INDEX;
    #endif
#endif

#ifdef SIM
    rc = wolfTPM2_Init(&dev, NULL, NULL);
#else
    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
#endif

    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_Init failed 0x%x: %s\n", rc, TPM2_GetRCString(rc));
        goto exit;
    }

    pcrReset.pcrHandle = pcrArray[0];

    rc = TPM2_PCR_Reset(&pcrReset);
    if (rc != TPM_RC_SUCCESS) {
        printf("TPM2_PCR_Reset failed 0x%x: %s\n", rc, TPM2_GetRCString(rc));
        goto exit;
    }

    rc = wolfTPM2_ExtendPCR(&dev, pcrArray[0], TPM_ALG_SHA256,
        imageDigest, imageDigestSz);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_ExtendPCR failed 0x%x: %s\n", rc, TPM2_GetRCString(rc));
        goto exit;
    }

    rc = wolfTPM2_StartSession(&dev, &tpmSession, NULL, NULL,
        TPM_SE_POLICY, paramEncAlg);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_StartSession failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }

    rc = wolfTPM2_SetAuthSession(&dev, 0, &tpmSession,
        (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
        TPMA_SESSION_continueSession));
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_SetAuthSession failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }

    rc = wolfTPM2_LoadEccPublicKey(&dev, (WOLFTPM2_KEY*)&authKey,
        TPM_ECC_NIST_P256, policyPubkey, 32, policyPubkey + 32, 32);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_LoadEccPublicKey failed\n");
        goto exit;
    }

    rc = wolfTPM2_SealWithAuthSigNV(&dev, &authKey, &tpmSession, TPM_ALG_SHA256,
        TPM_ALG_SHA256, (word32*)pcrArray, pcrArraySz, pubkey, pubkeySz, NULL,
        0, policySigned, policySignedSz, sealNvIndex, policyDigestNvIndex);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_SealWithAuthPolicyNV failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }

exit:
    if (rc != 0) {
        printf("Failure 0x%x: %s\n", rc, wolfTPM2_GetRCString(rc));
    }

    wolfTPM2_UnloadHandle(&dev, &authKey.handle);
    wolfTPM2_UnloadHandle(&dev, &tpmSession.handle);

    wolfTPM2_Cleanup(&dev);

    return rc;
}
