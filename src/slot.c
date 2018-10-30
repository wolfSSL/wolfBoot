/* slot.c
 *
 * Copyright (C) 2006-2018 wolfSSL Inc.
 *
 * This file is part of wolfPKCS11.
 *
 * wolfPKCS11 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <wolfpkcs11/pkcs11.h>
#include <wolfpkcs11/internal.h>


/**
 * Gets a list of slot identifiers for available slots.
 *
 * @param  tokenPresent  [in]      Require slot to have a token inserted.
 * @param  pSlotList     [in]      Array of slot ids to fill.
 *                                 NULL indicates the length is required.
 * @param  pulCount      [in,out]  On in, the number of array entries in
 *                                 pSlotList.
 *                                 On out, the number of slot ids put in array.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_ARGUMENTS_BAD when pulCount is NULL or tokenPresent isn't
 *          a valid boolean value.
 *          CKR_BUFFER_TOO_SMALL when more slot ids match that entries in array.
 *          CKR_OK on success.
 */
CK_RV C_GetSlotList(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList,
                    CK_ULONG_PTR pulCount)
{
    int ret;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (tokenPresent != CK_FALSE && tokenPresent != CK_TRUE)
        return CKR_ARGUMENTS_BAD;
    if (pulCount == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_GetSlotList(tokenPresent, pSlotList, pulCount);
    if (ret == BUFFER_E)
        return CKR_BUFFER_TOO_SMALL;

    return CKR_OK;
}

/* Index into slot id string to place number. */
#define SLOT_ID_IDX     20

/* Template for slot information. */
static CK_SLOT_INFO slotInfoTemplate = {
    "wolfSSL HSM slot ID xx",
    "wolfpkcs11",
    CKF_TOKEN_PRESENT,
    { WOLFPKCS11_MAJOR_VERSION, WOLFPKCS11_MINOR_VERSION },
    { WOLFPKCS11_MAJOR_VERSION, WOLFPKCS11_MINOR_VERSION }
};

/**
 * Get information on the slot.
 *
 * @param  slotID  [in]  Id of slot to query.
 * @param  pInfo   [in]  Slot information copied into it.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_ARGUMENTS_BAD when pInfo is NULL.
 *          CKR_OK on success.
 */
CK_RV C_GetSlotInfo(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (!WP11_SlotIdValid(slotID))
        return CKR_SLOT_ID_INVALID;
    if (pInfo == NULL)
        return CKR_ARGUMENTS_BAD;

    XMEMCPY(pInfo, &slotInfoTemplate, sizeof(slotInfoTemplate));
    /* Put in the slot id value as two decimal digits. */
    pInfo->slotDescription[SLOT_ID_IDX + 0] = ((slotID / 10) % 10) + '0';
    pInfo->slotDescription[SLOT_ID_IDX + 1] = ((slotID     ) % 10) + '0';

    return CKR_OK;
}

/* Template for token information. */
static CK_TOKEN_INFO tokenInfoTemplate = {
    "",
    "wolfpkcs11",
    "wolfpkcs11",
    "0000000000000000",
    CKF_RNG | CKF_CLOCK_ON_TOKEN,
    WP11_SESSION_CNT_MAX, /* ulMaxSessionCount */
    CK_UNAVAILABLE_INFORMATION, /* ulSessionCount */
    WP11_SESSION_CNT_MAX, /* ulMaxRwSessionCount */
    CK_UNAVAILABLE_INFORMATION, /* ulRwSessionCount */
    WP11_MAX_PIN_LEN, /* ulMaxPinLen */
    WP11_MIN_PIN_LEN, /* ulMinPinLen */
    CK_UNAVAILABLE_INFORMATION, /* ulTotalPublicMemory */
    CK_UNAVAILABLE_INFORMATION, /* ulFreePublicMemory */
    CK_UNAVAILABLE_INFORMATION, /* ulTotalPrivateMemory */
    CK_UNAVAILABLE_INFORMATION, /* ulFreePrivateMemory */
    { WOLFPKCS11_MAJOR_VERSION, WOLFPKCS11_MINOR_VERSION },
    { WOLFPKCS11_MAJOR_VERSION, WOLFPKCS11_MINOR_VERSION },
    "YYYYMMDDhhmmss00"
};

/**
 * Get information on the token.
 *
 * @param  slotID  [in]  Id of slot to use.
 * @param  pInfo   [in]  Token information copied into it.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_ARGUMENTS_BAD when pInfo is NULL.
 *          CKR_OK on success.
 */
CK_RV C_GetTokenInfo(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
    time_t now, expire;
    struct tm nowTM;
    WP11_Slot* slot;
    int cnt;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Slot_Get(slotID, &slot) != 0)
        return CKR_SLOT_ID_INVALID;
    if (pInfo == NULL)
        return CKR_ARGUMENTS_BAD;

    XMEMCPY(pInfo, &tokenInfoTemplate, sizeof(tokenInfoTemplate));
    WP11_Slot_GetTokenLabel(slot, (char*)pInfo->label);
    pInfo->serialNumber[14] = ((slotID / 10) % 10) + '0';
    pInfo->serialNumber[15] = ((slotID /  1) % 10) + '0';

    now = XTIME(0);
    XMEMSET(&nowTM, 0, sizeof(nowTM));
    if (XGMTIME(&now, &nowTM) != NULL) {
        pInfo->utcTime[ 0] = (((1900 + nowTM.tm_year) / 1000) % 10) + '0';
        pInfo->utcTime[ 1] = (((1900 + nowTM.tm_year) /  100) % 10) + '0';
        pInfo->utcTime[ 2] = (((1900 + nowTM.tm_year) /   10) % 10) + '0';
        pInfo->utcTime[ 3] = (((1900 + nowTM.tm_year) /    1) % 10) + '0';
        pInfo->utcTime[ 4] = (((1 + nowTM.tm_mon) / 10) % 10) + '0';
        pInfo->utcTime[ 5] = (((1 + nowTM.tm_mon) /  1) % 10) + '0';
        pInfo->utcTime[ 6] = ((nowTM.tm_mday / 10) % 10) + '0';
        pInfo->utcTime[ 7] = ((nowTM.tm_mday /  1) % 10) + '0';
        pInfo->utcTime[ 8] = ((nowTM.tm_hour / 10) % 10) + '0';
        pInfo->utcTime[ 9] = ((nowTM.tm_hour /  1) % 10) + '0';
        pInfo->utcTime[10] = ((nowTM.tm_min / 10) % 10) + '0';
        pInfo->utcTime[11] = ((nowTM.tm_min /  1) % 10) + '0';
        pInfo->utcTime[12] = ((nowTM.tm_sec / 10) % 10) + '0';
        pInfo->utcTime[13] = ((nowTM.tm_sec /  1) % 10) + '0';
    }
    else {
        /* Set date to all zeros. */
        XMEMCPY(pInfo->utcTime, "00000000000000", 14);
    }

    cnt = WP11_Slot_TokenFailedLogin(slot, WP11_LOGIN_SO);
    expire = WP11_Slot_TokenFailedExpire(slot, WP11_LOGIN_SO);
    if (cnt > 0)
        pInfo->flags |= CKF_SO_PIN_COUNT_LOW;
    if (cnt == WP11_MAX_LOGIN_FAILS_SO - 1)
        pInfo->flags |= CKF_SO_PIN_FINAL_TRY;
    else if (cnt == WP11_MAX_LOGIN_FAILS_SO && now < expire)
        pInfo->flags |= CKF_SO_PIN_LOCKED;

    cnt = WP11_Slot_TokenFailedLogin(slot, WP11_LOGIN_USER);
    expire = WP11_Slot_TokenFailedExpire(slot, WP11_LOGIN_USER);
    if (cnt > 0)
        pInfo->flags |= CKF_USER_PIN_COUNT_LOW;
    if (cnt == WP11_MAX_LOGIN_FAILS_USER - 1)
        pInfo->flags |= CKF_USER_PIN_FINAL_TRY;
    else if (cnt == WP11_MAX_LOGIN_FAILS_USER && now < expire)
        pInfo->flags |= CKF_USER_PIN_LOCKED;

    if (WP11_Slot_IsTokenInitialized(slot))
        pInfo->flags |= CKF_TOKEN_INITIALIZED;
    if (WP11_Slot_IsTokenUserPinInitialized(slot))
        pInfo->flags |= CKF_USER_PIN_INITIALIZED;

    return CKR_OK;
}

