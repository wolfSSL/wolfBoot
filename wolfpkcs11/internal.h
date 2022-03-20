/* internal.h
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


#ifndef WOLFPKCS11_INTERNAL_H
#define WOLFPKCS11_INTERNAL_H

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/wc_encrypt.h>

#include <wolfpkcs11/options.h>
#include <wolfpkcs11/pkcs11.h>
#include <wolfpkcs11/version.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Crypto-Ki supported version information. */
#define WOLFPKCS11_MAJOR_VERSION  (LIBWOLFPKCS11_VERSION_HEX >> 24)
#define WOLFPKCS11_MINOR_VERSION  ((LIBWOLFPKCS11_VERSION_HEX >> 16) & 0xff)


/* Maximum number of sessions allocated per slot/token. */
#ifndef WP11_SESSION_CNT_MAX
#define WP11_SESSION_CNT_MAX           70
#endif
/* Minimum number of sessions allocated per slot/token. */
#ifndef WP11_SESSION_CNT_MIN
#define WP11_SESSION_CNT_MIN           2
#endif

/* Session was opened read-only or read/write. */
#define WP11_SESSION_RO                1
#define WP11_SESSION_RW                2

/* Maximum number of objects in a session. */
#ifndef WP11_SESSION_OBJECT_CNT_MAX
#define WP11_SESSION_OBJECT_CNT_MAX    8
#endif

/* Maximum number of objects in a token. */
#ifndef WP11_TOKEN_OBJECT_CNT_MAX
#define WP11_TOKEN_OBJECT_CNT_MAX      64
#endif

/* Session was opened read-only or read/write. */
#define WP11_TOKEN_STATE_UNKNOWN       0
#define WP11_TOKEN_STATE_INITIALIZED   1

/* Application state - session login. */
#define WP11_APP_STATE_RO_PUBLIC       CKS_RO_PUBLIC_SESSION
#define WP11_APP_STATE_RW_PUBLIC       CKS_RW_PUBLIC_SESSION
#define WP11_APP_STATE_RW_SO           CKS_RW_SO_FUNCTIONS
#define WP11_APP_STATE_RO_USER         CKS_RO_USER_FUNCTIONS
#define WP11_APP_STATE_RW_USER         CKS_RW_USER_FUNCTIONS

/* Login types */
#define WP11_LOGIN_SO                  1
#define WP11_LOGIN_USER                2

/* Session find states */
#define WP11_FIND_STATE_NULL           0
#define WP11_FIND_STATE_INIT           1
#define WP11_FIND_STATE_FOUND          2
/* Maximum number of matching objects to hold handles of. */
#ifndef WP11_FIND_MAX
#define WP11_FIND_MAX                  10
#endif

/* Flags for object. */
#define WP11_FLAG_PRIVATE              0x0001
#define WP11_FLAG_SENSITIVE            0x0002
#define WP11_FLAG_EXTRACTABLE          0x0004
#define WP11_FLAG_MODIFIABLE           0x0008
#define WP11_FLAG_ALWAYS_SENSITIVE     0x0010
#define WP11_FLAG_NEVER_EXTRACTABLE    0x0020
#define WP11_FLAG_ALWAYS_AUTHENTICATE  0x0040
#define WP11_FLAG_WRAP_WITH_TRUSTED    0x0080
#define WP11_FLAG_TRUSTED              0x0100

