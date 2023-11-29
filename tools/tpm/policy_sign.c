/* policy_sign.c
 *
 * Copyright (C) 2006-2023 wolfSSL Inc.
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

/* Standalone tool to create a singed policy from a pcr digest. Based on
 * wolfTPM/example/pcr/policy_sign.c
 */



#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>
#include "tpm.h"

/* Default PCR (test) */
#define DEFAULT_PCR 16

/* Prefer SHA2-256 for PCR's, and all TPM 2.0 devices support it */
#define USE_PCR_ALG   TPM_ALG_SHA256

static void usage(void)
{
    printf("Expected usage:\n");
    printf("./examples/pcr/policy_sign [-ecc256/-ecc384] [-key=pem/der] [-pcr=] [-pcrdigest=] [-policydigest=][-outpolicy=]\n");
    printf("* -ecc256/-ecc384: Key type (currently only ECC) (default SECP256R1)\n");
    printf("* -key=keyfile: Private key to sign PCR policy (PEM or DER) (default wolfboot_signing_private_key.der)\n");
    printf("* -pcr=index: PCR index < 24 (multiple can be supplied) (default %d)\n", DEFAULT_PCR);
    printf("* -pcrdigest=hexstr: PCR Digest (default=Read actual PCR's)\n");
    printf("* -policydigest=hexstr: Policy Digest (policy based on PCR digest and PCR(s)\n");
    printf("* -outpolicy=file: Signature file (default policy.bin.sig)\n");
    printf("Example:\n");
    printf("\t./tools/tpm/policy_sign -ecc256 -pcr=0 -pcrdigest=eca4e8eda468b8667244ae972b8240d3244ea72341b2bf2383e79c66643bbecc\n");
}


#ifndef WC_MAX_ENCODED_DIG_ASN_SZ
#define WC_MAX_ENCODED_DIG_ASN_SZ 9 /* enum(bit or octet) + length(4) */
#endif

