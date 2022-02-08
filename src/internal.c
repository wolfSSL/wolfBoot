/* internal.c
 *
 * Copyright (C) 2006-2022 wolfSSL Inc.
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


#ifdef HAVE_CONFIG_H
    #include <wolfpkcs11/config.h>
#endif

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/ecc.h>

#include <wolfpkcs11/internal.h>

#ifndef HAVE_SCRYPT
    #error PKCS11 requires scrypt. Please build wolfssl with `./configure --enable-rsapss --enable-keygen --enable-pwdbased --enable-scrypt C_EXTRA_FLAGS="-DWOLFSSL_PUBLIC_MP"`
#endif

/* Size of hash calculated from PIN. */
#define PIN_HASH_SZ                    32
/* Size of seed used when calculating hash from PIN. */
#define PIN_SEED_SZ                    16
/* Size of token's label. */
#define LABEL_SZ                       32

/* Length of seed from global random to seed local random. */
#define RNG_SEED_SZ                    32

/* Maximum size of storage for generated/derived DH key. */
#define WP11_MAX_DH_KEY_SZ             (4096/8)

/* Maximum size of storage for generated/derived symmetric key. */
#ifndef NO_DH
#define WP11_MAX_SYM_KEY_SZ            (4096/8)
#elif defined(HAVE_ECC)
#define WP11_MAX_SYM_KEY_SZ            ((521+7)/8)
#else
#define WP11_MAX_SYM_KEY_SZ            64
#endif

/* Sizes for storage. */
#define WP11_MAX_IV_SZ                 16
#define WP11_MAX_GCM_NONCE_SZ          16
#define WP11_MAX_GCM_TAG_SZ            16
#define WP11_MAX_GCM_TAG_BITS          128

/* wolfSSL ECC signatures are ASN.1 encoding - need to encode and decode. */
#define ASN_INTEGER                    0x02
#define ASN_OCTET_STRING               0x04
#define ASN_OBJECT_ID                  0x06
#define ASN_SEQUENCE                   0x10
#define ASN_CONSTRUCTED                0x20
#define ASN_LONG_LENGTH                0x80

/* Create a session handle from slot id and session id. */
#define SESS_HANDLE(slot, s)           (((slot) << 16) | (s))
/* Determine slot id from session handle. */
#define SESS_HANDLE_SLOT_ID(s)         ((CK_SLOT_ID)((s) >> 16))
/* Determine session id from session handle. */
#define SESS_HANDLE_SESS_ID(s)         ((s) & 0xffff)

/* Create an object handle from a onToken and object id. */
#define OBJ_HANDLE(on, i)              (((on) << 28) | (i))
/* Determine whether object is onToken from object handle. */
#define OBJ_HANDLE_ON_TOKEN(h)         ((int)((h) >> 28))
/* Determine object id from object handle. */
#define OBJ_HANDLE_OBJ_ID(h)           ((h) & 0xfffffff)

#ifdef SINGLE_THREADED
/* Disable locking. */
typedef int WP11_Lock;

#define WP11_Lock_Init(l)              0
#define WP11_Lock_Free(l)
#define WP11_Lock_LockRW(l)            0
#define WP11_Lock_UnlockRW(l)          0
#define WP11_Lock_LockRO(l)            0
#define WP11_Lock_UnlockRO(l)          0

#else
typedef struct WP11_Lock {
    wolfSSL_Mutex read;                /* Mutex for accessing count           */
    wolfSSL_Mutex write;               /* Mutex for writing                   */
    int cnt;                           /* Count of readers                    */
} WP11_Lock;
#endif


/* Symmetric key data. */
typedef struct WP11_Data {
    byte data[WP11_MAX_SYM_KEY_SZ];    /* Key data                            */
    word32 len;                        /* Length of key data in bytes         */
} WP11_Data;

#ifndef NO_DH
typedef struct WP11_DhKey {
    byte key[WP11_MAX_DH_KEY_SZ];      /* Public or private key               */
    word32 len;                        /* Length of public key                */
    DhKey params;                      /* DH parameters object                */
} WP11_DhKey;
#endif

struct WP11_Object {
    union {
    #ifndef NO_RSA
        RsaKey rsaKey;                 /* RSA key object                      */
    #endif
    #ifdef HAVE_ECC
        ecc_key ecKey;                 /* EC key object                       */
    #endif
    #ifndef NO_DH
        WP11_DhKey dhKey;              /* DH parameters object                */
    #endif
        WP11_Data symmKey;             /* Symmetric key object                */
    } data;
    CK_KEY_TYPE type;                  /* Key type of this object             */
    word32 size;                       /* Size of the key in bits or bytes    */

    WP11_Session* session;             /* Session object belongs to           */
    WP11_Slot* slot;                   /* Slot object belongs to              */

    CK_OBJECT_HANDLE handle;           /* Handle of this object               */
    CK_OBJECT_CLASS objClass;          /* Object class                        */
    CK_MECHANISM_TYPE keyGenMech;      /* Key Gen mechanism created with      */
    byte onToken:1;                    /* Object on token or session          */
    byte local:1;                      /* Locally created object              */
    word32 flag;                       /* Flags about object                  */
    word32 opFlag;                     /* Flags of operations allowed         */

    char startDate[8];                 /* Start date of usage                 */
    char endDate[8];                   /* End data of usage                   */

    unsigned char* keyId;              /* Key identifier                      */
    int keyIdLen;                      /* Length of key identifier            */
    unsigned char* label;              /* Object label                        */
    int labelLen;                      /* length of object label              */

    WP11_Lock* lock;                   /* Object specific lock                */

    WP11_Object* next;                 /* Next object in linked list          */
};

typedef struct WP11_Find {
    int state;                         /* Whether operation is initialized    */
    CK_OBJECT_HANDLE found[WP11_FIND_MAX];
                                       /* List of object handles found        */
    int count;                         /* Count of object handles             */
    int curr;                          /* Index of last object returned       */
} WP11_Find;

#ifndef NO_RSA
#ifndef WC_NO_RSA_OAEP
typedef struct WP11_OaepParams {
    enum wc_HashType hashType;         /* Type of hash algorithm              */
    int mgf;                           /* Mask Generation Function            */
    byte* label;                       /* Label or AAD                        */
    int labelSz;                       /* Size of label in bytes              */
} WP11_OaepParams;
#endif

#ifdef WC_RSA_PSS
typedef struct WP11_PssParams {
    enum wc_HashType hashType;
    int mgf;
    int saltLen;
} WP11_PssParams;
#endif
#endif

#ifndef NO_AES
#ifdef HAVE_AES_CBC
typedef struct WP11_CbcParams {
    unsigned char iv[WP11_MAX_IV_SZ];  /* IV of CBC operation                 */
    Aes aes;                           /* AES object from wolfCrypt           */
    unsigned char partial[AES_BLOCK_SIZE];
                                       /* Partial block when streaming        */
    byte partialSz;                    /* Size of partial block data          */
} WP11_CbcParams;
#endif

#ifdef HAVE_AESGCM
typedef struct WP11_GcmParams {
    unsigned char iv[WP11_MAX_GCM_NONCE_SZ];
                                       /* IV/nonce data                       */
    int ivSz;                          /* IV/nonce size in bytes              */
    unsigned char* aad;                /* Additional Authentication Data      */
    int aadSz;                         /* AAD size in bytes                   */
    int tagBits;                       /* Authentication tag size in bits     */
    unsigned char authTag[WP11_MAX_GCM_TAG_SZ];
                                       /* Authentication tag calculated       */
    unsigned char* enc;                /* Encrypted data - cached for decrypt */
    int encSz;                         /* Size of encrypted data in bytes     */
} WP11_GcmParams;
#endif
#endif

#ifndef NO_HMAC
typedef struct WP11_Hmac {
    Hmac hmac;
    word32 hmacSz;
} WP11_Hmac;
#endif

struct WP11_Session {
    unsigned char inUse;               /* Inidicates session has been opened  */
    CK_SESSION_HANDLE handle;          /* CryptoKi API session handle value   */
    CK_MECHANISM_TYPE mechanism;       /* Op that is being performed          */
    CK_SLOT_ID slotId;                 /* Id of slot that session is on       */
    WP11_Slot* slot;                   /* Slot that session is on             */
    WP11_Object* object;               /* Linked list of objects on session   */
    int objCnt;                        /* Count of objects in session         */
    WP11_Object* curr;                 /* Current object                      */
    WP11_Find find;                    /* Find data                           */
    int init;                          /* Which op is initialized             */
    union {
#ifndef NO_RSA
    #ifndef WC_NO_RSA_OAEP
        WP11_OaepParams oaep;          /* RSA-OAEP parameters                 */
    #endif
    #ifdef WC_RSA_PSS
        WP11_PssParams pss;            /* RSA-PSS parameters                  */
    #endif
#endif
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        WP11_CbcParams cbc;            /* AES-CBC parameters                  */
    #endif
    #ifdef HAVE_AESGCM
        WP11_GcmParams gcm;            /* AES-GCM parameters                  */
    #endif
#endif
#ifndef NO_HMAC
        WP11_Hmac hmac;                /* HMAC parameters                     */
#endif
    } params;

    WP11_Session* next;                /* Next session for slot               */
};

typedef struct WP11_Token {
    char label[LABEL_SZ];              /* Token label                         */
    int state;                         /* Token initialize state              */
    byte soPin[PIN_HASH_SZ];           /* SO's PIN hashed with seed           */
    int soPinLen;                      /* Used to indicate PIN set            */
    byte soPinSeed[PIN_SEED_SZ];       /* Seed for calculating SO's PIN       */
    int soFailedLogin;                 /* Count of consecutive failed logins  */
    time_t soLastFailedLogin;          /* Time of last login if it failed     */
    time_t soFailLoginTimeout;         /* Timeout after max login fails       */
    byte userPin[PIN_HASH_SZ];         /* User's PIN hased with seed          */
    int userPinLen;                    /* Used to indicate PIN set            */
    byte userPinSeed[PIN_SEED_SZ];     /* Seed for calculating user's PIN     */
    int userFailedLogin;               /* Count of consecutive failed logins  */
    time_t userLastFailedLogin;        /* Time of last login if it failed     */
    time_t userFailLoginTimeout;       /* Timeout after max login fails       */
    WC_RNG rng;                        /* Random number generator             */
    WP11_Lock rngLock;                 /* Lock for random access              */
    WP11_Lock lock;                    /* Lock for object access              */
    int loginState;                    /* Login state of the token            */
    WP11_Object* object;               /* Linked list of token objects        */
    int objCnt;                        /* Count of objects on token           */
} WP11_Token;

struct WP11_Slot {
    CK_SLOT_ID id;                     /* CryptoKi API slot id value          */
    WP11_Token token;                  /* Token information for slot          */
    WP11_Session* session;             /* Linked list of sessions             */
    WP11_Lock lock;                    /* Lock for access to slot info        */
};


/* Number of slots. */
static int slotCnt = 1;
/* List of slot objects. */
static WP11_Slot slotList[1];
/* Global random used in random API, cryptographic operations and generating
 * seed when creating new hash of PIN.
 */
static WC_RNG globalRandom;
/* Count of times library has had init called. */
static int libraryInitCount = 0;
/* Lock for globals including global random. */
static WP11_Lock globalLock;


#ifndef SINGLE_THREADED
/**
 * Initialize a lock.
 *
 * @param  lock  [in]  Lock object.
 * @return  BAD_MUTEX_E on failure.
 *          0 on success.
 */
static int WP11_Lock_Init(WP11_Lock* lock)
{
    int ret;

    ret = wc_InitMutex(&lock->read);
    if (ret == 0) {
        ret = wc_InitMutex(&lock->write);
        if (ret != 0)
            wc_FreeMutex(&lock->read);
    }
    if (ret == 0)
        lock->cnt = 0;
    if (ret != 0)
        ret = BAD_MUTEX_E;

    return ret;
}

/**
 * Free a lock.
 *
 * @param  lock  [in]  Lock object.
 */
static void WP11_Lock_Free(WP11_Lock* lock)
{
    wc_FreeMutex(&lock->write);
    wc_FreeMutex(&lock->read);
}

/**
 * Lock for read/write.
 *
 * @param  lock  [in]  Lock object.
 * @return  BAD_MUTEX_E on failure.
 *          0 on success.
 */
static int WP11_Lock_LockRW(WP11_Lock* lock)
{
    int ret;

    ret = wc_LockMutex(&lock->write);
#ifdef DEBUG_LOCK
    fprintf(stderr, "LRW: %p - %d\n", &lock->write, lock->cnt);
#endif

    return ret;
}

/**
 * Unlock after read/write.
 *
 * @param  lock  [in]  Lock object.
 * @return  BAD_MUTEX_E on failure.
 *          0 on success.
 */
static int WP11_Lock_UnlockRW(WP11_Lock* lock)
{
    int ret;

#ifdef DEBUG_LOCK
    fprintf(stderr, "URW: %p - %d\n", &lock->write, lock->cnt);
#endif
    ret = wc_UnLockMutex(&lock->write);
    if (ret != 0)
        ret = BAD_MUTEX_E;

    return ret;
}

/**
 * Lock for read-only.
 *
 * @param  lock  [in]  Lock object.
 * @return  BAD_MUTEX_E on failure.
 *          0 on success.
 */
static int WP11_Lock_LockRO(WP11_Lock* lock)
{
    int ret;

    ret = wc_LockMutex(&lock->read);
    if (ret == 0) {
        if (++lock->cnt == 1)
            ret = wc_LockMutex(&lock->write);
#ifdef DEBUG_LOCK
        fprintf(stderr, "LRO: %p - %d\n", &lock->write, lock->cnt);
#endif
    }
    if (ret == 0)
        ret = wc_UnLockMutex(&lock->read);
    if (ret != 0)
        ret = BAD_MUTEX_E;

    return ret;
}

/**
 * Unlock after reading.
 *
 * @param  lock  [in]  Lock object.
 * @return  BAD_MUTEX_E on failure.
 *          0 on success.
 */
static int WP11_Lock_UnlockRO(WP11_Lock* lock)
{
    int ret;

    ret = wc_LockMutex(&lock->read);
    if (ret == 0) {
        if (--lock->cnt == 0)
            ret = wc_UnLockMutex(&lock->write);
#ifdef DEBUG_LOCK
        fprintf(stderr, "URO: %p - %d\n", &lock->write, lock->cnt);
#endif
    }
    if (ret == 0)
        ret = wc_UnLockMutex(&lock->read);
    if (ret != 0)
        ret = BAD_MUTEX_E;

    return ret;
}
#endif

static int Rng_New(WC_RNG* baseRng, WP11_Lock* lock, WC_RNG* rng)
{
    int ret;
    unsigned char seed[RNG_SEED_SZ];

    WP11_Lock_LockRW(lock);
    ret = wc_RNG_GenerateBlock(baseRng, seed, sizeof(seed));
    WP11_Lock_UnlockRW(lock);

    if (ret == 0)
        ret = wc_InitRngNonce_ex(rng, seed, sizeof(seed), NULL, INVALID_DEVID);

    return ret;
}

static void Rng_Free(WC_RNG* rng)
{
    wc_FreeRng(rng);
}


/**
 * Allocate and initialize a new session.
 *
 * @param  slot     [in]   Slot object session is connected with.
 * @param  handle   [in]   Session handle for the session object.
 * @param  session  [out]  New, initialized session object or NULL on error.
 * @return  MEMORY_E when dynamic memory allocation fails.
 *          0 on success.
 */