/* Operation session has initialized for. */
#define WP11_INIT_AES_CBC_ENC          0x0001
#define WP11_INIT_AES_CBC_DEC          0x0002
#define WP11_INIT_AES_GCM_ENC          0x0003
#define WP11_INIT_AES_GCM_DEC          0x0004
#define WP11_INIT_AES_CBC_PAD_ENC      0x0005
#define WP11_INIT_AES_CBC_PAD_DEC      0x0006
#define WP11_INIT_HMAC_SIGN            0x0010
#define WP11_INIT_HMAC_VERIFY          0x0011
#define WP11_INIT_RSA_X_509_ENC        0x0020
#define WP11_INIT_RSA_X_509_DEC        0x0021
#define WP11_INIT_RSA_PKCS_ENC         0x0022
#define WP11_INIT_RSA_PKCS_DEC         0x0023
#define WP11_INIT_RSA_PKCS_OAEP_ENC    0x0024
#define WP11_INIT_RSA_PKCS_OAEP_DEC    0x0025
#define WP11_INIT_RSA_PKCS_SIGN        0x0030
#define WP11_INIT_RSA_PKCS_VERIFY      0x0031
#define WP11_INIT_RSA_PKCS_PSS_SIGN    0x0032
#define WP11_INIT_RSA_PKCS_PSS_VERIFY  0x0033
#define WP11_INIT_RSA_X_509_SIGN       0x0034
#define WP11_INIT_RSA_X_509_VERIFY     0x0035
#define WP11_INIT_ECDSA_SIGN           0x0040
#define WP11_INIT_ECDSA_VERIFY         0x0041

/* scrypt parameters when generating hash from PIN. */
#ifndef WP11_HASH_PIN_COST
#define WP11_HASH_PIN_COST             10
#endif
#ifndef WCK_HASH_PIN_BLOCKSIZE
#define WP11_HASH_PIN_BLOCKSIZE        8
#endif
#ifndef WP11_HASH_PIN_PARALLEL
#define WP11_HASH_PIN_PARALLEL         1
#endif

/* PIN length constraints. */
#ifndef WP11_MIN_PIN_LEN
#define WP11_MIN_PIN_LEN               4
#endif
#ifndef WP11_MAX_PIN_LEN
#define WP11_MAX_PIN_LEN               32
#endif

/* Login failure constants. */
#ifndef WP11_MAX_LOGIN_FAILS_SO
#define WP11_MAX_LOGIN_FAILS_SO        3
#endif
/* Timeout added on each time a maximum failed SO logins are made */
#ifndef WP11_SO_LOGIN_FAIL_TIMEOUT
#define WP11_SO_LOGIN_FAIL_TIMEOUT     600
#endif
#ifndef WP11_MAX_LOGIN_FAILS_USER
#define WP11_MAX_LOGIN_FAILS_USER      3
#endif
/* Timeout added on each time a maximum failed User logins are made */
#ifndef WP11_USER_LOGIN_FAIL_TIMEOUT
#define WP11_USER_LOGIN_FAIL_TIMEOUT   60
#endif

/* Internal errors. */
#define PIN_INVALID_E                  -1
#define PIN_NOT_SET_E                  -2
#define READ_ONLY_E                    -3
#define NOT_AVAILABE_E                 -4
#define FIND_FULL_E                    -5
#define FIND_NO_MORE_E                 -6
#define SESSION_EXISTS_E               -7
#define SESSION_COUNT_E                -8
#define LOGGED_IN_E                    -9
#define OBJ_COUNT_E                    -10


typedef struct WP11_Object WP11_Object;
typedef struct WP11_Session WP11_Session;
typedef struct WP11_Slot WP11_Slot;


int WP11_Library_Init(void);
void WP11_Library_Final(void);
int WP11_Library_IsInitialized(void);

int WP11_SlotIdValid(CK_SLOT_ID slotId);

int WP11_GetSlotList(int tokenIn, CK_SLOT_ID* slotList, CK_ULONG* count);

int WP11_Slot_Get(CK_SLOT_ID slotId, WP11_Slot** slot);
int WP11_Slot_OpenSession(WP11_Slot* slot, unsigned long flags, void* app,
                          CK_NOTIFY notify, CK_SESSION_HANDLE* session);
