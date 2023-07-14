#include "wolfpkcs11/pkcs11.h"
#include "wolfboot/wcs_pkcs11.h"

#define WOLFPKCS11NS_MAJOR_VERSION 1
#define WOLFPKCS11NS_MINOR_VERSION 0


CK_FUNCTION_LIST wolfpkcs11nsFunctionList = {
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
const char pkcs11_library_name[]="wolfCrypt_secure_mode";

extern unsigned int _start_heap;
#define NULL (((void *)0))

void * _sbrk(unsigned int incr)
{
    static unsigned char *heap = (unsigned char *)&_start_heap;
    void *old_heap = heap;
    if (((incr >> 2) << 2) != incr)
        incr = ((incr >> 2) + 1) << 2;

    if (heap == NULL)
        heap = (unsigned char *)&_start_heap;
    else
        heap += incr;
    return old_heap;
}

CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    if (ppFunctionList == NULL)
        return CKR_ARGUMENTS_BAD;

    *ppFunctionList = &wolfpkcs11nsFunctionList;

    return CKR_OK;
}

CK_RV C_Initialize(CK_VOID_PTR pInitArgs)
{
    return C_Initialize_nsc_call(pInitArgs);
}

CK_RV C_Finalize(CK_VOID_PTR pReserved)
{
    return C_Finalize_nsc_call(pReserved);
}

static const CK_INFO wolfpkcs11nsInfo = {
    { CRYPTOKI_VERSION_MAJOR, CRYPTOKI_VERSION_MINOR },
    "wolfpkcs11ns",
    0,
    "NSC-PKCS11-TrustZone-M",
    { WOLFPKCS11NS_MAJOR_VERSION, WOLFPKCS11NS_MINOR_VERSION }
};

CK_RV C_GetInfo(CK_INFO_PTR pInfo)
{
    return C_GetInfo_nsc_call(pInfo);
}

CK_RV C_GetSlotList(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList,
        CK_ULONG_PTR pulCount) {
    return C_GetSlotList_nsc_call(tokenPresent, pSlotList, pulCount);
}

CK_RV C_GetSlotInfo(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo) {
    return C_GetSlotInfo_nsc_call(slotID, pInfo);
}

CK_RV C_GetTokenInfo(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo) {
    return C_GetTokenInfo_nsc_call(slotID, pInfo);
}

CK_RV C_GetMechanismList(CK_SLOT_ID slotID,
        CK_MECHANISM_TYPE_PTR pMechanismList,
        CK_ULONG_PTR pulCount) {
    return C_GetMechanismList_nsc_call(slotID, pMechanismList, pulCount);
}

CK_RV C_GetMechanismInfo(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type,
        CK_MECHANISM_INFO_PTR pInfo) {
    return C_GetMechanismInfo_nsc_call(slotID, type, pInfo);
}

CK_RV C_InitToken(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin,
        CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel) {
    return C_InitToken_nsc_call(slotID, pPin, ulPinLen, pLabel);
}

CK_RV C_InitPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin,
        CK_ULONG ulPinLen) {
    return C_InitPIN_nsc_call(hSession, pPin, ulPinLen);
}

CK_RV C_SetPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pOldPin,
        CK_ULONG ulOldLen, CK_UTF8CHAR_PTR pNewPin,
        CK_ULONG ulNewLen) {
    struct C_SetPIN_nsc_args args = { hSession, pOldPin, ulOldLen, pNewPin, ulNewLen };
    return C_SetPIN_nsc_call(&args);
}


CK_RV C_OpenSession(CK_SLOT_ID slotID, CK_FLAGS flags,
        CK_VOID_PTR pApplication, CK_NOTIFY Notify,
        CK_SESSION_HANDLE_PTR phSession) {
    struct C_OpenSession_nsc_args args = {
        .slotID = slotID,
        .flags = flags,
        .pApplication = pApplication,
        .Notify = Notify,
        .phSession = phSession
    };
    return C_OpenSession_nsc_call(&args);
}

