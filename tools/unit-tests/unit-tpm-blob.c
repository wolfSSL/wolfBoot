/* unit-tpm-blob.c
 *
 * Unit tests for TPM blob NV reads.
 */

#include <check.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif
#ifndef WOLFBOOT_SHA_DIGEST_SIZE
#define WOLFBOOT_SHA_DIGEST_SIZE 32
#endif
#ifndef WOLFBOOT_TPM_HASH_ALG
#define WOLFBOOT_TPM_HASH_ALG TPM_ALG_SHA256
#endif

#include "wolfboot/wolfboot.h"
#include "tpm.h"

enum mock_mode {
    MOCK_OVERSIZE_PUB,
    MOCK_OVERSIZE_PRIV,
    MOCK_UNSEAL_OK,
    MOCK_UNSEAL_OVERSIZE
};

static enum mock_mode current_mode;
static int nvread_calls;
static int unexpected_nvcreate_calls;
static int unexpected_nvwrite_calls;
static int unexpected_nvopen_calls;
static int unexpected_nvdelete_calls;
static int oversized_pub_read_attempted;
static int oversized_priv_read_attempted;
static int forcezero_calls;
static word32 last_forcezero_len;
static word32 last_pub_read_request_sz;
static uint8_t test_hdr[64];
static uint8_t test_modulus[256];
static uint8_t test_exponent_der[] = { 0xAA, 0x01, 0x00, 0x01, 0x7B };

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

int wolfTPM2_SetAuthHandle(WOLFTPM2_DEV* dev, int index,
    const WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)index;
    (void)handle;
    return 0;
}

int wolfTPM2_UnsetAuth(WOLFTPM2_DEV* dev, int index)
{
    (void)dev;
    (void)index;
    return 0;
}

int wolfTPM2_SetAuthSession(WOLFTPM2_DEV* dev, int index,
    WOLFTPM2_SESSION* tpmSession, TPMA_SESSION sessionAttributes)
{
    (void)dev;
    (void)index;
    (void)tpmSession;
    (void)sessionAttributes;
    return 0;
}

int wolfTPM2_UnsetAuthSession(WOLFTPM2_DEV* dev, int index,
    WOLFTPM2_SESSION* tpmSession)
{
    (void)dev;
    (void)index;
    (void)tpmSession;
    return 0;
}

int wolfTPM2_SetAuthHandleName(WOLFTPM2_DEV* dev, int index,
    const WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)index;
    (void)handle;
    return 0;
}

int wolfTPM2_StartSession(WOLFTPM2_DEV* dev, WOLFTPM2_SESSION* session,
    WOLFTPM2_KEY* tpmKey, WOLFTPM2_HANDLE* bind, TPM_SE sesType,
    int encDecAlg)
{
    (void)dev;
    (void)tpmKey;
    (void)bind;
    (void)sesType;
    (void)encDecAlg;
    session->handle.hndl = 1;
    return 0;
}

int wolfTPM2_LoadKey(WOLFTPM2_DEV* dev, WOLFTPM2_KEYBLOB* keyBlob,
    WOLFTPM2_HANDLE* parent)
{
    (void)dev;
    (void)parent;
    keyBlob->handle.hndl = 2;
    return 0;
}

int wolfTPM2_UnloadHandle(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)handle;
    return 0;
}

int wolfTPM2_LoadEccPublicKey(WOLFTPM2_DEV* dev, WOLFTPM2_KEY* pubKey,
    int curveID, const byte* qx, word32 qxSz, const byte* qy, word32 qySz)
{
    (void)dev;
    (void)pubKey;
    (void)curveID;
    (void)qx;
    (void)qxSz;
    (void)qy;
    (void)qySz;
    return 0;
}

int wc_RsaPublicKeyDecode_ex(const byte* input, word32* inOutIdx, word32 inSz,
    const byte** n, word32* nSz, const byte** e, word32* eSz)
{
    (void)input;
    (void)inSz;

    *inOutIdx = 0;
    *n = test_modulus;
    *nSz = sizeof(test_modulus);
    *e = &test_exponent_der[1];
    *eSz = 3;
    return 0;
}

int wolfTPM2_LoadRsaPublicKey_ex(WOLFTPM2_DEV* dev, WOLFTPM2_KEY* key,
    const byte* rsaPub, word32 rsaPubSz, word32 exponent,
    TPM_ALG_ID scheme, TPMI_ALG_HASH hashAlg)
{
    (void)dev;
    (void)key;
    (void)rsaPub;
    (void)rsaPubSz;
    (void)exponent;
    (void)scheme;
    (void)hashAlg;
    return 0;
}