/* List of mechanism supported. */
static CK_MECHANISM_TYPE mechanismList[] = {
#ifndef NO_RSA
#ifdef WOLFSSL_KEY_GEN
    CKM_RSA_PKCS_KEY_PAIR_GEN,
#endif
    CKM_RSA_X_509,
    CKM_RSA_PKCS,
#ifndef WC_NO_RSA_OAEP
    CKM_RSA_PKCS_OAEP,
#endif
#ifdef WC_RSA_PSS
    CKM_RSA_PKCS_PSS,
#endif
#endif
#ifdef HAVE_ECC
    CKM_EC_KEY_PAIR_GEN,
    CKM_ECDSA,
    CKM_ECDH1_DERIVE,
#endif
#ifndef NO_DH
    CKM_DH_PKCS_KEY_PAIR_GEN,
    CKM_DH_PKCS_DERIVE,
#endif
#ifndef NO_AES
#ifdef HAVE_AES_CBC
    CKM_AES_CBC,
#endif
#ifdef HAVE_AESGCM
    CKM_AES_GCM,
#endif
#endif
#ifndef NO_HMAC
#ifndef NO_MD5
    CKM_MD5_HMAC,
#endif
#ifndef NO_SHA
    CKM_SHA1_HMAC,
#endif
#ifdef WOLFSSL_SHA224
    CKM_SHA224_HMAC,
#endif
#ifndef NO_SHA256
    CKM_SHA256_HMAC,
#endif
#ifdef WOLFSSL_SHA384
    CKM_SHA384_HMAC,
#endif
#ifdef WOLFSSL_SHA512
    CKM_SHA512_HMAC,
#endif
#endif
};

