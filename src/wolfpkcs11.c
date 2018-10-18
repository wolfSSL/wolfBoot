/* wolfpkcs11.c
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

/* Function list table. */
static CK_FUNCTION_LIST wolfpkcs11FunctionList = {
    { CRYPTOKI_VERSION_MAJOR, CRYPTOKI_VERSION_MINOR },

    C_Initialize,
    C_Finalize,
    C_GetInfo,
    C_GetFunctionList,
    C_GetSlotList,
    C_GetSlotInfo,
    C_GetTokenInfo,
    C_GetMechanismList,
    C_GetMechanismInfo,
    C_InitToken,
    C_InitPIN,
    C_SetPIN,
    C_OpenSession,
    C_CloseSession,
    C_CloseAllSessions,
    C_GetSessionInfo,
    C_GetOperationState,
    C_SetOperationState,
    C_Login,
    C_Logout,
    C_CreateObject,
    C_CopyObject,
    C_DestroyObject,
    C_GetObjectSize,
    C_GetAttributeValue,
    C_SetAttributeValue,
    C_FindObjectsInit,
    C_FindObjects,
    C_FindObjectsFinal,
    C_EncryptInit,
    C_Encrypt,
    C_EncryptUpdate,
    C_EncryptFinal,
    C_DecryptInit,
    C_Decrypt,
    C_DecryptUpdate,
    C_DecryptFinal,
    C_DigestInit,
    C_Digest,
    C_DigestUpdate,
    C_DigestKey,
    C_DigestFinal,
    C_SignInit,
    C_Sign,
    C_SignUpdate,
    C_SignFinal,
    C_SignRecoverInit,
    C_SignRecover,
    C_VerifyInit,
    C_Verify,
    C_VerifyUpdate,
    C_VerifyFinal,
    C_VerifyRecoverInit,
    C_VerifyRecover,
    C_DigestEncryptUpdate,
    C_DecryptDigestUpdate,
    C_SignEncryptUpdate,
    C_DecryptVerifyUpdate,
    C_GenerateKey,
    C_GenerateKeyPair,
    C_WrapKey,
    C_UnwrapKey,
    C_DeriveKey,
    C_SeedRandom,
    C_GenerateRandom,
    C_GetFunctionStatus,
    C_CancelFunction,
    C_WaitForSlotEvent
};

/**
 * Return the function list for accessing Crypto-Ki API.
 *
 * @param  ppFunctionList  [out]  Pointer to hold reference to function list.
 * @return  CKR_ARGUMENTS_BAD when ppFunctionList is NULL.
 *          CKR_OK on success.
 */
CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    if (ppFunctionList == NULL)
        return CKR_ARGUMENTS_BAD;

    *ppFunctionList = &wolfpkcs11FunctionList;

    return CKR_OK;
}

/**
 * Initialize the Crypto-Ki library.
 *
 * @param  pInitArgs  [out]  Ignored.
 * @return  CKR_FUNCTION_FAILED when initializing fails.
 *          CKR_OK on success.
 */
CK_RV C_Initialize(CK_VOID_PTR pInitArgs)
{
    if (WP11_Library_Init() != 0)
        return CKR_FUNCTION_FAILED;

    (void)pInitArgs;

    return CKR_OK;
}

/**
 * Finalize the Crypto-Ki library.
 *
 * @param  pReserved  [out]  Ignored.
 * @return  CKR_OK on success.
 */
CK_RV C_Finalize(CK_VOID_PTR pReserved)
{
    WP11_Library_Final();

    (void)pReserved;

    return CKR_OK;
}

/* Information about the Crypto-Ki library. */
static CK_INFO wolfpkcs11Info = {
    { CRYPTOKI_VERSION_MAJOR, CRYPTOKI_VERSION_MINOR },
    "wolfpkcs11",
    0,
    "Implementation using wolfCrypt",
    { WOLFPKCS11_MAJOR_VERSION, WOLFPKCS11_MINOR_VERSION }
};

/**
 * Get information on the library.
 *
 * @param  pInfo  [in]  Library information copied into it.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_ARGUMENTS_BAD when pInfo is NULL.
 *          CKR_OK on success.
 */
CK_RV C_GetInfo(CK_INFO_PTR pInfo)
{
    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (pInfo == NULL)
        return CKR_ARGUMENTS_BAD;

    XMEMCPY(pInfo, &wolfpkcs11Info, sizeof(wolfpkcs11Info));

    return CKR_OK;
}