int wolfTPM2_VerifyHashTicket(WOLFTPM2_DEV* dev, WOLFTPM2_KEY* key,
    const byte* sig, int sigSz, const byte* digest, int digestSz,
    TPMI_ALG_SIG_SCHEME sigAlg, TPMI_ALG_HASH hashAlg,
    TPMT_TK_VERIFIED* checkTicket)
{
    (void)dev;
    (void)key;
    (void)sig;
    (void)sigSz;
    (void)digest;
    (void)digestSz;
    (void)sigAlg;
    (void)hashAlg;
    (void)checkTicket;
    return 0;
}

int wolfTPM2_GetPolicyDigest(WOLFTPM2_DEV* dev, TPM_HANDLE sessionHandle,
    byte* policyDigest, word32* policyDigestSz)
{
    (void)dev;
    (void)sessionHandle;
    memset(policyDigest, 0x11, *policyDigestSz);
    return 0;
}

int wolfTPM2_PolicyPCR(WOLFTPM2_DEV* dev, TPM_HANDLE sessionHandle,
    TPM_ALG_ID pcrAlg, byte* pcrArray, word32 pcrArraySz)
{
    (void)dev;
    (void)sessionHandle;
    (void)pcrAlg;
    (void)pcrArray;
    (void)pcrArraySz;
    return 0;
}

int wolfTPM2_PolicyAuthorize(WOLFTPM2_DEV* dev, TPM_HANDLE sessionHandle,
    const TPM2B_PUBLIC* pub, const TPMT_TK_VERIFIED* checkTicket,
    const byte* pcrDigest, word32 pcrDigestSz,
    const byte* policyRef, word32 policyRefSz)
{
    (void)dev;
    (void)sessionHandle;
    (void)pub;
    (void)checkTicket;
    (void)pcrDigest;
    (void)pcrDigestSz;
    (void)policyRef;
    (void)policyRefSz;
    return 0;
}

int wolfTPM2_PolicyRefMake(TPM_ALG_ID pcrAlg, byte* digest, word32* digestSz,
    const byte* policyRef, word32 policyRefSz)
{
    (void)pcrAlg;
    (void)digest;
    (void)digestSz;
    (void)policyRef;
    (void)policyRefSz;
    return 0;
}

int TPM2_GetHashDigestSize(TPMI_ALG_HASH hashAlg)
{
    (void)hashAlg;
    return 32;
}

int wolfTPM2_GetKeyTemplate_KeySeal(TPMT_PUBLIC* publicTemplate,
    TPM_ALG_ID nameAlg)
{
    memset(publicTemplate, 0, sizeof(*publicTemplate));
    publicTemplate->nameAlg = nameAlg;
    return 0;
}

int wolfTPM2_PolicyAuthorizeMake(TPM_ALG_ID hashAlg, const TPM2B_PUBLIC* pub,
    byte* digest, word32* digestSz, const byte* policyRef,
    word32 policyRefSz)
{
    (void)hashAlg;
    (void)pub;
    (void)policyRef;
    (void)policyRefSz;
    memset(digest, 0x11, *digestSz);
    return 0;
}

int wolfTPM2_CreateKeySeal_ex(WOLFTPM2_DEV* dev, WOLFTPM2_KEYBLOB* keyBlob,
    WOLFTPM2_HANDLE* parent, TPMT_PUBLIC* publicTemplate, const byte* auth,
    int authSz, TPM_ALG_ID alg, byte* pcrSel,
    word32 pcrSelSz, const byte* sealData, int sealSize)
{
    (void)dev;
    (void)keyBlob;
    (void)parent;
    (void)publicTemplate;
    (void)auth;
    (void)authSz;
    (void)alg;
    (void)pcrSel;
    (void)pcrSelSz;
    (void)sealData;
    (void)sealSize;
    unexpected_nvcreate_calls++;
    ck_abort_msg("Unexpected wolfTPM2_CreateKeySeal_ex call");
    return -1;
}

int wolfTPM2_GetNvAttributesTemplate(TPM_HANDLE authHandle,
    word32* attr)
{
    (void)authHandle;
    *attr = 0;
    return 0;
}