void WP11_Slot_CloseSession(WP11_Slot* slot, WP11_Session* session);
void WP11_Slot_CloseSessions(WP11_Slot* slot);
int WP11_Slot_HasSession(WP11_Slot* slot);
int WP11_Slot_CheckSOPin(WP11_Slot* slot, char* pin, int pinLen);
int WP11_Slot_CheckUserPin(WP11_Slot* slot, char* pin, int pinLen);
int WP11_Slot_SOLogin(WP11_Slot* slot, char* pin, int pinLen);
int WP11_Slot_UserLogin(WP11_Slot* slot, char* pin, int pinLen);
void WP11_Slot_Logout(WP11_Slot* slot);
int WP11_Slot_SetSOPin(WP11_Slot* slot, char* pin, int pinLen);
int WP11_Slot_SetUserPin(WP11_Slot* slot, char* pin, int pinLen);
int WP11_Slot_TokenReset(WP11_Slot* slot, char* pin, int pinLen,
                         char* label);
void WP11_Slot_GetTokenLabel(WP11_Slot* slot, char* label);
int WP11_Slot_IsTokenInitialized(WP11_Slot* slot);
int WP11_Slot_TokenFailedLogin(WP11_Slot* slot, int login);
time_t WP11_Slot_TokenFailedExpire(WP11_Slot* slot, int login);
int WP11_Slot_IsTokenUserPinInitialized(WP11_Slot* slot);

int WP11_Session_Get(CK_SESSION_HANDLE sessionHandle, WP11_Session** session);
int WP11_Session_GetState(WP11_Session* session);
int WP11_Session_IsRW(WP11_Session* session);
int WP11_Session_IsOpInitialized(WP11_Session* session, int init);
void WP11_Session_SetOpInitialized(WP11_Session* session, int init);
WP11_Slot* WP11_Session_GetSlot(WP11_Session* session);
CK_MECHANISM_TYPE WP11_Session_GetMechanism(WP11_Session* session);
void WP11_Session_SetMechanism(WP11_Session* session,
                               CK_MECHANISM_TYPE mechanism);
int WP11_Session_SetPssParams(WP11_Session* session, CK_MECHANISM_TYPE hashAlg,
                              CK_MECHANISM_TYPE mgf, int sLen);
int WP11_Session_SetOaepParams(WP11_Session* session, CK_MECHANISM_TYPE hashAlg,
                               CK_MECHANISM_TYPE mgf, byte* label, int labelSz);
int WP11_Session_SetCbcParams(WP11_Session* session, unsigned char* iv, int enc,
                              WP11_Object* object);
int WP11_Session_SetGcmParams(WP11_Session* session, unsigned char* iv,
                              int ivSz, unsigned char* aad, int aadLen,
                              int tagBits);
int WP11_Session_AddObject(WP11_Session* session, int onToken,
                           WP11_Object* object);
void WP11_Session_RemoveObject(WP11_Session* session, WP11_Object* object);
void WP11_Session_GetObject(WP11_Session* session, WP11_Object** object);
void WP11_Session_SetObject(WP11_Session* session, WP11_Object* object);

int WP11_Session_FindInit(WP11_Session* session);
void WP11_Session_Find(WP11_Session* session, int onToken,
                       CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
int WP11_Session_FindGet(WP11_Session* session, CK_OBJECT_HANDLE* id);
void WP11_Session_FindFinal(WP11_Session* session);


int WP11_Object_New(WP11_Session* session, CK_KEY_TYPE type,
                    WP11_Object** object);
void WP11_Object_Free(WP11_Object* object);

CK_OBJECT_HANDLE WP11_Object_GetHandle(WP11_Object* object);
CK_KEY_TYPE WP11_Object_GetType(WP11_Object* object);

int WP11_Object_SetRsaKey(WP11_Object* object, unsigned char** data,
                          CK_ULONG* len);
int WP11_Object_SetEcKey(WP11_Object* object, unsigned char** data,
                         CK_ULONG* len);
int WP11_Object_SetDhKey(WP11_Object* object, unsigned char** data,
                         CK_ULONG* len);
int WP11_Object_SetSecretKey(WP11_Object* object, unsigned char** data,
                             CK_ULONG* len);
int WP11_Object_SetClass(WP11_Object* object, CK_OBJECT_CLASS objClass);

int WP11_Object_Find(WP11_Session* session, CK_OBJECT_HANDLE objHandle,
                     WP11_Object** object);
int WP11_Object_GetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type, byte* data,
                        CK_ULONG* len);