static int wp11_Session_New(WP11_Slot* slot, CK_OBJECT_HANDLE handle,
                            WP11_Session** session)
{
    int ret = 0;
    WP11_Session* sess;

    sess = XMALLOC(sizeof(*sess), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (sess == NULL)
        ret = MEMORY_E;

    if (ret == 0) {
        XMEMSET(sess, 0, sizeof(*sess));
        sess->slotId = slot->id;
        sess->slot = slot;
        sess->handle = handle;
        *session = sess;
    }

    return ret;
}

/**
 * Add a new session to the token in the slot.
 *
 * @param  slot     [in]   Slot object.
 * @param  session  [out]  New, initialized session object or NULL on error.
 * @return  MEMORY_E when out of memory.
 *          0 on success.
 */
static int wp11_Slot_AddSession(WP11_Slot* slot, WP11_Session** session)
{
    int ret;
    CK_OBJECT_HANDLE handle;

    /* Calculate session handle value. */
    if (slot->session != NULL)
        handle = slot->session->handle + 1;
    else
        handle = SESS_HANDLE(slot->id, 1);
    ret = wp11_Session_New(slot, handle, session);
    if (ret == 0) {
        /* Add to front of list */
        (*session)->next = slot->session;
        slot->session = *session;
    }

    return ret;
}

/**
 * Finalize a session - clean-up but don't clear out.
 *
 * @param  session  [in]  Session object.
 */
static void wp11_Session_Final(WP11_Session* session)
{
    WP11_Object* obj;

    if (session->inUse) {
        /* Free objects in session. */
        while ((obj = session->object) != NULL) {
            WP11_Session_RemoveObject(session, obj);
            WP11_Object_Free(obj);
        }
        session->inUse = 0;
    }
    session->curr = NULL;
    /* Finalize any find. */
    WP11_Session_FindFinal(session);

#if !defined(NO_RSA) && !defined(WC_NO_RSA_OAEP)
    if (session->mechanism == CKM_RSA_PKCS_OAEP &&
                                           session->params.oaep.label != NULL) {
        XFREE(session->params.oaep.label, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        session->params.oaep.label = NULL;
    }
#endif
#ifndef NO_RSA
#ifdef HAVE_AES_CBC
    if (session->mechanism == CKM_AES_CBC && session->init) {
        wc_AesFree(&session->params.cbc.aes);
        session->init = 0;
    }
#endif
#ifdef HAVE_AESGCM
    if (session->mechanism == CKM_AES_GCM) {
        if (session->params.gcm.aad != NULL) {
            XFREE(session->params.gcm.aad, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            session->params.gcm.aad = NULL;
        }
        if (session->params.gcm.enc != NULL) {
            XFREE(session->params.gcm.enc, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            session->params.gcm.enc = NULL;
        }
    }
#endif
#endif
}

/**
 * Initialize the token.
 *
 * @param  token  [in]  Token object.
 */
static int wp11_Token_Init(WP11_Token* token, const char* label)
{
    int ret;

    ret = WP11_Lock_Init(&token->lock);
    if (ret == 0)
        ret = WP11_Lock_Init(&token->rngLock);
    if (ret == 0)
        ret = Rng_New(&globalRandom, &globalLock, &token->rng);
    if (ret == 0) {
        token->state = WP11_TOKEN_STATE_INITIALIZED;
        token->loginState = WP11_APP_STATE_RW_PUBLIC;
        XMEMCPY(token->label, label, sizeof(token->label));
    }

    return ret;
}

/**
 * Free the dynamic memory associated with the token.
 *
 * @param  token  [in]  Token object.
 */
static void wp11_Token_Final(WP11_Token* token)
{
    WP11_Object* obj = token->object;
    WP11_Object* next;

    while (obj != NULL) {
        next = obj->next;
        WP11_Object_Free(obj);
        obj = next;
    }
    Rng_Free(&token->rng);
    WP11_Lock_Free(&token->rngLock);
    WP11_Lock_Free(&token->lock);
    XMEMSET(token, 0, sizeof(*token));
}

/**
 * Free first session in slot and any others not in use down to a minimum.
 *
 * @param  slot  [in]  Slot object.
 */
static void wp11_Slot_FreeSession(WP11_Slot* slot, WP11_Session* session)
{
    WP11_Session* curr;

    if (session == slot->session) {
        /* Free the first session as it is no longer required. */
        curr = slot->session;
        slot->session = curr->next;
        wp11_Session_Final(curr);
        XFREE(curr, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }

    /* Free the leading unused sessions down to the minimum. */
    while (slot->session != NULL && !slot->session->inUse &&
            SESS_HANDLE_SESS_ID(slot->session->handle) > WP11_SESSION_CNT_MIN) {
        curr = slot->session;
        slot->session = slot->session->next;
        wp11_Session_Final(curr);
        XFREE(curr, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }
}

/**
 * Free dynamic memory associated with the slot.
 *
 * @param  slot  [in]  Slot object.
 */
static void wp11_Slot_Final(WP11_Slot* slot)
{
    while (slot->session != NULL)
        wp11_Slot_FreeSession(slot, slot->session);
    wp11_Token_Final(&slot->token);
    WP11_Lock_Free(&slot->lock);
}

/**
 * Initialize a slot.
 *
 * @param  slot  [in]  Slot object.
 * @param  id    [in]  Slot id for slot.
 * @return  MEMORY_E when failing to allocation session.
 *          BAD_MUTEX_E when the lock initialization failed.
 *          0 on success.
 */
static int wp11_Slot_Init(WP11_Slot* slot, int id)
{
    int ret = 0;
    int i;
    WP11_Session* curr;
    char label[LABEL_SZ] = { 0, };

    XMEMSET(slot, 0, sizeof(*slot));
    slot->id = id;

    ret = WP11_Lock_Init(&slot->lock);
    if (ret == 0) {
        /* Create the minimum number of unused sessions. */
        for (i = 0; ret == 0 && i < WP11_SESSION_CNT_MIN; i++)
            ret = wp11_Slot_AddSession(slot, &curr);

        if (ret == 0) {
            ret = wp11_Token_Init(&slot->token, label);
            slot->token.state = WP11_TOKEN_STATE_UNKNOWN;
        }

        if (ret != 0)
            wp11_Slot_Final(slot);
    }

    return ret;
}


/**
 * Initialize the globals for the library.
 * Multiple initializations allowed.
 *
 * @return  MEMORY_E when out of memory.
 *          BAD_MUTEX_E when initializing lock fails.
 *          0 on success.
 */
int WP11_Library_Init(void)
{
    int ret = 0;
    int i;

    if (libraryInitCount == 0) {
        ret = WP11_Lock_Init(&globalLock);
        if (ret == 0)
            ret = wc_InitRng(&globalRandom);
        if (ret == 0) {
            for (i = 0; i < slotCnt; i++)
                ret = wp11_Slot_Init(&slotList[i], i + 1);
        }
    }
    if (ret == 0) {
        WP11_Lock_LockRW(&globalLock);
        libraryInitCount++;
        WP11_Lock_UnlockRW(&globalLock);
    }

    return ret;
}

/**
 * Finalize the globals for the library.
 * Multiple finalizations allowed.
 */
void WP11_Library_Final(void)
{
    int i;
    int cnt;

    WP11_Lock_LockRW(&globalLock);
    cnt = --libraryInitCount;
    WP11_Lock_UnlockRW(&globalLock);
    if (cnt == 0) {
        /* Cleanup the slots. */
        for (i = 0; i < slotCnt; i++)
            wp11_Slot_Final(&slotList[i]);

        wc_FreeRng(&globalRandom);
        WP11_Lock_Free(&globalLock);
    }
}

/**
 * Checks if the library is initialized.
 *
 * @return  0 when library is not initialized.
 *          1 when library is initialized.
 */
int WP11_Library_IsInitialized(void)
{
    int ret;

    WP11_Lock_LockRO(&globalLock);
    ret = libraryInitCount > 0;
    WP11_Lock_UnlockRO(&globalLock);

    return ret;
}

/**
 * Check if slot id is valid.
 *
 * @param  slotid  [in]  Slot handle value.
 * @return  0 when not valid.
 *          1 when is valid.
 */
int WP11_SlotIdValid(CK_SLOT_ID slotId)
{
    return slotId > 0 && slotId <= (CK_SLOT_ID)slotCnt;
}

/**
 * Get the list of slots identifiers.
 * When slotIdList is NULL the count is returned.
 * Otherwise, the count must be equal to or larger than the number of slot
 * identifiers to be returned.
 *
 * @param  tokenIn     [in]       Whether the token must be in.
 * @param  slotIdList  [in]       Where the list of slot identifier is stored.
 * @param  count       [in, out]  On in, the number of entries in slotIdList.
 *                                On out, the number of entries put in
 *                                slotIdList.
 * @return  BUFFER_E when the count is too small.
 *          0 on success.
 */
int WP11_GetSlotList(int tokenIn, CK_SLOT_ID* slotIdList, CK_ULONG* count)
{
    int ret = 0;
    int i;

    /* All slots are assumed to have a token inserted. */
    (void)tokenIn;

    if (slotIdList == NULL)
        *count = slotCnt;
    else if ((int)*count < slotCnt)
        ret = BUFFER_E;
    else {
        for (i = 0; i < slotCnt && i < (int)*count; i++)
            slotIdList[i] = i + 1;
        *count = i;
    }

    return ret;
}

/**
 * Get the Slot object with the id.
 *
 * @param  slotid  [in]   Slot id.
 * @param  slot    [out]  Slot object.
 * @return  BAD_FUNC_ARG when the slot handle is not valid.
 *          0 on success.
 */
int WP11_Slot_Get(CK_SLOT_ID slotId, WP11_Slot** slot)
{
    int ret = 0;

    if (WP11_SlotIdValid(slotId))
        *slot = &slotList[slotId - 1];
    else
        ret = BAD_FUNC_ARG;

    return ret;
}

/**
 * Open a new session on the token in the slot.
 * Notification callback and application data ignored.
 *
 * @param  slot    [in]  Slot object.
 * @param  flags   [in]  Read/write or read-only session flag and others.
 * @param  app     [in]  Application data passed in notification callback.
 * @param  notify  [in]  Notification callback.
 * @return  BAD_FUNC_ARG when the slot handle is not valid.
 *          0 on success.
 */
int WP11_Slot_OpenSession(WP11_Slot* slot, unsigned long flags, void* app,
                          CK_NOTIFY notify, CK_SESSION_HANDLE* session)
{
    int ret = 0;
    WP11_Session* curr;

    WP11_Lock_LockRW(&slot->lock);
    /* Cannot open a read-only session if SO is logged in. */
    if ((flags & CKF_RW_SESSION) == 0) {
        if (slot->token.loginState == WP11_APP_STATE_RW_SO)
            ret = SESSION_EXISTS_E;
    }

    if (ret == 0) {
        /* Find and unused session. */
        curr = slot->session;
        for (curr = slot->session; curr != NULL; curr = curr->next) {
            if (!curr->inUse)
                break;
        }
        /* None found and already at max means cannot create a new session. */
        if (curr == NULL && slot->session != NULL &&
                                   SESS_HANDLE_SESS_ID(slot->session->handle) ==
                                                         WP11_SESSION_CNT_MAX) {
            ret = SESSION_COUNT_E;
        }
    }

    /* Add a new session. */
    if (ret == 0 && curr == NULL)
        ret = wp11_Slot_AddSession(slot, &curr);

    /* Return the handle of the session. */
    if (ret == 0) {
        /* Set slot read/write state. */
        if ((flags & CKF_RW_SESSION) == CKF_RW_SESSION)
            curr->inUse = WP11_SESSION_RW;
        else
            curr->inUse = WP11_SESSION_RO;
        *session = curr->handle;
    }
    WP11_Lock_UnlockRW(&slot->lock);

    /* Ignored at this time. */
    (void)app;
    (void)notify;

    return ret;
}

/**
 * Close a session associated with a slot.
 *
 * @param  slot     [in]  Slot of session.
 * @param  session  [in]  Session to close.
 */
void WP11_Slot_CloseSession(WP11_Slot* slot, WP11_Session* session)
{
    int dynamic;
    int noMore = 1;
    WP11_Session* curr;

    WP11_Lock_LockRW(&slot->lock);
    /* Only free the session object if it is on top and there is more than the
     * minimum number of sessions associated with the slot.
     */
    dynamic = slot->session == session &&
                    SESS_HANDLE_SESS_ID(session->handle) > WP11_SESSION_CNT_MIN;

    if (dynamic)
        wp11_Slot_FreeSession(slot, session);
    else
        wp11_Session_Final(session);
    WP11_Lock_UnlockRW(&slot->lock);

    WP11_Lock_LockRO(&slot->lock);
    for (curr = slot->session; curr != NULL; curr = curr->next) {
        if (curr->inUse) {
            noMore = 0;
            break;
        }
    }
    WP11_Lock_UnlockRO(&slot->lock);
    if (noMore)
        WP11_Slot_Logout(slot);
}

/**
 * Close all sessions associated with a slot.
 *
 * @param  slot  [in]  Slot object.
 */
void WP11_Slot_CloseSessions(WP11_Slot* slot)
{
    WP11_Session* curr;

    /* Free all sessions down to minimum. */
    while (slot->session != NULL &&
            SESS_HANDLE_SESS_ID(slot->session->handle) > WP11_SESSION_CNT_MIN) {
        wp11_Slot_FreeSession(slot, slot->session);
    }
    WP11_Lock_LockRW(&slot->lock);
    /* Finalize the rest. */
    for (curr = slot->session; curr != NULL; curr = curr->next)
        wp11_Session_Final(slot->session);
    WP11_Lock_UnlockRW(&slot->lock);
}

/**
 * Check for a session in use that is associated with the slot.
 *
 * @param  slot  [in]  Slot object.
 * @return  1 when a session is in use.
 *          0 when no sessions are in use.
 */
int WP11_Slot_HasSession(WP11_Slot* slot)
{
    int ret;
    WP11_Session* curr;

    WP11_Lock_LockRO(&slot->lock);
    /* Find a session in use. */
    curr = slot->session;
    while (curr != NULL) {
        if (curr->inUse)
            break;
        curr = curr->next;
    }
    ret = curr != NULL;
    WP11_Lock_UnlockRO(&slot->lock);

    return ret;
}

/**
 * Hash the PIN into a secret.
 * WP11_HASH_PIN_COST, WP11_HASH_PIN_BLOCKSIZE and WP11_HASH_PIN_PARALLEL have
 * default values but can be specified at compile time.
 *
 * @param  pin      [in]  PIN to hash.
 * @param  pinLen   [in]  PIN length in bytes.
 * @param  seed     [in]  Seed for hashing.
 * @param  seedLen  [in]  Seed length in bytes.
 * @param  hash     [in]  Buffer to hold hash result.
 * @param  hashlen  [in]  Size of buffer.
 * @return  0 on success.
 *          -ve on failure.
 */
static int HashPIN(char* pin, int pinLen, byte* seed, int seedLen, byte* hash,
                   int hashLen)
{
    /* Convert PIN into secret using scrypt algorithm. */
    return wc_scrypt(hash, (byte*)pin, pinLen, seed, seedLen,
                                    WP11_HASH_PIN_COST, WP11_HASH_PIN_BLOCKSIZE,
                                    WP11_HASH_PIN_PARALLEL, hashLen);
}

/**
 * Reset the token.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  SO PIN to set.
 * @param  pinLen  [in]  Length of PIN in bytes.
 * @param  label   [in]  Name of token.
 * @return  0 on success.
 *          -ve on failure.
 */
int WP11_Slot_TokenReset(WP11_Slot* slot, char* pin, int pinLen, char* label)
{
    int ret;
    WP11_Token* token;

    WP11_Lock_LockRW(&slot->lock);
    /* Zeroizes token. */
    token = &slot->token;
    wp11_Token_Final(token);
    wp11_Token_Init(token, label);
    WP11_Lock_UnlockRW(&slot->lock);

    /* Locking used in setting SO PIN. */
    ret = WP11_Slot_SetSOPin(slot, pin, pinLen);

    return ret;
}

/**
 * Check the PIN is correct for SO (Security officer).
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to check.
 * @param  pinLen  [in]  Length of PIN.
 * @return  PIN_NOT_SET_E when the token is not initialized.
 *          PIN_INVALID_E when the PIN is not correct.
 *          Other -ve value when hashing PIN fails.
 *          0 when PIN is correct.
 */
int WP11_Slot_CheckSOPin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    WP11_Token* token;
    byte hash[PIN_HASH_SZ];

    WP11_Lock_LockRO(&slot->lock);
    token = &slot->token;
    if (token->state != WP11_TOKEN_STATE_INITIALIZED || token->soPinLen == 0)
        ret = PIN_NOT_SET_E;
    if (ret == 0) {
        WP11_Lock_UnlockRO(&slot->lock);

        /* Costly Operation done out of lock. */
        ret = HashPIN(pin, pinLen, token->soPinSeed, sizeof(token->soPinSeed),
                                                            hash, sizeof(hash));

        WP11_Lock_LockRO(&slot->lock);
    }
    if (ret == 0 && XMEMCMP(hash, token->soPin, token->soPinLen) != 0)
        ret = PIN_INVALID_E;
    WP11_Lock_UnlockRO(&slot->lock);

    return ret;
}

/**
 * Check the PIN is correct for user.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to check.
 * @param  pinLen  [in]  Length of PIN.
 * @return  PIN_NOT_SET_E when the token is not initialized.
 *          PIN_INVALID_E when the PIN is not correct.
 *          Other -ve value when hashing PIN fails.
 *          0 when PIN is correct.
 */
int WP11_Slot_CheckUserPin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    WP11_Token* token;
    byte hash[PIN_HASH_SZ];

    WP11_Lock_LockRO(&slot->lock);
    token = &slot->token;
    if (token->state != WP11_TOKEN_STATE_INITIALIZED || token->userPinLen == 0)
        ret = PIN_NOT_SET_E;

    if (ret == 0) {
        WP11_Lock_UnlockRO(&slot->lock);

        /* Costly Operation done out of lock. */
        ret = HashPIN(pin, pinLen, token->userPinSeed,
                                sizeof(token->userPinSeed), hash, sizeof(hash));

        WP11_Lock_LockRO(&slot->lock);
    }
    if (ret == 0 && XMEMCMP(hash, token->userPin, token->userPinLen) != 0)
        ret = PIN_INVALID_E;
    WP11_Lock_UnlockRO(&slot->lock);

    return ret;
}

/**
 * Log the SO (Security Officer) into the token.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to use to login.
 * @param  pinLen  [in]  Length of PIN.
 * @return  READ_ONLY_E when there is a read-only session open.
 *          PIN_NOT_SET_E when the token is not initialized.
 *          PIN_INVALID_E when the PIN is not correct.
 *          Other -ve value when hashing PIN fails.
 *          0 on success.
 */
int WP11_Slot_SOLogin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    WP11_Session* curr;
    time_t now;
    time_t allowed;
    int state;

    if (wc_GetTime(&now, sizeof(now)) != 0)
        ret = PIN_INVALID_E;

    WP11_Lock_LockRO(&slot->lock);
    if (ret == 0) {
        /* Have we already logged in? */
        state = slot->token.loginState;
        if (state == WP11_APP_STATE_RW_SO || state == WP11_APP_STATE_RO_USER ||
                                              state == WP11_APP_STATE_RW_USER) {
            ret = LOGGED_IN_E;
        }
    }
    /* Check for too many fails and timeout. */
    if (ret == 0 && slot->token.soFailedLogin == WP11_MAX_LOGIN_FAILS_SO) {
        allowed = slot->token.soLastFailedLogin +
                                                 slot->token.soFailLoginTimeout;
        if (allowed < now)
            slot->token.soFailedLogin = 0;
        else
            ret = PIN_INVALID_E;
    }
    if (ret == 0) {
        for (curr = slot->session; curr != NULL; curr = curr->next) {
            if (curr->inUse == WP11_SESSION_RO)
                break;
        }
        if (curr != NULL)
            ret = READ_ONLY_E;
    }
    WP11_Lock_UnlockRO(&slot->lock);

    if (ret == 0) {
        ret = WP11_Slot_CheckSOPin(slot, pin, pinLen);
        WP11_Lock_LockRW(&slot->lock);
        /* PIN Failed - Update failure info. */
        if (ret == PIN_INVALID_E) {
            slot->token.soFailedLogin++;
            if (slot->token.soFailedLogin == WP11_MAX_LOGIN_FAILS_SO) {
                slot->token.soLastFailedLogin = now;
                slot->token.soFailLoginTimeout += WP11_SO_LOGIN_FAIL_TIMEOUT;
            }
        }
        /* Worked - clear failure info. */
        else if (ret == 0) {
            slot->token.soFailedLogin = 0;
            slot->token.soLastFailedLogin = 0;
            slot->token.soFailLoginTimeout = 0;
        }
        WP11_Lock_UnlockRW(&slot->lock);
    }

    if (ret == 0) {
        WP11_Lock_LockRW(&slot->lock);
        slot->token.loginState = WP11_APP_STATE_RW_SO;
        WP11_Lock_UnlockRW(&slot->lock);
    }

    return ret;
}

/**
 * Log the user into the token.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to use to login.
 * @param  pinLen  [in]  Length of PIN.
 * @return  READ_ONLY_E when there is a read-only session open.
 *          PIN_NOT_SET_E when the token is not initialized.
 *          PIN_INVALID_E when the PIN is not correct.
 *          Other -ve value when hashing PIN fails.
 *          0 on success.
 */
int WP11_Slot_UserLogin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    time_t now;
    time_t allowed;
    int state;

    if (wc_GetTime(&now, sizeof(now)) != 0)
        ret = PIN_INVALID_E;

    WP11_Lock_LockRW(&slot->lock);
    /* Have we already logged in? */
    if (ret == 0) {
        /* Have we already logged in? */
        state = slot->token.loginState;
        if (state == WP11_APP_STATE_RW_SO || state == WP11_APP_STATE_RO_USER ||
                                              state == WP11_APP_STATE_RW_USER) {
            ret = LOGGED_IN_E;
        }
    }
    /* Check for too many fails */
    if (ret == 0 && slot->token.userFailedLogin == WP11_MAX_LOGIN_FAILS_USER) {
        allowed = slot->token.userLastFailedLogin +
                                               slot->token.userFailLoginTimeout;
        if (allowed < now)
            slot->token.userFailedLogin = 0;
        else
            ret = PIN_INVALID_E;
    }
    WP11_Lock_UnlockRW(&slot->lock);

    if (ret == 0) {
        ret = WP11_Slot_CheckUserPin(slot, pin, pinLen);
        WP11_Lock_LockRW(&slot->lock);
        /* PIN Failed - Update failure info. */
        if (ret == PIN_INVALID_E) {
            slot->token.userFailedLogin++;
            if (slot->token.userFailedLogin == WP11_MAX_LOGIN_FAILS_USER) {
                slot->token.userLastFailedLogin = now;
                slot->token.userFailLoginTimeout +=
                                                   WP11_USER_LOGIN_FAIL_TIMEOUT;
            }
        }
        /* Worked - clear failure info. */
        else if (ret == 0) {
            slot->token.userFailedLogin = 0;
            slot->token.userLastFailedLogin = 0;
            slot->token.userFailLoginTimeout = 0;
        }
        WP11_Lock_UnlockRW(&slot->lock);
    }

    if (ret == 0) {
        WP11_Lock_LockRW(&slot->lock);
        slot->token.loginState = WP11_APP_STATE_RW_USER;
        WP11_Lock_UnlockRW(&slot->lock);
    }

    return ret;

}

/**
 * Set the SO's (Security Officer's) PIN.
 * Store the hash of the PIN and new seed.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to set.
 * @param  pinLen  [in]  Length of PIN.
 * @return  -ve value when generating random or hashing PIN fails.
 *          0 on success.
 */
int WP11_Slot_SetSOPin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    WP11_Token* token;

    WP11_Lock_LockRW(&slot->lock);
    token = &slot->token;
    /* New seed each time. */
    WP11_Lock_LockRW(&slot->token.rngLock);
    ret = wc_RNG_GenerateBlock(&slot->token.rng, token->soPinSeed,
                                                      sizeof(token->soPinSeed));
    WP11_Lock_UnlockRW(&slot->token.rngLock);
    if (ret == 0) {
        WP11_Lock_UnlockRW(&slot->lock);
        /* Costly Operation done out of lock. */
        ret = HashPIN(pin, pinLen, token->soPinSeed,
                                         sizeof(token->soPinSeed), token->soPin,
                                         sizeof(token->soPin));
        WP11_Lock_LockRW(&slot->lock);
    }
    if (ret == 0)
        token->soPinLen = sizeof(token->soPin);
    WP11_Lock_UnlockRW(&slot->lock);

    return ret;
}

/**
 * Set the User's PIN.
 * Store the hash of the PIN and new seed.
 *
 * @param  slot    [in]  Slot object.
 * @param  pin     [in]  PIN to set.
 * @param  pinLen  [in]  Length of PIN.
 * @return  -ve value when generating random or hashing PIN fails.
 *          0 on success.
 */
int WP11_Slot_SetUserPin(WP11_Slot* slot, char* pin, int pinLen)
{
    int ret = 0;
    WP11_Token* token;

    WP11_Lock_LockRW(&slot->lock);
    token = &slot->token;
    /* New seed each time. */
    WP11_Lock_LockRW(&slot->token.rngLock);
    ret = wc_RNG_GenerateBlock(&slot->token.rng, token->userPinSeed,
                                                    sizeof(token->userPinSeed));
    WP11_Lock_UnlockRW(&slot->token.rngLock);
    if (ret == 0) {
        WP11_Lock_UnlockRW(&slot->lock);
        /* Costly Operation done out of lock. */
        ret = HashPIN(pin, pinLen, token->userPinSeed,
                                     sizeof(token->userPinSeed), token->userPin,
                                     sizeof(token->userPin));
        WP11_Lock_LockRW(&slot->lock);
    }
    if (ret == 0)
        token->userPinLen = sizeof(token->userPin);
    WP11_Lock_UnlockRW(&slot->lock);

    return ret;
}

/**
 * Logout of the token.
 *
 * @param  slot  [in]  Slot object referencing token.
 */
void WP11_Slot_Logout(WP11_Slot* slot)
{
    WP11_Lock_LockRW(&slot->lock);
    slot->token.loginState = WP11_APP_STATE_RW_PUBLIC;
    WP11_Lock_UnlockRW(&slot->lock);
}

/**
 * Retrieve the token's label.
 * A token's label is 32 bytes long.
 *
 * @param  slot   [in]  Slot object.
 * @param  label  [in]  Buffer to put label in.
 */
void WP11_Slot_GetTokenLabel(WP11_Slot* slot, char* label)
{
    char* tokenLabel;

    WP11_Lock_LockRO(&slot->lock);
    tokenLabel = slot->token.label;
    /* An unset label is all zeros - label is padded with ' ' and no NUL. */
    if (tokenLabel[0] == '\0')
        XMEMSET(label, ' ', LABEL_SZ);
    else
        XMEMCPY(label, tokenLabel, LABEL_SZ);
    WP11_Lock_UnlockRO(&slot->lock);
}

/**
 * Check if token has been initialized.
 *
 * @param  slot  [in]  Slot object referencing token.
 * @return  1 when token is initialized.
 *          0 when token is not initialized.
 */
int WP11_Slot_IsTokenInitialized(WP11_Slot* slot)
{
    int ret;

    WP11_Lock_LockRO(&slot->lock);
    ret = slot->token.state != WP11_TOKEN_STATE_UNKNOWN;
    WP11_Lock_UnlockRO(&slot->lock);

    return ret;
}

/**
 * Get the number of failed logins on the slot/token for the login type.
 *
 * @param  slot   [in]  Slot object.
 * @param  login  [in]  Security officer or user.
 * @return  Count of consecutive failed logins.
 */
int WP11_Slot_TokenFailedLogin(WP11_Slot* slot, int login)
{
    if (login == WP11_LOGIN_SO)
        return slot->token.soFailedLogin;
    else
        return slot->token.userFailedLogin;
}

/**
 * Get the expirary time of failed login timeout on the slot/token for the login
 * type.
 *
 * @param  slot   [in]  Slot object.
 * @param  login  [in]  Security officer or user.
 * @return  Count of consecutive failed logins.
 */
time_t WP11_Slot_TokenFailedExpire(WP11_Slot* slot, int login)
{
    if (login == WP11_LOGIN_SO)
        return slot->token.soLastFailedLogin + slot->token.soFailLoginTimeout;
    else {
        return slot->token.userLastFailedLogin +
                                               slot->token.userFailLoginTimeout;
    }
}

/**
 * Check whether the User PIN has been initialized for this slot/token.
 *
 * @param  slot   [in]  Slot object.
 * @return  1 when PIN initialized.
 *          0 when PIN not initialized.
 */
int WP11_Slot_IsTokenUserPinInitialized(WP11_Slot* slot)
{
    return slot->token.userPinLen > 0;
}

/**
 * Get the session object identified by the session handle.
 *
 * @param  sessionHandle  [in]   Session handle identifier.
 * @param  session        [out]  Session object with the session handle.
 * @return  BAD_FUNC_ARG when no session object found.
 *          0 when session object found.
 */
int WP11_Session_Get(CK_SESSION_HANDLE sessionHandle, WP11_Session** session)
{
    int ret = 0;
    CK_SLOT_ID slotHandle = SESS_HANDLE_SLOT_ID(sessionHandle);
    WP11_Slot* slot;
    WP11_Session *sess;

    ret = WP11_Slot_Get(slotHandle, &slot);
    if (ret == 0) {
        WP11_Lock_LockRO(&slot->lock);
        sess = slot->session;
        while (sess != NULL && sess->handle != sessionHandle)
            sess = sess->next;
        if (sess == NULL || !sess->inUse)
            ret = BAD_FUNC_ARG;
        else
            *session = sess;
        WP11_Lock_UnlockRO(&slot->lock);
    }

    return ret;
}

/**
 * Get the current state of the session.
 *
 * @param  session  [in]  Session object.
 * @return  The session state.
 */
int WP11_Session_GetState(WP11_Session* session)
{
    int ret;

    WP11_Lock_LockRO(&session->slot->lock);
    if (session->slot->token.loginState == WP11_APP_STATE_RW_SO)
        ret = WP11_APP_STATE_RW_SO;
    else if (session->slot->token.loginState == WP11_APP_STATE_RW_USER) {
        if (session->inUse == WP11_SESSION_RW)
            ret = WP11_APP_STATE_RW_USER;
        else
            ret = WP11_APP_STATE_RO_USER;
    }
    else {
        if (session->inUse == WP11_SESSION_RW)
            ret = WP11_APP_STATE_RW_PUBLIC;
        else
            ret = WP11_APP_STATE_RO_PUBLIC;
    }
    WP11_Lock_UnlockRO(&session->slot->lock);

    return ret;
}

/**
 * Return whether this session is in use.
 *
 * @param  session  [in]  Session object.
 * @return  1 when session is in used.
 *          0 when session is not in used.
 */
int WP11_Session_IsRW(WP11_Session* session)
{
    return session->inUse == WP11_SESSION_RW;
}

/**
 * Return whether this session has been initialized for the operation.
 *
 * @param  session  [in]  Session object.
 * @param  init     [in]  Operation to check.
 * @return  1 when session is in used.
 *          0 when session is not in used.
 */
int WP11_Session_IsOpInitialized(WP11_Session* session, int init)
{
    return session->init == init;
}

/**
 * Set the operation this session has been initialized for.
 *
 * @param  session  [in]  Session object.
 */
void WP11_Session_SetOpInitialized(WP11_Session* session, int init)
{
    session->init = init;
}

/**
 * Get the slot object associated with the session.
 *
 * @param  session  [in]  Session object.
 * @return  Slot object.
 */
WP11_Slot* WP11_Session_GetSlot(WP11_Session* session)
{
    return session->slot;
}

/**
 * Get the mechansim associated with the session.
 *
 * @param  session  [in]  Session object.
 * @return  Mechanism of the session.
 */
CK_MECHANISM_TYPE WP11_Session_GetMechanism(WP11_Session* session)
{
    return session->mechanism;
}

/**
 * Set the mechansim for this session.
 *
 * @param  session    [in]  Session object.
 * @param  mechansim  [in]  Mechanism value.
 */
void WP11_Session_SetMechanism(WP11_Session* session,
                               CK_MECHANISM_TYPE mechanism)
{
    session->mechanism = mechanism;
}

#ifndef NO_RSA
#if !defined(WC_NO_RSA_OAEP) || defined(WC_RSA_PSS)
/**
 * Convert the digest mechanism to a hash type for wolfCrypt.
 *
 * @param  hashMech  [in]   Digest mechanism.
 * @param  hashType  [out]  Hash type.
 * @return  BAD_FUNC_ARG when the digest mechanism is not recognized.
 *          0 on success.
 */
static int wp11_hash_type(CK_MECHANISM_TYPE hashMech,
                          enum wc_HashType *hashType)
{
    int ret = 0;

    switch (hashMech) {
        case CKM_SHA1:
            *hashType = WC_HASH_TYPE_SHA;
            break;
        case CKM_SHA224:
            *hashType = WC_HASH_TYPE_SHA224;
            break;
        case CKM_SHA256:
            *hashType = WC_HASH_TYPE_SHA256;
            break;
        case CKM_SHA384:
            *hashType = WC_HASH_TYPE_SHA384;
            break;
        case CKM_SHA512:
            *hashType = WC_HASH_TYPE_SHA512;
            break;
        default:
            ret = BAD_FUNC_ARG;
            break;
    }

    return ret;
}

/**
 * Convert the mask generation function id to a wolfCrypt MGF id.
 *
 * @param  mgfType  [in]   Mask generation function id.
 * @param  mgf      [out]  wolfCrypt MGF id.
 * @return  BAD_FUNC_ARG when the mask generation function id is not recognized.
 *          0 on success.
 */
static int wp11_mgf(CK_MECHANISM_TYPE mgfType, int *mgf)
{
    int ret = 0;

    switch (mgfType) {
        case CKG_MGF1_SHA1:
            *mgf = WC_MGF1SHA1;
            break;
        case CKG_MGF1_SHA224:
            *mgf = WC_MGF1SHA224;
            break;
        case CKG_MGF1_SHA256:
            *mgf = WC_MGF1SHA256;
            break;
        case CKG_MGF1_SHA384:
            *mgf = WC_MGF1SHA384;
            break;
        case CKG_MGF1_SHA512:
            *mgf = WC_MGF1SHA512;
            break;
        default:
            ret = BAD_FUNC_ARG;
            break;
    }

    return ret;
}
#endif

#ifndef WC_NO_RSA_OAEP
/**
 * Set the parameters to use for an OAEP operation.
 *
 * @param  session  [in]  Session object.
 * @param  hashAlg  [in]  Digest algorithm id.
 * @param  mgf      [in]  Mask generation function id.
 * @param  label    [in]  Additional authentication data.
 * @param  labelSz  [in]  Length of data in bytes.
 * @return  MEMORY_E when dynamic memory allocation fails.
 *          BAD_FUNC_ARG when the digest algorithm id or the mask generation
 *          function id are not recognized.
 *          0 on success.
 */
int WP11_Session_SetOaepParams(WP11_Session* session, CK_MECHANISM_TYPE hashAlg,
                               CK_MECHANISM_TYPE mgf, byte* label, int labelSz)
{
    int ret;
    WP11_OaepParams* oaep = &session->params.oaep;

    XMEMSET(oaep, 0, sizeof(*oaep));
    ret = wp11_hash_type(hashAlg, &oaep->hashType);
    if (ret == 0)
        ret = wp11_mgf(mgf, &oaep->mgf);
    if (ret == 0 && label == NULL) {
        oaep->label = NULL;
        oaep->labelSz = 0;
    }
    if (ret == 0 && label != NULL) {
        oaep->label = XMALLOC(labelSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (oaep->label == NULL)
            ret = MEMORY_E;
        else {
            XMEMCPY(oaep->label, label, labelSz);
            oaep->labelSz = labelSz;
        }
    }

    return ret;
}
#endif

#ifdef WC_RSA_PSS
/**
 * Set the parameters to use for a PSS operation.
 *
 * @param  session  [in]  Session object.
 * @param  hashAlg  [in]  Digest algorithm id.
 * @param  mgf      [in]  Mask generation function id.
 * @param  sLen     [in]  Salt length.
 * @return  BAD_FUNC_ARG when the digest algorithm id or the mask generation
 *          function id are not recognized or salt length is too big.
 *          0 on success.
 */
int WP11_Session_SetPssParams(WP11_Session* session, CK_MECHANISM_TYPE hashAlg,
                              CK_MECHANISM_TYPE mgf, int sLen)
{
    int ret;
    WP11_PssParams* pss = &session->params.pss;

    XMEMSET(pss, 0, sizeof(*pss));
    ret = wp11_hash_type(hashAlg, &pss->hashType);
    if (ret == 0)
        ret = wp11_mgf(mgf, &pss->mgf);
    if (ret == 0 && sLen > RSA_PSS_SALT_MAX_SZ)
        ret = BAD_FUNC_ARG;
    else
        pss->saltLen = sLen;

    return ret;
}
#endif
#endif

#ifndef NO_AES
#ifdef HAVE_AES_CBC
/**
 * Set the parameters to use for an AES-CBC operation.
 *
 * @param  session  [in]  Session object.
 * @param  iv       [in]  Initialization vector.
 * @param  enc      [in]  Whether operation is encryption.
 * @param  object   [in]  AES key object.
 * @return  -ve on failure.
 *          0 on success.
 */
int WP11_Session_SetCbcParams(WP11_Session* session, unsigned char* iv,
                              int enc, WP11_Object* object)
{
    int ret;
    WP11_CbcParams* cbc = &session->params.cbc;
    WP11_Data* key;

    /* AES object on session. */
    ret = wc_AesInit(&cbc->aes, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (object->onToken)
            WP11_Lock_LockRO(object->lock);
        key = &object->data.symmKey;
        ret = wc_AesSetKey(&cbc->aes, key->data, key->len, iv,
                                         enc ? AES_ENCRYPTION : AES_DECRYPTION);
        if (object->onToken)
            WP11_Lock_UnlockRO(object->lock);
    }

    return ret;
}
#endif

#ifdef HAVE_AESGCM
/**
 * Set the parameters to use for an AES-CBC operation.
 *
 * @param  session  [in]  Session object.
 * @param  iv       [in]  Initialization vector.
 * @param  ivSz     [in]  Length of initialization vector in bytes.
 * @param  aad      [in]  Additional authentication data.
 * @param  aadSz    [in]  Length of additional authentication data.
 * @param  tagBits  [in]  Number of bits to use as the authentication tag.
 * @return  BAD_FUNC_ARG if the IV/nonce or the tagBits are too big.
 *          Other -ve value on failure.
 *          0 on success.
 */
int WP11_Session_SetGcmParams(WP11_Session* session, unsigned char* iv,
                              int ivSz, unsigned char* aad, int aadLen,
                              int tagBits)
{
    int ret = 0;
    WP11_GcmParams* gcm = &session->params.gcm;

    if (tagBits > 128 || ivSz > WP11_MAX_GCM_NONCE_SZ)
        ret = BAD_FUNC_ARG;

    if (ret == 0) {
        XMEMSET(gcm, 0, sizeof(*gcm));
        XMEMCPY(gcm->iv, iv, ivSz);
        gcm->ivSz = ivSz;
        gcm->tagBits = tagBits;
        if (aad != NULL) {
            gcm->aad = XMALLOC(aadLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            if (gcm->aad == NULL)
                ret = MEMORY_E;
            if (ret == 0) {
                XMEMCPY(gcm->aad, aad, aadLen);
                gcm->aadSz = aadLen;
            }
        }
    }

    return ret;
}
#endif
#endif

/**
 * Add object to the session or token.
 *
 * @param  session  [in]  Session object.
 * @param  onToken  [in]  Whether to put object on token.
 * @param  object   [in]  Key Object object.
 * @return  0 on success.
 */
int WP11_Session_AddObject(WP11_Session* session, int onToken,
                           WP11_Object* object)
{
    int ret = 0;
    WP11_Object* next;
    WP11_Token* token;

    object->onToken = onToken;
    if (!onToken)
        object->session = session;

    if (onToken) {
        token = &session->slot->token;
        WP11_Lock_LockRW(&token->lock);
        if (token->objCnt >= WP11_TOKEN_OBJECT_CNT_MAX)
            ret = OBJ_COUNT_E;
        if (ret == 0) {
            token->objCnt++;
            object->lock = &token->lock;
            /* Get next item in list after this object has been added. */
            next = token->object;
            /* Determine handle value */
            if (next != NULL)
                object->handle = next->handle + 1;
            else
                object->handle = OBJ_HANDLE(onToken, 1);
            object->next = next;
            token->object = object;
        }
        WP11_Lock_UnlockRW(&token->lock);
    }
    else {
        if (session->objCnt >= WP11_SESSION_OBJECT_CNT_MAX)
            ret = OBJ_COUNT_E;
        if (ret == 0) {
            session->objCnt++;
            /* Get next item in list after this object has been added. */
            next = session->object;
            /* Determine handle value */
            if (next != NULL)
                object->handle = next->handle + 1;
            else
                object->handle = OBJ_HANDLE(onToken, 1);
            object->next = next;
            session->object = object;
        }
    }

    return ret;
}

/**
 * Remove object to the session or token.
 *
 * @param  session  [in]  Session object.
 * @param  object   [in]  Key Object object.
 */
void WP11_Session_RemoveObject(WP11_Session* session, WP11_Object* object)
{
    WP11_Object** curr;
    WP11_Token* token;

    /* Find the object in list and relink. */
    if (object->onToken) {
        WP11_Lock_LockRW(object->lock);
        token = &session->slot->token;
        token->objCnt--;
        curr = &token->object;
    }
    else {
        session->objCnt--;
        curr = &session->object;
    }

    while (*curr != NULL) {
        if (*curr == object) {
            *curr = object->next;
            break;
        }
        curr = &(*curr)->next;
    }
    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);
}

/**
 * Get the current object of the session - key for operation.
 *
 * @param  session  [in]   Session object.
 * @param  object   [out]  Key Object object.
 */
void WP11_Session_GetObject(WP11_Session* session, WP11_Object** object)
{
    *object = session->curr;
}

/**
 * Set the current object on the session - key for operation.
 *
 * @param  session  [in]  Session object.
 * @param  object   [in]  Key Object object.
 */
void WP11_Session_SetObject(WP11_Session* session, WP11_Object* object)
{
    session->curr = object;
}

/**
 * Initialize a find operation for an object in the session or the token.
 *
 * @param  session  [in]  Session object.
 * @return  BAD_STATE_E when a find operation is already active on the session.
 *          0 on success.
 */
int WP11_Session_FindInit(WP11_Session* session)
{
    int ret = 0;

    if (session->find.state != WP11_FIND_STATE_NULL)
        ret = BAD_STATE_E;
    if (ret == 0) {
        session->find.state = WP11_FIND_STATE_INIT;
        session->find.count = 0;
        session->find.curr = 0;
    }

    return ret;
}

/**
 * Find the next object on the session or token.
 *
 * @param  session  [in]  Session object.
 * @param  onToken  [in]  Whether to look on token.
 * @param  object   [in]  Last object found.
 * @return  The next object in session or token.
 */
static WP11_Object* wp11_Session_FindNext(WP11_Session* session, int onToken,
                                          WP11_Object* object)
{
    WP11_Object* ret = NULL;

    while (ret == NULL) {
        if (object == NULL) {
            ret = session->object;
            if (ret == NULL && onToken) {
                ret = session->slot->token.object;
            }
        }
        else if (object != NULL) {
            if (object->next != NULL)
                ret = object->next;
            else if (!object->onToken && onToken) {
                ret = object->slot->token.object;
            }
        }

        if (ret == NULL)
            break;

        if ((ret->opFlag | WP11_FLAG_PRIVATE) == WP11_FLAG_PRIVATE) {
            if (!onToken)
                WP11_Lock_LockRO(&session->slot->token.lock);
            if (session->slot->token.loginState == WP11_APP_STATE_RW_PUBLIC ||
                  session->slot->token.loginState == WP11_APP_STATE_RO_PUBLIC) {
                object = ret;
                ret = NULL;
            }
            if (!onToken)
                WP11_Lock_UnlockRO(&session->slot->token.lock);
        }
    }

    return ret;
}

/**
 * Store a match in the found list against the session.
 *
 * @param  session  [in]  Session object.
 * @param  object   [in]  Object object to store reference to.
 * @return  FIND_FULL_E when the found list is full.
 *          0 on success.
 */
static int wp11_Session_FindMatched(WP11_Session* session, WP11_Object* object)
{
    int ret = 0;

    if (session->find.count == WP11_FIND_MAX)
        ret = FIND_FULL_E;
    else {
        session->find.found[session->find.count++] = object->handle;
        session->find.state = WP11_FIND_STATE_FOUND;
    }

    return ret;
}

/**
 * Find objects on session or token with attributes matching template.
 *
 * @param  session    [in]  Session object.
 * @param  onToken    [in]  Whether to look on token.
 * @param  pTemplate  [in]  Array of attributes that must match.
 * @param  ulCount    [in]  Number of attributes in array.
 */
void WP11_Session_Find(WP11_Session* session, int onToken,
                       CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    WP11_Object* obj = NULL;
    int i;
    CK_ATTRIBUTE* attr;

    if (onToken)
        WP11_Lock_LockRO(&session->slot->token.lock);
    while ((obj = wp11_Session_FindNext(session, onToken, obj)) != NULL) {
        for (i = 0; i < (int)ulCount; i++) {
            attr = &pTemplate[i];
            if (!WP11_Object_MatchAttr(obj, attr->type, attr->pValue,
                                                            attr->ulValueLen)) {
                break;
            }
        }

        if (i == (int)ulCount) {
            if (wp11_Session_FindMatched(session, obj) == FIND_FULL_E)
                break;
        }
    }
    if (onToken)
        WP11_Lock_UnlockRO(&session->slot->token.lock);
}

/**
 * Get the next object handle from list of objects identified during find
 * operation.
 *
 * @param  session  [in]   Session object.
 * @param  handle   [out]  Object handle.
 * @return  FIND_NO_MORE_E when all in the found list has been returned.
 *          0 on success.
 */
int WP11_Session_FindGet(WP11_Session* session, CK_OBJECT_HANDLE* handle)
{
    int ret = 0;

    if (session->find.curr == session->find.count)
        ret = FIND_NO_MORE_E;
    if (ret == 0)
        *handle = session->find.found[session->find.curr++];

    return ret;
}

/**
 * Finalize the find operation.
 *
 * @param  session  [in]  Session object.
 */
void WP11_Session_FindFinal(WP11_Session* session)
{
    session->find.state = WP11_FIND_STATE_NULL;
}


/**
 * Create a new Object object.
 *
 * @param  session  [in]   Session object.
 * @param  type     [in]   Type of Object.
 * @param  object   [out]  New Object object.
 * @return  MEMORY_E when dynamic memory allocation fails.
 *          0 on success.
 */
int WP11_Object_New(WP11_Session* session, CK_KEY_TYPE type,
                    WP11_Object** object)
{
    int ret = 0;
    WP11_Object* obj = NULL;

    obj = XMALLOC(sizeof(*obj), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (obj == NULL)
        ret = MEMORY_E;

    if (ret == 0) {
        XMEMSET(obj, 0, sizeof(*obj));
        obj->type = type;
        obj->onToken = 0;
        obj->slot = session->slot;
        obj->keyGenMech = CK_UNAVAILABLE_INFORMATION;

        *object = obj;
    }

    return ret;
}

/**
 * Free the object and take it out of the linked list.
 *
 * @param  object  [in]  Object object.
 */
void WP11_Object_Free(WP11_Object* object)
{
    /* Release dynamic memory. */
    if (object->label != NULL)
        XFREE(object->label, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (object->keyId != NULL)
        XFREE(object->keyId, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#ifndef NO_RSA
    if (object->type == CKK_RSA)
        wc_FreeRsaKey(&object->data.rsaKey);
#endif
#ifdef HAVE_ECC
    if (object->type == CKK_EC)
        wc_ecc_free(&object->data.ecKey);
#endif
    if (object->type == CKK_AES || object->type == CKK_GENERIC_SECRET)
        XMEMSET(object->data.symmKey.data, 0, object->data.symmKey.len);

    /* Dispose of object. */
    XFREE(object, NULL, DYNAMIC_TYPE_TMP_BUFFER);
}

/**
 * Get the object's handle.
 *
 * @param  object  [in]  Object object.
 * @return  Object's handle.
 */
CK_OBJECT_HANDLE WP11_Object_GetHandle(WP11_Object* object)
{
    return object->handle;
}

/**
 * Get the object's type - for example the key type.
 *
 * @param  object  [in]  Object object.
 * @return  Object's type.
 */
CK_KEY_TYPE WP11_Object_GetType(WP11_Object* object)
{
    return object->type;
}

#if !defined(NO_RSA) || defined(HAVE_ECC)
/**
 * Set the multi-precision integer from the data.
 *
 * @param  mpi   [in]  Multi-precision integer.
 * @param  data  [in]  Big-endian encoding of number.
 * @param  len   [in]  Length in bytes.
 * @return  -ve on failure.
 *          0 on success.
 */
static int SetMPI(mp_int* mpi, unsigned char* data, int len)
{
    int ret = 0;

    if (data != NULL) {
        ret = mp_init(mpi);
        if (ret == 0)
            ret = mp_read_unsigned_bin(mpi, data, len);
    }

    return ret;
}
#endif

#ifndef NO_RSA
/**
 * Set the RSA key data into the object.
 * Store the data in the wolfCrypt data structure.
 *
 * @param  object  [in]  Object object.
 * @param  data    [in]  Array of byte arrays.
 * @param  len     [in]  Array of lengths of byte arrays.
 * @return  -ve on failure.
 *          0 on success.
 */
int WP11_Object_SetRsaKey(WP11_Object* object, unsigned char** data,
                          CK_ULONG* len)
{
    int ret;
    RsaKey* key;

    if (object->onToken)
        WP11_Lock_LockRW(object->lock);

    key = &object->data.rsaKey;
    ret = wc_InitRsaKey_ex(key, NULL, INVALID_DEVID);
    if (ret == 0) {
        ret = SetMPI(&key->n, data[0], (int)len[0]);
        if (ret == 0)
           ret = SetMPI(&key->d, data[1], (int)len[1]);
        if (ret == 0)
           ret = SetMPI(&key->p, data[2], (int)len[2]);
        if (ret == 0)
           ret = SetMPI(&key->q, data[3], (int)len[3]);
        if (ret == 0)
           ret = SetMPI(&key->dP, data[4], (int)len[4]);
        if (ret == 0)
           ret = SetMPI(&key->dQ, data[5], (int)len[5]);
        if (ret == 0)
           ret = SetMPI(&key->u, data[6], (int)len[6]);
        if (ret == 0)
           ret = SetMPI(&key->e, data[7], (int)len[7]);
        if (ret == 0) {
           if (len[8] == sizeof(CK_ULONG))
               object->size = (word32)*(CK_ULONG*)data[8];
           else if (len[8] != 0)
               ret = BUFFER_E;
        }

        if (ret != 0)
            wc_FreeRsaKey(key);
    }

    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return ret;
}
#endif

#ifdef HAVE_ECC

#if !defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION <= 2))
/* this function is not in the FIPS 140-2 version */
/* ecc_sets is exposed in ecc.h */
static int wc_ecc_get_curve_id_from_oid(const byte* oid, word32 len)
{
    int curve_idx;

    if (oid == NULL)
        return BAD_FUNC_ARG;

    for (curve_idx = 0; ecc_sets[curve_idx].size != 0; curve_idx++) {
        if (
        #ifndef WOLFSSL_ECC_CURVE_STATIC
            ecc_sets[curve_idx].oid &&
        #endif
            ecc_sets[curve_idx].oidSz == len &&
                              XMEMCMP(ecc_sets[curve_idx].oid, oid, len) == 0) {
            break;
        }
    }
    if (ecc_sets[curve_idx].size == 0) {
        return ECC_CURVE_INVALID;
    }

    return ecc_sets[curve_idx].id;
}

#endif
/**
 * Set the EC Parameters based on the DER encoding of the OID.
 *
 * @param  key  [in]  EC Key object.
 * @param  der  [in]  DER encoding of OID.
 * @param  len  [in]  Length of DER encoding.
 * @return  BUFFER_E when len is too short.
 *          ASN_PARSE_E when DER encoding is bad.
 *          BAD_FUNC_ARG when OID is not known.
 *          Other -ve on failure.
 *          0 on success.
 */
static int EcSetParams(ecc_key* key, byte* der, int len)
{
    int ret = 0;
    int keySize;
    int curveId;

    if (len < 2)
        ret = BUFFER_E;
    if (ret == 0 && der[0] != ASN_OBJECT_ID)
        ret = ASN_PARSE_E;
    if (ret == 0 && der[1] != len - 2)
        ret = BUFFER_E;
    if (ret == 0) {
        /* Find the curve matching the OID. */
        curveId = wc_ecc_get_curve_id_from_oid(der + 2, der[1]);
        if (curveId == ECC_CURVE_INVALID)
            ret = BAD_FUNC_ARG;
    }
    if (ret == 0) {
        /* Set the curve into the EC key. */
        keySize = wc_ecc_get_curve_size_from_id(curveId);
        ret = wc_ecc_set_curve(key, keySize, curveId);
    }

    return ret;
}

/**
 * Set the EC Point, encoded in DER and X9.63, as the public key.
 *
 * @param  key  [in]  EC Key object.
 * @param  der  [in]  DER encoding of OID.
 * @param  len  [in]  Length of DER encoding.
 * @return  BUFFER_E when len is too short.
 *          ASN_PARSE_E when DER encoding is bad.
 *          Other -ve on failure.
 *          0 on success.
 */
static int EcSetPoint(ecc_key* key, byte* der, int len)
{
    int ret = 0;
    int dataLen;
    int i = 0;

    if (len < 3)
        ret = BUFFER_E;
    if (ret == 0 && der[i++] != ASN_OCTET_STRING)
        ret = ASN_PARSE_E;
    if (ret == 0 && der[i] >= ASN_LONG_LENGTH) {
        if (der[i] != (ASN_LONG_LENGTH | 1))
            ret = ASN_PARSE_E;
        else
            i++;
    }
    if (ret == 0) {
        dataLen = der[i++];
        if (dataLen != len - i)
            ret = BUFFER_E;
    }
    if (ret == 0) {
        /* Now stripped of DER encoding. */
        ret = wc_ecc_import_x963_ex(der + i, len - i, key, key->dp->id);
    }

    return ret;
}

/**
 * Set the EC key data into the object.
 * Store the data in the wolfCrypt data structure.
 *
 * @param  object  [in]  Object object.
 * @param  data    [in]  Array of byte arrays.
 * @param  len     [in]  Array of lengths of byte arrays.
 * @return  -ve on failure.
 *          0 on success.
 */
int WP11_Object_SetEcKey(WP11_Object* object, unsigned char** data,
                         CK_ULONG* len)
{
    int ret;
    ecc_key* key;

    if (object->onToken)
        WP11_Lock_LockRW(object->lock);

    key = &object->data.ecKey;
    ret = wc_ecc_init_ex(key, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (ret == 0 && data[0] != NULL)
            ret = EcSetParams(key, data[0], (int)len[0]);
        if (ret == 0 && data[1] != NULL) {
            key->type = ECC_PRIVATEKEY_ONLY;
            ret = SetMPI(&key->k, data[1], (int)len[1]);
        }
        if (ret == 0 && data[2] != NULL) {
            if (key->type == ECC_PRIVATEKEY_ONLY)
                key->type = ECC_PRIVATEKEY;
            else
                key->type = ECC_PUBLICKEY;
            ret = EcSetPoint(key, data[2], (int)len[2]);
        }

        if (ret != 0)
            wc_ecc_free(key);
    }

    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return ret;
}
#endif

#ifndef NO_DH
/**
 * Set the DH key data into the object.
 * Store the data in the wolfCrypt data structure.
 *
 * @param  object  [in]  Object object.
 * @param  data    [in]  Array of byte arrays.
 * @param  len     [in]  Array of lengths of byte arrays.
 * @return  BAD_FUNC_ARG when public key is larger than pre-allocated buffer.
 *          Other -ve on failure.
 *          0 on success.
 */
int WP11_Object_SetDhKey(WP11_Object* object, unsigned char** data,
                         CK_ULONG* len)
{
    int ret;
    WP11_DhKey* key;

    if (object->onToken)
        WP11_Lock_LockRW(object->lock);

    key = &object->data.dhKey;
    ret = wc_InitDhKey_ex(&key->params, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (data[0] != NULL && data[1] != NULL)
            ret = wc_DhSetKey(&key->params, data[0], (int)len[0], data[1],
                                                                   (int)len[1]);
        if (ret == 0 && data[2] != NULL) {
            if (len[2] > (int)sizeof(key->key))
                ret = BAD_FUNC_ARG;
            else {
                XMEMCPY(key->key, data[2], len[2]);
                key->len = (word32)len[2];
            }
        }

        if (ret != 0)
            wc_FreeDhKey(&key->params);
    }

    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return ret;
}
#endif

/**
 * Set the DH key data into the object.
 *
 * @param  object  [in]  Object object.
 * @param  data    [in]  Array of byte arrays.
 * @param  len     [in]  Array of lengths of byte arrays.
 * @return  BAD_FUNC_ARG when key length data is not size of CK_ULONG or
 *          key length is not a valid size for the key type.
 *          BUFFER_E when key data length is less than key length data value.
 *          Other -ve on failure.
 *          0 on success.
 */
int WP11_Object_SetSecretKey(WP11_Object* object, unsigned char** data,
                             CK_ULONG* len)
{
    int ret = 0;
    WP11_Data* key;

    if (object->onToken)
        WP11_Lock_LockRW(object->lock);

    key = &object->data.symmKey;
    key->len = 0;
    XMEMSET(key->data, 0, sizeof(key->data));

    /* First item is the key's length. */
    if (ret == 0 && data[0] != NULL && len[0] != (int)sizeof(CK_ULONG))
        ret = BAD_FUNC_ARG;
#ifndef NO_AES
    if (ret == 0 && object->type == CKK_AES && data[0] != NULL) {
        if (*(CK_ULONG*)data[0] != AES_128_KEY_SIZE &&
            *(CK_ULONG*)data[0] != AES_192_KEY_SIZE &&
            *(CK_ULONG*)data[0] != AES_256_KEY_SIZE) {
            ret = BAD_FUNC_ARG;
        }
    }
#endif
    if (ret == 0 && data[0] != NULL)
        key->len = (word32)*(CK_ULONG*)data[0];

    /* Second item is the key data. */
    if (ret == 0 && data[1] != NULL) {
        if (key->len == 0)
            key->len = (word32)len[1];
        else if (len[1] < (CK_ULONG)key->len)
            ret = BUFFER_E;
    }
    if (ret == 0 && data[1] != NULL)
        XMEMCPY(key->data, data[1], key->len);

    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return ret;
}

/**
 * Set the object's class.
 *
 * @param  object    [in]  Object object.
 * @param  objClass  [in]  Object's class.
 * @return  0 on success.
 */
int WP11_Object_SetClass(WP11_Object* object, CK_OBJECT_CLASS objClass)
{
    if (object->onToken)
        WP11_Lock_LockRW(object->lock);
    object->objClass = objClass;
    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return 0;
}

/**
 * Find an object based on the handle.
 *
 * @param  session    [in]   Session object.
 * @param  objHandle  [in]   Object's handle id.
 * @param  object     [out]  Found Object object.
 * @return  BAD_FUNC_ARG when object not found.
 *          0 when object found.
 */
int WP11_Object_Find(WP11_Session* session, CK_OBJECT_HANDLE objHandle,
                     WP11_Object** object)
{
    int ret = BAD_FUNC_ARG;
    WP11_Object* obj;
    int onToken = OBJ_HANDLE_ON_TOKEN(objHandle);

    if (!onToken) {
        obj = session->object;
        while (obj != NULL) {
            if (obj->handle == objHandle) {
                ret = 0;
                break;
            }
            obj = obj->next;
        }
    }
    else {
        WP11_Lock_LockRO(&session->slot->token.lock);
        obj = session->slot->token.object;
        while (obj != NULL) {
            if (obj->handle == objHandle) {
                ret = 0;
                break;
            }
            obj = obj->next;
        }
        WP11_Lock_UnlockRO(&session->slot->token.lock);
    }

    *object = obj;

    return ret;
}

#if !defined(NO_RSA) || defined(HAVE_ECC) || !defined(NO_DH)
/**
 * Get the data from a multi-precision integer.
 *
 * @param  mpi   [in]      Multi-precision integer.
 * @param  data  [in]      Buffer to hold big-endian encoding of number.
 * @param  len   [in,out]  On in, length of buffer in bytes.
 *                         On out, length of data in buffer.
 * @return  BUFFER_E when buffer is too small for number.
 *          0 on success.
 */
static int GetMPIData(mp_int* mpi, byte* data, CK_ULONG* len)
{
    int ret = 0;
    CK_ULONG dataLen = mp_unsigned_bin_size(mpi);

    if (data == NULL)
        *len = dataLen;
    else if (*len < dataLen)
        ret = BUFFER_E;
    else {
        *len = dataLen;
        ret = mp_to_unsigned_bin(mpi, data);
    }

    return ret;
}
#endif

/**
 * Get the data for a boolean.
 *
 * @param  value  [in]      Boolean value.
 * @param  data   [in]      Buffer to hold boolean.
 * @param  len    [in,out]  On in, length of buffer in bytes.
 *                          On out, length of data in buffer.
 * @return  BUFFER_E when buffer is too small for boolean.
 *          0 on success.
 */
static int GetBool(CK_BBOOL value, byte* data, CK_ULONG* len)
{
    int ret = 0;
    CK_ULONG dataLen = sizeof(value);

    if (data == NULL)
        *len = dataLen;
    else if (*len < dataLen)
        ret = BUFFER_E;
    else {
        *len = dataLen;
        *(CK_BBOOL*)data = value != 0;
    }

    return ret;
}

/**
 * Get the data for a CK_ULONG.
 *
 * @param  value  [in]      CK_ULONG value.
 * @param  data   [in]      Buffer to hold CK_ULONG value.
 * @param  len    [in,out]  On in, length of buffer in bytes.
 *                          On out, length of data in buffer.
 * @return  BUFFER_E when buffer is too small for number.
 *          0 on success.
 */
static int GetULong(CK_ULONG value, byte* data, CK_ULONG* len)
{
    int ret = 0;
    CK_ULONG dataLen = sizeof(value);

    if (data == NULL)
        *len = dataLen;
    else if (*len < dataLen)
        ret = BUFFER_E;
    else {
        *len = dataLen;
        *(CK_ULONG*)data = value;
    }

    return ret;
}

/**
 * Get the data of a data array.
 *
 * @param  data      [in]      Data array.
 * @param  dataLen   [in]      Length of data in data array in bytes.
 * @param  out       [in]      Buffer to hold data.
 * @param  outLen    [in,out]  On in, length of buffer in bytes.
 *                             On out, length of data in buffer.
 * @return  BUFFER_E when buffer is too small for data.
 *          0 on success.
 */
static int GetData(byte* data, CK_ULONG dataLen, byte* out, CK_ULONG* outLen)
{
    int ret = 0;

    if (out == NULL)
        *outLen = dataLen;
    else if (*outLen < dataLen)
        ret = BUFFER_E;
    else {
        *outLen = dataLen;
        XMEMCPY(out, data, dataLen);
    }

    return ret;
}

#ifndef NO_RSA
/**
 * Get an RSA object's data as an attribute.
 *
 * @param  object  [in]      Object object.
 * @param  type    [in]      Attribute type.
 * @param  data    [in]      Attribute data buffer.
 * @param  len     [in,out]  On in, length of attribute data buffer in bytes.
 *                           On out, length of attribute data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          NOT_AVAILABLE_E when attribute type is not supported.
 *          0 on success.
 */
static int RsaObject_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                             byte* data, CK_ULONG* len)
{
    int ret = 0;
    int noPriv = (((object->flag & WP11_FLAG_SENSITIVE) != 0) ||
                                 ((object->flag & WP11_FLAG_EXTRACTABLE) == 0));

    if (mp_iszero(&object->data.rsaKey.d))
        noPriv = 1;

    switch (type) {
        case CKA_MODULUS:
            ret = GetMPIData(&object->data.rsaKey.n, data, len);
            break;
        case CKA_PRIVATE_EXPONENT:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.d, data, len);
            break;
        case CKA_PRIME_1:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.p, data, len);
            break;
        case CKA_PRIME_2:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.q, data, len);
            break;
        case CKA_EXPONENT_1:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.dP, data, len);
            break;
        case CKA_EXPONENT_2:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.dQ, data, len);
            break;
        case CKA_COEFFICIENT:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.rsaKey.u, data, len);
            break;
        case CKA_PUBLIC_EXPONENT:
            ret = GetMPIData(&object->data.rsaKey.e, data, len);
            break;
        case CKA_MODULUS_BITS:
            ret = GetULong(mp_count_bits(&object->data.rsaKey.n), data, len);
            break;
        case CKA_WRAP_TEMPLATE:
        case CKA_UNWRAP_TEMPLATE:
        default:
            ret = NOT_AVAILABE_E;
            break;
    }

    return ret;
}
#endif