TPM_RC TPM2_Unseal(Unseal_In* in, Unseal_Out* out)
{
    (void)in;

    if (current_mode == MOCK_UNSEAL_OK) {
        out->outData.size = 4;
        memset(out->outData.buffer, 0x5A, out->outData.size);
        return 0;
    }

    if (current_mode != MOCK_UNSEAL_OVERSIZE) {
        ck_abort_msg("Unexpected TPM2_Unseal call in mode %d", current_mode);
    }

    out->outData.size = 16;
    memset(out->outData.buffer, 0x5A, out->outData.size);
    return 0;
}

void TPM2_ForceZero(void* mem, word32 len)
{
    forcezero_calls++;
    last_forcezero_len = len;
    memset(mem, 0, len);
}

int keyslot_id_by_sha(const uint8_t* pubkey_hint)
{
    (void)pubkey_hint;
    return 0;
}

uint32_t keystore_get_key_type(int id)
{
    ck_assert_int_eq(id, 0);
    return AUTH_KEY_RSA2048;
}

uint8_t *keystore_get_buffer(int id)
{
    ck_assert_int_eq(id, 0);
    return test_hdr;
}

int keystore_get_size(int id)
{
    ck_assert_int_eq(id, 0);
    return (int)sizeof(test_hdr);
}

const char* TPM2_GetRCString(int rc)
{
    (void)rc;
    return "mock";
}

int TPM2_ParsePublic(TPM2B_PUBLIC* pub, byte* buf, word32 size, int* sizeUsed)
{
    (void)buf;
    *sizeUsed = 0;
    pub->size = (UINT16)size;
    return 0;
}

int TPM2_AppendPublic(byte* out, word32 outSz, int* pubAreaSize,
    TPM2B_PUBLIC* pub)
{
    (void)pub;
    ck_assert_uint_ge(outSz, 4);
    memset(out, 0, 4);
    *pubAreaSize = 4;
    return 0;
}

int wolfTPM2_NVCreateAuth(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* parent,
    WOLFTPM2_NV* nv, word32 nvIndex, word32 nvAttributes, word32 maxSize,
    const byte* auth, int authSz)
{
    (void)dev;
    (void)parent;
    (void)nv;
    (void)nvIndex;
    (void)nvAttributes;
    (void)maxSize;
    (void)auth;
    (void)authSz;
    unexpected_nvcreate_calls++;
    ck_abort_msg("Unexpected wolfTPM2_NVCreateAuth call");
    return -1;
}

int wolfTPM2_NVWriteAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv,
    word32 nvIndex, byte* dataBuf, word32 dataSz, word32 offset)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)dataBuf;
    (void)dataSz;
    (void)offset;
    unexpected_nvwrite_calls++;
    ck_abort_msg("Unexpected wolfTPM2_NVWriteAuth call");
    return -1;
}

int wolfTPM2_NVOpen(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv,
    word32 nvIndex, const byte* auth, word32 authSz)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)auth;
    (void)authSz;
    unexpected_nvopen_calls++;
    ck_abort_msg("Unexpected wolfTPM2_NVOpen call");
    return -1;
}

int wolfTPM2_NVDeleteAuth(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* parent,
    word32 nvIndex)
{
    (void)dev;
    (void)parent;
    (void)nvIndex;
    unexpected_nvdelete_calls++;
    ck_abort_msg("Unexpected wolfTPM2_NVDeleteAuth call");
    return -1;
}

int wolfTPM2_NVReadAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv,
    word32 nvIndex, byte* dataBuf, word32* pDataSz, word32 offset)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)offset;

    nvread_calls++;

    switch (nvread_calls) {
        case 1:
            if (current_mode == MOCK_OVERSIZE_PUB) {
                uint16_t value = (uint16_t)(sizeof(TPM2B_PUBLIC) + 1);
                memcpy(dataBuf, &value, sizeof(value));
            }
            else {
                uint16_t value = 4;
                memcpy(dataBuf, &value, sizeof(value));
            }
            *pDataSz = sizeof(uint16_t);
            return 0;
        case 2:
            if (current_mode == MOCK_OVERSIZE_PUB) {
                last_pub_read_request_sz = *pDataSz;
                if (*pDataSz > sizeof(TPM2B_PUBLIC)) {
                    oversized_pub_read_attempted = 1;
                }
                return -100;
            }
            memset(dataBuf, 0, *pDataSz);
            return 0;
        case 3:
        {
            uint16_t value =
                (uint16_t)(sizeof(((WOLFTPM2_KEYBLOB*)0)->priv.buffer) + 1);
            memcpy(dataBuf, &value, sizeof(value));
        }
            *pDataSz = sizeof(uint16_t);
            return 0;
        case 4:
            oversized_priv_read_attempted = 1;
            return -101;
        default:
            ck_abort_msg("Unexpected NV read call %d", nvread_calls);
            return -102;
    }
}

