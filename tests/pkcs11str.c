/* pkcs11str.c - unit tests
 *
 * Copyright (C) 2006-2023 wolfSSL Inc.
 *
 * This file is part of wolfPKCS11.
 *
 * wolfPKCS11 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfPKCS11 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef HAVE_PKCS11_STATIC
#include <dlfcn.h>
#endif

#ifdef HAVE_CONFIG_H
    #include <wolfpkcs11/config.h>
#endif

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/misc.h>

#include <wolfpkcs11/options.h>
#include <wolfpkcs11/pkcs11.h>

#include "testdata.h"

int verbose = 0;

#ifdef DEBUG_WOLFPKCS11
#define CHECK_COND(cond, ret, msg)                                         \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s - ", __FILE__, __LINE__, msg);     \
            if (!(cond)) {                                                 \
                fprintf(stderr, "FAIL\n");                                 \
                ret = -1;                                                  \
            }                                                              \
            else                                                           \
                fprintf(stderr, "PASS\n");                                 \
        }                                                                  \
        else if (!(cond)) {                                                \
            fprintf(stderr, "\n%s:%d - %s - FAIL\n",                       \
                    __FILE__, __LINE__, msg);                              \
            ret = -1;                                                      \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR(rv, msg)                                                 \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s", __FILE__, __LINE__, msg);        \
            if (rv != CKR_OK)                                              \
                fprintf(stderr, ": %lx - FAIL\n", rv);                     \
            else                                                           \
                fprintf(stderr, " - PASS\n");                              \
        }                                                                  \
        else if (rv != CKR_OK) {                                           \
            fprintf(stderr, "\n%s:%d - %s: %lx - FAIL\n",                  \
                    __FILE__, __LINE__, msg, rv);                          \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR_FAIL(rv, exp, msg)                                       \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s", __FILE__, __LINE__, msg);        \
            if (rv != exp) {                                               \
                fprintf(stderr, " RETURNED %lx - FAIL\n", rv);             \
                if (rv == CKR_OK)                                          \
                    rv = -1;                                               \
            }                                                              \
            else {                                                         \
                fprintf(stderr, " - PASS\n");                              \
                rv = CKR_OK;                                               \
            }                                                              \
        }                                                                  \
        else if (rv != exp) {                                              \
            fprintf(stderr, "\n%s:%d - %s RETURNED %lx - FAIL\n",          \
                    __FILE__, __LINE__, msg, rv);                          \
            if (rv == CKR_OK)                                              \
                rv = -1;                                                   \
        }                                                                  \
        else                                                               \
            rv = CKR_OK;                                                   \
    }                                                                      \
    while (0)
#else
#define CHECK_COND(cond, ret, msg)                                         \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "\n%s:%d - %s - FAIL\n",                       \
                    __FILE__, __LINE__, msg);                              \
            ret = -1;                                                      \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR(rv, msg)                                                 \
    do {                                                                   \
        if (rv != CKR_OK) {                                                \
            fprintf(stderr, "\n%s:%d - %s: %lx - FAIL\n",                  \
                    __FILE__, __LINE__, msg, rv);                          \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR_FAIL(rv, exp, msg)                                       \
    do {                                                                   \
        if (rv != exp) {                                                   \
            fprintf(stderr, "\n%s:%d - %s RETURNED %lx - FAIL\n",          \
                    __FILE__, __LINE__, msg, rv);                          \
            if (rv == CKR_OK)                                              \
                rv = -1;                                                   \
        }                                                                  \
        else                                                               \
            rv = CKR_OK;                                                   \
    }                                                                      \
    while (0)
#endif

#ifndef HAVE_PKCS11_STATIC
static void* dlib;
#endif
static CK_FUNCTION_LIST* funcList;
static int slot = 0;
const char* tokenName = "wolfpkcs11";

/* FIPS requires pin to be at least 14 characters, since it is used for
 * the HMAC key */
static byte* soPin = (byte*)"password123456";
static int soPinLen = 14;
byte* userPin = (byte*)"wolfpkcs11-test";
int userPinLen;