/* Count of mechanisms in list. */
static int mechanismCnt = ((int)(sizeof(mechanismList)/sizeof(*mechanismList)));

/**
 * Get list of supported mechanism fo for the slot.
 *
 * @param  slotID           [in]      Id of slot to use.
 * @param  pMechanismList   [in]      Array to hold mechanisms.
 *                                    NULL indicates the length is required.
 * @param  pulCount         [in,out]  On in, the number of array entries in
 *                                    pMechanismList.
 *                                    On out, the number of mechanisms put in
 *                                    array.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_ARGUMENTS_BAD when pulCount is NULL.
 *          CKR_BUFFER_TOO_SMALL when pulCount is NULL.
 *          CKR_BUFFER_TOO_SMALL when there are more mechanisms than entries in
 *          array.
 *          CKR_OK on success.
 */
CK_RV C_GetMechanismList(CK_SLOT_ID slotID,
                         CK_MECHANISM_TYPE_PTR pMechanismList,
                         CK_ULONG_PTR pulCount)
{
    int i;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (!WP11_SlotIdValid(slotID))
        return CKR_SLOT_ID_INVALID;
    if (pulCount == NULL)
        return CKR_ARGUMENTS_BAD;

    if (pMechanismList == NULL)
        *pulCount = mechanismCnt;
    else if (*pulCount < (CK_ULONG)mechanismCnt)
        return CKR_BUFFER_TOO_SMALL;
    else {
        for (i = 0; i < mechanismCnt; i++)
            pMechanismList[i] = mechanismList[i];
        *pulCount = mechanismCnt;
    }

    return CKR_OK;
}

