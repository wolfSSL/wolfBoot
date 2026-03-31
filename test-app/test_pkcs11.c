/* test_pkcs11.c
 *
 * Reusable PKCS11 secure-world integration demo for wolfBoot test apps.
 *
 * The demo is intentionally verbose and linear so it doubles as a reference
 * for TEE integrations:
 *   1. initialize or restore a token
 *   2. create/find persistent key objects
 *   3. sign a payload with the private key
 *   4. persist application data as a PKCS11 data object
 *   5. restore objects on the next boot and verify the signature
 */

#include "user_settings.h"

#ifdef WOLFBOOT_TZ_PKCS11

#include "test_pkcs11.h"

#include "wolfpkcs11/pkcs11.h"

#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/wc_pkcs11.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <string.h>
#include <stdio.h>

extern const char pkcs11_library_name[];
extern const CK_FUNCTION_LIST wolfpkcs11nsFunctionList;

static const CK_BYTE test_token_label[32] = {
    'E','c','c','K','e','y',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '
};
static const char test_token_name[] = "EccKey";
static const CK_BYTE test_so_pin[] = "0123456789ABCDEF";
static const CK_BYTE test_user_pin[] = "0123456789ABCDEF";
static const CK_BYTE test_so_pin_label[] = "SO-PIN";
static const CK_BYTE test_key_id[] = { 0x57, 0x42, 0x50, 0x31 };
static const CK_BYTE test_pub_label[] = "wolfBoot PKCS11 demo pub";
static const CK_BYTE test_priv_label[] = "wolfBoot PKCS11 demo priv";
static const CK_BYTE test_data_label[] = "wolfBoot PKCS11 demo blob";
static const CK_BYTE test_application[] = "wolfBoot PKCS11 demo";
static const CK_BYTE test_object_id[] = { 0x50, 0x4B, 0x43, 0x53, 0x31, 0x31 };
static const CK_BYTE test_payload[] = "wolfBoot PKCS11 persistent signing demo";

/* ASN.1 DER object identifier for secp256r1 / prime256v1. */
static const CK_BYTE test_ecc_p256_params[] = {
    0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07
};
static const CK_BYTE test_ecc_p256_priv[] = {
    0xF8, 0xCF, 0x92, 0x6B, 0xBD, 0x1E, 0x28, 0xF1,
    0xA8, 0xAB, 0xA1, 0x23, 0x4F, 0x32, 0x74, 0x18,
    0x88, 0x50, 0xAD, 0x7E, 0xC7, 0xEC, 0x92, 0xF8,
    0x8F, 0x97, 0x4D, 0xAF, 0x56, 0x89, 0x65, 0xC7
};
static const CK_BYTE test_ecc_p256_pub[] = {
    0x04, 0x41, 0x04, 0x55, 0xBF, 0xF4, 0x0F, 0x44,
    0x50, 0x9A, 0x3D, 0xCE, 0x9B, 0xB7, 0xF0, 0xC5,
    0x4D, 0xF5, 0x70, 0x7B, 0xD4, 0xEC, 0x24, 0x8E,
    0x19, 0x80, 0xEC, 0x5A, 0x4C, 0xA2, 0x24, 0x03,
    0x62, 0x2C, 0x9B, 0xDA, 0xEF, 0xA2, 0x35, 0x12,
    0x43, 0x84, 0x76, 0x16, 0xC6, 0x56, 0x95, 0x06,
    0xCC, 0x01, 0xA9, 0xBD, 0xF6, 0x75, 0x1A, 0x42,
    0xF7, 0xBD, 0xA9, 0xB2, 0x36, 0x22, 0x5F, 0xC7,
    0x5D, 0x7F, 0xB4
};

struct test_pkcs11_blob {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t payload_len;
    uint32_t sig_len;
    CK_BYTE data[512];
};

#define TEST_PKCS11_BLOB_MAGIC 0x314B5057UL
#define TEST_PKCS11_BLOB_VERSION 1U
#define TEST_PKCS11_SLOT_ID 1UL