#if !defined(NO_RSA) || defined(HAVE_ECC) || !defined(NO_DH)
static CK_OBJECT_CLASS pubKeyClass     = CKO_PUBLIC_KEY;
#endif
static CK_OBJECT_CLASS privKeyClass    = CKO_PRIVATE_KEY;
#ifndef NO_AES
static CK_OBJECT_CLASS secretKeyClass  = CKO_SECRET_KEY;
#endif
static CK_BBOOL ckTrue  = CK_TRUE;

#ifndef NO_RSA
static CK_KEY_TYPE rsaKeyType  = CKK_RSA;
#endif
#ifdef HAVE_ECC
static CK_KEY_TYPE eccKeyType  = CKK_EC;
#endif
#ifndef NO_DH
static CK_KEY_TYPE dhKeyType  = CKK_DH;
#endif
#ifndef NO_AES
static CK_KEY_TYPE aesKeyType  = CKK_AES;
#endif
static CK_KEY_TYPE genericKeyType  = CKK_GENERIC_SECRET;


static CK_RV pkcs11_lib_init(void)
{
    CK_RV ret;
    CK_C_INITIALIZE_ARGS args;

    XMEMSET(&args, 0x00, sizeof(args));
    args.flags = CKF_OS_LOCKING_OK;
    ret = funcList->C_Initialize(NULL);
    CHECK_CKR(ret, "Initialize");

    return ret;
}

static CK_RV pkcs11_init_token(void)
{
    CK_RV ret;
    unsigned char label[32];

    XMEMSET(label, ' ', sizeof(label));
    XMEMCPY(label, tokenName, XSTRLEN(tokenName));

    ret = funcList->C_InitToken(slot, soPin, soPinLen, label);
    CHECK_CKR(ret, "Init Token");

    return ret;
}

static void pkcs11_final(int closeDl)
{
    funcList->C_Finalize(NULL);
    if (closeDl) {
    #ifndef HAVE_PKCS11_STATIC
        dlclose(dlib);
    #endif
    }
}

static CK_RV pkcs11_set_user_pin(int slotId)
{
    CK_RV ret;
    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    int flags = CKF_SERIAL_SESSION | CKF_RW_SESSION;

    ret = funcList->C_OpenSession(slotId, flags, NULL, NULL, &session);
    CHECK_CKR(ret, "Set User PIN - Open Session");
    if (ret == CKR_OK) {
        ret = funcList->C_Login(session, CKU_SO, soPin, soPinLen);
        CHECK_CKR(ret, "Set User PIN - Login");
        if (ret == CKR_OK) {
            ret = funcList->C_InitPIN(session, userPin, userPinLen);
            CHECK_CKR(ret, "Set User PIN - Init PIN");
        }
        funcList->C_CloseSession(session);
    }

    if (ret != CKR_OK)
        fprintf(stderr, "FAILED: Setting user PIN\n");
    return ret;
}

static CK_RV pkcs11_open_session(CK_SESSION_HANDLE* session)
{
    CK_RV ret;
    int sessFlags = CKF_SERIAL_SESSION | CKF_RW_SESSION;

    ret = funcList->C_OpenSession(slot, sessFlags, NULL, NULL, session);
    CHECK_CKR(ret, "Open Session");
    if (ret == CKR_OK && userPinLen != 0) {
        ret = funcList->C_Login(*session, CKU_USER, userPin, userPinLen);
        CHECK_CKR(ret, "Login");
    }

    return ret;
}

static void pkcs11_close_session(CK_SESSION_HANDLE session)
{
    if (userPinLen != 0)
        funcList->C_Logout(session);
    funcList->C_CloseSession(session);
}

