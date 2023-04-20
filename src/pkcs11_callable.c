#include "wolfpkcs11/pkcs11.h"
#include "wolfboot/wcs_pkcs11.h"

CK_RV __attribute__((cmse_nonsecure_entry)) C_Initialize_nsc_call(CK_VOID_PTR pInitArgs)
{
    return C_Initialize(pInitArgs); 
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Finalize_nsc_call(CK_VOID_PTR pReserved)
{
    return C_Finalize(pReserved);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetInfo_nsc_call(CK_INFO_PTR pInfo)
{
    return C_GetInfo(pInfo);
}

WP11_API CK_RV __attribute__((cmse_nonsecure_entry)) C_GetFunctionList_nsc_call(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    return C_GetFunctionList(ppFunctionList);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSlotList_nsc_call(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount)
{
    return C_GetSlotList(tokenPresent, pSlotList, pulCount);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSlotInfo_nsc_call(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
    return C_GetSlotInfo(slotID, pInfo);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetTokenInfo_nsc_call(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
    return C_GetTokenInfo(slotID, pInfo);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetMechanismList_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount)
{
    return C_GetMechanismList(slotID, pMechanismList, pulCount);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetMechanismInfo_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo)
{
    return C_GetMechanismInfo(slotID, type, pInfo);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_InitToken_nsc_call(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel)
{
    return C_InitToken(slotID, pPin, ulPinLen, pLabel);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_InitPIN_nsc_call(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
    return C_InitPIN(hSession, pPin, ulPinLen);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_SetPIN_nsc_call(struct C_SetPIN_nsc_args *args)
    
{
    return C_SetPIN(args->hSession, args->pOldPin, args->ulOldLen, args->pNewPin,
            args->ulNewLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_OpenSession_nsc_call(struct C_OpenSession_nsc_args *args) {
    
    return C_OpenSession(args->slotID, args->flags, args->pApplication, args->Notify, args->phSession);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_CloseSession_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_CloseSession(hSession);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_CloseAllSessions_nsc_call(CK_SLOT_ID slotID)
{
    return C_CloseAllSessions(slotID);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetSessionInfo_nsc_call(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo)
{
    return C_GetSessionInfo(hSession, pInfo);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetOperationState_nsc_call(
        CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG_PTR pulOperationStateLen) {
    return C_GetOperationState(hSession, pOperationState, pulOperationStateLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SetOperationState_nsc_call(struct C_SetOperationState_nsc_args *args) {
    
    return C_SetOperationState(args->hSession, args->pOperationState, args->ulOperationStateLen, args->hEncryptionKey, args->hAuthenticationKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Login_nsc_call(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
              CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen) {
    return C_Login(hSession, userType, pPin, ulPinLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Logout_nsc_call(CK_SESSION_HANDLE hSession) {
    return C_Logout(hSession);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_CreateObject_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phObject) {
    return C_CreateObject(hSession, pTemplate, ulCount, phObject);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DestroyObject_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject) {
    return C_DestroyObject(hSession, hObject);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetObjectSize_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize) {
    return C_GetObjectSize(hSession, hObject, pulSize);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_GetAttributeValue(hSession, hObject, pTemplate, ulCount);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_SetAttributeValue(hSession, hObject, pTemplate, ulCount);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjectsInit_nsc_call(CK_SESSION_HANDLE hSession,
                        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    return C_FindObjectsInit(hSession, pTemplate, ulCount);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjects_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_OBJECT_HANDLE_PTR phObject,
                    CK_ULONG ulMaxObjectCount,
                    CK_ULONG_PTR pulObjectCount) {
    return C_FindObjects(hSession, phObject, ulMaxObjectCount, pulObjectCount);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_CopyObject_nsc_call(struct C_CopyObject_nsc_args *args) {
    
    return C_CopyObject(args->hSession, args->hObject, args->pTemplate, args->ulCount, args->phNewObject);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_FindObjectsFinal_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_FindObjectsFinal(hSession);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    return C_EncryptInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Encrypt_nsc_call(struct C_Encrypt_nsc_args *args) {
    
    return C_Encrypt(args->hSession, args->pData, args->ulDataLen, args->pEncryptedData, args->pulEncryptedDataLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptUpdate_nsc_call(struct C_EncryptUpdate_nsc_args *args) {
    
    return C_EncryptUpdate(args->hSession, args->pPart, args->ulPartLen, args->pEncryptedPart, args->pulEncryptedPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_EncryptFinal_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_BYTE_PTR pLastEncryptedPart,
                     CK_ULONG_PTR pulLastEncryptedPartLen)
{
    return C_EncryptFinal(hSession, pLastEncryptedPart, pulLastEncryptedPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    return C_DecryptInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Decrypt_nsc_call(struct C_Decrypt_nsc_args *args) {
    
    return C_Decrypt(args->hSession, args->pEncryptedData, args->ulEncryptedDataLen, args->pData, args->pulDataLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptUpdate_nsc_call(struct C_DecryptUpdate_nsc_args *args) {
    
    return C_DecryptUpdate(args->hSession, args->pEncryptedPart, args->ulEncryptedPartLen, args->pPart, args->pulPartLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
                     CK_ULONG_PTR pulLastPartLen)
{
    return C_DecryptFinal(hSession, pLastPart, pulLastPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestInit_nsc_call(CK_SESSION_HANDLE hSession,
                                                                   CK_MECHANISM_PTR pMechanism)
{
    return C_DigestInit(hSession, pMechanism);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Digest_nsc_call(struct C_Digest_nsc_args *args) {
    
    return C_Digest(args->hSession, args->pData, args->ulDataLen, args->pDigest, args->pulDigestLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
    return C_DigestUpdate(hSession, pPart, ulPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestKey_nsc_call(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey)
{
    return C_DigestKey(hSession, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
    return C_DigestFinal(hSession, pDigest, pulDigestLen);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_SignInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    return C_SignInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Sign_nsc_call(struct C_Sign_nsc_args *args) {
    
    return C_Sign(args->hSession, args->pData, args->ulDataLen, args->pSignature, args->pulSignatureLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
    return C_SignUpdate(hSession, pPart, ulPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
    return C_SignFinal(hSession, pSignature, pulSignatureLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignRecoverInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    return C_SignRecoverInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SignRecover_nsc_call(struct C_SignRecover_nsc_args *args) {
    
    return C_SignRecover(args->hSession, args->pData, args->ulDataLen, args->pSignature, args->pulSignatureLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyInit_nsc_call(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    return C_VerifyInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_Verify_nsc_call(struct C_Verify_nsc_args *args) {
    
    return C_Verify(args->hSession, args->pData, args->ulDataLen, args->pSignature, args->ulSignatureLen);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                     CK_ULONG ulPartLen)
{
    return C_VerifyUpdate(hSession, pPart, ulPartLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyFinal_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
    return C_VerifyFinal(hSession, pSignature, ulSignatureLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyRecoverInit_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_MECHANISM_PTR pMechanism,
                          CK_OBJECT_HANDLE hKey)
{
    return C_VerifyRecoverInit(hSession, pMechanism, hKey);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_VerifyRecover_nsc_call(struct C_VerifyRecover_nsc_args *args) {
    
    return C_VerifyRecover(args->hSession, args->pSignature, args->ulSignatureLen, args->pData, args->pulDataLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_DigestEncryptUpdate_nsc_call(struct C_DigestEncryptUpdate_nsc_args *args) {
    
    return C_DigestEncryptUpdate(args->hSession, args->pPart, args->ulPartLen, args->pEncryptedPart, args->pulEncryptedPartLen);
}


CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptDigestUpdate_nsc_call(struct C_DecryptDigestUpdate_nsc_args *args) {
    
    return C_DecryptDigestUpdate(args->hSession, args->pEncryptedPart, args->ulEncryptedPartLen, args->pPart, args->pulPartLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_SignEncryptUpdate_nsc_call(struct C_SignEncryptUpdate_nsc_args *args) {
    
    return C_SignEncryptUpdate(args->hSession, args->pPart, args->ulPartLen, args->pEncryptedPart, args->pulEncryptedPartLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_DecryptVerifyUpdate_nsc_call(struct C_DecryptVerifyUpdate_nsc_args *args) {
    
    return C_DecryptVerifyUpdate(args->hSession, args->pEncryptedPart, args->ulEncryptedPartLen, args->pPart, args->pulPartLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateKey_nsc_call(struct C_GenerateKey_nsc_args *args) {
    
    return C_GenerateKey(args->hSession, args->pMechanism, args->pTemplate, args->ulCount, args->phKey);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateKeyPair_nsc_call(struct C_GenerateKeyPair_nsc_args *args) {
    
    return C_GenerateKeyPair(args->hSession, args->pMechanism, args->pPublicKeyTemplate, args->ulPublicKeyAttributeCount, args->pPrivateKeyTemplate, args->ulPrivateKeyAttributeCount, args->phPublicKey, args->phPrivateKey);

}



CK_RV __attribute__((cmse_nonsecure_entry)) C_WrapKey_nsc_call(struct C_WrapKey_nsc_args *args) {
    
    return C_WrapKey(args->hSession, args->pMechanism, args->hWrappingKey, args->hKey, args->pWrappedKey, args->pulWrappedKeyLen);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_UnwrapKey_nsc_call(struct C_UnwrapKey_nsc_args *args) {
    
    return C_UnwrapKey(args->hSession, args->pMechanism, args->hUnwrappingKey, args->pWrappedKey, args->ulWrappedKeyLen, args->pTemplate, args->ulAttributeCount, args->phKey);
}



CK_RV __attribute__((cmse_nonsecure_entry)) C_DeriveKey_nsc_call(struct C_DeriveKey_nsc_args *args) {
    
    return C_DeriveKey(args->hSession, args->pMechanism, args->hBaseKey, args->pTemplate, args->ulAttributeCount, args->phKey);

}

CK_RV __attribute__((cmse_nonsecure_entry)) C_SeedRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen)
{
    return C_SeedRandom(hSession, pSeed, ulSeedLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GenerateRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen)
{
    return C_GenerateRandom(hSession, pRandomData, ulRandomLen);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_GetFunctionStatus_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_GetFunctionStatus(hSession);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_CancelFunction_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_CancelFunction(hSession);
}

CK_RV __attribute__((cmse_nonsecure_entry)) C_WaitForSlotEvent_nsc_call(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved)
{
    return C_WaitForSlotEvent(flags, pSlot, pReserved);
}


 
