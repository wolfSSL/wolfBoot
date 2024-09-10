/* policy_create.c
 *
 * Copyright (C) 2006-2023 wolfSSL Inc.
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

/* Tool for creating a policy digest file that is then signed by the key tool
 * and included in the image header using HDR_POLICY_SIGNATURE.
 */


#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>
#include <hal/tpm_io.h>
#include "keystore.h"
#include "tpm.h"

#define DEFAULT_PCR 16

static void usage(void)
{
    printf("Expected usage:\n");
    printf("./examples/pcr/policy_create [-pcr=/-pcrmask] [-pcrdigest=] [-out=]\n");
    printf("* -pcr=index: SHA2-256 PCR index < 24 (multiple can be supplied) (default %d)\n", DEFAULT_PCR);
    printf("* -pcrmask=0x00000000: PCR mask (or -pcr= args)\n");
    printf("* -pcrdigest=hexstr: PCR Digest (default=Read actual PCR's)\n");
    printf("* -out=file: Policy Digest to sign (default policy.bin)\n");
}

int writeBin(const char* filename, const uint8_t*buf, word32 bufSz)
{
    int rc = TPM_RC_FAILURE;

    if (filename == NULL || buf == NULL)
        return BAD_FUNC_ARG;

    XFILE fp = NULL;
    size_t fileSz = 0;

    fp = XFOPEN(filename, "wt");
    if (fp != XBADFILE) {
        fileSz = XFWRITE(buf, 1, bufSz, fp);
        /* sanity check */
        if (fileSz == (word32)bufSz) {
            rc = TPM_RC_SUCCESS;
        }
        printf("Wrote %d bytes to %s\n", (int)fileSz, filename);
        XFCLOSE(fp);
    }
    return rc;
}

static signed char hexCharToByte(signed char ch)
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
static int hexToByte(const char *hex, unsigned char *output, unsigned long sz)
{
    int outSz = 0;
    word32 i;
    for (i = 0; i < sz; i+=2) {
        signed char ch1, ch2;
        ch1 = hexCharToByte(hex[i]);
        ch2 = hexCharToByte(hex[i+1]);
        if ((ch1 < 0) || (ch2 < 0)) {
            return -1;
        }
        output[outSz++] = (unsigned char)((ch1 << 4) + ch2);
    }
    return outSz;
}
static void printHexString(const unsigned char* bin, unsigned long sz,
    unsigned long maxLine)
{
    unsigned long i;
    printf("\t");
    if (maxLine == 0) maxLine = sz;
    for (i = 0; i < sz; i++) {
        printf("%02x", bin[i]);
        if (((i+1) % maxLine) == 0 && i+1 != sz)
            printf("\n\t");
    }
    printf("\n");
}

uint32_t wolfBoot_tpm_pcrmask_sel(uint32_t pcrMask, uint8_t* pcrArray,
    uint32_t pcrArraySz)
{
    int i;
    uint32_t pcrArraySzAct = 0;
    for (i=0; i<IMPLEMENTATION_PCR; i++) {
        if (pcrMask & (1 << i)) {
            /* add if we have room */
            if (pcrArraySzAct < pcrArraySz) {
                pcrArray[pcrArraySzAct++] = i;
            }
        }
    }
    return pcrArraySzAct;
}