#ifndef NO_RSA
static CK_RV create_rsa_priv_key(CK_SESSION_HANDLE session,
    unsigned char* privId, int privIdLen, CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE rsa_2048_priv_key[] = {
        { CKA_CLASS,             &privKeyClass,     sizeof(privKeyClass)      },
        { CKA_KEY_TYPE,          &rsaKeyType,       sizeof(rsaKeyType)        },
        { CKA_DECRYPT,           &ckTrue,           sizeof(ckTrue)            },
        { CKA_MODULUS,           rsa_2048_modulus,  sizeof(rsa_2048_modulus)  },
        { CKA_PRIVATE_EXPONENT,  rsa_2048_priv_exp, sizeof(rsa_2048_priv_exp) },
        { CKA_PRIME_1,           rsa_2048_p,        sizeof(rsa_2048_p)        },
        { CKA_PRIME_2,           rsa_2048_q,        sizeof(rsa_2048_q)        },
        { CKA_EXPONENT_1,        rsa_2048_dP,       sizeof(rsa_2048_dP)       },
        { CKA_EXPONENT_2,        rsa_2048_dQ,       sizeof(rsa_2048_dQ)       },
        { CKA_COEFFICIENT,       rsa_2048_u,        sizeof(rsa_2048_u)        },
        { CKA_PUBLIC_EXPONENT,   rsa_2048_pub_exp,  sizeof(rsa_2048_pub_exp)  },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                privId,            privIdLen                 },
    };
    int cnt = sizeof(rsa_2048_priv_key)/sizeof(*rsa_2048_priv_key);

    if (privId == NULL)
        cnt -= 2;

    ret = funcList->C_CreateObject(session, rsa_2048_priv_key, cnt, obj);
    CHECK_CKR(ret, "RSA Private Key Create Object");

    return ret;
}

static CK_RV create_rsa_pub_key(CK_SESSION_HANDLE session, unsigned char* pubId,
    int pubIdLen, CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE rsa_2048_pub_key[] = {
        { CKA_CLASS,             &pubKeyClass,      sizeof(pubKeyClass)       },
        { CKA_KEY_TYPE,          &rsaKeyType,       sizeof(rsaKeyType)        },
        { CKA_ENCRYPT,           &ckTrue,           sizeof(ckTrue)            },
        { CKA_MODULUS,           rsa_2048_modulus,  sizeof(rsa_2048_modulus)  },
        { CKA_PUBLIC_EXPONENT,   rsa_2048_pub_exp,  sizeof(rsa_2048_pub_exp)  },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                pubId,             pubIdLen                  },
    };
    int cnt = sizeof(rsa_2048_pub_key)/sizeof(*rsa_2048_pub_key);

    if (pubId == NULL)
        cnt -= 2;

    ret = funcList->C_CreateObject(session, rsa_2048_pub_key, cnt, obj);
    CHECK_CKR(ret, "RSA Public Key Create Object");

    return ret;
}

static CK_RV find_rsa_pub_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* pubKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      pubKeyTmpl[] = {
        { CKA_CLASS,     &pubKeyClass,   sizeof(pubKeyClass)  },
        { CKA_KEY_TYPE,  &rsaKeyType,    sizeof(rsaKeyType)   },
        { CKA_ID,        id,             idLen                }
    };
    CK_ULONG pubKeyTmplCnt = sizeof(pubKeyTmpl) / sizeof(*pubKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, pubKeyTmpl, pubKeyTmplCnt);
    CHECK_CKR(ret, "RSA Public Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, pubKey, 1, &count);
        CHECK_CKR(ret, "RSA Public Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "RSA Public Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "RSA Public Key Find Objects Count");
    }

    return ret;
}

static CK_RV find_rsa_priv_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* privKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      privKeyTmpl[] = {
        { CKA_CLASS,     &privKeyClass,  sizeof(privKeyClass) },
        { CKA_KEY_TYPE,  &rsaKeyType,    sizeof(rsaKeyType)   },
        { CKA_ID,        id,             idLen                }
    };
    CK_ULONG privKeyTmplCnt = sizeof(privKeyTmpl) / sizeof(*privKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, privKeyTmpl, privKeyTmplCnt);
    CHECK_CKR(ret, "RSA Private Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, privKey, 1, &count);
        CHECK_CKR(ret, "RSA Private Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "RSA Private Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "RSA Private Key Find Objects Count");
    }

    return ret;
}
#endif