int WP11_Object_SetAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type, byte* data,
                        CK_ULONG len);
int WP11_Object_MatchAttr(WP11_Object* object, CK_ATTRIBUTE_TYPE type,
                          byte* data, CK_ULONG len);

int WP11_Rsa_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                             WP11_Slot* slot);
word32 WP11_Rsa_KeyLen(WP11_Object* key);
int WP11_Rsa_PublicEncrypt(unsigned char* in, word32 inLen, unsigned char* out,
                           word32* outLen, WP11_Object* pub, WP11_Slot* slot);
int WP11_Rsa_PrivateDecrypt(unsigned char* in, word32 inLen, unsigned char* out,
                            word32* outLen, WP11_Object* priv, WP11_Slot* slot);
int WP11_RsaPkcs15_PublicEncrypt(unsigned char* in, word32 inLen,
                                 unsigned char* out, word32* outLen,
                                 WP11_Object* pub, WP11_Slot* slot);
int WP11_RsaPkcs15_PrivateDecrypt(unsigned char* in, word32 inLen,
                                  unsigned char* out, word32* outLen,
                                  WP11_Object* priv, WP11_Slot* slot);
int WP11_RsaOaep_PublicEncrypt(unsigned char* in, word32 inLen,
                               unsigned char* out, word32* outLen,
                               WP11_Object* pub, WP11_Session* session);
int WP11_RsaOaep_PrivateDecrypt(unsigned char* in, word32 inLen,
                                unsigned char* out, word32* outLen,
                                WP11_Object* priv, WP11_Session* session);
int WP11_Rsa_Sign(unsigned char* in, word32 inLen, unsigned char* sig,
                  word32* sigLen, WP11_Object* priv, WP11_Slot* slot);
int WP11_Rsa_Verify(unsigned char* sig, word32 sigLen, unsigned char* in,
                    word32 inLen, int* stat, WP11_Object* pub);
int WP11_RsaPkcs15_Sign(unsigned char* encHash, word32 encHashLen,
                        unsigned char* sig, word32* sigLen, WP11_Object* priv,
                        WP11_Slot* slot);
int WP11_RsaPkcs15_Verify(unsigned char* sig, word32 sigLen,
                          unsigned char* encHash, word32 encHashLen, int* stat,
                          WP11_Object* pub);
int WP11_RsaPKCSPSS_Sign(unsigned char* hash, word32 hashLen,
                         unsigned char* sig, word32* sigLen,
                         WP11_Object* pub, WP11_Session* session);
int WP11_RsaPKCSPSS_Verify(unsigned char* sig, word32 sigLen,
                           unsigned char* hash, word32 hashLen, int* stat,
                           WP11_Object* pub, WP11_Session* session);

int WP11_Ec_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                            WP11_Slot* slot);
int WP11_Ec_SigLen(WP11_Object* key);
int WP11_Ec_Sign(unsigned char* hash, word32 hashLen, unsigned char* sig,
                 word32* sigLen, WP11_Object* priv, WP11_Slot* slot);
int WP11_Ec_Verify(unsigned char* sig, word32 sigLen, unsigned char* hash,
                   word32 hashLen, int* stat, WP11_Object* pub);
int WP11_EC_Derive(unsigned char* point, word32 pointLen, unsigned char* key,
                   word32 keyLen, WP11_Object* priv);

int WP11_Dh_GenerateKeyPair(WP11_Object* pub, WP11_Object* priv,
                            WP11_Slot* slot);
int WP11_Dh_Derive(unsigned char* pub, word32 pubLen, unsigned char* key,
                   word32* keyLen, WP11_Object* priv);

int WP11_AesGenerateKey(WP11_Object* secret, WP11_Slot* slot);

int WP11_AesCbc_PartLen(WP11_Session* session);
int WP11_AesCbc_Encrypt(unsigned char* plain, word32 plainSz,
                        unsigned char* enc, word32* encSz,
                        WP11_Session* session);