CK_RV C_CloseSession(CK_SESSION_HANDLE hSession) {
    return C_CloseSession_nsc_call(hSession);
}

CK_RV C_CloseAllSessions(CK_SLOT_ID slotID) {
    return C_CloseAllSessions_nsc_call(slotID);
}


CK_RV C_GetSessionInfo(CK_SESSION_HANDLE hSession,
        CK_SESSION_INFO_PTR pInfo) {
    return C_GetSessionInfo_nsc_call(hSession, pInfo);
}

CK_RV C_GetOperationState(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG_PTR pulOperationStateLen) {
    return C_GetOperationState_nsc_call(hSession, pOperationState, pulOperationStateLen);
}

CK_RV C_SetOperationState(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG ulOperationStateLen,
        CK_OBJECT_HANDLE hEncryptionKey,
        CK_OBJECT_HANDLE hAuthenticationKey) {
    struct C_SetOperationState_nsc_args args = {
        .hSession = hSession,
        .pOperationState = pOperationState,
        .ulOperationStateLen = ulOperationStateLen,
        .hEncryptionKey = hEncryptionKey,
        .hAuthenticationKey = hAuthenticationKey
    };
    return C_SetOperationState_nsc_call(&args);
}

CK_RV C_Login(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
        CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen) {
    return C_Login_nsc_call(hSession, userType, pPin, ulPinLen);
}

CK_RV C_Logout(CK_SESSION_HANDLE hSession) {
    return C_Logout_nsc_call(hSession);
}

CK_RV C_CreateObject(CK_SESSION_HANDLE hSession,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
        CK_OBJECT_HANDLE_PTR phObject) {
    return C_CreateObject_nsc_call(hSession, pTemplate, ulCount, phObject);
}

CK_RV C_CopyObject(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
        CK_OBJECT_HANDLE_PTR phNewObject) {
    struct C_CopyObject_nsc_args args = {
        .hSession = hSession,
        .hObject = hObject,
        .pTemplate = pTemplate,
        .ulCount = ulCount,
        .phNewObject = phNewObject
    };
    return C_CopyObject_nsc_call(&args);
}

CK_RV C_DestroyObject(CK_SESSION_HANDLE hSession,
        CK_OBJECT_HANDLE hObject) {
    return C_DestroyObject_nsc_call(hSession, hObject);
}

CK_RV C_GetObjectSize(CK_SESSION_HANDLE hSession,
        CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize) {
    return C_GetObjectSize_nsc_call(hSession, hObject, pulSize);
}

CK_RV C_GetAttributeValue(CK_SESSION_HANDLE hSession,
        CK_OBJECT_HANDLE hObject,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_GetAttributeValue_nsc_call(hSession, hObject, pTemplate, ulCount);
}


CK_RV C_SetAttributeValue(CK_SESSION_HANDLE hSession,
        CK_OBJECT_HANDLE hObject,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_SetAttributeValue_nsc_call(hSession, hObject, pTemplate, ulCount);
}

CK_RV C_FindObjectsInit(CK_SESSION_HANDLE hSession,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_FindObjectsInit_nsc_call(hSession, pTemplate, ulCount);
}

CK_RV C_FindObjects(CK_SESSION_HANDLE hSession,
        CK_OBJECT_HANDLE_PTR phObject,
        CK_ULONG ulMaxObjectCount,
        CK_ULONG_PTR pulObjectCount) {
    return C_FindObjects_nsc_call(hSession, phObject, ulMaxObjectCount, pulObjectCount);
}

CK_RV C_FindObjectsFinal(CK_SESSION_HANDLE hSession) {
    return C_FindObjectsFinal_nsc_call(hSession);
}

CK_RV C_Encrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
        CK_ULONG ulDataLen, CK_BYTE_PTR pEncryptedData,
        CK_ULONG_PTR pulEncryptedDataLen) {
    struct C_Encrypt_nsc_args args = {
        .hSession = hSession,
        .pData = pData,
        .ulDataLen = ulDataLen,
        .pEncryptedData = pEncryptedData,
        .pulEncryptedDataLen = pulEncryptedDataLen
    };
    return C_Encrypt_nsc_call(&args);
}