#ifdef HAVE_ECC
static CK_OBJECT_HANDLE create_ecc_priv_key(CK_SESSION_HANDLE session,
     unsigned char* privId, int privIdLen, CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE ecc_p256_priv_key[] = {
        { CKA_CLASS,             &privKeyClass,     sizeof(privKeyClass)      },
        { CKA_KEY_TYPE,          &eccKeyType,       sizeof(eccKeyType)        },
        { CKA_VERIFY,            &ckTrue,           sizeof(ckTrue)            },
        { CKA_EC_PARAMS,         ecc_p256_params,   sizeof(ecc_p256_params)   },
        { CKA_VALUE,             ecc_p256_priv,     sizeof(ecc_p256_priv)     },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                privId,            privIdLen                 },
    };
    int ecc_p256_priv_key_cnt =
                           sizeof(ecc_p256_priv_key)/sizeof(*ecc_p256_priv_key);

    ret = funcList->C_CreateObject(session, ecc_p256_priv_key,
                                                    ecc_p256_priv_key_cnt, obj);

    CHECK_CKR(ret, "EC Private Key Create Object");

    return ret;
}

static CK_OBJECT_HANDLE create_ecc_pub_key(CK_SESSION_HANDLE session,
    unsigned char* pubId, int pubIdLen, CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE ecc_p256_pub_key[] = {
        { CKA_CLASS,             &pubKeyClass,      sizeof(pubKeyClass)       },
        { CKA_KEY_TYPE,          &eccKeyType,       sizeof(eccKeyType)        },
        { CKA_SIGN,              &ckTrue,           sizeof(ckTrue)            },
        { CKA_EC_PARAMS,         ecc_p256_params,   sizeof(ecc_p256_params)   },
        { CKA_EC_POINT,          ecc_p256_pub,      sizeof(ecc_p256_pub)      },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                pubId,             pubIdLen                  },
    };
    static int ecc_p256_pub_key_cnt =
                             sizeof(ecc_p256_pub_key)/sizeof(*ecc_p256_pub_key);

    ret = funcList->C_CreateObject(session, ecc_p256_pub_key,
                                                     ecc_p256_pub_key_cnt, obj);
    CHECK_CKR(ret, "EC Public Key Create Object");

    return ret;
}

static CK_RV find_ecc_priv_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* privKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      privKeyTmpl[] = {
        { CKA_CLASS,     &privKeyClass,  sizeof(privKeyClass) },
        { CKA_KEY_TYPE,  &eccKeyType,    sizeof(eccKeyType)   },
        { CKA_ID,        id,             idLen                }
    };
    CK_ULONG privKeyTmplCnt = sizeof(privKeyTmpl) / sizeof(*privKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, privKeyTmpl, privKeyTmplCnt);
    CHECK_CKR(ret, "EC Private Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, privKey, 1, &count);
        CHECK_CKR(ret, "EC Private Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "EC Private Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "EC Private Key Find Objects Count");
    }

    return ret;
}

static CK_RV find_ecc_pub_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* pubKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      pubKeyTmpl[] = {
        { CKA_CLASS,     &pubKeyClass,  sizeof(pubKeyClass) },
        { CKA_KEY_TYPE,  &eccKeyType,   sizeof(eccKeyType)  },
        { CKA_ID,        id,            idLen               }
    };
    CK_ULONG pubKeyTmplCnt = sizeof(pubKeyTmpl) / sizeof(*pubKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, pubKeyTmpl, pubKeyTmplCnt);
    CHECK_CKR(ret, "EC Public Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, pubKey, 1, &count);
        CHECK_CKR(ret, "EC Public Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "EC Public Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "EC Public Key Find Objects Count");
    }

    return ret;
}
#endif

