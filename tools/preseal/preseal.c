#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>

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

static void usage()
{
    printf("preseal pubkey policypubkey policysignature imagedigest sealNVindex digestNVindex [pcrindex]\n");
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
    /* default to aes since parm encryption is required */
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

    if (argc < 7) {
        usage();
        return 0;
    }

    XMEMSET(&dev, 0, sizeof(WOLFTPM2_DEV));
    XMEMSET(&tpmSession, 0, sizeof(WOLFTPM2_SESSION));
    XMEMSET(&authKey, 0, sizeof(WOLFTPM2_KEY));
    XMEMSET(&pcrReset, 0, sizeof(PCR_Reset_In));

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

    rc = wolfTPM2_Init(&dev, NULL, NULL);
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