CK_RV C_EncryptInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey) {
    return C_EncryptInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_EncryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
        CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart,
        CK_ULONG_PTR pulEncryptedPartLen) {
    struct C_EncryptUpdate_nsc_args args = {
        .hSession = hSession,
        .pPart = pPart,
        .ulPartLen = ulPartLen,
        .pEncryptedPart = pEncryptedPart,
        .pulEncryptedPartLen = pulEncryptedPartLen
    };
    return C_EncryptUpdate_nsc_call(&args);
}

CK_RV C_EncryptFinal(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pLastEncryptedPart,
        CK_ULONG_PTR pulLastEncryptedPartLen) {
    return C_EncryptFinal_nsc_call(hSession, pLastEncryptedPart, pulLastEncryptedPartLen);
}

CK_RV C_DecryptInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey) {
    return C_DecryptInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_Decrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedData,
        CK_ULONG ulEncryptedDataLen, CK_BYTE_PTR pData,
        CK_ULONG_PTR pulDataLen) {
    struct C_Decrypt_nsc_args args = {
        .hSession = hSession,
        .pEncryptedData = pEncryptedData,
        .ulEncryptedDataLen = ulEncryptedDataLen,
        .pData = pData,
        .pulDataLen = pulDataLen
    };
    return C_Decrypt_nsc_call(&args);
}

CK_RV C_DecryptUpdate(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pEncryptedPart,
        CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart,
        CK_ULONG_PTR pulPartLen) {
    struct C_DecryptUpdate_nsc_args args = {
        .hSession = hSession,
        .pEncryptedPart = pEncryptedPart,
        .ulEncryptedPartLen = ulEncryptedPartLen,
        .pPart = pPart,
        .pulPartLen = pulPartLen
    };
    return C_DecryptUpdate_nsc_call(&args);
}

CK_RV C_DecryptFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
        CK_ULONG_PTR pulLastPartLen) {
    return C_DecryptFinal_nsc_call(hSession, pLastPart, pulLastPartLen);
}

CK_RV C_DigestInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism) {
    return C_DigestInit_nsc_call(hSession, pMechanism);
}

CK_RV C_Digest(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
        CK_ULONG ulDataLen, CK_BYTE_PTR pDigest,
        CK_ULONG_PTR pulDigestLen) {
    struct C_Digest_nsc_args args = {
        .hSession = hSession,
        .pData = pData,
        .ulDataLen = ulDataLen,
        .pDigest = pDigest,
        .pulDigestLen = pulDigestLen
    };
    return C_Digest_nsc_call(&args);
}

CK_RV C_DigestUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
        CK_ULONG ulPartLen) {
    return C_DigestUpdate_nsc_call(hSession, pPart, ulPartLen);
}

CK_RV C_DigestKey(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey) {
    return C_DigestKey_nsc_call(hSession, hKey);
}

CK_RV C_DigestFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest,
        CK_ULONG_PTR pulDigestLen) {
    return C_DigestFinal_nsc_call(hSession, pDigest, pulDigestLen);
}

CK_RV C_SignInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hKey) {
    return C_SignInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_Sign(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
        CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
        CK_ULONG_PTR pulSignatureLen) {
    struct C_Sign_nsc_args args = {
        .hSession = hSession,
        .pData = pData,
        .ulDataLen = ulDataLen,
        .pSignature = pSignature,
        .pulSignatureLen = pulSignatureLen
    };
    return C_Sign_nsc_call(&args);
}

CK_RV C_SignUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
        CK_ULONG ulPartLen) {
    return C_SignUpdate_nsc_call(hSession, pPart, ulPartLen);
}

CK_RV C_SignFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature,
        CK_ULONG_PTR pulSignatureLen) {
    return C_SignFinal_nsc_call(hSession, pSignature, pulSignatureLen);
}

