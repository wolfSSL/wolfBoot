#include "wolfpkcs11/pkcs11.h"

#ifndef WOLFBOOT_PKCS11_H
#define WOLFBOOT_PKCS11_H

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

CK_RV __attribute__((cmse_nonsecure_entry)) C_Initialize_nsc_call(CK_VOID_PTR pInitArgs);

CK_RV __attribute__((cmse_nonsecure_entry)) C_Finalize_nsc_call(CK_VOID_PTR pReserved);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetInfo_nsc_call(CK_INFO_PTR pInfo);
CK_RV __attribute__((cmse_nonsecure_entry)) C_GetFunctionList_nsc_call(CK_FUNCTION_LIST_PTR_PTR ppFunctionList);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSlotList_nsc_call(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSlotInfo_nsc_call(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetTokenInfo_nsc_call(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetMechanismList_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetMechanismInfo_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo);

CK_RV __attribute__((cmse_nonsecure_entry)) C_InitToken_nsc_call(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel);

CK_RV __attribute__((cmse_nonsecure_entry)) C_InitPIN_nsc_call(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen);


CK_RV __attribute__((cmse_nonsecure_entry)) C_SetPIN_nsc_call(struct C_SetPIN_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_OpenSession_nsc_call(struct C_OpenSession_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_CloseSession_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV __attribute__((cmse_nonsecure_entry)) C_CloseAllSessions_nsc_call(CK_SLOT_ID slotID);


CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSessionInfo_nsc_call(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo);
CK_RV __attribute__((cmse_nonsecure_entry)) C_GetOperationState_nsc_call(
        CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG_PTR pulOperationStateLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_SetOperationState_nsc_call(struct C_SetOperationState_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_Login_nsc_call(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
              CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_Logout_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV __attribute__((cmse_nonsecure_entry)) C_CreateObject_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phObject);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DestroyObject_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject);
CK_RV __attribute__((cmse_nonsecure_entry)) C_GetObjectSize_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize);
CK_RV __attribute__((cmse_nonsecure_entry)) C_GetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV __attribute__((cmse_nonsecure_entry)) C_SetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjectsInit_nsc_call(CK_SESSION_HANDLE hSession,
                        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjects_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_OBJECT_HANDLE_PTR phObject,
                    CK_ULONG ulMaxObjectCount,
                    CK_ULONG_PTR pulObjectCount);

CK_RV __attribute__((cmse_nonsecure_entry)) C_CopyObject_nsc_call(struct C_CopyObject_nsc_args *args);

CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjectsFinal_nsc_call(CK_SESSION_HANDLE hSession);

CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);
CK_RV __attribute__((cmse_nonsecure_entry)) C_Encrypt_nsc_call(struct C_Encrypt_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptUpdate_nsc_call(struct C_EncryptUpdate_nsc_args *args);

CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptFinal_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_BYTE_PTR pLastEncryptedPart,
                     CK_ULONG_PTR pulLastEncryptedPartLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);
CK_RV __attribute__((cmse_nonsecure_entry)) C_Decrypt_nsc_call(struct C_Decrypt_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptUpdate_nsc_call(struct C_DecryptUpdate_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
                     CK_ULONG_PTR pulLastPartLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestInit_nsc_call(CK_SESSION_HANDLE hSession,
                                                                   CK_MECHANISM_PTR pMechanism);
CK_RV __attribute__((cmse_nonsecure_entry)) C_Digest_nsc_call(struct C_Digest_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestKey_nsc_call(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey);


CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen);


CK_RV __attribute__((cmse_nonsecure_entry)) C_SignInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV __attribute__((cmse_nonsecure_entry)) C_Sign_nsc_call(struct C_Sign_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_SignUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen);

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen);

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignRecoverInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignRecover_nsc_call(struct C_SignRecover_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyInit_nsc_call(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey);

CK_RV __attribute__((cmse_nonsecure_entry)) C_Verify_nsc_call(struct C_Verify_nsc_args *args);

CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                     CK_ULONG ulPartLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyFinal_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen);


CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyRecoverInit_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_MECHANISM_PTR pMechanism,
                          CK_OBJECT_HANDLE hKey);
CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyRecover_nsc_call(struct C_VerifyRecover_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestEncryptUpdate_nsc_call(struct C_DigestEncryptUpdate_nsc_args *args);

CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptDigestUpdate_nsc_call(struct C_DecryptDigestUpdate_nsc_args *args);

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignEncryptUpdate_nsc_call(struct C_SignEncryptUpdate_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptVerifyUpdate_nsc_call(struct C_DecryptVerifyUpdate_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateKey_nsc_call(struct C_GenerateKey_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateKeyPair_nsc_call(struct C_GenerateKeyPair_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_WrapKey_nsc_call(struct C_WrapKey_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_UnwrapKey_nsc_call(struct C_UnwrapKey_nsc_args *args);


CK_RV __attribute__((cmse_nonsecure_entry)) C_DeriveKey_nsc_call(struct C_DeriveKey_nsc_args *args);
CK_RV __attribute__((cmse_nonsecure_entry)) C_SeedRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen);

CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen);
CK_RV __attribute__((cmse_nonsecure_entry)) C_GetFunctionStatus_nsc_call(CK_SESSION_HANDLE hSession);


CK_RV __attribute__((cmse_nonsecure_entry)) C_CancelFunction_nsc_call(CK_SESSION_HANDLE hSession);
CK_RV __attribute__((cmse_nonsecure_entry)) C_WaitForSlotEvent_nsc_call(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved);

#endif