#ifndef NO_DH
static CK_OBJECT_HANDLE create_dh_priv_key(CK_SESSION_HANDLE session,
                                           unsigned char* id, int idLen,
                                           CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE dh_2048_priv_key[] = {
        { CKA_CLASS,             &privKeyClass,     sizeof(privKeyClass)      },
        { CKA_KEY_TYPE,          &dhKeyType,        sizeof(dhKeyType)         },
        { CKA_DERIVE,            &ckTrue,           sizeof(ckTrue)            },
        { CKA_PRIME,             dh_ffdhe2048_p,    sizeof(dh_ffdhe2048_p)    },
        { CKA_BASE,              dh_ffdhe2048_g,    sizeof(dh_ffdhe2048_g)    },
        { CKA_VALUE,             dh_2048_priv,      sizeof(dh_2048_priv)      },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                id,                idLen                     }
    };
    int dh_2048_priv_key_cnt =
                             sizeof(dh_2048_priv_key)/sizeof(*dh_2048_priv_key);

    ret = funcList->C_CreateObject(session, dh_2048_priv_key,
                                                     dh_2048_priv_key_cnt, obj);

    CHECK_CKR(ret, "DH Private Key Create Object");

    return ret;
}

static CK_OBJECT_HANDLE create_dh_pub_key(CK_SESSION_HANDLE session,
                                          unsigned char* id, int idLen,
                                          CK_OBJECT_HANDLE* obj)
{
    CK_RV ret;
    CK_ATTRIBUTE dh_2048_pub_key[] = {
        { CKA_CLASS,             &pubKeyClass,      sizeof(pubKeyClass)       },
        { CKA_KEY_TYPE,          &dhKeyType,        sizeof(dhKeyType)         },
        { CKA_PRIME,             dh_ffdhe2048_p,    sizeof(dh_ffdhe2048_p)    },
        { CKA_BASE,              dh_ffdhe2048_g,    sizeof(dh_ffdhe2048_g)    },
        { CKA_VALUE,             dh_2048_pub,       sizeof(dh_2048_pub)       },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                id,                idLen                     }
    };
    static int dh_2048_pub_key_cnt =
                               sizeof(dh_2048_pub_key)/sizeof(*dh_2048_pub_key);

    ret = funcList->C_CreateObject(session, dh_2048_pub_key,
                                                      dh_2048_pub_key_cnt, obj);
    CHECK_CKR(ret, "DH Public Key Create Object");

    return ret;
}

static CK_RV find_dh_priv_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* privKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      privKeyTmpl[] = {
        { CKA_CLASS,     &privKeyClass,  sizeof(privKeyClass) },
        { CKA_KEY_TYPE,  &dhKeyType,     sizeof(dhKeyType)    },
        { CKA_ID,        id,             idLen                }
    };
    CK_ULONG privKeyTmplCnt = sizeof(privKeyTmpl) / sizeof(*privKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, privKeyTmpl, privKeyTmplCnt);
    CHECK_CKR(ret, "DH Private Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, privKey, 1, &count);
        CHECK_CKR(ret, "DH Private Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "DH Private Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "DH Private Key Find Objects Count");
    }

    return ret;
}

static CK_RV find_dh_pub_key(CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE* pubKey, unsigned char* id, int idLen)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      pubKeyTmpl[] = {
        { CKA_CLASS,     &pubKeyClass,  sizeof(pubKeyClass) },
        { CKA_KEY_TYPE,  &dhKeyType,    sizeof(dhKeyType)   },
        { CKA_ID,        id,            idLen               }
    };
    CK_ULONG pubKeyTmplCnt = sizeof(pubKeyTmpl) / sizeof(*pubKeyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, pubKeyTmpl, pubKeyTmplCnt);
    CHECK_CKR(ret, "DH Public Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, pubKey, 1, &count);
        CHECK_CKR(ret, "DH Public Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "DH Public Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "DH Public Key Find Objects Count");
    }

    return ret;
}
#endif

#ifndef NO_AES
static CK_RV create_aes_128_key(CK_SESSION_HANDLE session, unsigned char* id,
                                int idLen, CK_OBJECT_HANDLE* key)
{
    CK_RV ret;
    CK_ATTRIBUTE aes_key[] = {
        { CKA_CLASS,             &secretKeyClass,   sizeof(secretKeyClass)    },
#ifndef NO_AES
        { CKA_KEY_TYPE,          &aesKeyType,       sizeof(aesKeyType)        },
#else
        { CKA_KEY_TYPE,          &genericKeyType,   sizeof(genericKeyType)    },
#endif
        { CKA_ENCRYPT,           &ckTrue,           sizeof(ckTrue)            },
        { CKA_DECRYPT,           &ckTrue,           sizeof(ckTrue)            },
        { CKA_VALUE,             aes_128_key,       sizeof(aes_128_key)       },
        { CKA_TOKEN,             &ckTrue,           sizeof(ckTrue)            },
        { CKA_ID,                id,                idLen                     },
    };
    int cnt = sizeof(aes_key)/sizeof(*aes_key);

    if (id == NULL)
        cnt -= 2;

    ret = funcList->C_CreateObject(session, aes_key, cnt, key);
    CHECK_CKR(ret, "AES-128 Key Create Object");

    return ret;
}