static int loadFile(const char* fname, byte** buf, size_t* bufLen)
{
    int ret = 0;
    ssize_t fileSz, readLen;
    FILE *fp;

    if (fname == NULL || buf == NULL || bufLen == NULL)
        return BAD_FUNC_ARG;

    /* open file (read-only binary) */
    fp = fopen(fname, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error loading %s\n", fname);
        return BUFFER_E;
    }

    fseek(fp, 0, SEEK_END);
    fileSz = ftell(fp);
    rewind(fp);
    if (fileSz > 0) {
        if (*buf == NULL) {
            *buf = (byte*)XMALLOC(fileSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            if (*buf == NULL)
                ret = MEMORY_E;
        }
        else if (*buf != NULL && fileSz > (ssize_t)*bufLen) {
            ret = INPUT_SIZE_E;
        }
        *bufLen = (size_t)fileSz;
        if (ret == 0) {
            readLen = fread(*buf, 1, *bufLen, fp);
            ret = (readLen == (ssize_t)*bufLen) ? 0 : -1;
        }
    }
    else {
        ret = BUFFER_E;
    }
    fclose(fp);

    return ret;
}

/* Function to sign policy with external key */
static int PolicySign(int alg, const char* keyFile, byte* hash, word32 hashSz,
    byte* sig, word32* sigSz)
{
    int rc = 0;
    byte* buf = NULL;
    size_t bufSz = 0;
    WC_RNG rng;
    union {
    #ifndef NO_RSA
        RsaKey rsa;
    #endif
    #ifdef HAVE_ECC
        ecc_key ecc;
    #endif
    } key;

    memset(&key, 0, sizeof(key));
    memset(&rng, 0, sizeof(rng));

    rc = wc_InitRng(&rng);
    if (rc != 0) {
        printf("wc_InitRng failed\n");
        return rc;
    }

    rc = loadFile(keyFile, &buf, &bufSz);
    if (rc == 0 && (alg == ECC_SECP256R1 || alg == ECC_SECP384R1)) {
        word32 keySz = 32;
        if (alg == ECC_SECP384R1)
            keySz = 48;
        rc = wc_ecc_init(&key.ecc);
        if (rc == 0) {
            rc = wc_ecc_import_unsigned(&key.ecc, buf,
                (buf) + keySz, buf + (keySz*2), alg);
            if (rc == 0) {
                mp_int r, s;
                rc = mp_init_multi(&r, &s, NULL, NULL, NULL, NULL);
                if (rc == 0) {
                    rc = wc_ecc_sign_hash_ex(hash, hashSz, &rng, &key.ecc, &r, &s);
                }
                if (rc == 0) {
                    word32 rSz, sSz;
                    *sigSz = keySz * 2;
                    memset(sig, 0, *sigSz);
                    /* export sign r/s - zero pad to key size */
                    rSz = mp_unsigned_bin_size(&r);
                    mp_to_unsigned_bin(&r, &sig[keySz - rSz]);
                    sSz = mp_unsigned_bin_size(&s);
                    mp_to_unsigned_bin(&s, &sig[keySz + (keySz - sSz)]);
                    mp_clear(&r);
                    mp_clear(&s);
                }
            }
            wc_ecc_free(&key.ecc);
        }
    }
    else {
        rc = BAD_FUNC_ARG;
    }

    XFREE(buf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    wc_FreeRng(&rng);

    if (rc != 0) {
        printf("Policy Sign with external key failed %d\n", rc);
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
    for (i = 0; i < sz; i++) {
        printf("%02x", bin[i]);
        if (((i+1) % maxLine) == 0 && i+1 != sz)
            printf("\n\t");
    }
    printf("\n");
}

static int writeBin(const char* filename, const byte *buf, word32 bufSz)
{
    int rc = TPM_RC_FAILURE;

    if (filename == NULL || buf == NULL)
        return BAD_FUNC_ARG;

    FILE *fp = NULL;
    size_t fileSz = 0;

    fp = fopen(filename, "wt");
    if (fp != NULL) {
        fileSz = fwrite(buf, 1, bufSz, fp);
        /* sanity check */
        if (fileSz == (word32)bufSz) {
            rc = TPM_RC_SUCCESS;
        }
        fclose(fp);
    }
    return rc;
}

int policy_sign(int argc, char *argv[])
{
    int i;
    int rc = -1;
    TPM_ALG_ID pcrAlg = USE_PCR_ALG;
    int alg = ECC_SECP256R1;
    byte pcrArray[PCR_SELECT_MAX*2];
    word32 pcrArraySz = 0;
    const char* keyFile = "wolfboot_signing_private_key.der";
    const char* outPolicyFile = "policy.bin.sig";
    byte pcrDigest[WC_MAX_DIGEST_SIZE];
    word32 pcrDigestSz = 0;
    byte digest[WC_MAX_DIGEST_SIZE];
    word32 digestSz = 0;
    byte policy[512 + sizeof(word32)];
    byte sig[512]; /* up to 4096-bit key */
    word32 sigSz = 0;
    byte* policyRef = NULL; /* optional nonce */
    word32 policyRefSz = 0;
    word32 pcrMask;

    if (argc >= 2) {
        if (XSTRCMP(argv[1], "-?") == 0 ||
            XSTRCMP(argv[1], "-h") == 0 ||
            XSTRCMP(argv[1], "--help") == 0) {
            usage();
            return 0;
        }
    }
    while (argc > 1) {
        if (XSTRCMP(argv[argc-1], "-ecc256") == 0) {
            alg = ECC_SECP256R1;
        }
        else if (XSTRCMP(argv[argc-1], "-ecc384") == 0) {
            alg = ECC_SECP384R1;
        }
        else if (strncmp(argv[argc-1], "-pcr=", strlen("-pcr=")) == 0) {
            const char* pcrStr = argv[argc-1] + strlen("-pcr=");
            byte pcrIndex = (byte)XATOI(pcrStr);
            if (pcrIndex > PCR_LAST) {
                printf("PCR index is out of range (0-23)\n");
                usage();
                return 0;
            }
            pcrArray[pcrArraySz] = pcrIndex;
            pcrArraySz++;
        }
        else if (strncmp(argv[argc-1], "-pcrdigest=", strlen("-pcrdigest=")) == 0) {
            const char* hashHexStr = argv[argc-1] + strlen("-pcrdigest=");
            int hashHexStrlen = (int)strlen(hashHexStr);
            if (hashHexStrlen > (int)sizeof(pcrDigest)*2+1)
                pcrDigestSz = -1;
            else
                pcrDigestSz = hexToByte(hashHexStr, pcrDigest, hashHexStrlen);
            if (pcrDigestSz <= 0) {
                fprintf(stderr, "Invalid PCR hash length\n");
                usage();
                return -1;
            }
        }
        else if (strncmp(argv[argc-1], "-policydigest=", strlen("-policydigest=")) == 0) {
            const char* hashHexStr = argv[argc-1] + strlen("-policydigest=");
            int hashHexStrlen = (int)strlen(hashHexStr);
            if (hashHexStrlen > (int)sizeof(digest)*2+1)
                digestSz = -1;
            else
                digestSz = hexToByte(hashHexStr, digest, hashHexStrlen);
            if (digestSz <= 0) {
                fprintf(stderr, "Invalid Policy Digest hash length\n");
                usage();
                return -1;
            }
        }
        else if (strncmp(argv[argc-1], "-key=",
                strlen("-key=")) == 0) {
            keyFile = argv[argc-1] + strlen("-key=");
        }
        else if (strncmp(argv[argc-1], "-outpolicy=",
                strlen("-outpolicy=")) == 0) {
            outPolicyFile = argv[argc-1] + strlen("-outpolicy=");
        }
        else {
            printf("Warning: Unrecognized option: %s\n", argv[argc-1]);
        }
        argc--;
    }

    printf("Sign PCR Policy Tool\n");

    if (pcrArraySz == 0) {
        pcrArray[pcrArraySz] = DEFAULT_PCR;
        pcrArraySz++;
    }

    printf("Signing Algorithm: %s\n",
        (alg == ECC_SECP256R1) ? "ECC256" :
        (alg == ECC_SECP384R1) ? "ECC384" :
        "Unknown"
    );

    printf("PCR Index(s) (%s): ", TPM2_GetAlgName(pcrAlg));
    for (i = 0; i < (int)pcrArraySz; i++) {
        printf("%d ", pcrArray[i]);
    }
    printf("\n");

    /* Policy Signing Key */
    if (keyFile == NULL) {
        printf("Need private key to sign the policy\n");
        goto exit;
    }
    else {
        printf("Policy Signing Key: %s\n", keyFile);
    }

    /* PCR Hash - Use provided PCR digest or Policy digest */
    if (pcrDigestSz == 0 && digestSz == 0) {
        printf("Error: Must supply either PCR or Policy digest!\n");
        usage();
        return -1;
    }
    printf("PCR Digest (%d bytes):\n", pcrDigestSz);
    printHexString(pcrDigest, pcrDigestSz, pcrDigestSz);

    if (digestSz == 0) {
        /* If not supplied, build PCR Policy to Sign */
        memset(digest, 0, sizeof(digest));
        digestSz = TPM2_GetHashDigestSize(pcrAlg);
        rc = wolfTPM2_PolicyPCRMake(pcrAlg, pcrArray, pcrArraySz,
                                    pcrDigest, pcrDigestSz, digest, &digestSz);
        if (rc != 0) goto exit;

        printf("PCR Policy Digest (%d bytes):\n", digestSz);
        printHexString(digest, digestSz, digestSz);

        /* Add policyRef (if blank just re-hash) */
        rc = wolfTPM2_PolicyRefMake(pcrAlg, digest, &digestSz, policyRef, policyRefSz);
        if (rc != 0) goto exit;
    }

    printf("PCR Policy Digest (w/PolicyRef) (%d bytes):\n", digestSz);
    printHexString(digest, digestSz, digestSz);

    /* Sign the PCR policy (use private key provided or do externally) */
    rc = PolicySign(alg, keyFile, digest, digestSz, sig, &sigSz);
    if (rc == 0) {
        pcrMask = 0;
        for (i = 0; i < (int)pcrArraySz; i++)
            pcrMask |= (1 << pcrArray[i]);

        memcpy(policy, &pcrMask, sizeof(pcrMask));
        memcpy(policy + sizeof(pcrMask), sig, sigSz);
        printf("PCR Mask (0x%x) and Policy Signature (%d bytes):\n",
            (int)pcrMask, (int)(sigSz + sizeof(pcrMask)));
        printHexString(policy, sizeof(pcrMask), 0);
        printHexString(policy + sizeof(pcrMask), sigSz, 32);
        rc = writeBin(outPolicyFile, policy, sigSz+sizeof(pcrMask));
        if (rc == 0) {
            printf("Wrote PCR Mask + Signature (%d bytes) to %s\n",
                (int)(sigSz + sizeof(pcrMask)), outPolicyFile);
        }
    }

exit:
    if (rc != 0) {
        printf("Failure 0x%x: %s\n", rc, wolfTPM2_GetRCString(rc));
    }

    return rc;
}

int main(int argc, char *argv[])
{
    return policy_sign(argc, argv);
}