#ifndef NO_RSA
#ifdef WOLFSSL_KEY_GEN
/* Info on RSA key generation mechanism. */
static CK_MECHANISM_INFO rsaKgMechInfo = {
    1024, 4096, CKF_GENERATE_KEY_PAIR
};
#endif
/* Info on RSA X.509 mechanism. */
static CK_MECHANISM_INFO rsaX509MechInfo = {
    1024, 4096, CKF_ENCRYPT | CKF_DECRYPT
};
/* Info on RSA PKCS#1.5 mechanism. */
static CK_MECHANISM_INFO rsaPkcsMechInfo = {
    1024, 4096, CKF_ENCRYPT | CKF_DECRYPT | CKF_SIGN | CKF_VERIFY
};
#ifndef WC_NO_RSA_OAEP
/* Info on RSA PKCS#1 OAEP mechanism. */
static CK_MECHANISM_INFO rsaOaepMechInfo = {
    1024, 4096, CKF_ENCRYPT | CKF_DECRYPT
};
#endif
#ifdef WC_RSA_PSS
/* Info on RSA PKCS#1 PSS mechanism. */
static CK_MECHANISM_INFO rsaPssMechInfo = {
    256, 521, CKF_SIGN | CKF_VERIFY
};
#endif
#endif
#ifdef HAVE_ECC
/* Info on EC key generation mechanism. */
static CK_MECHANISM_INFO ecKgMechInfo = {
    256, 521, CKF_GENERATE_KEY_PAIR
};
/* Info on ECDSA mechanism. */
static CK_MECHANISM_INFO ecdsaMechInfo = {
    256, 521, CKF_SIGN | CKF_VERIFY
};
/* Info on ECDH mechanism. */
static CK_MECHANISM_INFO ecdhMechInfo = {
    256, 521, CKF_DERIVE
};
#endif
#ifndef NO_DH
/* Info on DH key generation mechanism. */
static CK_MECHANISM_INFO dhKgMechInfo = {
    1024, 4096, CKF_GENERATE_KEY_PAIR
};
/* Info on DH key derivation mechanism. */
static CK_MECHANISM_INFO dhPkcsMechInfo = {
    1024, 4096, CKF_DERIVE
};
#endif
#ifndef NO_AES
#ifdef HAVE_AES_CBC
/* Info on AES-CBC mechanism. */
static CK_MECHANISM_INFO aesCbcMechInfo = {
    16, 32, CKF_ENCRYPT | CKF_DECRYPT
};
#endif
#ifdef HAVE_AESGCM
/* Info on AES-GCM mechanism. */
static CK_MECHANISM_INFO aesGcmMechInfo = {
    16, 32, CKF_ENCRYPT | CKF_DECRYPT
};
#endif
#endif
#ifndef NO_HMAC
#ifndef NO_MD5
/* Info on HMAC-MD5 mechanism. */
static CK_MECHANISM_INFO hmacMd5MechInfo = {
    16, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#ifndef NO_SHA
/* Info on HMAC-SHA1 mechanism. */
static CK_MECHANISM_INFO hmacSha1MechInfo = {
    20, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#ifdef WOLFSSL_SHA224
/* Info on HMAC-SHA224 mechanism. */
static CK_MECHANISM_INFO hmacSha224MechInfo = {
    28, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#ifndef NO_SHA256
/* Info on HMAC-SHA256 mechanism. */
static CK_MECHANISM_INFO hmacSha256MechInfo = {
    32, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#ifdef WOLFSSL_SHA384
/* Info on HMAC-SHA384 mechanism. */
static CK_MECHANISM_INFO hmacSha384MechInfo = {
    48, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#ifdef WOLFSSL_SHA512
/* Info on HMAC-SHA512 mechanism. */
static CK_MECHANISM_INFO hmacSha512MechInfo = {
    64, 512, CKF_SIGN | CKF_VERIFY
};
#endif
#endif

/**
 * Get information on a mechanism.
 *
 * @param  slotID  [in]  Id of slot to use.
 * @param  type    [in]  Mechanism type.
 * @param  pInfo   [in]  Mechnism information copied into it.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_ARGUMENTS_BAD when pInfo is NULL.
 *          CKR_MECHANISM_INVALID when mechanism is not supported.
 *          CKR_OK on success.
 */
CK_RV C_GetMechanismInfo(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type,
                         CK_MECHANISM_INFO_PTR pInfo)
{
    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (!WP11_SlotIdValid(slotID))
        return CKR_SLOT_ID_INVALID;
    if (pInfo == NULL)
        return CKR_ARGUMENTS_BAD;

    switch (type) {
#ifndef NO_RSA
    #ifdef WOLFSSL_KEY_GEN
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
            XMEMCPY(pInfo, &rsaKgMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
    #endif
        case CKM_RSA_X_509:
            XMEMCPY(pInfo, &rsaX509MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
        case CKM_RSA_PKCS:
            XMEMCPY(pInfo, &rsaPkcsMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
    #ifndef WC_NO_RSA_OAEP
        case CKM_RSA_PKCS_OAEP:
            XMEMCPY(pInfo, &rsaOaepMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
    #endif
    #ifdef WC_RSA_PSS
        case CKM_RSA_PKCS_PSS:
            XMEMCPY(pInfo, &rsaPssMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
    #endif
#endif
#ifdef HAVE_ECC
        case CKM_EC_KEY_PAIR_GEN:
            XMEMCPY(pInfo, &ecKgMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
        case CKM_ECDSA:
            XMEMCPY(pInfo, &ecdsaMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
        case CKM_ECDH1_DERIVE:
            XMEMCPY(pInfo, &ecdhMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifndef NO_DH
        case CKM_DH_PKCS_KEY_PAIR_GEN:
            XMEMCPY(pInfo, &dhKgMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
        case CKM_DH_PKCS_DERIVE:
            XMEMCPY(pInfo, &dhPkcsMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifndef NO_AES
#ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            XMEMCPY(pInfo, &aesCbcMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            XMEMCPY(pInfo, &aesGcmMechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#endif
#ifndef NO_HMAC
#ifndef NO_MD5
        case CKM_MD5_HMAC:
            XMEMCPY(pInfo, &hmacMd5MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifndef NO_SHA
        case CKM_SHA1_HMAC:
            XMEMCPY(pInfo, &hmacSha1MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
            XMEMCPY(pInfo, &hmacSha224MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifndef NO_SHA256
        case CKM_SHA256_HMAC:
            XMEMCPY(pInfo, &hmacSha256MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
            XMEMCPY(pInfo, &hmacSha384MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
            XMEMCPY(pInfo, &hmacSha512MechInfo, sizeof(CK_MECHANISM_INFO));
            break;
#endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Initialize or re-initialize token in slot.
 *
 * @param  slotId    [in]  Id of slot to use.
 * @param  pPin      [in]  PIN for Security Officer (SO).
 * @param  ulPinLen  [in]  Length of PIN in bytes.
 * @param  pLabel    [in]  Label for token.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_ARGUMENTS_BAD when pPin or pLabel is NULL.
 *          CKR_PIN_INCORRECT when length of PIN is not valid or PIN does not
 *          match initialized PIN.
 *          CKR_SESSION_EXISTS when a session is open on the token.
 *          CKR_FUNCTION_FAILED when resetting token fails.
 *          CKR_OK on success.
 */
CK_RV C_InitToken(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin,
                  CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel)
{
    int ret;
    WP11_Slot* slot;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Slot_Get(slotID, &slot) != 0)
        return CKR_SLOT_ID_INVALID;
    if (pPin == NULL || pLabel == NULL)
        return CKR_ARGUMENTS_BAD;

    if (ulPinLen < WP11_MIN_PIN_LEN || ulPinLen > WP11_MAX_PIN_LEN)
        return CKR_PIN_INCORRECT;

    if (WP11_Slot_IsTokenInitialized(slot)) {
        if (WP11_Slot_HasSession(slot))
            return CKR_SESSION_EXISTS;
        ret = WP11_Slot_CheckSOPin(slot, (char*)pPin, (int)ulPinLen);
        if (ret != 0)
            return CKR_PIN_INCORRECT;
    }

    ret = WP11_Slot_TokenReset(slot, (char*)pPin, (int)ulPinLen, (char*)pLabel);
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Initialize User PIN.
 *
 * @param  hSession  [in]  Session handle.
 * @param  pPin      [in]  PIN to set for User.
 * @param  ulPinLen  [in]  Length of PIN in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPin is NULL.
 *          CKR_USER_NOT_LOGGED_IN when not logged in as Security Officer.
 *          CKR_PIN_INCORRECT when length of PIN is not valid.
 *          CKR_FUNCTION_FAILED when setting User PIN fails.
 *          CKR_OK on success.
 */
CK_RV C_InitPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin,
                CK_ULONG ulPinLen)
{
    int ret;
    WP11_Slot* slot;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPin == NULL)
        return CKR_ARGUMENTS_BAD;
    if (WP11_Session_GetState(session) != WP11_APP_STATE_RW_SO)
        return CKR_USER_NOT_LOGGED_IN;

    if (ulPinLen < WP11_MIN_PIN_LEN || ulPinLen > WP11_MAX_PIN_LEN)
        return CKR_PIN_INCORRECT;

    slot = WP11_Session_GetSlot(session);
    ret = WP11_Slot_SetUserPin(slot, (char*)pPin, (int)ulPinLen);
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Change the PIN of the currently logged in user.
 *
 * @param  hSession     [in]  Session handle.
 * @param  pOldPin      [in]  Old PIN of user.
 * @param  ulOldPinLen  [in]  Length of old PIN in bytes.
 * @param  pNewPin      [in]  New PIN to set for user.
 * @param  ulNewPinLen  [in]  Length of new PIN in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pOldPin or pNewPin is NULL.
 *          CKR_PIN_INCORRECT when length of old or new PIN is not valid or
 *          old PIN does not verify.
 *          CKR_SESSION_READ_ONLY when session not read/write.
 *          CKR_USER_PIN_NOT_INITIALIZED when no previous PIN set for user.
 *          CKR_FUNCTION_FAILED when setting user PIN fails.
 *          CKR_OK on success.
 */
CK_RV C_SetPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pOldPin,
               CK_ULONG ulOldLen, CK_UTF8CHAR_PTR pNewPin,
               CK_ULONG ulNewLen)
{
    int ret;
    int state;
    WP11_Slot* slot;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pOldPin == NULL || pNewPin == NULL)
        return CKR_ARGUMENTS_BAD;
    if (ulOldLen < WP11_MIN_PIN_LEN || ulOldLen > WP11_MAX_PIN_LEN)
        return CKR_PIN_INCORRECT;
    if (ulNewLen < WP11_MIN_PIN_LEN || ulNewLen > WP11_MAX_PIN_LEN)
        return CKR_PIN_INCORRECT;

    state = WP11_Session_GetState(session);
    if (state != WP11_APP_STATE_RW_SO && state != WP11_APP_STATE_RW_USER &&
                                            state != WP11_APP_STATE_RW_PUBLIC) {
        return CKR_SESSION_READ_ONLY;
    }

    slot = WP11_Session_GetSlot(session);
    if (state == WP11_APP_STATE_RW_SO) {
        ret = WP11_Slot_CheckSOPin(slot, (char*)pOldPin, (int)ulOldLen);
        if (ret == PIN_NOT_SET_E)
            return CKR_USER_PIN_NOT_INITIALIZED;
        if (ret != 0)
            return CKR_PIN_INCORRECT;

        ret = WP11_Slot_SetSOPin(slot, (char*)pNewPin, (int)ulNewLen);
        if (ret != 0)
            return CKR_FUNCTION_FAILED;
    }
    else {
        ret = WP11_Slot_CheckUserPin(slot, (char*)pOldPin, (int)ulOldLen);
        if (ret == PIN_NOT_SET_E)
            return CKR_USER_PIN_NOT_INITIALIZED;
        if (ret != 0)
            return CKR_PIN_INCORRECT;

        ret = WP11_Slot_SetUserPin(slot, (char*)pNewPin, (int)ulNewLen);
        if (ret != 0)
            return CKR_FUNCTION_FAILED;
    }

    return CKR_OK;
}

/**
 * Open session on the token.
 *
 * @param  slotID        [in]  Id of slot to use.
 * @param  flags         [in]  Flags to indicate type of session to open.
 *                             CKF_SERIAL_SESSION must be set.
 * @param  pApplication  [in]  Application data to pass to notify callback.
 *                             Ignored.
 * @param  Notify        [in]  Notification callback.
 *                             Ignored.
 * @param  phsession     [in]  Session handle of opened session.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_SESSION_PARALLEL_NOT_SUPPORTED when CKF_SERIAL_SESSION is not
 *          set in the flags.
 *          CKR_ARGUMENTS_BAD when phSession is NULL.
 *          CKR_SESSION_READ_WRITE_SO_EXISTS when there is an existing open
 *          Security Officer session.
 *          CKR_SESSION_COUNT when no more sessions can be opened on token.
 *          CKR_OK on success.
 */
CK_RV C_OpenSession(CK_SLOT_ID slotID, CK_FLAGS flags,
                    CK_VOID_PTR pApplication, CK_NOTIFY Notify,
                    CK_SESSION_HANDLE_PTR phSession)
{
    WP11_Slot* slot;
    int ret;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Slot_Get(slotID, &slot) != 0)
        return CKR_SLOT_ID_INVALID;
    if ((flags & CKF_SERIAL_SESSION) == 0)
        return CKR_SESSION_PARALLEL_NOT_SUPPORTED;
    if (phSession == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Slot_OpenSession(slot, flags, pApplication, Notify, phSession);
    if (ret == SESSION_EXISTS_E)
        return CKR_SESSION_READ_WRITE_SO_EXISTS;
    if (ret == SESSION_COUNT_E)
        return CKR_SESSION_COUNT;

    return CKR_OK;
}

/**
 * Close the session.
 *
 * @param  hSession  [in]  Session handle.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_OK on success.
 */
CK_RV C_CloseSession(CK_SESSION_HANDLE hSession)
{
    WP11_Slot* slot;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;

    slot = WP11_Session_GetSlot(session);
    WP11_Slot_CloseSession(slot, session);

    return CKR_OK;
}

/**
 * Close all open sessions on token in slot.
 *
 * @param  slotID        [in]  Id of slot to use.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SLOT_ID_INVALID when no slot with id can be found.
 *          CKR_OK on success.
 */
CK_RV C_CloseAllSessions(CK_SLOT_ID slotID)
{
    WP11_Slot* slot;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Slot_Get(slotID, &slot) != 0)
        return CKR_SLOT_ID_INVALID;
    WP11_Slot_CloseSessions(slot);

    return CKR_OK;
}

/**
 * Get the session info.
 *
 * @param  hSession  [in]  Session handle.
 * @param  pInfo     [in]  Session information copies into it.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pInfo is NULL.
 *          CKR_OK on success.
 */
CK_RV C_GetSessionInfo(CK_SESSION_HANDLE hSession,
                       CK_SESSION_INFO_PTR pInfo)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pInfo == NULL)
        return CKR_ARGUMENTS_BAD;

    pInfo->state = WP11_Session_GetState(session);
    pInfo->flags = CKF_SERIAL_SESSION;
    if (WP11_Session_IsRW(session))
        pInfo->flags |= CKF_RW_SESSION;
    pInfo->ulDeviceError = 0;

    return CKR_OK;
}

/**
 * Get the state of the current operation.
 * Not supported.
 *
 * @param  hSession            [in]      Session handle.
 * @param  pOperationState     [in]      Buffer to hold operation state.
 *                                       NULL indicates the length is required.
 * @param  pOperationStateLen  [in,out]  On in, length of buffer in bytes.
 *                                       On out, length of serialized state in
 *                                       bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pulOperationStateLen is NULL.
 *          CKR_STATE_UNSAVEABLE indicating function not supported.
 */
CK_RV C_GetOperationState(CK_SESSION_HANDLE hSession,
                          CK_BYTE_PTR pOperationState,
                          CK_ULONG_PTR pulOperationStateLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulOperationStateLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pOperationState;

    return CKR_STATE_UNSAVEABLE;
}

/**
 * Get the state of the current operation.
 * Not supported.
 *
 * @param  hSession             [in]  Session handle.
 * @param  pOperationState      [in]  Serialized state.
 * @param  ulOperationStateLen  [in]  Length of serialized state in bytes.
 * @param  hEncryptionKey       [in]  Object handle for encryption key.
 * @param  hAuthenticationKey   [in]  Object handle for authentication key.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pOperationState is NULL.
 *          CKR_SAVED_STATE_INVALID indicating function not supported.
 */
CK_RV C_SetOperationState(CK_SESSION_HANDLE hSession,
                          CK_BYTE_PTR pOperationState,
                          CK_ULONG ulOperationStateLen,
                          CK_OBJECT_HANDLE hEncryptionKey,
                          CK_OBJECT_HANDLE hAuthenticationKey)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pOperationState == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)ulOperationStateLen;
    (void)hEncryptionKey;
    (void)hAuthenticationKey;

    return CKR_SAVED_STATE_INVALID;
}

/**
 * Log the specified user type into the session.
 *
 * @param  hSession  [in]  Session handle.
 * @param  userType  [in]  Type of user to login.
 * @param  pPin      [in]  PIN to use to login.
 * @param  ulPinLen  [in]  Length of PIN in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPin is NULL.
 *          CKR_USER_ALREADY_LOGGED_IN when already logged into session.
 *          CKR_SESSION_READ_ONLY_EXISTS when logging into read/write session
 *          and a read-only session is open.
 *          CKR_USER_PIN_NOT_INITIALIZED when PIN is not initialized for user
 *          type.
 *          CKR_PIN_INCORRECT when PIN is wrong length or does not verify.
 *          CKR_OPERATION_NOT_INITIALIZED when using user type
 *          CKU_CONTEXT_SPECIFIC - uesr type not supported.
 *          CKR_USER_TYPE_INVALID when other user type is specified.
 *          CKR_OK on success.
 */
CK_RV C_Login(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
              CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
    int ret;
    WP11_Slot* slot;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPin == NULL)
        return CKR_ARGUMENTS_BAD;

    if (ulPinLen < WP11_MIN_PIN_LEN || ulPinLen > WP11_MAX_PIN_LEN)
        return CKR_PIN_INCORRECT;

    slot = WP11_Session_GetSlot(session);
    if (userType == CKU_SO) {
        ret = WP11_Slot_SOLogin(slot, (char*)pPin, (int)ulPinLen);
        if (ret == LOGGED_IN_E)
            return CKR_USER_ALREADY_LOGGED_IN;
        if (ret == READ_ONLY_E)
            return CKR_SESSION_READ_ONLY_EXISTS;
        if (ret == PIN_NOT_SET_E)
            return CKR_USER_PIN_NOT_INITIALIZED;
        if (ret != 0)
            return CKR_PIN_INCORRECT;

    }
    else if (userType == CKU_USER) {
        ret = WP11_Slot_UserLogin(slot, (char*)pPin, (int)ulPinLen);
        if (ret == LOGGED_IN_E)
            return CKR_USER_ALREADY_LOGGED_IN;
        if (ret == PIN_NOT_SET_E)
            return CKR_USER_PIN_NOT_INITIALIZED;
        if (ret != 0)
            return CKR_PIN_INCORRECT;
    }
    else if (userType == CKU_CONTEXT_SPECIFIC)
        return CKR_OPERATION_NOT_INITIALIZED;
    else
        return CKR_USER_TYPE_INVALID;

    return CKR_OK;
}

/**
 * Log out the user from the session.
 *
 * @param  hSession  [in]  Session handle.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_OK on success.
 */
CK_RV C_Logout(CK_SESSION_HANDLE hSession)
{
    WP11_Slot* slot;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;

    slot = WP11_Session_GetSlot(session);
    WP11_Slot_Logout(slot);

    return CKR_OK;
}

/**
 * Get the status of the current cryptographic function.
 *
 * @param  hSession  [in]  Session handle.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_FUNCTION_NOT_PARALLEL indicating function not supported.
 */
CK_RV C_GetFunctionStatus(CK_SESSION_HANDLE hSession)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    return CKR_FUNCTION_NOT_PARALLEL;
}

/**
 * Cancel the current cryptographic function.
 *
 * @param  hSession  [in]  Session handle.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_FUNCTION_NOT_PARALLEL indicating function not supported.
 */
CK_RV C_CancelFunction(CK_SESSION_HANDLE hSession)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    return CKR_FUNCTION_NOT_PARALLEL;
}

/**
 * Wait for an event on any slot.
 *
 * @param  flags      [in]  Indicate whether to block.
 * @param  pSlot      [in]  Handle of slot that event occurred on.
 * @param  pReserved  [in]  Reserved for future use.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_FUNCTION_NOT_PARALLEL indicating function not supported.
 */
CK_RV C_WaitForSlotEvent(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot,
                         CK_VOID_PTR pReserved)
{
    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;

    (void)pSlot;
    (void)flags;
    (void)pReserved;

    return CKR_FUNCTION_NOT_SUPPORTED;
}