static CK_RV find_aes_key(CK_SESSION_HANDLE session, unsigned char* id,
                          int idLen, CK_OBJECT_HANDLE* key)
{
    CK_RV ret = CKR_OK;
    CK_ATTRIBUTE      keyTmpl[] = {
        { CKA_CLASS,     &secretKeyClass,  sizeof(secretKeyClass) },
        { CKA_KEY_TYPE,  &aesKeyType,      sizeof(aesKeyType)     },
        { CKA_ID,        id,               idLen                  }
    };
    CK_ULONG keyTmplCnt = sizeof(keyTmpl) / sizeof(*keyTmpl);
    CK_ULONG count;

    ret = funcList->C_FindObjectsInit(session, keyTmpl, keyTmplCnt);
    CHECK_CKR(ret, "AES Key Find Objects Init");
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjects(session, key, 1, &count);
        CHECK_CKR(ret, "AES Key Find Objects");
    }
    if (ret == CKR_OK) {
        ret = funcList->C_FindObjectsFinal(session);
        CHECK_CKR(ret, "AES Key Find Objects Final");
    }
    if (ret == CKR_OK && count == 0) {
        ret = -1;
        CHECK_CKR(ret, "AES Key Find Objects Count");
    }

    return ret;
}
#endif

static CK_RV pkcs11_test(int slotId, int setPin, int closeDl)
{
    CK_RV ret;
    int inited = 0;
    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE pub = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE priv = CK_INVALID_HANDLE;
#ifndef NO_RSA
    unsigned char* privId = (unsigned char *)"123rsafixedpriv";
    int privIdLen = (int)strlen((char*)privId);
    unsigned char* pubId = (unsigned char *)"123rsafixedpub";
    int pubIdLen = (int)strlen((char*)pubId);
#endif
#ifdef HAVE_ECC
    unsigned char* eccPrivId = (unsigned char *)"123eccfixedpriv";
    int eccPrivIdLen = (int)strlen((char*)eccPrivId);
    unsigned char* eccPubId = (unsigned char *)"123eccfixedpub";
    int eccPubIdLen = (int)strlen((char*)eccPubId);
#endif
#ifndef NO_DH
    unsigned char* dhPrivId = (unsigned char *)"123dhfixedpriv";
    int dhPrivIdLen = (int)strlen((char*)dhPrivId);
    unsigned char* dhPubId = (unsigned char *)"123dhfixedpub";
    int dhPubIdLen = (int)strlen((char*)dhPubId);
#endif
#ifndef NO_AES
    unsigned char* aesKeyId = (unsigned char *)"123aes128key";
    int aesKeyIdLen = (int)strlen((char*)aesKeyId);
#endif

    /* Set it global. */
    slot = slotId;

    printf("Initialize library ... ");
    ret = pkcs11_lib_init();
    if (ret == CKR_OK) {
        printf("Done\n");
    }
    if (ret == CKR_OK) {
        printf("Initialize token ... ");
        ret = pkcs11_init_token();
        if (ret == CKR_OK) {
            printf("Done\n");
        }
    }
    if (ret == CKR_OK) {
        inited = 1;

        /* Set user PIN. */
        if (setPin) {
            printf("Set user pin ... ");
            ret = pkcs11_set_user_pin(slotId);
            if (ret == CKR_OK)
                printf("Done\n");
        }

        if (ret == CKR_OK) {
            ret = pkcs11_open_session(&session);
#ifndef NO_RSA
            if (ret == CKR_OK) {
                printf("Create RSA key pair ... ");
                ret = create_rsa_priv_key(session, privId, privIdLen, &priv);
                if (ret == CKR_OK) {
                    ret = create_rsa_pub_key(session, pubId, pubIdLen, &pub);
                }
                if (ret == CKR_OK) {
                    printf("Done\n");
                }
            }
#endif
#ifdef HAVE_ECC
            (void)ecc_p256_point;
            if (ret == CKR_OK) {
                printf("Create ECC key pair ... ");
                ret = create_ecc_priv_key(session, eccPrivId, eccPrivIdLen,
                    &priv);
                if (ret == CKR_OK) {
                    ret = create_ecc_pub_key(session, eccPubId, eccPubIdLen,
                        &pub);
                }
                if (ret == CKR_OK) {
                    printf("Done\n");
                }
            }
#endif
#ifndef NO_DH
            (void)dh_2048_peer;
            if (ret == CKR_OK) {
                printf("Create DH key pair ... ");
                priv = CK_INVALID_HANDLE;
                ret = create_dh_priv_key(session, dhPrivId, dhPrivIdLen,
                    &priv);
                if (ret == CKR_OK) {
                    pub = CK_INVALID_HANDLE;
                    ret = create_dh_pub_key(session, dhPubId, dhPubIdLen,
                        &pub);
                }
                if (ret == CKR_OK) {
                    printf("Done\n");
                }
            }
#endif
            (void)genericKeyType;
#ifndef NO_AES
    #ifdef HAVE_AESGCM
            (void)aes_128_gcm_exp_tag;
            (void)aes_128_gcm_exp;
    #endif
            (void)aes_128_cbc_pad_exp;
            (void)aes_128_cbc_exp;
            if (ret == CKR_OK) {
                printf("Create AES key ... ");
                priv = CK_INVALID_HANDLE;
                ret = create_aes_128_key(session, aesKeyId, aesKeyIdLen,
                    &priv);
                if (ret == CKR_OK) {
                    printf("Done\n");
                }
            }
#endif
            pkcs11_close_session(session);
        }
    }
#ifndef WOLFPKCS11_NO_STORE
    if (inited) {
        printf("Finalize library\n");
        pkcs11_final(0);
        inited = 0;
        priv = CK_INVALID_HANDLE;
        pub = CK_INVALID_HANDLE;
    }

    if (ret == CKR_OK) {
        printf("Initialize library ... ");
        ret = pkcs11_lib_init();
        if (ret == CKR_OK) {
            printf("Done\n");
        }
    }
#endif
    if (ret == CKR_OK) {
        inited = 1;

        ret = pkcs11_open_session(&session);
        if (ret == CKR_OK) {
#ifndef NO_RSA
            printf("Find RSA key ... ");
            if (ret == CKR_OK) {
                ret = find_rsa_priv_key(session, &priv, privId, privIdLen);
            }
            if (ret == CKR_OK) {
                ret = find_rsa_pub_key(session, &pub, pubId, pubIdLen);
            }
            if (ret == CKR_OK) {
                printf("Done\n");
            }
#endif
#ifdef HAVE_ECC
            printf("Find ECC key ... ");
            if (ret == CKR_OK) {
                ret = find_ecc_priv_key(session, &priv, eccPrivId,
                                        eccPrivIdLen);
            }
            if (ret == CKR_OK) {
                ret = find_ecc_pub_key(session, &pub, eccPubId, eccPubIdLen);
            }
            if (ret == CKR_OK) {
                printf("Done\n");
            }
#endif
#ifndef NO_DH
            printf("Find DH key ... ");
            if (ret == CKR_OK) {
                ret = find_dh_priv_key(session, &priv, dhPrivId, dhPrivIdLen);
            }
            if (ret == CKR_OK) {
                ret = find_dh_pub_key(session, &pub, dhPubId, dhPubIdLen);
            }
            if (ret == CKR_OK) {
                printf("Done\n");
            }
#endif
#ifndef NO_AES
            printf("Find AES key ... ");
            if (ret == CKR_OK) {
                ret = find_aes_key(session, aesKeyId, aesKeyIdLen, &priv);
            }
            if (ret == CKR_OK) {
                printf("Done\n");
            }
#endif
            pkcs11_close_session(session);
        }
    }
    if (inited) {
        printf("Finalize library\n");
        pkcs11_final(closeDl);
    }

    return ret;
}