#ifdef HAVE_ECC
/**
 * Get the DER encoded OID of the elliptic curve.
 *
 * @param  key   [in]      Elliptic curve key.
 * @param  data  [in]      Buffer to hold DER encoded OID.
 * @param  len   [in,out]  On in, the length of the buffer in bytes.
 *                         On out, the length of the data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          0 on success.
 */
static int GetEcParams(ecc_key* key, byte* data, CK_ULONG* len)
{
    int ret = 0;
    CK_ULONG dataLen = key->dp->oidSz + 2;

    if (data == NULL)
        *len = dataLen;
    else if (*len < dataLen)
        ret = BUFFER_E;
    else {
        *len = dataLen;
        data[0] = ASN_OBJECT_ID;
        data[1] = dataLen - 2;
        XMEMCPY(data + 2, key->dp->oid, data[1]);
    }

    return ret;
}

/**
 * Get the DER encoded EC public point.
 *
 * @param  key   [in]      Elliptic curve key.
 * @param  data  [in]      Buffer to hold DER encoded point.
 * @param  len   [in,out]  On in, the length of the buffer in bytes.
 *                         On out, the length of the data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          0 on success.
 */
static int GetEcPoint(ecc_key* key, byte* data, CK_ULONG* len)
{
    int ret = 0;
    word32 dataLen = key->dp->size * 2 + 1;
    int longLen = dataLen >= ASN_LONG_LENGTH;
    int i;

    if (data == NULL)
        *len = dataLen + 2 + longLen;
    else if (*len < (CK_ULONG)dataLen)
        ret = BUFFER_E;
    else {
        *len = dataLen + 2 + longLen;
        i = 0;
        data[i++] = ASN_OCTET_STRING;
        if (longLen)
            data[i++] = ASN_LONG_LENGTH | 1;
        data[i++] = dataLen;
        ret = wc_ecc_export_x963(key, data + i, &dataLen);
    }

    return ret;
}