int WP11_AesCbc_EncryptUpdate(unsigned char* plain, word32 plainSz,
                        unsigned char* enc, word32* encSz,
                        WP11_Session* session);
int WP11_AesCbc_EncryptFinal(WP11_Session* session);
int WP11_AesCbc_Decrypt(unsigned char* enc, word32 encSz, unsigned char* dec,
                        word32* decSz, WP11_Session* session);
int WP11_AesCbc_DecryptUpdate(unsigned char* enc, word32 encSz,
                              unsigned char* dec, word32* decSz,
                              WP11_Session* session);
int WP11_AesCbc_DecryptFinal(WP11_Session* session);
int WP11_AesCbcPad_Encrypt(unsigned char* plain, word32 plainSz,
                           unsigned char* enc, word32* encSz,
                           WP11_Session* session);
int WP11_AesCbcPad_EncryptUpdate(unsigned char* plain, word32 plainSz,
                           unsigned char* enc, word32* encSz,
                           WP11_Session* session);
int WP11_AesCbcPad_EncryptFinal(unsigned char* enc, word32* encSz,
                                WP11_Session* session);
int WP11_AesCbcPad_Decrypt(unsigned char* enc, word32 encSz, unsigned char* dec,
                           word32* decSz, WP11_Session* session);
int WP11_AesCbcPad_DecryptUpdate(unsigned char* enc, word32 encSz,
                                 unsigned char* dec, word32* decSz,
                                 WP11_Session* session);
int WP11_AesCbcPad_DecryptFinal(unsigned char* dec, word32* decSz,
                                WP11_Session* session);

int WP11_AesGcm_GetTagBits(WP11_Session* session);
int WP11_AesGcm_EncDataLen(WP11_Session* session);
int WP11_AesGcm_Encrypt(unsigned char* plain, word32 plainSz,
                        unsigned char* enc, word32* encSz, WP11_Object* secret,
                        WP11_Session* session);
int WP11_AesGcm_EncryptUpdate(unsigned char* plain, word32 plainSz,
                              unsigned char* enc, word32* encSz,
                              WP11_Object* secret, WP11_Session* session);
int WP11_AesGcm_EncryptFinal(unsigned char* enc, word32* encSz,
                             WP11_Session* session);
int WP11_AesGcm_Decrypt(unsigned char* enc, word32 encSz, unsigned char* dec,
                        word32* decSz, WP11_Object* secret,
                        WP11_Session* session);
int WP11_AesGcm_DecryptUpdate(unsigned char* enc, word32 encSz,
                              WP11_Session* session);
int WP11_AesGcm_DecryptFinal(unsigned char* dec, word32* decSz,
                             WP11_Object* secret, WP11_Session* session);

int WP11_Hmac_SigLen(WP11_Session* session);
int WP11_Hmac_Init(CK_MECHANISM_TYPE mechansim, WP11_Object* secret,
                   WP11_Session* session);
int WP11_Hmac_Sign(unsigned char* data, word32 dataLen, unsigned char* sig,
                   word32* sigLen, WP11_Session* session);
int WP11_Hmac_Verify(unsigned char* sig, word32 sigLen, unsigned char* data,
                     word32 dataLen, int* stat, WP11_Session* session);
int WP11_Hmac_Update(unsigned char* data, word32 dataLen,
                     WP11_Session* session);
int WP11_Hmac_SignFinal(unsigned char* sig, word32* sigLen,
                        WP11_Session* session);
int WP11_Hmac_VerifyFinal(unsigned char* sig, word32 sigLen, int* stat,
                          WP11_Session* session);

int WP11_Slot_SeedRandom(WP11_Slot* slot, unsigned char* seed, int seedLen);
int WP11_Slot_GenerateRandom(WP11_Slot* slot, unsigned char* data, int len);

#ifdef __cplusplus
}
#endif


#endif /* WOLFPKCS11_INTERNAL_H */