static CK_RV pkcs11_init(const char* library)
{
    CK_RV ret = CKR_OK;
#ifndef HAVE_PKCS11_STATIC
    void* func;

    dlib = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (dlib == NULL) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        ret = -1;
    }

    if (ret == CKR_OK) {
        func = (void*)(CK_C_GetFunctionList)dlsym(dlib, "C_GetFunctionList");
        if (func == NULL) {
            fprintf(stderr, "Failed to get function list function\n");
            ret = -1;
        }
    }

    if (ret == CKR_OK) {
        ret = ((CK_C_GetFunctionList)func)(&funcList);
        CHECK_CKR(ret, "Get Function List call");
    }

    if (ret != CKR_OK && dlib != NULL)
        dlclose(dlib);

#else
    ret = C_GetFunctionList(&funcList);
    (void)library;
#endif

    return ret;
}

/* Display the usage options of the benchmark program. */
static void Usage(void)
{
    printf("pkcs11test\n");
    printf("-?                 Help, print this usage\n");
    printf("-lib <file>        PKCS#11 library to test\n");
    printf("-slot <num>        Slot number to use\n");
    printf("-token <string>    Name of token\n");
    printf("-soPin <string>    Security Officer PIN\n");
    printf("-userPin <string>  User PIN\n");
    printf("-no-close          Do not close the PKCS#11 library before exit\n");
    printf("-v                 Verbose output\n");
}