/**
 * Get an EC object's data as an attribute.
 *
 * @param  object  [in]      Object object.
 * @param  type    [in]      Attribute type.
 * @param  data    [in]      Attribute data buffer.
 * @param  len     [in,out]  On in, length of attribute data buffer in bytes.
 *                           On out, length of attribute data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          NOT_AVAILABLE_E when attribute type is not supported.
 *          0 on success.
 */
static int EcObject_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                            byte* data, CK_ULONG* len)
{
    int ret = 0;
    int noPriv = (((object->flag & WP11_FLAG_SENSITIVE) != 0) ||
                                 ((object->flag & WP11_FLAG_EXTRACTABLE) == 0));
    int noPub = 0;

    if (object->data.ecKey.type == ECC_PUBLICKEY)
        noPriv = 1;
    else if (object->data.ecKey.type == ECC_PRIVATEKEY_ONLY)
        noPub = 1;

    switch (type) {
        case CKA_EC_PARAMS:
            ret = GetEcParams(&object->data.ecKey, data, len);
            break;
        case CKA_VALUE:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetMPIData(&object->data.ecKey.k, data, len);
            break;
        case CKA_EC_POINT:
            if (noPub)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetEcPoint(&object->data.ecKey, data, len);
            break;
        case CKA_WRAP_TEMPLATE:
        case CKA_UNWRAP_TEMPLATE:
        case CKA_DERIVE_TEMPLATE:
        default:
            ret = NOT_AVAILABE_E;
            break;
    }

    return ret;
}
#endif