int TPM2_PCR_Policy_Create(TPM_ALG_ID pcrAlg,
    uint8_t* pcrArray, word32 pcrArraySz,
    const char* outFile,
    uint8_t* pcrDigest, word32 pcrDigestSz,
    uint8_t* policyRef, word32 policyRefSz)
{
    int rc = -1, i;
    word32 pcrMask = 0;
    byte policy[sizeof(pcrMask) + WC_MAX_DIGEST_SIZE]; /* pcrmask + digest */
    word32 policySz = 0;
    byte* digest = &policy[sizeof(pcrMask)];
    word32 digestSz;

    printf("Policy Create Tool\n");

    /* calculate pcrMask */
    printf("PCR Index(s) (%s): ", TPM2_GetAlgName(pcrAlg));
    for (i = 0; i < (int)pcrArraySz; i++) {
        printf("%d ", pcrArray[i]);
        pcrMask |= (1 << pcrArray[i]);
    }
    printf(" (mask 0x%08x)\n", pcrMask);
    XMEMSET(policy, 0, sizeof(policy));
    XMEMCPY(policy, &pcrMask, sizeof(pcrMask));
    policySz += (word32)sizeof(pcrMask);

    /* PCR Hash - Use provided hash or read PCR's and get hash */
    if (pcrDigestSz == 0) {
        WOLFTPM2_DEV dev;
        XMEMSET(&dev, 0, sizeof(WOLFTPM2_DEV));
        rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
        if (rc == 0) {
            rc = wolfTPM2_PCRGetDigest(&dev, pcrAlg, pcrArray, pcrArraySz,
                pcrDigest, &pcrDigestSz);
            wolfTPM2_Cleanup(&dev);
        }
        if (rc != TPM_RC_SUCCESS) {
            printf("Error getting PCR's! 0x%x: %s\n", rc, TPM2_GetRCString(rc));
            goto exit;
        }
    }
    printf("PCR Digest (%d bytes):\n", pcrDigestSz);
    printHexString(pcrDigest, pcrDigestSz, 0);

    /* Build PCR Policy to Sign */
    digestSz = TPM2_GetHashDigestSize(pcrAlg);
    /* Note: digest is used an input here to allow chaining policies,
     * but for this use-case should be zero'd */
    rc = wolfTPM2_PolicyPCRMake(pcrAlg, pcrArray, pcrArraySz,
        pcrDigest, pcrDigestSz, digest, &digestSz);
    if (rc == 0) {
        /* Add policyRef (if blank just re-hash) */
        rc = wolfTPM2_PolicyRefMake(pcrAlg, digest, &digestSz,
            policyRef, policyRefSz);
    }
    if (rc == 0) {
        policySz += digestSz;

        printf("PCR Mask (0x%08x) and PCR Policy Digest (%d bytes):\n",
            pcrMask, digestSz);
        printHexString(digest, digestSz, 0);

        /* Write pcrMask (4) and digest */
        writeBin(outFile, policy, policySz);
    }

exit:
    if (rc != 0) {
        printf("Failure 0x%x: %s\n", rc, wolfTPM2_GetRCString(rc));
    }

    return rc;
}

int main(int argc, char *argv[])
{
    TPM_ALG_ID pcrAlg = WOLFBOOT_TPM_PCR_ALG;
    byte pcrArray[PCR_SELECT_MAX*2];
    word32 pcrArraySz = 0;
    const char* outFile = "policy.bin";
    byte pcrDigest[WC_MAX_DIGEST_SIZE];
    word32 pcrDigestSz = 0;
    uint8_t* policyRef = NULL; /* optional nonce */
    word32 policyRefSz = 0;
    word32 pcrMask = 0;

    if (argc >= 2) {
        if (XSTRCMP(argv[1], "-?") == 0 ||
            XSTRCMP(argv[1], "-h") == 0 ||
            XSTRCMP(argv[1], "--help") == 0) {
            usage();
            return 0;
        }
    }
    while (argc > 1) {
        if (XSTRNCMP(argv[argc-1], "-pcr=", XSTRLEN("-pcr=")) == 0) {
            const char* pcrStr = argv[argc-1] + XSTRLEN("-pcr=");
            byte pcrIndex = (byte)XATOI(pcrStr);
            if (pcrIndex > PCR_LAST) {
                printf("PCR index is out of range (0-23)\n");
                usage();
                return 0;
            }
            pcrArray[pcrArraySz++] = pcrIndex;
        }
        else if (XSTRNCMP(argv[argc-1], "-pcrmask=", XSTRLEN("-pcrmask=")) == 0) {
            const char* pcrMaskStr = argv[argc-1] + XSTRLEN("-pcrmask=");
            pcrMask = (word32)XSTRTOL(pcrMaskStr, NULL, 0);
        }
        else if (XSTRNCMP(argv[argc-1], "-pcrdigest=", XSTRLEN("-pcrdigest=")) == 0) {
            const char* hashHexStr = argv[argc-1] + XSTRLEN("-pcrdigest=");
            int hashHexStrLen = (int)XSTRLEN(hashHexStr);
            if (hashHexStrLen > (int)sizeof(pcrDigest)*2+1)
                pcrDigestSz = -1;
            else
                pcrDigestSz = hexToByte(hashHexStr, pcrDigest, hashHexStrLen);
            if (pcrDigestSz <= 0) {
                fprintf(stderr, "Invalid PCR hash length\n");
                usage();
                return -1;
            }
        }
        else if (XSTRNCMP(argv[argc-1], "-out=",
                XSTRLEN("-out=")) == 0) {
            outFile = argv[argc-1] + XSTRLEN("-out=");
        }
        else {
            printf("Warning: Unrecognized option: %s\n", argv[argc-1]);
        }
        argc--;
    }

    /* Determine PCR's based on mask or default (if none set) */
    if (pcrArraySz == 0) {
        if (pcrMask == 0) {
            pcrArray[pcrArraySz++] = DEFAULT_PCR;
        }
        else {
            pcrArraySz = wolfBoot_tpm_pcrmask_sel(pcrMask,
                pcrArray, sizeof(pcrArray));
        }
    }

    return TPM2_PCR_Policy_Create(pcrAlg,
        pcrArray, pcrArraySz, outFile,
        pcrDigest, pcrDigestSz, policyRef, policyRefSz);
}