#include "../../src/tpm.c"

static void setup(void)
{
    current_mode = MOCK_OVERSIZE_PUB;
    nvread_calls = 0;
    unexpected_nvcreate_calls = 0;
    unexpected_nvwrite_calls = 0;
    unexpected_nvopen_calls = 0;
    unexpected_nvdelete_calls = 0;
    oversized_pub_read_attempted = 0;
    oversized_priv_read_attempted = 0;
    forcezero_calls = 0;
    last_forcezero_len = 0;
    last_pub_read_request_sz = 0;
    memset(test_hdr, 0x22, sizeof(test_hdr));
    memset(test_modulus, 0x33, sizeof(test_modulus));
}

START_TEST(test_wolfBoot_read_blob_rejects_oversized_public_area)
{
    WOLFTPM2_KEYBLOB blob;
    int rc;

    memset(&blob, 0, sizeof(blob));
    current_mode = MOCK_OVERSIZE_PUB;

    rc = wolfBoot_read_blob(0x01400300, &blob, NULL, 0);

    ck_assert_int_eq(rc, BUFFER_E);
    ck_assert_int_eq(nvread_calls, 1);
    ck_assert_uint_eq(last_pub_read_request_sz, 0);
    ck_assert_int_eq(oversized_pub_read_attempted, 0);
}
END_TEST

START_TEST(test_wolfBoot_store_blob_rejects_oversized_auth)
{
    WOLFTPM2_KEYBLOB blob;
    uint8_t auth[sizeof(((WOLFTPM2_NV*)0)->handle.auth.buffer) + 1];
    int rc;

    memset(&blob, 0, sizeof(blob));
    memset(auth, 0x44, sizeof(auth));

    rc = wolfBoot_store_blob(TPM_RH_PLATFORM, 0x01400300, 0, &blob,
        auth, (uint32_t)sizeof(auth));

    ck_assert_int_eq(rc, BAD_FUNC_ARG);
    ck_assert_int_eq(unexpected_nvcreate_calls, 0);
    ck_assert_int_eq(unexpected_nvwrite_calls, 0);
}
END_TEST

START_TEST(test_wolfBoot_read_blob_rejects_oversized_auth)
{
    WOLFTPM2_KEYBLOB blob;
    uint8_t auth[sizeof(((WOLFTPM2_NV*)0)->handle.auth.buffer) + 1];
    int rc;

    memset(&blob, 0, sizeof(blob));
    memset(auth, 0x55, sizeof(auth));

    rc = wolfBoot_read_blob(0x01400300, &blob, auth, (uint32_t)sizeof(auth));

    ck_assert_int_eq(rc, BAD_FUNC_ARG);
    ck_assert_int_eq(nvread_calls, 0);
}
END_TEST

START_TEST(test_wolfBoot_delete_blob_rejects_oversized_auth)
{
    uint8_t auth[sizeof(((WOLFTPM2_NV*)0)->handle.auth.buffer) + 1];
    int rc;

    memset(auth, 0x66, sizeof(auth));

    rc = wolfBoot_delete_blob(TPM_RH_PLATFORM, 0x01400300, auth,
        (uint32_t)sizeof(auth));

    ck_assert_int_eq(rc, BAD_FUNC_ARG);
    ck_assert_int_eq(unexpected_nvopen_calls, 0);
    ck_assert_int_eq(unexpected_nvdelete_calls, 0);
}
END_TEST