CK_RV C_SignRecoverInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hKey) {
    return C_SignRecoverInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_SignRecover(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
        CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
        CK_ULONG_PTR pulSignatureLen) {
    struct C_SignRecover_nsc_args args = {
        .hSession = hSession,
        .pData = pData,
        .ulDataLen = ulDataLen,
        .pSignature = pSignature,
        .pulSignatureLen = pulSignatureLen
    };
    return C_SignRecover_nsc_call(&args);
}   


CK_RV C_VerifyInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey) {
    return C_VerifyInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_Verify(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
        CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
        CK_ULONG ulSignatureLen) {
    struct C_Verify_nsc_args args = {
        .hSession = hSession,
        .pData = pData,
        .ulDataLen = ulDataLen,
        .pSignature = pSignature,
        .ulSignatureLen = ulSignatureLen
    };
    return C_Verify_nsc_call(&args);
}

CK_RV C_VerifyUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
        CK_ULONG ulPartLen) {
    return C_VerifyUpdate_nsc_call(hSession, pPart, ulPartLen);
}

CK_RV C_VerifyFinal(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen) {
    return C_VerifyFinal_nsc_call(hSession, pSignature, ulSignatureLen);
}

CK_RV C_VerifyRecoverInit(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hKey) {
    return C_VerifyRecoverInit_nsc_call(hSession, pMechanism, hKey);
}

CK_RV C_VerifyRecover(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen,
        CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen) {
    struct C_VerifyRecover_nsc_args args = {
        .hSession = hSession,
        .pSignature = pSignature,
        .ulSignatureLen = ulSignatureLen,
        .pData = pData,
        .pulDataLen = pulDataLen
    };
    return C_VerifyRecover_nsc_call(&args);
}

CK_RV C_DigestEncryptUpdate(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pPart, CK_ULONG ulPartLen,
        CK_BYTE_PTR pEncryptedPart,
        CK_ULONG_PTR pulEncryptedPartLen) {
    struct C_DigestEncryptUpdate_nsc_args args = {
        .hSession = hSession,
        .pPart = pPart,
        .ulPartLen = ulPartLen,
        .pEncryptedPart = pEncryptedPart,
        .pulEncryptedPartLen = pulEncryptedPartLen
    };
    return C_DigestEncryptUpdate_nsc_call(&args);
}

CK_RV C_DecryptDigestUpdate(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pEncryptedPart,
        CK_ULONG ulEncryptedPartLen,
        CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen) {
    struct C_DecryptDigestUpdate_nsc_args args = {
        .hSession = hSession,
        .pEncryptedPart = pEncryptedPart,
        .ulEncryptedPartLen = ulEncryptedPartLen,
        .pPart = pPart,
        .pulPartLen = pulPartLen
    };
    return C_DecryptDigestUpdate_nsc_call(&args);
}

CK_RV C_SignEncryptUpdate(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pPart, CK_ULONG ulPartLen,
        CK_BYTE_PTR pEncryptedPart,
        CK_ULONG_PTR pulEncryptedPartLen) {
    struct C_SignEncryptUpdate_nsc_args args = {
        .hSession = hSession,
        .pPart = pPart,
        .ulPartLen = ulPartLen,
        .pEncryptedPart = pEncryptedPart,
        .pulEncryptedPartLen = pulEncryptedPartLen
    };
    return C_SignEncryptUpdate_nsc_call(&args);
}

CK_RV C_DecryptVerifyUpdate(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pEncryptedPart,
        CK_ULONG ulEncryptedPartLen,
        CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen) {
    struct C_DecryptVerifyUpdate_nsc_args args = {
        .hSession = hSession,
        .pEncryptedPart = pEncryptedPart,
        .ulEncryptedPartLen = ulEncryptedPartLen,
        .pPart = pPart,
        .pulPartLen = pulPartLen
    };
    return C_DecryptVerifyUpdate_nsc_call(&args);
}

CK_RV C_GenerateKey(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
        CK_OBJECT_HANDLE_PTR phKey) {
    struct C_GenerateKey_nsc_args args = {
        .hSession = hSession,
        .pMechanism = pMechanism,
        .pTemplate = pTemplate,
        .ulCount = ulCount,
        .phKey = phKey
    };
    return C_GenerateKey_nsc_call(&args);
}