#ifndef NO_DH
/**
 * Get a DH object's data as an attribute.
 *
 * @param  object  [in]      Object object.
 * @param  type    [in]      Attribute type.
 * @param  data    [in]      Attribute data buffer.
 * @param  len     [in,out]  On in, length of attribute data buffer in bytes.
 *                           On out, length of attribute data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          NOT_AVAILABLE_E when attribute type is not supported.
 *          0 on success.
 */
static int DhObject_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                            byte* data, CK_ULONG* len)
{
    int ret = 0;
    int noPriv = (((object->flag & WP11_FLAG_SENSITIVE) != 0) ||
                                 ((object->flag & WP11_FLAG_EXTRACTABLE) == 0));

    switch (type) {
        case CKA_PRIME:
            ret = GetMPIData(&object->data.dhKey.params.p, data, len);
            break;
        case CKA_BASE:
            ret = GetMPIData(&object->data.dhKey.params.g, data, len);
            break;
        case CKA_VALUE:
            /* Public key held in key when object class is CKO_PUBLIC_KEY and
             * private key when object class is CKO_PRIVATE_KEY.
             */
            if (object->objClass != CKO_PRIVATE_KEY || !noPriv) {
                ret = GetData(object->data.dhKey.key, object->data.dhKey.len,
                                                                     data, len);
            }
            else
                *len = CK_UNAVAILABLE_INFORMATION;
            break;
        case CKA_WRAP_TEMPLATE:
        case CKA_UNWRAP_TEMPLATE:
        case CKA_DERIVE_TEMPLATE:
        default:
            ret = NOT_AVAILABE_E;
            break;
    }

    return ret;
}
#endif

/**
 * Get a secret object's data as an attribute.
 *
 * @param  object  [in]      Object object.
 * @param  type    [in]      Attribute type.
 * @param  data    [in]      Attribute data buffer.
 * @param  len     [in,out]  On in, length of attribute data buffer in bytes.
 *                           On out, length of attribute data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          NOT_AVAILABLE_E when attribute type is not supported.
 *          0 on success.
 */
static int SecretObject_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                                byte* data, CK_ULONG* len)
{
    int ret = 0;
    int noPriv = ((object->flag & WP11_FLAG_SENSITIVE) != 0 ||
                                   (object->flag & WP11_FLAG_EXTRACTABLE) == 0);

    switch (type) {
        case CKA_VALUE:
            if (noPriv)
                *len = CK_UNAVAILABLE_INFORMATION;
            else
                ret = GetData(object->data.symmKey.data,
                                           object->data.symmKey.len, data, len);
            break;
        case CKA_VALUE_LEN:
            ret = GetULong(object->data.symmKey.len, data, len);
            break;
        case CKA_WRAP_TEMPLATE:
        case CKA_UNWRAP_TEMPLATE:
        default:
            ret = NOT_AVAILABE_E;
            break;
    }

    return ret;
}

/**
 * Get the data for an attribute from the object.
 *
 * @param  object  [in]      Object object.
 * @param  type    [in]      Attribute type.
 * @param  data    [in]      Attribute data buffer.
 * @param  len     [in,out]  On in, length of data buffer in bytes.
 *                           On out, length of data in bytes.
 * @return  BUFFER_E when buffer is too small for data.
 *          NOT_AVAILABLE_E when attribute type is not supported.
 *          0 on success.
 */
int WP11_Object_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type, byte* data,
                        CK_ULONG* len)
{
    int ret = 0;

    if (object->onToken)
        WP11_Lock_LockRO(object->lock);

    switch (type) {
        case CKA_CLASS:
            ret = GetULong(object->objClass, data, len);
            break;
        case CKA_LABEL:
            ret = GetData(object->label, object->labelLen, data, len);
            break;
        case CKA_TOKEN:
            ret = GetBool(object->onToken, data, len);
            break;
        case CKA_PRIVATE:
            ret = GetBool(object->opFlag | WP11_FLAG_PRIVATE, data, len);
            break;
        case CKA_SENSITIVE:
            ret = GetBool(object->opFlag | WP11_FLAG_SENSITIVE, data, len);
            break;
        case CKA_EXTRACTABLE:
            ret = GetBool(object->opFlag | WP11_FLAG_EXTRACTABLE, data, len);
            break;
        case CKA_MODIFIABLE:
            ret = GetBool(object->opFlag | WP11_FLAG_MODIFIABLE, data, len);
            break;
        case CKA_ALWAYS_SENSITIVE:
            ret = GetBool(object->opFlag | WP11_FLAG_ALWAYS_SENSITIVE, data,
                                                                           len);
            break;
        case CKA_NEVER_EXTRACTABLE:
            ret = GetBool(object->opFlag | WP11_FLAG_NEVER_EXTRACTABLE, data,
                                                                           len);
            break;
        case CKA_ALWAYS_AUTHENTICATE:
            ret = GetBool(object->opFlag | WP11_FLAG_ALWAYS_AUTHENTICATE, data,
                                                                           len);
            break;
        case CKA_WRAP_WITH_TRUSTED:
            ret = GetBool(object->opFlag | WP11_FLAG_WRAP_WITH_TRUSTED, data,
                                                                           len);
            break;
        case CKA_TRUSTED:
            ret = GetBool(object->opFlag | WP11_FLAG_TRUSTED, data, len);
            break;
        case CKA_COPYABLE:
            ret = GetBool(CK_FALSE, data, len);
            break;
        case CKA_DESTROYABLE:
            ret = GetBool(CK_TRUE, data, len);
            break;
        case CKA_APPLICATION:
            ret = NOT_AVAILABE_E;
            break;
        case CKA_ID:
            ret = GetData(object->keyId, object->keyIdLen, data, len);
            break;
        case CKA_KEY_TYPE:
            ret = GetULong(object->type, data, len);
            break;
        case CKA_START_DATE:
            if (object->startDate[0] == '\0')
                *len = 0;
            else
                ret = GetData((byte*)object->startDate,
                                          sizeof(object->startDate), data, len);
            break;
        case CKA_END_DATE:
            if (object->endDate[0] == '\0')
                *len = 0;
            else
                ret = GetData((byte*)object->endDate, sizeof(object->endDate),
                                                                     data, len);
            break;
        case CKA_LOCAL:
            ret = GetBool(object->local, data, len);
            break;
        case CKA_KEY_GEN_MECHANISM:
            ret = GetULong(object->keyGenMech, data, len);
            break;
        case CKA_ALLOWED_MECHANISMS:
            ret = NOT_AVAILABE_E;
            break;

        case CKA_ENCRYPT:
            ret = GetBool(object->opFlag | CKF_ENCRYPT, data, len);
            break;
        case CKA_DECRYPT:
            ret = GetBool(object->opFlag | CKF_DECRYPT, data, len);
            break;
        case CKA_VERIFY:
            ret = GetBool(object->opFlag | CKF_VERIFY, data, len);
            break;
        case CKA_VERIFY_RECOVER:
            ret = GetBool(object->opFlag | CKF_VERIFY_RECOVER, data, len);
            break;
        case CKA_SIGN:
            ret = GetBool(object->opFlag | CKF_SIGN, data, len);
            break;
        case CKA_SIGN_RECOVER:
            ret = GetBool(object->opFlag | CKF_SIGN_RECOVER, data, len);
            break;
        case CKA_WRAP:
            ret = GetBool(object->opFlag | CKF_WRAP, data, len);
            break;
        case CKA_UNWRAP:
            ret = GetBool(object->opFlag | CKF_UNWRAP, data, len);
            break;
        case CKA_DERIVE:
            ret = GetBool(object->opFlag | CKF_DERIVE, data, len);
            break;

        case CKA_SUBJECT:
            ret = NOT_AVAILABE_E;
            break;

        default:
            switch (object->type) {
#ifndef NO_RSA
                case CKK_RSA:
                    ret = RsaObject_GetAttr(object, type, data, len);
                    break;
#endif
#ifdef HAVE_ECC
                case CKK_EC:
                    ret = EcObject_GetAttr(object, type, data, len);
                    break;
#endif
#ifndef NO_DH
                case CKK_DH:
                    ret = DhObject_GetAttr(object, type, data, len);
                    break;
#endif
#ifndef NO_AES
                case CKK_AES:
#endif
                case CKK_GENERIC_SECRET:
                    ret = SecretObject_GetAttr(object, type, data, len);
                    break;
                default:
                    ret = NOT_AVAILABE_E;
                    break;
            }
            break;
    }

    if (object->onToken)
        WP11_Lock_UnlockRO(object->lock);

    return ret;
}


/**
 * Set the operation flag against the object.
 *
 * @param  object  [in]  Object object.
 * @param  flag    [in]  Operation flag value.
 * @param  set     [in]  Whether the flag is to be set (or cleared).
 */
static void WP11_Object_SetOpFlag(WP11_Object* object, word32 flag, int set)
{
    if (set)
        object->opFlag |= flag;
    else
        object->opFlag &= ~flag;
}

/**
 * Set the key identifier against the object.
 *
 * @param  object    [in]  Object object.
 * @param  keyId     [in]  Key identifier data.
 * @param  keyIdLen  [in]  Length of key identifier in bytes.
 * @return  MEMORY_E when dynamic memory allocation fails.
 *          0 on success.
 */