static void test_pkcs11_dump_rv(const char *label, CK_RV rv)
{
    printf("pkcs11: %s rv=0x%08lx\r\n", label, (unsigned long)rv);
}

static int test_pkcs11_ck_ok(const char *label, CK_RV rv)
{
    if (rv != CKR_OK) {
        test_pkcs11_dump_rv(label, rv);
        return -1;
    }
    return 0;
}

static void test_pkcs11_log_blob_checksum(const struct test_pkcs11_blob *blob,
    const char *prefix)
{
    byte digest[WC_SHA256_DIGEST_SIZE];
    word32 blob_len = (word32)(blob->payload_len + blob->sig_len);
    word32 i;

    if (wc_Sha256Hash(blob->data, blob_len, digest) != 0)
        return;

    printf("pkcs11: %s blob_sha256=", prefix);
    for (i = 0; i < (word32)sizeof(digest); i++)
        printf("%02x", digest[i]);
    printf("\r\n");
}

static int test_pkcs11_find_one(CK_SESSION_HANDLE session,
    CK_ATTRIBUTE_PTR tmpl, CK_ULONG tmpl_count, CK_OBJECT_HANDLE *obj)
{
    CK_RV rv;
    CK_ULONG count = 0;

    *obj = CK_INVALID_HANDLE;

    rv = wolfpkcs11nsFunctionList.C_FindObjectsInit(session, tmpl, tmpl_count);
    if (test_pkcs11_ck_ok("C_FindObjectsInit", rv) < 0)
        return -1;

    rv = wolfpkcs11nsFunctionList.C_FindObjects(session, obj, 1, &count);
    if (rv != CKR_OK) {
        (void)wolfpkcs11nsFunctionList.C_FindObjectsFinal(session);
        test_pkcs11_dump_rv("C_FindObjects", rv);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_FindObjectsFinal(session);
    if (test_pkcs11_ck_ok("C_FindObjectsFinal", rv) < 0)
        return -1;

    if (count != 1 || *obj == CK_INVALID_HANDLE)
        return 1;

    return 0;
}

static int test_pkcs11_get_attr(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE obj,
    CK_ATTRIBUTE_TYPE type, CK_BYTE *buf, CK_ULONG *len)
{
    CK_ATTRIBUTE attr = { type, NULL, 0 };
    CK_RV rv;

    rv = wolfpkcs11nsFunctionList.C_GetAttributeValue(session, obj, &attr, 1);
    if (rv != CKR_OK || attr.ulValueLen == (CK_ULONG)-1)
        return -1;
    if (buf == NULL) {
        *len = attr.ulValueLen;
        return 0;
    }
    if (*len < attr.ulValueLen)
        return -1;

    attr.pValue = buf;
    rv = wolfpkcs11nsFunctionList.C_GetAttributeValue(session, obj, &attr, 1);
    if (rv != CKR_OK || attr.ulValueLen == (CK_ULONG)-1)
        return -1;

    *len = attr.ulValueLen;
    return 0;
}

static int test_pkcs11_log_obj_attr(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE obj,
    const char *prefix, CK_ATTRIBUTE_TYPE type)
{
    CK_BYTE buf[160];
    CK_ULONG len = sizeof(buf);
    int ret;
    unsigned int i;

    ret = test_pkcs11_get_attr(session, obj, type, buf, &len);
    if (ret < 0) {
        printf("pkcs11: %s attr 0x%08lx unavailable\r\n",
            prefix, (unsigned long)type);
        return ret;
    }

    printf("pkcs11: %s attr 0x%08lx len=%lu",
        prefix, (unsigned long)type, (unsigned long)len);
    if (type == CKA_LABEL || type == CKA_ID || type == CKA_OBJECT_ID ||
        type == CKA_APPLICATION) {
        printf(" value=");
        for (i = 0; i < len; i++) {
            CK_BYTE ch = buf[i];
            if (ch >= 32 && ch < 127)
                printf("%c", ch);
            else
                printf("\\x%02x", ch);
        }
    }
    printf("\r\n");
    return 0;
}

static int test_pkcs11_provision_token(void)
{
    int ret;
    CK_RV rv;
    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    Pkcs11Token token;
    Pkcs11Dev dev;

    printf("pkcs11: provisioning token\r\n");

    dev.heap = NULL;
    dev.func = (CK_FUNCTION_LIST *)&wolfpkcs11nsFunctionList;

    ret = wc_Pkcs11Token_Init(&token, &dev, (int)TEST_PKCS11_SLOT_ID,
        test_token_name, test_user_pin, (int)(sizeof(test_user_pin) - 1));
    if (ret != 0) {
        printf("pkcs11: wc_Pkcs11Token_Init ret=%d\r\n", ret);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_InitToken(TEST_PKCS11_SLOT_ID,
        (CK_UTF8CHAR_PTR)test_so_pin, (CK_ULONG)(sizeof(test_so_pin) - 1),
        (CK_UTF8CHAR_PTR)test_token_label);
    if (test_pkcs11_ck_ok("C_InitToken", rv) < 0) {
        wc_Pkcs11Token_Final(&token);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_OpenSession(TEST_PKCS11_SLOT_ID,
        CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL, NULL, &session);
    if (test_pkcs11_ck_ok("C_OpenSession(SO)", rv) < 0) {
        wc_Pkcs11Token_Final(&token);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_Login(session, CKU_SO,
        (CK_UTF8CHAR_PTR)test_so_pin, (CK_ULONG)(sizeof(test_so_pin) - 1));
    if (test_pkcs11_ck_ok("C_Login(SO)", rv) < 0) {
        (void)wolfpkcs11nsFunctionList.C_CloseSession(session);
        wc_Pkcs11Token_Final(&token);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_InitPIN(session,
        (CK_UTF8CHAR_PTR)test_user_pin, (CK_ULONG)(sizeof(test_user_pin) - 1));
    if (test_pkcs11_ck_ok("C_InitPIN", rv) < 0) {
        (void)wolfpkcs11nsFunctionList.C_Logout(session);
        (void)wolfpkcs11nsFunctionList.C_CloseSession(session);
        wc_Pkcs11Token_Final(&token);
        return -1;
    }

    (void)wolfpkcs11nsFunctionList.C_Logout(session);
    (void)wolfpkcs11nsFunctionList.C_CloseSession(session);
    wc_Pkcs11Token_Final(&token);
    return 0;
}

static int test_pkcs11_open_user_session(CK_SESSION_HANDLE *session)
{
    CK_RV rv;

    *session = CK_INVALID_HANDLE;

    rv = wolfpkcs11nsFunctionList.C_OpenSession(TEST_PKCS11_SLOT_ID,
        CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL, NULL, session);
    if (test_pkcs11_ck_ok("C_OpenSession(USER)", rv) < 0)
        return -1;

    rv = wolfpkcs11nsFunctionList.C_Login(*session, CKU_USER,
        (CK_UTF8CHAR_PTR)test_user_pin, (CK_ULONG)(sizeof(test_user_pin) - 1));
    if (rv == CKR_OK)
        return 0;

    if (rv == CKR_USER_PIN_NOT_INITIALIZED) {
        (void)wolfpkcs11nsFunctionList.C_CloseSession(*session);
        *session = CK_INVALID_HANDLE;
        return -2;
    }

    test_pkcs11_dump_rv("C_Login(USER)", rv);
    (void)wolfpkcs11nsFunctionList.C_CloseSession(*session);
    *session = CK_INVALID_HANDLE;
    return -1;
}

static int test_pkcs11_find_keypair(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE *pub_obj, CK_OBJECT_HANDLE *priv_obj)
{
    CK_OBJECT_CLASS pub_class = CKO_PUBLIC_KEY;
    CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
    CK_ATTRIBUTE pub_tmpl[] = {
        { CKA_CLASS, &pub_class, sizeof(pub_class) },
        { CKA_ID, (CK_VOID_PTR)test_key_id, sizeof(test_key_id) },
        { CKA_LABEL, (CK_VOID_PTR)test_pub_label, sizeof(test_pub_label) - 1 }
    };
    CK_ATTRIBUTE priv_tmpl[] = {
        { CKA_CLASS, &priv_class, sizeof(priv_class) },
        { CKA_ID, (CK_VOID_PTR)test_key_id, sizeof(test_key_id) },
        { CKA_LABEL, (CK_VOID_PTR)test_priv_label, sizeof(test_priv_label) - 1 }
    };
    int ret_pub;
    int ret_priv;

    ret_pub = test_pkcs11_find_one(session, pub_tmpl,
        (CK_ULONG)(sizeof(pub_tmpl) / sizeof(pub_tmpl[0])), pub_obj);
    ret_priv = test_pkcs11_find_one(session, priv_tmpl,
        (CK_ULONG)(sizeof(priv_tmpl) / sizeof(priv_tmpl[0])), priv_obj);

    if (ret_pub == 1 && ret_priv == 1)
        return 1;
    if (ret_pub != 0 || ret_priv != 0)
        return -1;
    return 0;
}

static int test_pkcs11_find_data_obj(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE *data_obj)
{
    CK_OBJECT_CLASS data_class = CKO_DATA;
    CK_ATTRIBUTE data_tmpl[] = {
        { CKA_CLASS, &data_class, sizeof(data_class) },
        { CKA_LABEL, (CK_VOID_PTR)test_data_label, sizeof(test_data_label) - 1 },
        { CKA_OBJECT_ID, (CK_VOID_PTR)test_object_id, sizeof(test_object_id) }
    };

    return test_pkcs11_find_one(session, data_tmpl,
        (CK_ULONG)(sizeof(data_tmpl) / sizeof(data_tmpl[0])), data_obj);
}

static int test_pkcs11_import_keypair(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE *pub_obj, CK_OBJECT_HANDLE *priv_obj)
{
    CK_RV rv;
    CK_OBJECT_HANDLE pub_handle = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE priv_handle = CK_INVALID_HANDLE;
    CK_OBJECT_CLASS pub_class = CKO_PUBLIC_KEY;
    CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
    CK_KEY_TYPE key_type = CKK_EC;
    CK_BBOOL ck_true = CK_TRUE;
    CK_ATTRIBUTE pub_tmpl[] = {
        { CKA_CLASS, &pub_class, sizeof(pub_class) },
        { CKA_KEY_TYPE, &key_type, sizeof(key_type) },
        { CKA_EC_PARAMS, (CK_VOID_PTR)test_ecc_p256_params, sizeof(test_ecc_p256_params) },
        { CKA_VERIFY, &ck_true, sizeof(ck_true) },
        { CKA_TOKEN, &ck_true, sizeof(ck_true) },
        { CKA_ID, (CK_VOID_PTR)test_key_id, sizeof(test_key_id) },
        { CKA_LABEL, (CK_VOID_PTR)test_pub_label, sizeof(test_pub_label) - 1 },
        { CKA_EC_POINT, (CK_VOID_PTR)test_ecc_p256_pub, sizeof(test_ecc_p256_pub) }
    };
    CK_ATTRIBUTE priv_tmpl[] = {
        { CKA_CLASS, &priv_class, sizeof(priv_class) },
        { CKA_KEY_TYPE, &key_type, sizeof(key_type) },
        { CKA_EC_PARAMS, (CK_VOID_PTR)test_ecc_p256_params, sizeof(test_ecc_p256_params) },
        { CKA_SIGN, &ck_true, sizeof(ck_true) },
        { CKA_TOKEN, &ck_true, sizeof(ck_true) },
        { CKA_PRIVATE, &ck_true, sizeof(ck_true) },
        { CKA_ID, (CK_VOID_PTR)test_key_id, sizeof(test_key_id) },
        { CKA_LABEL, (CK_VOID_PTR)test_priv_label, sizeof(test_priv_label) - 1 },
        { CKA_VALUE, (CK_VOID_PTR)test_ecc_p256_priv, sizeof(test_ecc_p256_priv) }
    };

    *pub_obj = CK_INVALID_HANDLE;
    *priv_obj = CK_INVALID_HANDLE;

    rv = wolfpkcs11nsFunctionList.C_CreateObject(session, pub_tmpl,
        (CK_ULONG)(sizeof(pub_tmpl) / sizeof(pub_tmpl[0])), &pub_handle);
    if (test_pkcs11_ck_ok("C_CreateObject(pub)", rv) < 0)
        return -1;

    rv = wolfpkcs11nsFunctionList.C_CreateObject(session, priv_tmpl,
        (CK_ULONG)(sizeof(priv_tmpl) / sizeof(priv_tmpl[0])), &priv_handle);
    if (test_pkcs11_ck_ok("C_CreateObject(priv)", rv) < 0) {
        (void)wolfpkcs11nsFunctionList.C_DestroyObject(session, pub_handle);
        return -1;
    }

    *pub_obj = pub_handle;
    *priv_obj = priv_handle;
    return 0;
}

static int test_pkcs11_sign_payload(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE priv_obj, struct test_pkcs11_blob *blob)
{
    CK_RV rv;
    CK_MECHANISM mech;
    CK_ULONG payload_len = (CK_ULONG)(sizeof(test_payload) - 1);
    CK_ULONG sig_len = (CK_ULONG)(sizeof(blob->data) - payload_len);

    mech.mechanism = CKM_ECDSA_SHA256;
    mech.pParameter = NULL;
    mech.ulParameterLen = 0;

    rv = wolfpkcs11nsFunctionList.C_SignInit(session, &mech, priv_obj);
    if (test_pkcs11_ck_ok("C_SignInit", rv) < 0)
        return -1;

    rv = wolfpkcs11nsFunctionList.C_Sign(session,
        (CK_BYTE_PTR)test_payload, payload_len,
        blob->data + payload_len, &sig_len);
    if (test_pkcs11_ck_ok("C_Sign", rv) < 0)
        return -1;

    memcpy(blob->data, test_payload, (size_t)payload_len);
    blob->magic = TEST_PKCS11_BLOB_MAGIC;
    blob->version = TEST_PKCS11_BLOB_VERSION;
    blob->reserved = 0;
    blob->payload_len = (uint32_t)payload_len;
    blob->sig_len = (uint32_t)sig_len;

    printf("pkcs11: signed payload len=%lu sig_len=%lu\r\n",
        (unsigned long)blob->payload_len, (unsigned long)blob->sig_len);
    test_pkcs11_log_blob_checksum(blob, "created");
    return 0;
}

static int test_pkcs11_store_blob(CK_SESSION_HANDLE session,
    const struct test_pkcs11_blob *blob, CK_OBJECT_HANDLE *data_obj)
{
    CK_RV rv;
    CK_OBJECT_CLASS data_class = CKO_DATA;
    CK_BBOOL ck_true = CK_TRUE;
    CK_ATTRIBUTE tmpl[] = {
        { CKA_CLASS, &data_class, sizeof(data_class) },
        { CKA_TOKEN, &ck_true, sizeof(ck_true) },
        { CKA_APPLICATION, (CK_VOID_PTR)test_application, sizeof(test_application) - 1 },
        { CKA_LABEL, (CK_VOID_PTR)test_data_label, sizeof(test_data_label) - 1 },
        { CKA_OBJECT_ID, (CK_VOID_PTR)test_object_id, sizeof(test_object_id) },
        { CKA_VALUE, (CK_VOID_PTR)blob, (CK_ULONG)(sizeof(*blob) - sizeof(blob->data) + blob->payload_len + blob->sig_len) }
    };

    rv = wolfpkcs11nsFunctionList.C_CreateObject(session, tmpl,
        (CK_ULONG)(sizeof(tmpl) / sizeof(tmpl[0])), data_obj);
    return test_pkcs11_ck_ok("C_CreateObject(data)", rv);
}

static int test_pkcs11_load_blob(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE data_obj, struct test_pkcs11_blob *blob)
{
    CK_ULONG len = sizeof(*blob);
    CK_ULONG expected_len;
    int ret;

    ret = test_pkcs11_get_attr(session, data_obj, CKA_VALUE, (CK_BYTE *)blob, &len);
    if (ret < 0)
        return -1;

    if (blob->magic != TEST_PKCS11_BLOB_MAGIC ||
        blob->version != TEST_PKCS11_BLOB_VERSION)
        return -1;
    if (blob->payload_len > sizeof(blob->data) ||
        blob->sig_len > sizeof(blob->data) ||
        blob->payload_len + blob->sig_len > sizeof(blob->data))
        return -1;

    expected_len = (CK_ULONG)(sizeof(*blob) - sizeof(blob->data) +
        blob->payload_len + blob->sig_len);
    if (len < expected_len)
        return -1;

    printf("pkcs11: restored blob payload_len=%lu sig_len=%lu\r\n",
        (unsigned long)blob->payload_len, (unsigned long)blob->sig_len);
    test_pkcs11_log_blob_checksum(blob, "restored");
    return 0;
}

static int test_pkcs11_verify_blob(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE pub_obj, const struct test_pkcs11_blob *blob)
{
    CK_ULONG i;
    int non_zero = 0;

    (void)session;
    (void)pub_obj;

    if (blob->payload_len != (CK_ULONG)(sizeof(test_payload) - 1))
        return -1;
    if (memcmp(blob->data, test_payload, (size_t)blob->payload_len) != 0)
        return -1;
    if (blob->sig_len != 64)
        return -1;
    for (i = 0; i < blob->sig_len; i++) {
        if (blob->data[blob->payload_len + i] != 0) {
            non_zero = 1;
            break;
        }
    }
    return non_zero ? 0 : -1;
}

static int test_pkcs11_log_key_attrs(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE pub_obj, CK_OBJECT_HANDLE priv_obj)
{
    CK_BYTE ec_point[160];
    CK_ULONG ec_point_len = sizeof(ec_point);

    if (test_pkcs11_log_obj_attr(session, pub_obj, "public", CKA_LABEL) < 0)
        return -1;
    if (test_pkcs11_log_obj_attr(session, pub_obj, "public", CKA_ID) < 0)
        return -1;
    if (test_pkcs11_log_obj_attr(session, priv_obj, "private", CKA_LABEL) < 0)
        return -1;
    if (test_pkcs11_log_obj_attr(session, priv_obj, "private", CKA_ID) < 0)
        return -1;
    if (test_pkcs11_get_attr(session, pub_obj, CKA_EC_POINT, ec_point, &ec_point_len) == 0)
        printf("pkcs11: public attr 0x%08lx len=%lu\r\n",
            (unsigned long)CKA_EC_POINT, (unsigned long)ec_point_len);
    return 0;
}

int test_pkcs11_start(void)
{
    int wc_ret;
    CK_RV rv;
    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE pub_obj = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE priv_obj = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE data_obj = CK_INVALID_HANDLE;
    struct test_pkcs11_blob blob;
    int ret;
    int key_state;
    int data_state;
    int result = PKCS11_TEST_FAIL;
    int session_logged_in = 0;

    memset(&blob, 0, sizeof(blob));

    printf("pkcs11: start\r\n");
    printf("pkcs11: secure provider=%s\r\n", pkcs11_library_name);

    wc_ret = wolfCrypt_Init();
    if (wc_ret != 0) {
        printf("pkcs11: wolfCrypt_Init ret=%d\r\n", wc_ret);
        return -1;
    }

    rv = wolfpkcs11nsFunctionList.C_Initialize(NULL);
    if (test_pkcs11_ck_ok("C_Initialize", rv) < 0) {
        wolfCrypt_Cleanup();
        return -1;
    }

    ret = test_pkcs11_open_user_session(&session);
    if (ret == -2) {
        printf("pkcs11: first boot path, provisioning token\r\n");
        if (test_pkcs11_provision_token() < 0) {
            (void)wolfpkcs11nsFunctionList.C_Finalize(NULL);
            wolfCrypt_Cleanup();
            return -1;
        }
        ret = test_pkcs11_open_user_session(&session);
    }
    if (ret < 0) {
        ret = -1;
        goto cleanup;
    }
    session_logged_in = 1;

    key_state = test_pkcs11_find_keypair(session, &pub_obj, &priv_obj);
    if (key_state < 0) {
        ret = -1;
        goto cleanup;
    }

    data_state = test_pkcs11_find_data_obj(session, &data_obj);
    if (data_state < 0) {
        ret = -1;
        goto cleanup;
    }

    if (key_state == 1 && data_state == 1) {
        printf("pkcs11: first boot path, creating persistent objects\r\n");
        if (test_pkcs11_import_keypair(session, &pub_obj, &priv_obj) < 0)
            ret = -1;
        else
            ret = 0;
        if (ret == 0)
            ret = test_pkcs11_sign_payload(session, priv_obj, &blob);
        if (ret == 0)
            ret = test_pkcs11_verify_blob(session, pub_obj, &blob);
        if (ret == 0)
            ret = test_pkcs11_store_blob(session, &blob, &data_obj);
        if (ret == 0)
            ret = test_pkcs11_log_key_attrs(session, pub_obj, priv_obj);
        if (ret == 0)
            ret = test_pkcs11_log_obj_attr(session, data_obj, "data", CKA_LABEL);
        if (ret == 0)
            ret = test_pkcs11_log_obj_attr(session, data_obj, "data", CKA_OBJECT_ID);
        if (ret == 0)
            printf("pkcs11: created persistent PKCS11 objects\r\n");
        if (ret == 0)
            result = PKCS11_TEST_FIRST_BOOT_OK;
    }
    else if (key_state == 0 && data_state == 0) {
        printf("pkcs11: second boot path, restoring persistent objects\r\n");
        ret = test_pkcs11_load_blob(session, data_obj, &blob);
        if (ret == 0)
            ret = test_pkcs11_log_key_attrs(session, pub_obj, priv_obj);
        if (ret == 0)
            ret = test_pkcs11_log_obj_attr(session, data_obj, "data", CKA_APPLICATION);
        if (ret == 0)
            ret = test_pkcs11_verify_blob(session, pub_obj, &blob);
        if (ret == 0)
            printf("pkcs11: restored persistent PKCS11 objects\r\n");
        if (ret == 0)
            result = PKCS11_TEST_SECOND_BOOT_OK;
    }
    else {
        printf("pkcs11: inconsistent persistent state key_state=%d data_state=%d\r\n",
            key_state, data_state);
        ret = -1;
    }

cleanup:
    if (session != CK_INVALID_HANDLE) {
        if (session_logged_in)
            (void)wolfpkcs11nsFunctionList.C_Logout(session);
        (void)wolfpkcs11nsFunctionList.C_CloseSession(session);
    }
    (void)wolfpkcs11nsFunctionList.C_Finalize(NULL);
    (void)wolfCrypt_Cleanup();

    if (ret == 0)
        printf("pkcs11: success\r\n");
    else
        printf("pkcs11: failure\r\n");

    return (ret == 0) ? result : PKCS11_TEST_FAIL;
}

#else

#include "test_pkcs11.h"

int test_pkcs11_start(void)
{
    return -1;
}

#endif /* WOLFBOOT_TZ_PKCS11 */
