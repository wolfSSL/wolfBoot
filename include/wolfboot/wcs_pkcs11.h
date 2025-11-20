/* wcs_pkcs11.h
 *
 * The wolfBoot library version
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef WOLFBOOT_PKCS11_H
#define WOLFBOOT_PKCS11_H

#ifdef SECURE_PKCS11
#include "wolfpkcs11/pkcs11.h"

struct C_SetPIN_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_UTF8CHAR_PTR pOldPin;
    CK_ULONG ulOldLen;
    CK_UTF8CHAR_PTR pNewPin;
    CK_ULONG ulNewLen;
};

struct C_OpenSession_nsc_args {
    CK_SLOT_ID slotID;
    CK_FLAGS flags;
    CK_VOID_PTR pApplication;
    CK_NOTIFY Notify;
    CK_SESSION_HANDLE_PTR phSession;
};
struct C_SetOperationState_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pOperationState;
    CK_ULONG ulOperationStateLen;
    CK_OBJECT_HANDLE hEncryptionKey;
    CK_OBJECT_HANDLE hAuthenticationKey;
};
struct C_CopyObject_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_OBJECT_HANDLE hObject;
    CK_ATTRIBUTE_PTR pTemplate;
    CK_ULONG ulCount;
    CK_OBJECT_HANDLE_PTR phNewObject;
};
struct C_Encrypt_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pData;
    CK_ULONG ulDataLen;
    CK_BYTE_PTR pEncryptedData;
    CK_ULONG_PTR pulEncryptedDataLen;
};
struct C_EncryptUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pPart;
    CK_ULONG ulPartLen;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG_PTR pulEncryptedPartLen;
};
struct C_Decrypt_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pEncryptedData;
    CK_ULONG ulEncryptedDataLen;
    CK_BYTE_PTR pData;
    CK_ULONG_PTR pulDataLen;
};
struct C_DecryptUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG ulEncryptedPartLen;
    CK_BYTE_PTR pPart;
    CK_ULONG_PTR pulPartLen;
};
struct C_Digest_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pData;
    CK_ULONG ulDataLen;
    CK_BYTE_PTR pDigest;
    CK_ULONG_PTR pulDigestLen;
};
struct C_Sign_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pData;
    CK_ULONG ulDataLen;
    CK_BYTE_PTR pSignature;
    CK_ULONG_PTR pulSignatureLen;
};
struct C_SignRecover_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pData;
    CK_ULONG ulDataLen;
    CK_BYTE_PTR pSignature;
    CK_ULONG_PTR pulSignatureLen;
};
struct C_Verify_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pData;
    CK_ULONG ulDataLen;
    CK_BYTE_PTR pSignature;
    CK_ULONG ulSignatureLen;
};
struct C_VerifyRecover_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pSignature;
    CK_ULONG ulSignatureLen;
    CK_BYTE_PTR pData;
    CK_ULONG_PTR pulDataLen;
};
struct C_DigestEncryptUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pPart;
    CK_ULONG ulPartLen;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG_PTR pulEncryptedPartLen;
};
struct C_DecryptDigestUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG ulEncryptedPartLen;
    CK_BYTE_PTR pPart;
    CK_ULONG_PTR pulPartLen;
};
struct C_SignEncryptUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pPart;
    CK_ULONG ulPartLen;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG_PTR pulEncryptedPartLen;
};
struct C_DecryptVerifyUpdate_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_BYTE_PTR pEncryptedPart;
    CK_ULONG ulEncryptedPartLen;
    CK_BYTE_PTR pPart;
    CK_ULONG_PTR pulPartLen;
};
struct C_GenerateKey_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_MECHANISM_PTR pMechanism;
    CK_ATTRIBUTE_PTR pTemplate;
    CK_ULONG ulCount;
    CK_OBJECT_HANDLE_PTR phKey;
};
struct C_GenerateKeyPair_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_MECHANISM_PTR pMechanism;
    CK_ATTRIBUTE_PTR pPublicKeyTemplate;
    CK_ULONG ulPublicKeyAttributeCount;
    CK_ATTRIBUTE_PTR pPrivateKeyTemplate;
    CK_ULONG ulPrivateKeyAttributeCount;
    CK_OBJECT_HANDLE_PTR phPublicKey;
    CK_OBJECT_HANDLE_PTR phPrivateKey;
};
struct C_WrapKey_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_MECHANISM_PTR pMechanism;
    CK_OBJECT_HANDLE hWrappingKey;
    CK_OBJECT_HANDLE hKey;
    CK_BYTE_PTR pWrappedKey;
    CK_ULONG_PTR pulWrappedKeyLen;
};
struct C_UnwrapKey_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_MECHANISM_PTR pMechanism;
    CK_OBJECT_HANDLE hUnwrappingKey;
    CK_BYTE_PTR pWrappedKey;
    CK_ULONG ulWrappedKeyLen;
    CK_ATTRIBUTE_PTR pTemplate;
    CK_ULONG ulAttributeCount;
    CK_OBJECT_HANDLE_PTR phKey;
};
struct C_DeriveKey_nsc_args {
    CK_SESSION_HANDLE hSession;
    CK_MECHANISM_PTR pMechanism;
    CK_OBJECT_HANDLE hBaseKey;
    CK_ATTRIBUTE_PTR pTemplate;
    CK_ULONG ulAttributeCount;
    CK_OBJECT_HANDLE_PTR phKey;
};

CK_RV CSME_NSE_API C_Initialize_nsc_call(CK_VOID_PTR pInitArgs);

CK_RV CSME_NSE_API C_Finalize_nsc_call(CK_VOID_PTR pReserved);

CK_RV CSME_NSE_API C_GetInfo_nsc_call(CK_INFO_PTR pInfo);
CK_RV CSME_NSE_API C_GetFunctionList_nsc_call(CK_FUNCTION_LIST_PTR_PTR ppFunctionList);

CK_RV CSME_NSE_API C_GetSlotList_nsc_call(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount);

CK_RV CSME_NSE_API C_GetSlotInfo_nsc_call(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo);

CK_RV CSME_NSE_API C_GetTokenInfo_nsc_call(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo);

CK_RV CSME_NSE_API C_GetMechanismList_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount);

CK_RV CSME_NSE_API C_GetMechanismInfo_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo);

CK_RV CSME_NSE_API C_InitToken_nsc_call(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel);

CK_RV CSME_NSE_API C_InitPIN_nsc_call(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen);


CK_RV CSME_NSE_API C_SetPIN_nsc_call(struct C_SetPIN_nsc_args *args);


CK_RV CSME_NSE_API C_OpenSession_nsc_call(struct C_OpenSession_nsc_args *args);
CK_RV CSME_NSE_API C_CloseSession_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV CSME_NSE_API C_CloseAllSessions_nsc_call(CK_SLOT_ID slotID);


CK_RV CSME_NSE_API C_GetSessionInfo_nsc_call(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo);
CK_RV CSME_NSE_API C_GetOperationState_nsc_call(
        CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG_PTR pulOperationStateLen);
CK_RV CSME_NSE_API C_SetOperationState_nsc_call(struct C_SetOperationState_nsc_args *args);
CK_RV CSME_NSE_API C_Login_nsc_call(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
              CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen);
CK_RV CSME_NSE_API C_Logout_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV CSME_NSE_API C_CreateObject_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phObject);
CK_RV CSME_NSE_API C_DestroyObject_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject);
CK_RV CSME_NSE_API C_GetObjectSize_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize);
CK_RV CSME_NSE_API C_GetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV CSME_NSE_API C_SetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV CSME_NSE_API C_FindObjectsInit_nsc_call(CK_SESSION_HANDLE hSession,
                        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV CSME_NSE_API C_FindObjects_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_OBJECT_HANDLE_PTR phObject,
                    CK_ULONG ulMaxObjectCount,
                    CK_ULONG_PTR pulObjectCount);

CK_RV CSME_NSE_API C_CopyObject_nsc_call(struct C_CopyObject_nsc_args *args);

CK_RV CSME_NSE_API C_FindObjectsFinal_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV CSME_NSE_API C_EncryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);
CK_RV CSME_NSE_API C_Encrypt_nsc_call(struct C_Encrypt_nsc_args *args);


CK_RV CSME_NSE_API C_EncryptUpdate_nsc_call(struct C_EncryptUpdate_nsc_args *args);

CK_RV CSME_NSE_API C_EncryptFinal_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_BYTE_PTR pLastEncryptedPart,
                     CK_ULONG_PTR pulLastEncryptedPartLen);
CK_RV CSME_NSE_API C_DecryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);
CK_RV CSME_NSE_API C_Decrypt_nsc_call(struct C_Decrypt_nsc_args *args);
CK_RV CSME_NSE_API C_DecryptUpdate_nsc_call(struct C_DecryptUpdate_nsc_args *args);


CK_RV CSME_NSE_API C_DecryptFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
                     CK_ULONG_PTR pulLastPartLen);
CK_RV CSME_NSE_API C_DigestInit_nsc_call(CK_SESSION_HANDLE hSession,
                                                                   CK_MECHANISM_PTR pMechanism);
CK_RV CSME_NSE_API C_Digest_nsc_call(struct C_Digest_nsc_args *args);
CK_RV CSME_NSE_API C_DigestUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen);
CK_RV CSME_NSE_API C_DigestKey_nsc_call(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey);


CK_RV CSME_NSE_API C_DigestFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen);


CK_RV CSME_NSE_API C_SignInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV CSME_NSE_API C_Sign_nsc_call(struct C_Sign_nsc_args *args);
CK_RV CSME_NSE_API C_SignUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen);

CK_RV CSME_NSE_API C_SignFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen);

CK_RV CSME_NSE_API C_SignRecoverInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV CSME_NSE_API C_SignRecover_nsc_call(struct C_SignRecover_nsc_args *args);
CK_RV CSME_NSE_API C_VerifyInit_nsc_call(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV CSME_NSE_API C_Verify_nsc_call(struct C_Verify_nsc_args *args);

CK_RV CSME_NSE_API C_VerifyUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                     CK_ULONG ulPartLen);
CK_RV CSME_NSE_API C_VerifyFinal_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen);


CK_RV CSME_NSE_API C_VerifyRecoverInit_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_MECHANISM_PTR pMechanism,
                          CK_OBJECT_HANDLE hKey);
CK_RV CSME_NSE_API C_VerifyRecover_nsc_call(struct C_VerifyRecover_nsc_args *args);


CK_RV CSME_NSE_API C_DigestEncryptUpdate_nsc_call(struct C_DigestEncryptUpdate_nsc_args *args);

CK_RV CSME_NSE_API C_DecryptDigestUpdate_nsc_call(struct C_DecryptDigestUpdate_nsc_args *args);

CK_RV CSME_NSE_API C_SignEncryptUpdate_nsc_call(struct C_SignEncryptUpdate_nsc_args *args);


CK_RV CSME_NSE_API C_DecryptVerifyUpdate_nsc_call(struct C_DecryptVerifyUpdate_nsc_args *args);


CK_RV CSME_NSE_API C_GenerateKey_nsc_call(struct C_GenerateKey_nsc_args *args);


CK_RV CSME_NSE_API C_GenerateKeyPair_nsc_call(struct C_GenerateKeyPair_nsc_args *args);


CK_RV CSME_NSE_API C_WrapKey_nsc_call(struct C_WrapKey_nsc_args *args);


CK_RV CSME_NSE_API C_UnwrapKey_nsc_call(struct C_UnwrapKey_nsc_args *args);


CK_RV CSME_NSE_API C_DeriveKey_nsc_call(struct C_DeriveKey_nsc_args *args);
CK_RV CSME_NSE_API C_SeedRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen);

CK_RV CSME_NSE_API C_GenerateRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen);
CK_RV CSME_NSE_API C_GetFunctionStatus_nsc_call(CK_SESSION_HANDLE hSession);


CK_RV CSME_NSE_API C_CancelFunction_nsc_call(CK_SESSION_HANDLE hSession);
CK_RV CSME_NSE_API C_WaitForSlotEvent_nsc_call(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved);

#endif /* SECURE_PKCS11 */
#endif /* !WOLFBOOT_PKCS11_H */