static int WP11_Object_SetKeyId(WP11_Object* object, unsigned char* keyId,
                                int keyIdLen)
{
    int ret = 0;

    if (object->keyId != NULL)
        XFREE(object->keyId, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    object->keyId = XMALLOC(keyIdLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (object->keyId == NULL)
        ret = MEMORY_E;
    if (ret == 0) {
        XMEMCPY(object->keyId, keyId, keyIdLen);
        object->keyIdLen = keyIdLen;
    }

    return ret;
}

/**
 * Set the label against the object.
 *
 * @param  object    [in]  Object object.
 * @param  label     [in]  Label data.
 * @param  labelLen  [in]  Length of label in bytes.
 * @return  MEMORY_E when dynamic memory allocation fails.
 *          0 on success.
 */
static int WP11_Object_SetLabel(WP11_Object* object, unsigned char* label,
                                int labelLen)
{
    int ret = 0;

    if (object->label != NULL)
        XFREE(object->label, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    object->label = XMALLOC(labelLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (object->label == NULL)
        ret = MEMORY_E;
    if (ret == 0) {
        XMEMCPY(object->label, label, labelLen);
        object->labelLen = labelLen;
    }

    return ret;
}

/**
 * Set the flag against the object.
 *
 * @param  object  [in]  Object object.
 * @param  flags   [in]  Flag value.
 * @param  set     [in]  Whether the flag is to be set (or cleared).
 */
static void WP11_Object_SetFlag(WP11_Object* object, word32 flag, int set)
{
    if (set)
        object->flag |= flag;
    else
        object->flag &= ~flag;
}

/**
 * Set the start date.
 *
 * @param  object     [in]  Object object.
 * @param  startDate  [in]  Start data as a string.
 * @param  len        [in]  Length of string.
 * @return  BUFFER_E when string is too small.
 *          0 on success.
 */
static int WP11_Object_SetStartDate(WP11_Object* object, char* startDate,
                                    int len)
{
    int ret = 0;

    if (len != sizeof(object->startDate))
        ret = BUFFER_E;
    if (ret == 0)
        XMEMCPY(object->startDate, startDate, sizeof(object->startDate));

    return ret;
}

/**
 * Set the end date.
 *
 * @param  object   [in]  Object object.
 * @param  endData  [in]  End data as a string.
 * @param  len      [in]  Length of string.
 * @return  BUFFER_E when string is too small.
 *          0 on success.
 */
static int WP11_Object_SetEndDate(WP11_Object* object, char* endDate, int len)
{
    int ret = 0;

    if (len != sizeof(object->endDate))
        ret = BUFFER_E;
    if (ret == 0)
        XMEMCPY(object->endDate, endDate, sizeof(object->endDate));

    return ret;
}


/**
 * Set an attribute against the object.
 *
 * @param  object  [in]  Object object.
 * @param  type    [in]  Attribute type.
 * @param  data    [in]  Attribute data to set.
 * @param  len     [in]  Length of attribute data in bytes.
 * @return  BUFFER_E when data is too small.
 *          BAD_FUNC_ARG when type is not supported with object.
 *          0 on success.
 */
int WP11_Object_SetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type, byte* data,
                        CK_ULONG len)
{
    int ret = 0;

    if (object->onToken)
        WP11_Lock_LockRW(object->lock);

    switch (type) {
        case CKA_CLASS:
            object->objClass = *(CK_ULONG*)data;
            break;
        case CKA_DECRYPT:
            WP11_Object_SetOpFlag(object, CKF_DECRYPT, *(CK_BBOOL*)data);
            break;
        case CKA_ENCRYPT:
            WP11_Object_SetOpFlag(object, CKF_ENCRYPT, *(CK_BBOOL*)data);
            break;
        case CKA_SIGN:
            WP11_Object_SetOpFlag(object, CKF_SIGN, *(CK_BBOOL*)data);
            break;
        case CKA_VERIFY:
            WP11_Object_SetOpFlag(object, CKF_VERIFY, *(CK_BBOOL*)data);
            break;
        case CKA_SIGN_RECOVER:
            WP11_Object_SetOpFlag(object, CKF_SIGN_RECOVER, *(CK_BBOOL*)data);
            break;
        case CKA_VERIFY_RECOVER:
            WP11_Object_SetOpFlag(object, CKF_VERIFY_RECOVER, *(CK_BBOOL*)data);
            break;
        case CKA_WRAP:
            WP11_Object_SetOpFlag(object, CKF_WRAP, *(CK_BBOOL*)data);
            break;
        case CKA_UNWRAP:
            WP11_Object_SetOpFlag(object, CKF_WRAP, *(CK_BBOOL*)data);
            break;
        case CKA_DERIVE:
            WP11_Object_SetOpFlag(object, CKF_DERIVE, *(CK_BBOOL*)data);
            break;
        case CKA_ID:
            ret = WP11_Object_SetKeyId(object, data, (int)len);
            break;
        case CKA_LABEL:
            ret = WP11_Object_SetLabel(object, data, (int)len);
            break;
        case CKA_PRIVATE:
            WP11_Object_SetFlag(object, WP11_FLAG_PRIVATE, *(CK_BBOOL*)data);
            break;
        case CKA_SENSITIVE:
            WP11_Object_SetFlag(object, WP11_FLAG_SENSITIVE, *(CK_BBOOL*)data);
            break;
        case CKA_EXTRACTABLE:
            WP11_Object_SetFlag(object, WP11_FLAG_EXTRACTABLE,
                                                              *(CK_BBOOL*)data);
            break;
        case CKA_MODIFIABLE:
            WP11_Object_SetFlag(object, WP11_FLAG_MODIFIABLE, *(CK_BBOOL*)data);
            break;
        case CKA_ALWAYS_SENSITIVE:
            WP11_Object_SetFlag(object, WP11_FLAG_ALWAYS_SENSITIVE,
                                                              *(CK_BBOOL*)data);
            break;
        case CKA_NEVER_EXTRACTABLE:
            WP11_Object_SetFlag(object, WP11_FLAG_NEVER_EXTRACTABLE,
                                                              *(CK_BBOOL*)data);
            break;
        case CKA_ALWAYS_AUTHENTICATE:
            WP11_Object_SetFlag(object, WP11_FLAG_ALWAYS_AUTHENTICATE,
                                                              *(CK_BBOOL*)data);
            break;
        case CKA_WRAP_WITH_TRUSTED:
            WP11_Object_SetFlag(object, WP11_FLAG_WRAP_WITH_TRUSTED,
                                                              *(CK_BBOOL*)data);
            break;
        case CKA_TRUSTED:
            WP11_Object_SetFlag(object, WP11_FLAG_TRUSTED, *(CK_BBOOL*)data);
            break;
        case CKA_START_DATE:
            ret = WP11_Object_SetStartDate(object, (char*)data, (int)len);
            break;
        case CKA_END_DATE:
            ret = WP11_Object_SetEndDate(object, (char*)data, (int)len);
            break;
        case CKA_MODULUS_BITS:
        case CKA_MODULUS:
        case CKA_PRIVATE_EXPONENT:
        case CKA_PRIME_1:
        case CKA_PRIME_2:
        case CKA_EXPONENT_1:
        case CKA_EXPONENT_2:
        case CKA_COEFFICIENT:
        case CKA_PUBLIC_EXPONENT:
#ifndef NO_RSA
            if (object->type != CKK_RSA)
#endif
                ret = BAD_FUNC_ARG;
            break;
        case CKA_EC_PARAMS:
        case CKA_EC_POINT:
#ifdef HAVE_ECC
            if (object->type != CKK_EC)
#endif
                ret = BAD_FUNC_ARG;
            break;
        case CKA_PRIME:
        case CKA_BASE:
#ifndef NO_DH
            if (object->type != CKK_DH)
#endif
                ret = BAD_FUNC_ARG;
            break;
        case CKA_VALUE_LEN:
            switch (object->type) {
#ifndef NO_DH
                case CKK_DH:
#endif
#ifndef NO_AES
                case CKK_AES:
#endif
                case CKK_GENERIC_SECRET:
                    break;
                default:
                    ret = BAD_FUNC_ARG;
                    break;
            }
            break;
        case CKA_VALUE:
            switch (object->type) {
#ifdef HAVE_ECC
                case CKK_EC:
#endif
#ifndef NO_DH
                case CKK_DH:
#endif
#ifndef NO_AES
                case CKK_AES:
#endif
                case CKK_GENERIC_SECRET:
                   break;
                default:
                   ret = BAD_FUNC_ARG;
                   break;
            }
            break;
        case CKA_KEY_TYPE:
            /* Handled in layer above */
            break;
        case CKA_TOKEN:
            /* Handled in layer above */
            break;
        default:
            ret = BAD_FUNC_ARG;
            break;
    }

    if (object->onToken)
        WP11_Lock_UnlockRW(object->lock);

    return ret;
}

/**
 * Check whether the attribute matches in the object.
 *
 * @param  object  [in]  Object object.
 * @param  type    [in]  Attribute type.
 * @param  data    [in]  Attribute data.
 * @param  len     [in]  Length of attribute data.
 * @return  1 when attribute matches.
 *          0 when attribute doesn't match.
 */
int WP11_Object_MatchAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                          byte* data, CK_ULONG len)
{
    int ret = 0;
    byte attrData[8];
    byte* ptr;
    CK_ULONG attrLen = len;

    /* Get the attribute data into the stack buffer if big enough. */
    if (len <= (int)sizeof(attrData)) {
        if (WP11_Object_GetAttr(object, type, attrData, &attrLen) == 0)
            ret = attrLen == len && XMEMCMP(attrData, data, len) == 0;
    }
    else {
        /* Allocate a buffer to hold data and then compare. */
        ptr = XMALLOC(len, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (ptr != NULL) {
            if (WP11_Object_GetAttr(object, type, ptr, &attrLen) == 0)
                ret = attrLen == len && XMEMCMP(ptr, data, len) == 0;
            XFREE(ptr, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        }
    }

    return ret;
}

#ifndef NO_RSA
#ifdef WOLFSSL_KEY_GEN
/**
 * Generate an RSA key pair.
 *
 * @param  pub   [in]  Public key object.
 * @param  priv  [in]  Private key object.
 * @param  slot  [in]  Slot operation is performed on.
 * @return  -ve when key generation fails.
 *          0 on success.
 */
int WP11_Rsa_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                             WP11_Slot* slot)
{
    int ret = 0;
    unsigned char eData[sizeof(long)];
    int i;
    long e = 0;
    WC_RNG rng;

    /* Use public exponent if public key has one set. */
    if (!mp_iszero(&pub->data.rsaKey.e)) {
        XMEMSET(eData, 0, sizeof(eData));
        /* Public exponent must be size of a long for API. */
        if (mp_unsigned_bin_size(&pub->data.rsaKey.e) > (int)sizeof(eData))
            ret = BAD_FUNC_ARG;
        if (ret == 0)
            ret = mp_to_unsigned_bin(&pub->data.rsaKey.e, eData);
        if (ret == 0) {
            /* Convert big-endian data into number. */
            for (i = sizeof(eData) - 1; i >= 0; i--) {
                e <<= 8;
                e |= eData[i];
            }
        }
    }
    else {
        e = WC_RSA_EXPONENT;
        ret = mp_set_int(&pub->data.rsaKey.e, e);
    }

    if (ret == 0) {
        ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
        if (ret == 0) {
            /* Generate into the private key. */
            ret = wc_MakeRsaKey(&priv->data.rsaKey, pub->size, e, &rng);
            Rng_Free(&rng);
        }
    }
    if (ret == 0) {
        /* Copy in the rest of public key from private key. */
        ret = mp_copy(&priv->data.rsaKey.n, &pub->data.rsaKey.n);
    }
    if (ret == 0) {
        priv->local = 1;
        pub->local = 1;
        priv->keyGenMech = CKM_RSA_PKCS_KEY_PAIR_GEN;
        pub->keyGenMech = CKM_RSA_PKCS_KEY_PAIR_GEN;
    }

    return ret;
}
#endif

/**
 * Return the length of the RSA key in bytes.
 *
 * @param  key  [in]  Key object.
 * @return RSA key length in bytes.
 */
word32 WP11_Rsa_KeyLen(WP11_Object* key)
{
    return mp_unsigned_bin_size(&key->data.rsaKey.n);
}

/**
 * Encrypt data with public key.
 * Raw encryption - exponentiate with public exponent.
 *
 * @param  in      [in]      Data to encrypt.
 * @param  inLen   [in]      Length of data to encrypt.
 * @param  out     [in]      Buffer to hold encrypted data.
 * @param  outLen  [in,out]  On in, length of buffer.
 *                           On out, length data in buffer.
 * @param  pub     [in]      Public key object.
 * @param  slot    [in]      Slot operation is performed on.
 * @return  BUFFER_E when outLen is too small for decrypted data.
 *          Other -ve when encryption fails.
 *          0 on success.
 */
int WP11_Rsa_PublicEncrypt(unsigned char* in, word32 inLen, unsigned char* out,
                           word32* outLen, WP11_Object* pub, WP11_Slot* slot)
{
    int ret;
    WC_RNG rng;

    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaFunction(in, inLen, out, outLen, RSA_PUBLIC_ENCRYPT,
                                                       &pub->data.rsaKey, &rng);
        Rng_Free(&rng);
    }
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    return ret;
}

/**
 * Decrypt data with private key.
 * Raw encryption - exponentiate with private exponent.
 *
 * @param  in      [in]      Data to decrypt.
 * @param  inLen   [in]      Length of data to decrypt.
 * @param  out     [in]      Buffer to hold decrypted data.
 * @param  outLen  [in,out]  On in, length of buffer.
 *                           On out, length data in buffer.
 * @param  priv    [in]      Private key object.
 * @param  slot    [in]      Slot operation is performed on.
 * @return  -ve when decryption fails.
 *          0 on success.
 */
int WP11_Rsa_PrivateDecrypt(unsigned char* in, word32 inLen, unsigned char* out,
                            word32* outLen, WP11_Object* priv, WP11_Slot* slot)
{
    int ret;
    WC_RNG rng;

    if (priv->onToken)
        WP11_Lock_LockRO(priv->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaFunction(in, inLen, out, outLen, RSA_PRIVATE_DECRYPT,
                                                      &priv->data.rsaKey, &rng);
        Rng_Free(&rng);
    }
    if (priv->onToken)
        WP11_Lock_UnlockRO(priv->lock);

    return ret;
}

/**
 * PKCS#1.5 encrypt data with public key.
 *
 * @param  in      [in]      Data to encrypt.
 * @param  inLen   [in]      Length of data to encrypt.
 * @param  out     [in]      Buffer to hold encrypted data.
 * @param  outLen  [in,out]  On in, length of buffer.
 *                           On out, length data in buffer.
 * @param  pub     [in]      Public key object.
 * @param  slot    [in]      Slot operation is performed on.
 * @return  -ve when encryption fails.
 *          0 on success.
 */
int WP11_RsaPkcs15_PublicEncrypt(unsigned char* in, word32 inLen,
                                 unsigned char* out, word32* outLen,
                                 WP11_Object* pub, WP11_Slot* slot)
{
    int ret;
    WC_RNG rng;

    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaPublicEncrypt_ex(in, inLen, out, *outLen, &pub->data.rsaKey,
                                       &rng, WC_RSA_PKCSV15_PAD,
                                       WC_HASH_TYPE_NONE, WC_MGF1NONE, NULL, 0);
        Rng_Free(&rng);
    }
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    if (ret >= 0) {
        *outLen = ret;
        ret = 0;
    }

    return ret;
}

/**
 * PKCS#1.5 decrypt data with private key.
 *
 * @param  in      [in]      Data to decrypt.
 * @param  inLen   [in]      Length of data to decrypt.
 * @param  out     [in]      Buffer to hold decrypted data.
 * @param  outLen  [in,out]  On in, length of buffer.
 *                           On out, length data in buffer.
 * @param  priv    [in]      Private key object.
 * @param  slot    [in]      Slot operation is performed on.
 * @return  -ve when decryption fails.
 *          0 on success.
 */
int WP11_RsaPkcs15_PrivateDecrypt(unsigned char* in, word32 inLen,
                                  unsigned char* out, word32* outLen,
                                  WP11_Object* priv, WP11_Slot* slot)
{
    int ret = 0;
#if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    WC_RNG rng;
#endif
    /* A random number generator is needed for blinding. */
    if (priv->onToken)
        WP11_Lock_LockRW(priv->lock);
#if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        priv->data.rsaKey.rng = &rng;
    }
#endif
    if (ret == 0) {
        ret = wc_RsaPrivateDecrypt_ex(in, inLen, out, *outLen,
                                       &priv->data.rsaKey, WC_RSA_PKCSV15_PAD,
                                       WC_HASH_TYPE_NONE, WC_MGF1NONE, NULL, 0);
    #if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
        (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
        priv->data.rsaKey.rng = NULL;
        Rng_Free(&rng);
    #endif
    }
    if (priv->onToken)
        WP11_Lock_UnlockRW(priv->lock);

    if (ret >= 0) {
        *outLen = ret;
        ret = 0;
    }
    (void)slot;

    return ret;
}

#ifndef WC_NO_RSA_OAEP
/**
 * PKCS#1 OAEP encrypt data with public key.
 *
 * @param  in       [in]      Data to encrypt.
 * @param  inLen    [in]      Length of data to encrypt.
 * @param  out      [in]      Buffer to hold encrypted data.
 * @param  outLen   [in,out]  On in, length of buffer.
 *                            On out, length data in buffer.
 * @param  pub      [in]      Public key object.
 * @param  session  [in]      Session object holding OAEP parameters.
 * @return  -ve when encryption fails.
 *          0 on success.
 */
int WP11_RsaOaep_PublicEncrypt(unsigned char* in, word32 inLen,
                               unsigned char* out, word32* outLen,
                               WP11_Object* pub, WP11_Session* session)
{
    int ret;
    WP11_OaepParams* oaep = &session->params.oaep;
    WP11_Slot* slot = WP11_Session_GetSlot(session);
    WC_RNG rng;

    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaPublicEncrypt_ex(in, inLen, out, *outLen, &pub->data.rsaKey,
                                         &rng, WC_RSA_OAEP_PAD, oaep->hashType,
                                         oaep->mgf, oaep->label, oaep->labelSz);
        Rng_Free(&rng);
    }
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    if (ret >= 0) {
        *outLen = ret;
        ret = 0;

        if (oaep->label != NULL) {
            XFREE(oaep->label, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            oaep->label = NULL;
        }
    }

    return ret;
}

/**
 * PKCS#1 OAEP decrypt data with private key.
 *
 * @param  in      [in]      Data to decrypt.
 * @param  inLen   [in]      Length of data to decrypt.
 * @param  out     [in]      Buffer to hold decrypted data.
 * @param  outLen  [in,out]  On in, length of buffer.
 *                           On out, length data in buffer.
 * @param  priv    [in]      Private key object.
 * @param  session  [in]     Session object holding OAEP parameters.
 * @return  -ve when decryption fails.
 *          0 on success.
 */
int WP11_RsaOaep_PrivateDecrypt(unsigned char* in, word32 inLen,
                                unsigned char* out, word32* outLen,
                                WP11_Object* priv, WP11_Session* session)
{
    int ret = 0;
    WP11_OaepParams* oaep = &session->params.oaep;
    WP11_Slot* slot = WP11_Session_GetSlot(session);
#if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    WC_RNG rng;
#endif

    /* A random number generator is needed for blinding. */
    if (priv->onToken)
        WP11_Lock_LockRW(priv->lock);
#if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        priv->data.rsaKey.rng = &rng;
    }
#endif
    if (ret == 0) {
        ret = wc_RsaPrivateDecrypt_ex(in, inLen, out, *outLen,
                                            &priv->data.rsaKey, WC_RSA_OAEP_PAD,
                                            oaep->hashType, oaep->mgf,
                                            oaep->label, oaep->labelSz);
    #if defined(WC_RSA_BLINDING) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
        priv->data.rsaKey.rng = NULL;
        Rng_Free(&rng);
    #endif
    }
    if (priv->onToken)
        WP11_Lock_UnlockRW(priv->lock);

    if (ret >= 0) {
        *outLen = ret;
        ret = 0;

        if (oaep->label != NULL) {
            XFREE(oaep->label, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            oaep->label = NULL;
        }
    }
    (void)slot;

    return ret;
}
#endif

/**
 * PKCS#1.5 sign encoded hash with private key.
 *
 * @param  encHash     [in]      Encoded hash to sign.
 * @param  encHashLen  [in]      Length of encoded hash.
 * @param  sig         [in]      Buffer to hold signature data.
 * @param  sigLen      [in,out]  On in, length of buffer.
 *                               On out, length data in buffer.
 * @param  priv        [in]      Private key object.
 * @param  slot        [in]      Slot operation is performed on.
 * @return  RSA_BUFFER_E or BUFFER_E when sigLen is too small.
 *          Other -ve when signing fails.
 *          0 on success.
 */
int WP11_RsaPkcs15_Sign(unsigned char* encHash, word32 encHashLen,
                        unsigned char* sig, word32* sigLen, WP11_Object* priv,
                        WP11_Slot* slot)
{
    int ret;
    WC_RNG rng;

    if (priv->onToken)
        WP11_Lock_LockRO(priv->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaSSL_Sign(encHash, encHashLen, sig, *sigLen,
                                                      &priv->data.rsaKey, &rng);
        Rng_Free(&rng);
    }
    if (priv->onToken)
        WP11_Lock_UnlockRO(priv->lock);

    if (ret > 0)
        *sigLen = ret;

    return ret;
}

/**
 * PKCS#1.5 verify encoded hash with public key.
 *
 * @param  sig         [in]   Signature data.
 * @param  sigLen      [in]   Length of buffer.
 * @param  encHash     [in]   Encoded hash to verify.
 * @param  encHashLen  [in]   Length of encoded hash.
 * @param  stat        [out]  Status of verification. 1 on success, otherwise 0.
 * @param  pub         [in]   Public key object.
 * @return  -ve when verifying fails.
 *          0 on success.
 */
int WP11_RsaPkcs15_Verify(unsigned char* sig, word32 sigLen,
                          unsigned char* encHash, word32 encHashLen, int* stat,
                          WP11_Object* pub)
{
    byte decSig[RSA_MAX_SIZE / 8];
    word32 decSigLen;
    int ret;

    *stat = 0;

    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    decSigLen = ret = wc_RsaSSL_Verify(sig, sigLen, decSig, sizeof(decSig),
                                                             &pub->data.rsaKey);
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    if (ret > 0)
        ret = 0;

    if (ret == 0) {
        *stat = encHashLen == decSigLen &&
                                       XMEMCMP(encHash, decSig, decSigLen) == 0;
    }

    return ret;
}

#ifdef WC_RSA_PSS
/**
 * PKCS#1 PSS sign data with private key.
 *
 * @param  hash     [in]      Hash to sign.
 * @param  hashLen  [in]      Length of hash.
 * @param  sig      [in]      Buffer to hold signature data.
 * @param  sigLen   [in]      Length of buffer.
 * @param  sigLen   [in,out]  On in, length of buffer.
 *                            On out, length data in buffer.
 * @param  priv     [in]      Private key object.
 * @param  session  [in]      Session object holding PSS parameters.
 * @return  RSA_BUFFER_E or BUFFER_E when sigLen is too small.
 *          Other -ve when signing fails.
 *          0 on success.
 */
int WP11_RsaPKCSPSS_Sign(unsigned char* hash, word32 hashLen,
                         unsigned char* sig, word32* sigLen,
                         WP11_Object* priv, WP11_Session* session)
{
    int ret;
    WP11_PssParams* pss = &session->params.pss;
    WP11_Slot* slot = WP11_Session_GetSlot(session);
    WC_RNG rng;

    if (priv->onToken)
        WP11_Lock_LockRO(priv->lock);
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_RsaPSS_Sign_ex(hash, hashLen, sig, *sigLen, pss->hashType,
                                     pss->mgf, pss->saltLen, &priv->data.rsaKey,
                                     &rng);
        Rng_Free(&rng);
    }
    if (priv->onToken)
        WP11_Lock_UnlockRO(priv->lock);
    if (ret > 0)
        *sigLen = ret;

    return ret;
}