/* Match the command line argument with the string.
 *
 * arg  Command line argument.
 * str  String to check for.
 * return 1 if the command line argument matches the string, 0 otherwise.
 */
static int string_matches(const char* arg, const char* str)
{
    int len = (int)XSTRLEN(str) + 1;
    return XSTRNCMP(arg, str, len) == 0;
}

int main(int argc, char* argv[])
{
    int ret;
    CK_RV rv;
    int slotId = WOLFPKCS11_DLL_SLOT;
    const char* libName = WOLFPKCS11_DLL_FILENAME;
    int setPin = 1;
    int closeDl = 1;

    if (!getenv("WOLFPKCS11_TOKEN_PATH")) {
        setenv("WOLFPKCS11_TOKEN_PATH", "./tests", 1);
    }

    argc--;
    argv++;
    while (argc > 0) {
        if (string_matches(*argv, "-?")) {
            Usage();
            return 0;
        }
        else if (string_matches(*argv, "-lib")) {
            argc--;
            argv++;
            if (argc == 0) {
                fprintf(stderr, "Library name not supplied\n");
                return 1;
            }
            libName = *argv;
        }
        else if (string_matches(*argv, "-token")) {
            argc--;
            argv++;
            if (argc == 0) {
                fprintf(stderr, "Token name not supplied\n");
                return 1;
            }
            tokenName = *argv;
        }
        else if (string_matches(*argv, "-soPin")) {
            argc--;
            argv++;
            if (argc == 0) {
                fprintf(stderr, "SO PIN not supplied\n");
                return 1;
            }
            soPin = (byte*)*argv;
            soPinLen = (int)XSTRLEN((const char*)soPin);
        }
        else if (string_matches(*argv, "-userPin")) {
            argc--;
            argv++;
            if (argc == 0) {
                fprintf(stderr, "User PIN not supplied\n");
                return 1;
            }
            userPin = (byte*)*argv;
        }
        else if (string_matches(*argv, "-no-close")) {
            closeDl = 0;
        }
        else if (string_matches(*argv, "-v")) {
            verbose = 1;
        }

        argc--;
        argv++;
    }

    userPinLen = (int)XSTRLEN((const char*)userPin);

    rv = pkcs11_init(libName);
    if (rv == CKR_OK) {
        rv = pkcs11_test(slotId, setPin, closeDl);
    }

    if (rv == CKR_OK)
        ret = 0;
    else
        ret = 1;
    return ret;
}