START_TEST(test_wolfBoot_seal_auth_rejects_oversized_auth)
{
    uint8_t auth[sizeof(((WOLFTPM2_KEYBLOB*)0)->handle.auth.buffer) + 1];
    uint8_t pubkey_hint[WOLFBOOT_SHA_DIGEST_SIZE] = {0};
    uint8_t policy[sizeof(uint32_t) + 4] = {0};
    uint8_t secret[8] = {0};
    int rc;

    memset(auth, 0x77, sizeof(auth));

    rc = wolfBoot_seal_auth(pubkey_hint, policy, sizeof(policy), 0,
        secret, sizeof(secret), auth, (int)sizeof(auth));

    ck_assert_int_eq(rc, BAD_FUNC_ARG);
    ck_assert_int_eq(unexpected_nvcreate_calls, 0);
    ck_assert_int_eq(unexpected_nvwrite_calls, 0);
    ck_assert_int_eq(unexpected_nvopen_calls, 0);
    ck_assert_int_eq(unexpected_nvdelete_calls, 0);
}
END_TEST

START_TEST(test_wolfBoot_unseal_blob_zeroes_unseal_output)
{
    uint8_t secret[WOLFBOOT_MAX_SEAL_SZ];
    WOLFTPM2_KEYBLOB blob;
    uint8_t pubkey_hint[WOLFBOOT_SHA_DIGEST_SIZE] = {0};
    uint8_t policy[sizeof(uint32_t) + 4] = {0};
    int secret_sz;
    int rc;

    memset(&blob, 0, sizeof(blob));
    memset(secret, 0, sizeof(secret));
    current_mode = MOCK_UNSEAL_OK;
    secret_sz = (int)sizeof(secret);

    rc = wolfBoot_unseal_blob(pubkey_hint, policy, sizeof(policy), &blob,
        secret, &secret_sz, NULL, 0);

    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(secret_sz, 4);
    ck_assert_int_eq(forcezero_calls, 1);
    ck_assert_uint_eq(last_forcezero_len, sizeof(Unseal_Out));
}
END_TEST

START_TEST(test_wolfBoot_unseal_blob_rejects_output_larger_than_capacity)
{
    struct {
        uint8_t secret[8];
        uint8_t canary[8];
    } output;
    WOLFTPM2_KEYBLOB blob;
    uint8_t pubkey_hint[WOLFBOOT_SHA_DIGEST_SIZE] = {0};
    uint8_t policy[sizeof(uint32_t) + 4] = {0};
    int secret_sz;
    int rc;
    int i;

    memset(&blob, 0, sizeof(blob));
    memset(&output, 0xA5, sizeof(output));
    current_mode = MOCK_UNSEAL_OVERSIZE;
    secret_sz = (int)sizeof(output.secret);

    rc = wolfBoot_unseal_blob(pubkey_hint, policy, sizeof(policy), &blob,
        output.secret, &secret_sz, NULL, 0);

    ck_assert_int_eq(rc, BUFFER_E);
    ck_assert_int_eq(secret_sz, 0);
    ck_assert_int_eq(forcezero_calls, 1);
    ck_assert_uint_eq(last_forcezero_len, sizeof(Unseal_Out));
    for (i = 0; i < (int)sizeof(output.canary); i++) {
        ck_assert_uint_eq(output.canary[i], 0xA5);
    }
}
END_TEST

START_TEST(test_wolfBoot_read_blob_rejects_oversized_private_area)
{
    WOLFTPM2_KEYBLOB blob;
    int rc;

    memset(&blob, 0, sizeof(blob));
    current_mode = MOCK_OVERSIZE_PRIV;
    blob.pub.size = 4;

    rc = wolfBoot_read_blob(0x01400300, &blob, NULL, 0);

    ck_assert_int_eq(rc, BUFFER_E);
    ck_assert_int_eq(nvread_calls, 3);
    ck_assert_int_eq(oversized_priv_read_attempted, 0);
}
END_TEST

static Suite *tpm_blob_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("TPM Blob");
    tc = tcase_create("wolfBoot_read_blob");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_wolfBoot_store_blob_rejects_oversized_auth);
    tcase_add_test(tc, test_wolfBoot_read_blob_rejects_oversized_auth);
    tcase_add_test(tc, test_wolfBoot_delete_blob_rejects_oversized_auth);
    tcase_add_test(tc, test_wolfBoot_seal_auth_rejects_oversized_auth);
    tcase_add_test(tc, test_wolfBoot_read_blob_rejects_oversized_public_area);
    tcase_add_test(tc, test_wolfBoot_read_blob_rejects_oversized_private_area);
    tcase_add_test(tc, test_wolfBoot_unseal_blob_zeroes_unseal_output);
    tcase_add_test(tc, test_wolfBoot_unseal_blob_rejects_output_larger_than_capacity);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = tpm_blob_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