CK_RV C_GenerateKeyPair(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_ATTRIBUTE_PTR pPublicKeyTemplate,
        CK_ULONG ulPublicKeyAttributeCount,
        CK_ATTRIBUTE_PTR pPrivateKeyTemplate,
        CK_ULONG ulPrivateKeyAttributeCount,
        CK_OBJECT_HANDLE_PTR phPublicKey,
        CK_OBJECT_HANDLE_PTR phPrivateKey) {
    struct C_GenerateKeyPair_nsc_args args = {
        .hSession = hSession,
        .pMechanism = pMechanism,
        .pPublicKeyTemplate = pPublicKeyTemplate,
        .ulPublicKeyAttributeCount = ulPublicKeyAttributeCount,
        .pPrivateKeyTemplate = pPrivateKeyTemplate,
        .ulPrivateKeyAttributeCount = ulPrivateKeyAttributeCount,
        .phPublicKey = phPublicKey,
        .phPrivateKey = phPrivateKey
    };
    return C_GenerateKeyPair_nsc_call(&args);
}

CK_RV C_WrapKey(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hWrappingKey, CK_OBJECT_HANDLE hKey,
        CK_BYTE_PTR pWrappedKey,
        CK_ULONG_PTR pulWrappedKeyLen) {
    struct C_WrapKey_nsc_args args = {
        .hSession = hSession,
        .pMechanism = pMechanism,
        .hWrappingKey = hWrappingKey,
        .hKey = hKey,
        .pWrappedKey = pWrappedKey,
        .pulWrappedKeyLen = pulWrappedKeyLen
    };
    return C_WrapKey_nsc_call(&args);
}

CK_RV C_UnwrapKey(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hUnwrappingKey,
        CK_BYTE_PTR pWrappedKey, CK_ULONG ulWrappedKeyLen,
        CK_ATTRIBUTE_PTR pTemplate,
        CK_ULONG ulAttributeCount,
        CK_OBJECT_HANDLE_PTR phKey) {
    struct C_UnwrapKey_nsc_args args = {
        .hSession = hSession,
        .pMechanism = pMechanism,
        .hUnwrappingKey = hUnwrappingKey,
        .pWrappedKey = pWrappedKey,
        .ulWrappedKeyLen = ulWrappedKeyLen,
        .pTemplate = pTemplate,
        .ulAttributeCount = ulAttributeCount,
        .phKey = phKey
    };
    return C_UnwrapKey_nsc_call(&args);
}

CK_RV C_DeriveKey(CK_SESSION_HANDLE hSession,
        CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hBaseKey,
        CK_ATTRIBUTE_PTR pTemplate,
        CK_ULONG ulAttributeCount,
        CK_OBJECT_HANDLE_PTR phKey) {
    struct C_DeriveKey_nsc_args args = {
        .hSession = hSession,
        .pMechanism = pMechanism,
        .hBaseKey = hBaseKey,
        .pTemplate = pTemplate,
        .ulAttributeCount = ulAttributeCount,
        .phKey = phKey
    };
    return C_DeriveKey_nsc_call(&args);
}

CK_RV C_SeedRandom(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed,
        CK_ULONG ulSeedLen) {
    return C_SeedRandom_nsc_call(hSession, pSeed, ulSeedLen);
}

CK_RV C_GenerateRandom(CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen) {
    return C_GenerateRandom_nsc_call(hSession, pRandomData, ulRandomLen);
}

CK_RV C_GetFunctionStatus(CK_SESSION_HANDLE hSession) {
    return C_GetFunctionStatus_nsc_call(hSession);
}

CK_RV C_CancelFunction(CK_SESSION_HANDLE hSession) {
    return C_CancelFunction_nsc_call(hSession);
}

CK_RV C_WaitForSlotEvent(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot,
        CK_VOID_PTR pReserved) {
    return C_WaitForSlotEvent_nsc_call(flags, pSlot, pReserved);
}