/**
 * PKCS#1 PSS verify encoded hash with public key.
 *
 * @param  sig      [in]   Signature data.
 * @param  sigLen   [in]   Length of buffer.
 * @param  hsh      [in]   Encoded hash to verify.
 * @param  hashLen  [in]   Length of encoded hash.
 * @param  stat     [out]  Status of verification. 1 on success, otherwise 0.
 * @param  pub      [in]   Public key object.
 * @param  session  [in]   Session object holding PSS parameters.
 * @return  -ve when verifying fails.
 *          0 on success.
 */
int WP11_RsaPKCSPSS_Verify(unsigned char* sig, word32 sigLen,
                           unsigned char* hash, word32 hashLen, int* stat,
                           WP11_Object* pub, WP11_Session* session)
{
    byte decSig[RSA_MAX_SIZE / 8];
    int decSz;
    int ret;
    WP11_PssParams* pss = &session->params.pss;

    *stat = 1;

    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    decSz = ret = wc_RsaPSS_Verify_ex(sig, sigLen, decSig, sizeof(decSig),
                                 pss->hashType, pss->mgf, pss->saltLen,
                                 &pub->data.rsaKey);
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    if (ret >= 0)
        ret = 0;
    else if (ret == BAD_PADDING_E) {
        *stat = 0;
        ret = 0;
    }

    if (ret == 0) {
        ret = wc_RsaPSS_CheckPadding_ex(hash, hashLen, decSig, decSz,
                                          pss->hashType, pss->saltLen, 0);
        if (ret == BAD_PADDING_E) {
            *stat = 0;
            ret = 0;
        }
    }

    return ret;
}
#endif
#endif

#ifdef HAVE_ECC
/**
 * Generate an EC key pair.
 *
 * @param  pub   [in]  Public key object.
 * @param  priv  [in]  Private key object.
 * @param  slot  [in]  Slot operation is performed on.
 * @return  -ve when key generation fails.
 *          0 on success.
 */
int WP11_Ec_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                            WP11_Slot* slot)
{
    int ret = 0;
    WC_RNG rng;

    /* Copy parameters from public key into private key. */
    priv->data.ecKey.dp = pub->data.ecKey.dp;

    /* Generate into the private key. */
    ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
    if (ret == 0) {
        ret = wc_ecc_make_key(&rng, priv->data.ecKey.dp->size,
                                                             &priv->data.ecKey);
        Rng_Free(&rng);
    }
    if (ret == 0) {
        /* Copy the public part into public key. */
        ret = wc_ecc_copy_point(&priv->data.ecKey.pubkey,
                                                       &pub->data.ecKey.pubkey);
    }
    if (ret == 0) {
        priv->data.ecKey.type = ECC_PRIVATEKEY;
        pub->data.ecKey.type = ECC_PUBLICKEY;
        priv->local = 1;
        pub->local = 1;
        priv->keyGenMech = CKM_EC_KEY_PAIR_GEN;
        pub->keyGenMech = CKM_EC_KEY_PAIR_GEN;
    }

    return ret;
}

/**
 * ASN.1 encode the ECDSA signature.
 *
 * @param  sig     [in]  Raw signature data.
 * @param  sigSz   [in]  Length of raw signature.
 * @param  encSig  [in]  Buffer to hold encoded signature.
 * @return  Length of encoded signature.
 */
static word32 Pkcs11ECDSASig_Encode(byte* sig, word32 sigSz, byte* encSig)
{
    word32 rHigh, sHigh, seqLen;
    word32 rStart = 0, sStart = 0;
    word32 sz, rSz, rLen, sSz, sLen;
    word32 i;

    /* Ordinate size. */
    sz = sigSz / 2;

    /* Find first byte of data in r and s. */
    while (sig[rStart] == 0x00 && rStart < sz - 1)
        rStart++;
    while (sig[sz + sStart] == 0x00 && sStart < sz - 1)
        sStart++;
    /* Check if 0 needs to be prepended to make integer a positive number. */
    rHigh = sig[rStart] >> 7;
    sHigh = sig[sz + sStart] >> 7;
    /* Calculate length of integer to put into ASN.1 encoding. */
    rLen = sz - rStart;
    sLen = sz - sStart;
    /* r and s: INT (2 bytes) + [ 0x00 ] + integer */
    rSz = 2 + rHigh + rLen;
    sSz = 2 + sHigh + sLen;
    /* Calculate the complete ASN.1 DER encoded size. */
    sigSz = rSz + sSz;
    if (sigSz >= ASN_LONG_LENGTH)
        seqLen = 3;
    else
        seqLen = 2;

    /* Move s and then r integers into their final places. */
    XMEMCPY(encSig + seqLen + rSz + (sSz - sLen), sig + sz + sStart, sLen);
    XMEMCPY(encSig + seqLen       + (rSz - rLen), sig      + rStart, rLen);

    /* Put the ASN.1 DER encoding around data. */
    i = 0;
    encSig[i++] = ASN_CONSTRUCTED | ASN_SEQUENCE;
    if (seqLen == 3)
        encSig[i++] = ASN_LONG_LENGTH | 0x01;
    encSig[i++] = sigSz;
    encSig[i++] = ASN_INTEGER;
    encSig[i++] = rHigh + (sz - rStart);
    if (rHigh)
        encSig[i++] = 0x00;
    i += sz - rStart;
    encSig[i++] = ASN_INTEGER;
    encSig[i++] = sHigh + (sz - sStart);
    if (sHigh)
        encSig[i] = 0x00;

    return seqLen + sigSz;
}

/**
 * Decode ASN.1 encoded ECDSA signature.
 *
 * @param  in     [in]  Encode signature data.
 * @param  inSz   [in]  Length of encoded signature in bytes.
 * @param  sig    [in]  Buffer to hold raw signature data.
 * @param  sigSz  [in]  Length of ordinate in bytes.
 * @return  ASN_PARSE_E when the ASN.1 encoding is invalid.
 *          0 on success.
 */
static int Pkcs11ECDSASig_Decode(const byte* in, word32 inSz, byte* sig,
                                 word32 sz)
{
    int ret = 0;
    word32 i = 0;
    int len, seqLen = 2;

    /* Make sure zeros in place when decoding short integers. */
    XMEMSET(sig, 0, sz * 2);

    /* Check min data for: SEQ + INT. */
    if (inSz < 5)
        ret = ASN_PARSE_E;
    /* Check SEQ */
    if (ret == 0 && in[i++] != (ASN_CONSTRUCTED | ASN_SEQUENCE))
        ret = ASN_PARSE_E;
    if (ret == 0 && in[i] >= ASN_LONG_LENGTH) {
        if (in[i] != (ASN_LONG_LENGTH | 0x01))
            ret = ASN_PARSE_E;
        else {
            i++;
            seqLen++;
        }
    }
    if (ret == 0 && in[i++] != inSz - seqLen)
        ret = ASN_PARSE_E;

    /* Check INT */
    if (ret == 0 && in[i++] != ASN_INTEGER)
        ret = ASN_PARSE_E;
    if (ret == 0 && (len = in[i++]) > sz + 1)
        ret = ASN_PARSE_E;
    /* Check there is space for INT data */
    if (ret == 0 && i + len > inSz)
        ret = ASN_PARSE_E;
    if (ret == 0) {
        /* Skip leading zero */
        if (in[i] == 0x00) {
            i++;
            len--;
        }
        /* Copy r into sig. */
        XMEMCPY(sig + sz - len, in + i, len);
        i += len;
    }

    /* Check min data for: INT. */
    if (ret == 0 && i + 2 > inSz)
        ret = ASN_PARSE_E;
    /* Check INT */
    if (ret == 0 && in[i++] != ASN_INTEGER)
        ret = ASN_PARSE_E;
    if (ret == 0 && (len = in[i++]) > sz + 1)
        ret = ASN_PARSE_E;
    /* Check there is space for INT data */
    if (ret == 0 && i + len > inSz)
        ret = ASN_PARSE_E;
    if (ret == 0) {
        /* Skip leading zero */
        if (in[i] == 0x00) {
            i++;
            len--;
        }
        /* Copy s into sig. */
        XMEMCPY(sig + sz + sz - len, in + i, len);
    }

    return ret;
}

/**
 * Return the length of a signature in bytes.
 *
 * @param  key  [in]  EC key object.
 * @return  Length of ECDSA signature in bytes.
 */
int WP11_Ec_SigLen(WP11_Object* key)
{
    return key->data.ecKey.dp->size * 2;
}

/**
 * ECDSA sign data with private key.
 *
 * @param  hash     [in]      Hash to sign.
 * @param  hashLen  [in]      Length of hash in bytes.
 * @param  sig      [in]      Buffer to hold signature data.
 * @param  sigLen   [in]      Length of buffer in bytes.
 * @param  sigLen   [in,out]  On in, length of buffer.
 *                            On out, length data in buffer.
 * @param  priv     [in]      Private key object.
 * @param  slot     [in]      Slot operation is performed on.
 * @return  BUFFER_E when sigLen is too small.
 *          Other -ve when signing fails.
 *          0 on success.
 */
int WP11_Ec_Sign(unsigned char* hash, word32 hashLen, unsigned char* sig,
                 word32* sigLen, WP11_Object* priv, WP11_Slot* slot)
{
    int ret = 0;
    byte encSig[ECC_MAX_SIG_SIZE];
    word32 encSigLen;
    word32 ordSz;
    WC_RNG rng;

    if (priv->onToken)
        WP11_Lock_LockRO(priv->lock);
    ordSz = priv->data.ecKey.dp->size;
    if (priv->onToken)
        WP11_Lock_UnlockRO(priv->lock);

    if (*sigLen < ordSz * 2)
        ret = BUFFER_E;
    if  (ret == 0) {
        encSigLen = sizeof(encSig);

        if (priv->onToken)
            WP11_Lock_LockRO(priv->lock);
        ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
        if (ret == 0) {
            ret = wc_ecc_sign_hash(hash, hashLen, encSig, &encSigLen, &rng,
                                                             &priv->data.ecKey);
            Rng_Free(&rng);
        }
        if (priv->onToken)
            WP11_Lock_UnlockRO(priv->lock);

        if (ret == 0) {
            ret = Pkcs11ECDSASig_Decode(encSig, encSigLen, sig, ordSz);
        }
        if (ret == 0)
            *sigLen = ordSz * 2;
    }

    return ret;
}

/**
 * ECDSA verify encoded hash with public key.
 *
 * @param  sig      [in]   Signature data.
 * @param  sigLen   [in]   Length of buffer in bytes.
 * @param  hash     [in]   Encoded hash to verify.
 * @param  hashLen  [in]   Length of encoded hash in bytes.
 * @param  pub      [in]   Public key object.
 * @param  stat     [out]  Status of verification. 1 on success, otherwise 0.
 * @param  pub      [in]   Public key object.
 * @return  -ve when verifying fails.
 *          0 on success.
 */
int WP11_Ec_Verify(unsigned char* sig, word32 sigLen, unsigned char* hash,
                   word32 hashLen, int* stat, WP11_Object* pub)
{
    int ret = 0;
    byte encSig[ECC_MAX_SIG_SIZE];
    word32 encSigLen;

    *stat = 0;
    if (pub->onToken)
        WP11_Lock_LockRO(pub->lock);
    if (sigLen != (word32)(2 * pub->data.ecKey.dp->size))
        ret = BAD_FUNC_ARG;
    if (ret == 0) {
        encSigLen = Pkcs11ECDSASig_Encode(sig, sigLen, encSig);
        ret = wc_ecc_verify_hash(encSig, encSigLen, hash, hashLen, stat,
                                                              &pub->data.ecKey);
    }
    if (pub->onToken)
        WP11_Lock_UnlockRO(pub->lock);

    return ret;
}


/**
 * Derive the secret from the private key and peer's point with ECDH.
 *
 * @param  point     [in]  Peer's point data.
 * @param  pointLen  [in]  Length of point data in bytes.
 * @param  key       [in]  Buffer to hold secret key.
 * @param  keyLen    [in]  Length of buffer in bytes.
 * @param  priv      [in]  The private key.
 * @return  -ve when derivation fails.
 *          0 on success.
 */
int WP11_EC_Derive(unsigned char* point, word32 pointLen, unsigned char* key,
                   word32 keyLen, WP11_Object* priv)
{
    int ret;
    ecc_key pubKey;
#if defined(ECC_TIMING_RESISTANT) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    WC_RNG rng;
#endif

    ret = wc_ecc_init_ex(&pubKey, NULL, INVALID_DEVID);
    if (ret == 0) {
        ret = wc_ecc_import_x963(point, pointLen, &pubKey);
    }
#if defined(ECC_TIMING_RESISTANT) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
    if (ret == 0) {
        ret = Rng_New(&priv->slot->token.rng, &priv->slot->token.rngLock, &rng);
        wc_ecc_set_rng(&priv->data.ecKey, &rng);
    }
#endif
    if (ret == 0) {
        if (priv->onToken)
            WP11_Lock_LockRO(priv->lock);
        ret = wc_ecc_shared_secret(&priv->data.ecKey, &pubKey, key, &keyLen);
        if (priv->onToken)
            WP11_Lock_UnlockRO(priv->lock);
#if defined(ECC_TIMING_RESISTANT) && (!defined(HAVE_FIPS) || \
    (defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION > 2)))
        Rng_Free(&rng);
#endif
    }

    wc_ecc_free(&pubKey);

    return ret;
}
#endif

#ifndef NO_DH
/**
 * Generate an DH key pair.
 *
 * @param  pub   [in]  Public key object.
 * @param  priv  [in]  Private key object.
 * @param  slot  [in]  Slot operation is performed on.
 * @return  -ve when key generation fails.
 *          0 on success.
 */
int WP11_Dh_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                            WP11_Slot* slot)
{
    int ret;
    WC_RNG rng;

    /* Copy the parameters from the public key into the private key. */
    ret = mp_copy(&pub->data.dhKey.params.p, &priv->data.dhKey.params.p);
    if (ret == 0)
        ret = mp_copy(&pub->data.dhKey.params.g, &priv->data.dhKey.params.g);
    if (ret == 0) {
        ret = Rng_New(&slot->token.rng, &slot->token.rngLock, &rng);
        if (ret == 0) {
            priv->data.dhKey.len = (word32)sizeof(priv->data.dhKey.key);
            pub->data.dhKey.len = (word32)sizeof(pub->data.dhKey.key);
            ret = wc_DhGenerateKeyPair(&pub->data.dhKey.params, &rng,
                                    priv->data.dhKey.key, &priv->data.dhKey.len,
                                    pub->data.dhKey.key, &pub->data.dhKey.len);
            Rng_Free(&rng);
        }
    }
    if (ret == 0) {
        priv->local = 1;
        pub->local = 1;
        priv->keyGenMech = CKM_DH_PKCS_KEY_PAIR_GEN;
        pub->keyGenMech = CKM_DH_PKCS_KEY_PAIR_GEN;
    }

    return ret;
}

/**
 * Derive the secret from the private key and peer's public value with DH.
 *
 * @param  point     [in]  Peer's point data.
 * @param  pointLen  [in]  Length of point data in bytes.
 * @param  key       [in]  Buffer to hold secret key.
 * @param  keyLen    [in]  Length of buffer in bytes.
 * @param  priv      [in]  The private key.
 * @return  -ve when derivation fails.
 *          0 on success.
 */
int WP11_Dh_Derive(unsigned char* pub, word32 pubLen, unsigned char* key,
                   word32* keyLen, WP11_Object* priv)
{
    int ret;

    if (priv->onToken)
        WP11_Lock_LockRO(priv->lock);
    ret = wc_DhAgree(&priv->data.dhKey.params, key, keyLen,
                       priv->data.dhKey.key, priv->data.dhKey.len, pub, pubLen);
    if (priv->onToken)
        WP11_Lock_UnlockRO(priv->lock);

    return ret;
}
#endif

#ifndef NO_AES
/**
 * Generate an AES secret key.
 *
 * @param  secret  [in]  Secret object.
 * @param  slot    [in]  Slot operation is performed on.
 * @return  -ve on random number generation failure.
 *          0 on success.
 */
int WP11_AesGenerateKey(WP11_Object* secret, WP11_Slot* slot)
{
    int ret;
    WP11_Data* key = &secret->data.symmKey;

    WP11_Lock_LockRW(&slot->token.rngLock);
    ret = wc_RNG_GenerateBlock(&slot->token.rng, key->data, key->len);
    WP11_Lock_UnlockRW(&slot->token.rngLock);

    return ret;
}

#ifdef HAVE_AES_CBC
/**
 * Return the amount of data not yet processed.
 *
 * @param  session  [in]  Session object.
 * @return  Unprocessed amount in bytes.
 */
int WP11_AesCbc_PartLen(WP11_Session* session)
{
    WP11_CbcParams* cbc = &session->params.cbc;

    return cbc->partialSz;
}

/**
 * Encrypt plain text with AES-CBC.
 * Output buffer must be large enough to hold all data.
 * No padding performed - plain text must be a multiple of the block size.
 *
 * @param  plain    [in]      Plain text.
 * @param  plainSz  [in]      Length of plain text in bytes.
 * @param  enc      [in]      Buffer to hold encrypted data.
 * @param  encSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of encrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesCbc_Encrypt(unsigned char* plain, word32 plainSz,
                        unsigned char* enc, word32* encSz,
                        WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;

    ret = wc_AesCbcEncrypt(&cbc->aes, enc, plain, plainSz);
    if (ret == 0)
        *encSz = plainSz;

    wc_AesFree(&cbc->aes);
    session->init = 0;

    return ret;
}

/**
 * Encrypt more plain text with AES-CBC.
 * Data not filling a block is cached.
 *
 * @param  plain    [in]      Plain text.
 * @param  plainSz  [in]      Length of plain text in bytes.
 * @param  enc      [in]      Buffer to hold encrypted data.
 * @param  encSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of encrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesCbc_EncryptUpdate(unsigned char* plain, word32 plainSz,
                              unsigned char* enc, word32* encSz,
                              WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;
    int sz = 0;
    int outSz = 0;

    if (cbc->partialSz > 0) {
        sz = AES_BLOCK_SIZE - cbc->partialSz;
        if (sz > (int)plainSz)
            sz = plainSz;
        XMEMCPY(cbc->partial + cbc->partialSz, plain, sz);
        cbc->partialSz += sz;
        plain += sz;
        plainSz -= sz;
        if (cbc->partialSz == AES_BLOCK_SIZE) {
            ret = wc_AesCbcEncrypt(&cbc->aes, enc, cbc->partial,
                                                                AES_BLOCK_SIZE);
            enc += AES_BLOCK_SIZE;
            outSz += AES_BLOCK_SIZE;
            cbc->partialSz = 0;
            XMEMSET(cbc->partial, 0, AES_BLOCK_SIZE);
        }
    }
    if (ret == 0 && plainSz > 0) {
        sz = plainSz & (~(AES_BLOCK_SIZE - 1));
        if (sz > 0) {
            ret = wc_AesCbcEncrypt(&cbc->aes, enc, plain, sz);
            outSz += sz;
            plain += sz;
            plainSz -= sz;
        }
    }
    if (ret == 0 && plainSz > 0) {
        XMEMCPY(cbc->partial, plain, plainSz);
        cbc->partialSz = plainSz;
    }
    if (ret == 0)
        *encSz = outSz;

    return ret;
}

/**
 * Finalize encryption with AES-CBC.
 * No encrypted data to be returned as no padding is performed.
 *
 * @param  session  [in]      Session object holding Aes object.
 * @return  0 on success.
 */
int WP11_AesCbc_EncryptFinal(WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;

    wc_AesFree(&cbc->aes);
    cbc->partialSz = 0;
    session->init = 0;

    return ret;
}

/**
 * Decrypt data with AES-CBC.
 * Output buffer must be large enough to hold all data.
 * No check for padding is performed.
 *
 * @param  enc      [in]      Encrypted data.
 * @param  encSz    [in]      Length of encrypted data in bytes.
 * @param  dec      [in]      Buffer to hold decrypted data.
 * @param  decSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of decrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesCbc_Decrypt(unsigned char* enc, word32 encSz, unsigned char* dec,
                        word32* decSz, WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;

    ret = wc_AesCbcDecrypt(&cbc->aes, dec, enc, encSz);
    if (ret == 0)
        *decSz = encSz;

    wc_AesFree(&cbc->aes);
    session->init = 0;

    return ret;
}

/**
 * Decrypt more data with AES-CBC.
 * No check for padding is performed.
 *
 * @param  enc      [in]      Encrypted data.
 * @param  encSz    [in]      Length of encrypted data in bytes.
 * @param  dec      [in]      Buffer to hold decrypted data.
 * @param  decSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of decrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesCbc_DecryptUpdate(unsigned char* enc, word32 encSz,
                              unsigned char* dec, word32* decSz,
                              WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;
    int sz = 0;
    int outSz = 0;

    if (cbc->partialSz > 0) {
        sz = AES_BLOCK_SIZE - cbc->partialSz;
        if (sz > (int)encSz)
            sz = encSz;
        XMEMCPY(cbc->partial + cbc->partialSz, enc, sz);
        cbc->partialSz += sz;
        enc += sz;
        encSz -= sz;
        if (cbc->partialSz == AES_BLOCK_SIZE) {
            ret = wc_AesCbcDecrypt(&cbc->aes, dec, cbc->partial,
                                                                AES_BLOCK_SIZE);
            dec += AES_BLOCK_SIZE;
            outSz += AES_BLOCK_SIZE;
            cbc->partialSz = 0;
        }
    }
    if (ret == 0 && encSz > 0) {
        sz = encSz & (~(AES_BLOCK_SIZE - 1));
        if (sz > 0) {
            ret = wc_AesCbcDecrypt(&cbc->aes, dec, enc, sz);
            outSz += sz;
            enc += sz;
            encSz -= sz;
        }
    }
    if (ret == 0 && encSz > 0) {
        XMEMCPY(cbc->partial, enc, encSz);
        cbc->partialSz = encSz;
    }
    if (ret == 0)
        *decSz = outSz;

    return ret;
}

/**
 * Finalize decryption with AES-CBC.
 * No decrypted data is returned as no check for padding is performed.
 *
 * @param  session  [in]      Session object holding Aes object.
 * @return  0 on success.
 */
int WP11_AesCbc_DecryptFinal(WP11_Session* session)
{
    int ret = 0;
    WP11_CbcParams* cbc = &session->params.cbc;

    wc_AesFree(&cbc->aes);
    cbc->partialSz = 0;
    session->init = 0;

    return ret;
}
#endif

#ifdef HAVE_AESGCM
/**
 * Return the tag bits of the GCM parameters.
 *
 * @param  session  [in]  Session object.
 * @return  GCM tag bits.
 */
int WP11_AesGcm_GetTagBits(WP11_Session* session)
{
    WP11_GcmParams* gcm = &session->params.gcm;

    return gcm->tagBits;
}

/**
 * Return the number of encryped data bytes to decrypt from GCM parameters.
 *
 * @param  session  [in]  Session object.
 * @return  Encrypted data bytes length.
 */
int WP11_AesGcm_EncDataLen(WP11_Session* session)
{
    WP11_GcmParams* gcm = &session->params.gcm;

    return gcm->encSz;
}

/**
 * Encrypt plain text with AES-GCM placing authentication tag on end.
 * Output buffer must be large enough to hold all data.
 *
 * @param  plain    [in]      Plain text.
 * @param  plainSz  [in]      Length of plain text in bytes.
 * @param  enc      [in]      Buffer to hold encrypted data.
 * @param  encSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of encrypted data including
 *                            authentication tag in bytes.
 * @param  secret   [in]      Secret key object.
 * @param  session  [in]      Session object holding GCM parameters.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesGcm_Encrypt(unsigned char* plain, word32 plainSz,
                        unsigned char* enc, word32* encSz, WP11_Object* secret,
                        WP11_Session* session)
{
    int ret;
    Aes aes;
    WP11_Data* key;
    WP11_GcmParams* gcm = &session->params.gcm;
    word32 authTagSz = gcm->tagBits / 8;
    unsigned char* authTag = enc + plainSz;

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (secret->onToken)
            WP11_Lock_LockRO(secret->lock);
        key = &secret->data.symmKey;
        ret = wc_AesGcmSetKey(&aes, key->data, key->len);
        if (secret->onToken)
            WP11_Lock_UnlockRO(secret->lock);

        if (ret == 0)
            ret = wc_AesGcmEncrypt(&aes, enc, plain, plainSz, gcm->iv,
                                        gcm->ivSz, authTag, authTagSz, gcm->aad,
                                        gcm->aadSz);
        if (ret == 0)
            *encSz = plainSz + authTagSz;

        if (gcm->aad != NULL) {
            XFREE(gcm->aad, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            gcm->aad = NULL;
        }

        wc_AesFree(&aes);
    }

    return ret;
}

/**
 * Encrypt plain text with AES-GCM.
 * Does not output authentication tag.
 *
 * @param  plain    [in]      Plain text.
 * @param  plainSz  [in]      Length of plain text in bytes.
 * @param  enc      [in]      Buffer to hold encrypted data.
 * @param  encSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of encrypted data in bytes.
 * @param  secret   [in]      Secret key object.
 * @param  session  [in]      Session object holding GCM parameters.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesGcm_EncryptUpdate(unsigned char* plain, word32 plainSz,
                              unsigned char* enc, word32* encSz,
                              WP11_Object* secret, WP11_Session* session)
{
    int ret;
    Aes aes;
    WP11_Data* key;
    WP11_GcmParams* gcm = &session->params.gcm;
    word32 authTagSz = gcm->tagBits / 8;
    unsigned char* authTag = gcm->authTag;

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (secret->onToken)
            WP11_Lock_LockRO(secret->lock);
        key = &secret->data.symmKey;
        ret = wc_AesGcmSetKey(&aes, key->data, key->len);
        if (secret->onToken)
            WP11_Lock_UnlockRO(secret->lock);

        if (ret == 0)
            ret = wc_AesGcmEncrypt(&aes, enc, plain, plainSz, gcm->iv,
                                        gcm->ivSz, authTag, authTagSz, gcm->aad,
                                        gcm->aadSz);
        if (ret == 0)
            *encSz = plainSz;

        if (gcm->aad != NULL) {
            XFREE(gcm->aad, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            gcm->aad = NULL;
        }

        wc_AesFree(&aes);
    }

    return ret;
}

/**
 * Finalize encryption with AES-GCM.
 * Returns the authentication tag.
 *
 * @param  enc      [in]      Buffer to hold encrypted data.
 * @param  encSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of encrypted data in bytes.
 * @param  session  [in]      Session object holding GCM parameters.
 * @return  BUFFER_E when encSz is too small for authentication tag.
 *          0 on success.
 */
int WP11_AesGcm_EncryptFinal(unsigned char* enc, word32* encSz,
                             WP11_Session* session)
{
    int ret = 0;
    WP11_GcmParams* gcm = &session->params.gcm;
    word32 authTagSz = gcm->tagBits / 8;

    if (*encSz < authTagSz)
        ret = BUFFER_E;
    if (ret == 0) {
        XMEMCPY(enc, gcm->authTag, authTagSz);
        *encSz = authTagSz;
    }

    return ret;
}

/**
 * Decrypt data, including authentication tag, with AES-GCM.
 * Output buffer must be large enough to hold all decrypted data.
 *
 * @param  enc      [in]      Encrypted data.
 * @param  encSz    [in]      Length of encrypted data in bytes.
 * @param  dec      [in]      Buffer to hold decrypted data.
 * @param  decSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of decrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  AES_GCM_AUTH_E when data does not authenticate.
 *          Other -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesGcm_Decrypt(unsigned char* enc, word32 encSz, unsigned char* dec,
                        word32* decSz, WP11_Object* secret,
                        WP11_Session* session)
{
    int ret;
    Aes aes;
    WP11_Data* key;
    WP11_GcmParams* gcm = &session->params.gcm;
    word32 authTagSz = gcm->tagBits / 8;
    unsigned char* authTag = enc + encSz - authTagSz;

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (secret->onToken)
            WP11_Lock_LockRO(secret->lock);
        key = &secret->data.symmKey;
        ret = wc_AesGcmSetKey(&aes, key->data, key->len);
        if (secret->onToken)
            WP11_Lock_UnlockRO(secret->lock);

        if (ret == 0)
            encSz -= authTagSz;
            ret = wc_AesGcmDecrypt(&aes, dec, enc, encSz, gcm->iv, gcm->ivSz,
                                      authTag, authTagSz, gcm->aad, gcm->aadSz);
        if (ret == 0)
            *decSz = encSz;

        if (gcm->aad != NULL) {
            XFREE(gcm->aad, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            gcm->aad = NULL;
        }

        wc_AesFree(&aes);
    }

    return ret;
}

/**
 * Decrypt more data with AES-GCM.
 * Authentication tag is at the end of the encrypted data.
 * All data is cached.
 *
 * @param  enc      [in]      Encrypted data.
 * @param  encSz    [in]      Length of encrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  AES_GCM_AUTH_E when data does not authenticate.
 *          Other -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesGcm_DecryptUpdate(unsigned char* enc, word32 encSz,
                              WP11_Session* session)
{
    int ret = 0;
    unsigned char* newEnc;
    WP11_GcmParams* gcm = &session->params.gcm;

    newEnc = XREALLOC(gcm->enc, gcm->encSz + encSz, NULL,
                                                       DYNAMIC_TYPE_TMP_BUFFER);
    if (newEnc == NULL)
        ret = MEMORY_E;
    if (ret == 0) {
        gcm->enc = newEnc;
        XMEMCPY(gcm->enc + gcm->encSz, enc, encSz);
        gcm->encSz += encSz;
    }

    return ret;
}

/**
 * Finalize decryption with AES-GCM.
 * Output buffer must be large enough to hold all decrypted data.
 *
 * @param  dec      [in]      Buffer to hold decrypted data.
 * @param  decSz    [in,out]  On in, length of buffer in bytes.
 *                            On out, length of decrypted data in bytes.
 * @param  session  [in]      Session object holding Aes object.
 * @return  AES_GCM_AUTH_E when data does not authenticate.
 *          Other -ve on encryption failure.
 *          0 on success.
 */
int WP11_AesGcm_DecryptFinal(unsigned char* dec, word32* decSz,
                             WP11_Object* secret, WP11_Session* session)
{
    int ret;
    WP11_GcmParams* gcm = &session->params.gcm;

    /* Auth tag on end of encrypted data. */
    ret = WP11_AesGcm_Decrypt(gcm->enc, gcm->encSz, dec, decSz, secret,
                                                                       session);
    XFREE(gcm->enc, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    gcm->enc = NULL;
    gcm->encSz = 0;

    return ret;
}
#endif
#endif

#ifndef NO_HMAC
/**
 * Convert the HMAC mechanism to a wolfCrypt hash type.
 *
 * @param  hmacMech  [in]   HMAC mechanism.
 * @param  hashType  [out]  Hash type.
 * @return  BAD_FUNC_ARG when mechanism is not supported.
 *          0 on success.
 */
static int wp11_hmac_hash_type(CK_MECHANISM_TYPE hmacMech, int* hashType)
{
    int ret = 0;

    switch (hmacMech) {
        case CKM_MD5_HMAC:
            *hashType = WC_MD5;
            break;
        case CKM_SHA1_HMAC:
            *hashType = WC_SHA;
            break;
        case CKM_SHA224_HMAC:
            *hashType = WC_SHA224;
            break;
        case CKM_SHA256_HMAC:
            *hashType = WC_SHA256;
            break;
        case CKM_SHA384_HMAC:
            *hashType = WC_SHA384;
            break;
        case CKM_SHA512_HMAC:
            *hashType = WC_SHA512;
            break;
        default:
            ret = BAD_FUNC_ARG;
            break;
    }

    return ret;
}

/**
 * Return the length of a signature in bytes.
 *
 * @param  session  [in]  Session object.
 * @return  Length of HMAC signature in bytes.
 */
int WP11_Hmac_SigLen(WP11_Session* session)
{
    WP11_Hmac* hmac = &session->params.hmac;

    return hmac->hmacSz;
}

/**
 * Initialize the HMAC operation.
 *
 * @param  mechanism  [in]  HMAC mechanism.
 * @param  secret     [in]  Secret key object.
 * @param  session    [in]  Session object with the Hmac object.
 * @return  BAD_FUNC_ARG when the mechanism is not supported.
 *          Other -ve value when hashing PIN fails.
 *          0 on success.
 */
int WP11_Hmac_Init(CK_MECHANISM_TYPE mechanism, WP11_Object* secret,
                   WP11_Session* session)
{
    int ret;
    int hashType = WC_HASH_TYPE_NONE;
    WP11_Hmac* hmac = &session->params.hmac;
    WP11_Data* key;

    ret = wp11_hmac_hash_type(mechanism, &hashType);
    if (ret == 0)
        hmac->hmacSz = wc_HmacSizeByType(hashType);
    if (ret == 0)
        ret = wc_HmacInit(&hmac->hmac, NULL, INVALID_DEVID);
    if (ret == 0) {
        if (secret->onToken)
            WP11_Lock_LockRO(secret->lock);
        key = &secret->data.symmKey;
        ret = wc_HmacSetKey(&hmac->hmac, hashType, key->data, key->len);
        if (secret->onToken)
            WP11_Lock_UnlockRO(secret->lock);
    }

    return ret;
}

/**
 * Sign the data using HMAC.
 *
 * @param  data     [in]      Data to sign.
 * @param  dataLen  [in]      Length of data in bytes.
 * @param  sig      [in]      Buffer to hold signature.
 * @param  sigLen   [in,out]  On in, the length of the buffer in bytes.
 *                            On out, the length of the signature in bytes.
 * @param  session  [in]      Session object with the Hmac object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Hmac_Sign(unsigned char* data, word32 dataLen, unsigned char* sig,
                   word32* sigLen, WP11_Session* session)
{
    int ret = 0;
    WP11_Hmac* hmac = &session->params.hmac;

    if (*sigLen < hmac->hmacSz)
        ret = BUFFER_E;
    if (ret == 0)
        ret = wc_HmacUpdate(&hmac->hmac, data, dataLen);
    if (ret == 0)
        ret = wc_HmacFinal(&hmac->hmac, sig);
    if (ret == 0)
        *sigLen = hmac->hmacSz;
    wc_HmacFree(&hmac->hmac);
    session->init = 0;

    return ret;
}

/**
 * Verify the data using HMAC.
 *
 * @param  data     [in]   Data to verify.
 * @param  dataLen  [in]   Length of data in bytes.
 * @param  sig      [in]   Signature data.
 * @param  sigLen   [in]   Length of the signature in bytes.
 * @param  stat     [out]  Status of verification. 1 on success, otherwise 0.
 * @param  session  [in]   Session object with the Hmac object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Hmac_Verify(unsigned char* sig, word32 sigLen, unsigned char* data,
                     word32 dataLen, int* stat, WP11_Session* session)
{
    int ret = 0;
    unsigned char genSig[WC_MAX_DIGEST_SIZE];
    word32 genSigLen = sizeof(genSig);

    if (ret == 0)
        ret = WP11_Hmac_Sign(data, dataLen, genSig, &genSigLen, session);
    if (ret == 0)
        *stat = genSigLen == sigLen && XMEMCMP(sig, genSig, sigLen) == 0;

    return ret;
}

/**
 * Update HMAC sign/verify operation with more data.
 *
 * @param  data     [in]  Data to sign.
 * @param  dataLen  [in]  Length of data in bytes.
 * @param  session  [in]  Session object with the Hmac object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Hmac_Update(unsigned char* data, word32 dataLen, WP11_Session* session)
{
    int ret;
    WP11_Hmac* hmac = &session->params.hmac;

    ret = wc_HmacUpdate(&hmac->hmac, data, dataLen);

    return ret;
}

/**
 * Finalize HMAC signing.
 *
 * @param  sig      [in]      Buffer to hold signature.
 * @param  sigLen   [in,out]  On in, the length of the buffer in bytes.
 *                            On out, the length of the signature in bytes.
 * @param  session  [in]      Session object with the Hmac object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Hmac_SignFinal(unsigned char* sig, word32* sigLen,
                        WP11_Session* session)
{
    int ret = 0;
    WP11_Hmac* hmac = &session->params.hmac;

    if (*sigLen < hmac->hmacSz)
        ret = BUFFER_E;
    if (ret == 0)
        ret = wc_HmacFinal(&hmac->hmac, sig);
    if (ret == 0)
        *sigLen = hmac->hmacSz;
    wc_HmacFree(&hmac->hmac);
    session->init = 0;

    return ret;
}

/**
 * Verify the data using HMAC.
 *
 * @param  sig      [in]   Signature data.
 * @param  sigLen   [in]   Length of the signature in bytes.
 * @param  stat     [out]  Status of verification. 1 on success, otherwise 0.
 * @param  session  [in]   Session object with the Hmac object.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Hmac_VerifyFinal(unsigned char* sig, word32 sigLen, int* stat,
                          WP11_Session* session)
{
    int ret;
    unsigned char genSig[WC_MAX_DIGEST_SIZE];
    word32 genSigLen = sizeof(genSig);

    ret = WP11_Hmac_SignFinal(genSig, &genSigLen, session);
    if (ret == 0)
        *stat = genSigLen == sigLen && XMEMCMP(sig, genSig, sigLen) == 0;

    return ret;
}
#endif

/**
 * Seed the random number generator of the token in the slot.
 *
 * @param  slot     [in]  Slot object with token that has random number
 *                        generator.
 * @param  seed     [in]  Seed data.
 * @param  seedLen  [in]  Length of seed data.
 * @return  -ve on encryption failure.
 *          0 on success.
 */
int WP11_Slot_SeedRandom(WP11_Slot* slot, unsigned char* seed, int seedLen)
{
    int ret;

    WP11_Lock_LockRW(&slot->token.rngLock);
    wc_FreeRng(&slot->token.rng);
    ret = wc_InitRngNonce_ex(&slot->token.rng, seed, seedLen, NULL,
                                                                 INVALID_DEVID);
    WP11_Lock_UnlockRW(&slot->token.rngLock);

    return ret;
}

/**
 * Generate random data using random number generator of the token in the slot.
 *
 * @param  slot  [in]  Slot object with token that has random number generator.
 * @param  data  [in]  Buffer to hold random data.
 * @param  len   [in]  Amount of random data to generate in bytes.
 */
int WP11_Slot_GenerateRandom(WP11_Slot* slot, unsigned char* data, int len)
{
    int ret;

    WP11_Lock_LockRW(&slot->token.rngLock);
    ret = wc_RNG_GenerateBlock(&slot->token.rng, data, len);
    WP11_Lock_UnlockRW(&slot->token.rngLock);

    return ret;
}

