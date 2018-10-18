/* crypto.c
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


#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include <wolfpkcs11/pkcs11.h>
#include <wolfpkcs11/internal.h>

#define ATTR_TYPE_ULONG        0
#define ATTR_TYPE_BOOL         1
#define ATTR_TYPE_DATA         2
#define ATTR_TYPE_DATE         3

#ifndef NO_RSA
/* RSA key data attributes. */
static CK_ATTRIBUTE_TYPE rsaKeyParams[] = {
    CKA_MODULUS,
    CKA_PRIVATE_EXPONENT,
    CKA_PRIME_1,
    CKA_PRIME_2,
    CKA_EXPONENT_1,
    CKA_EXPONENT_2,
    CKA_COEFFICIENT,
    CKA_PUBLIC_EXPONENT,
    CKA_MODULUS_BITS,
};
/* Count of RSA key data attributes. */
#define RSA_KEY_PARAMS_CNT    (sizeof(rsaKeyParams)/sizeof(*rsaKeyParams))
#endif

#ifdef HAVE_ECC
/* EC key data attributes. */
static CK_ATTRIBUTE_TYPE ecKeyParams[] = {
    CKA_EC_PARAMS,
    CKA_VALUE,
    CKA_EC_POINT
};
/* Count of EC key data attributes. */
#define EC_KEY_PARAMS_CNT     (sizeof(ecKeyParams)/sizeof(*ecKeyParams))
#endif

#ifndef NO_DH
/* DH key data attributes. */
static CK_ATTRIBUTE_TYPE dhKeyParams[] = {
    CKA_PRIME,
    CKA_BASE,
    CKA_VALUE,
};
/* Count of DH key data attributes. */
#define DH_KEY_PARAMS_CNT     (sizeof(dhKeyParams)/sizeof(*dhKeyParams))
#endif

#if !defined(NO_AES) || defined(HAVE_ECC) || !defined(NO_DH)
/* Secret key data attributes. */
static CK_ATTRIBUTE_TYPE secretKeyParams[] = {
    CKA_VALUE_LEN,
    CKA_VALUE,
};
/* Count of secret key data attributes. */
#define SECRET_KEY_PARAMS_CNT (sizeof(secretKeyParams)/sizeof(*secretKeyParams))
#endif

/* Identify maximum count for stack array. */
#ifndef NO_RSA
#define KEY_MAX_PARAMS        RSA_KEY_PARAMS_CNT
#elif defined(HAVE_ECC)
#define KEY_MAX_PARAMS        ECC_KEY_PARAMS_CNT
#elif !defined(NO_DH)
#define KEY_MAX_PARAMS        DH_KEY_PARAMS_CNT
#elif !defined(NO_AES)
#define KEY_MAX_PARAMS        SECRET_KEY_PARAMS_CNT
#endif

typedef struct AttributeType {
    CK_ATTRIBUTE_TYPE attr;            /* Crypto-Ki attribute                 */
    byte type;                         /* Data type associated with attribute */
} AttributeType;

/* List of recognized attributes and their data type. */
static AttributeType attrType[] = {
    { CKA_CLASS,                       ATTR_TYPE_ULONG },
    { CKA_TOKEN,                       ATTR_TYPE_DATA  },
    { CKA_PRIVATE,                     ATTR_TYPE_BOOL  },
    { CKA_LABEL,                       ATTR_TYPE_DATA  },
    { CKA_APPLICATION,                 ATTR_TYPE_DATA  },
    { CKA_VALUE,                       ATTR_TYPE_DATA  },
    { CKA_OBJECT_ID,                   ATTR_TYPE_DATA  },
    { CKA_OWNER,                       ATTR_TYPE_DATA  },
    { CKA_TRUSTED,                     ATTR_TYPE_BOOL  },
    { CKA_KEY_TYPE,                    ATTR_TYPE_ULONG },
    { CKA_SUBJECT,                     ATTR_TYPE_DATA  },
    { CKA_ID,                          ATTR_TYPE_DATA  },
    { CKA_SENSITIVE,                   ATTR_TYPE_BOOL  },
    { CKA_ENCRYPT,                     ATTR_TYPE_BOOL  },
    { CKA_DECRYPT,                     ATTR_TYPE_BOOL  },
    { CKA_WRAP,                        ATTR_TYPE_BOOL  },
    { CKA_UNWRAP,                      ATTR_TYPE_BOOL  },
    { CKA_SIGN,                        ATTR_TYPE_BOOL  },
    { CKA_SIGN_RECOVER,                ATTR_TYPE_BOOL  },
    { CKA_VERIFY,                      ATTR_TYPE_BOOL  },
    { CKA_VERIFY_RECOVER,              ATTR_TYPE_BOOL  },
    { CKA_DERIVE,                      ATTR_TYPE_BOOL  },
    { CKA_START_DATE,                  ATTR_TYPE_DATE  },
    { CKA_END_DATE,                    ATTR_TYPE_DATE  },
    { CKA_MODULUS,                     ATTR_TYPE_DATA  },
    { CKA_MODULUS_BITS,                ATTR_TYPE_ULONG },
    { CKA_PUBLIC_EXPONENT,             ATTR_TYPE_DATA  },
    { CKA_PRIVATE_EXPONENT,            ATTR_TYPE_DATA  },
    { CKA_PRIME_1,                     ATTR_TYPE_DATA  },
    { CKA_PRIME_2,                     ATTR_TYPE_DATA  },
    { CKA_EXPONENT_1,                  ATTR_TYPE_DATA  },
    { CKA_EXPONENT_2,                  ATTR_TYPE_DATA  },
    { CKA_COEFFICIENT,                 ATTR_TYPE_DATA  },
    { CKA_PUBLIC_KEY_INFO,             ATTR_TYPE_DATA  },
    { CKA_PRIME,                       ATTR_TYPE_DATA  },
    { CKA_BASE,                        ATTR_TYPE_DATA  },
    { CKA_PRIME_BITS,                  ATTR_TYPE_ULONG },
    { CKA_VALUE_BITS,                  ATTR_TYPE_ULONG },
    { CKA_VALUE_LEN,                   ATTR_TYPE_ULONG },
    { CKA_EXTRACTABLE,                 ATTR_TYPE_BOOL  },
    { CKA_LOCAL,                       ATTR_TYPE_BOOL  },
    { CKA_NEVER_EXTRACTABLE,           ATTR_TYPE_BOOL  },
    { CKA_ALWAYS_SENSITIVE,            ATTR_TYPE_BOOL  },
    { CKA_KEY_GEN_MECHANISM,           ATTR_TYPE_ULONG },
    { CKA_MODIFIABLE,                  ATTR_TYPE_BOOL  },
    { CKA_COPYABLE,                    ATTR_TYPE_BOOL  },
    { CKA_DESTROYABLE,                 ATTR_TYPE_BOOL  },
    { CKA_EC_PARAMS,                   ATTR_TYPE_DATA  },
    { CKA_EC_POINT,                    ATTR_TYPE_DATA  },
    { CKA_ALWAYS_AUTHENTICATE,         ATTR_TYPE_BOOL  },
    { CKA_WRAP_WITH_TRUSTED,           ATTR_TYPE_BOOL  },
    { CKA_HW_FEATURE_TYPE,             ATTR_TYPE_ULONG },
    { CKA_RESET_ON_INIT,               ATTR_TYPE_BOOL  },
    { CKA_HAS_RESET,                   ATTR_TYPE_BOOL  },
    { CKA_WRAP_TEMPLATE,               ATTR_TYPE_DATA  },
    { CKA_UNWRAP_TEMPLATE,             ATTR_TYPE_DATA  },
    { CKA_DERIVE_TEMPLATE,             ATTR_TYPE_DATA  },
    { CKA_ALLOWED_MECHANISMS,          ATTR_TYPE_DATA  },
};
/* Count of elements in attribute type list. */
#define ATTR_TYPE_SIZE     (sizeof(attrType) / sizeof(*attrType))

/**
 * Find the attribute type in the template.
 *
 * @param  pTemplate  [in]   Template of attributed for an object.
 * @param  ulCount    [in]   Number of attribute triplets in template.
 * @param  type       [in]   Attribute type to find.
 * @param  attribute  [out]  Attribute with the type.
 *                           NULL when type not found.
 */
static void FindAttributeType(CK_ATTRIBUTE* pTemplate, CK_ULONG ulCount,
                              CK_ATTRIBUTE_TYPE type, CK_ATTRIBUTE** attribute)
{
    int i;

    *attribute = NULL;
    for (i = 0; i < (int)ulCount; i++) {
        if (pTemplate[i].type == type)
            *attribute = &pTemplate[i];
    }
}

/**
 * Check the value and length are valid for the data type of the attributes in
 * the template.
 * Boolean value is checked for CK_TRUE or CK_FALSE when setting attributes.
 *
 * @param  pTemplate  [in]  Template of attributes for object.
 * @param  ulCount    [in]  Number of attribute triplets in template.
 * @param  set        [in]  Whether attributes are being used to set or get
 *                          value.
 * @return  CKR_ATTRIBUTE_TYPE_INVALID if the attribute type is not supported.
 *          CKR_ATTRIBUTE_VALUE_INVALID if value is not valid for data type.
 *          CKR_BUFFER_TOO_SMALL if length is too short for data type.
 *          CKR_OK on success.
 */
static CK_RV CheckAttributes(CK_ATTRIBUTE* pTemplate, CK_ULONG ulCount, int set)
{
    CK_ATTRIBUTE* attr;
    int i, j;

    for (i = 0; i < (int)ulCount; i++) {
        attr = &pTemplate[i];
        for (j = 0; j < (int)ATTR_TYPE_SIZE; j++) {
            if (attrType[j].attr == attr->type) {
                break;
            }
        }
        if (j == ATTR_TYPE_SIZE)
            return CKR_ATTRIBUTE_TYPE_INVALID;

        if (attrType[j].type == ATTR_TYPE_ULONG) {
            if (attr->pValue == NULL)
                return CKR_ATTRIBUTE_VALUE_INVALID;
            if (attr->ulValueLen != sizeof(CK_ULONG))
                return CKR_BUFFER_TOO_SMALL;
        }
        else if (attrType[j].type == ATTR_TYPE_BOOL) {
            if (attr->pValue == NULL)
                return CKR_ATTRIBUTE_VALUE_INVALID;
            if (attr->ulValueLen != sizeof(CK_BBOOL))
                return CKR_BUFFER_TOO_SMALL;
            if (set && *(CK_BBOOL*)attr->pValue != CK_TRUE &&
                                         *(CK_BBOOL*)attr->pValue != CK_FALSE) {
                return CKR_ATTRIBUTE_VALUE_INVALID;
            }
        }
        else if (attrType[j].type == ATTR_TYPE_DATE) {
            if (attr->pValue == NULL)
                return CKR_ATTRIBUTE_VALUE_INVALID;
            if (attr->ulValueLen != sizeof(CK_DATE))
                return CKR_BUFFER_TOO_SMALL;
        }
        else if (attrType[j].type == ATTR_TYPE_DATA) {
            if (set && attr->ulValueLen == CK_UNAVAILABLE_INFORMATION)
                return CKR_ATTRIBUTE_VALUE_INVALID;
        }
    }

    return CKR_OK;
}

/**
 * Set the values of the attributes into the object.
 *
 * @param  session    [in]  Session object.
 * @param  obj        [in]  Object to set value agqainst.
 * @param  pTemplate  [in]  Template of attributes set against object.
 * @param  ulCount    [in]  Number of attribute triplets in template.
 * @return  CKR_ARGUMENTS_BAD when pTemplate is NULL.
 *          CKR_SESSION_READ_ONLY when the session cannot modify objects.
 *          CKR_ATTRIBUTE_TYPE_INVALID if the attribute type is not supported.
 *          CKR_ATTRIBUTE_VALUE_INVALID if value is not valid for data type.
 *          CKR_BUFFER_TOO_SMALL if an attribute length is too short.
 *          CK_UNAVAILABLE_INFORMATION when an attribute type is not supported
 *          for modification.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when getting a value fails.
 *          CKR_OK on success.
 */
static CK_RV SetAttributeValue(WP11_Session* session, WP11_Object* obj,
                               CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    int ret = 0;
    CK_RV rv;
    CK_ATTRIBUTE* attr;
    int i, j;
    unsigned char* data[KEY_MAX_PARAMS] = { 0, };
    CK_ULONG len[KEY_MAX_PARAMS] = { 0, };
    CK_ATTRIBUTE_TYPE* attrs = NULL;
    int cnt;
    CK_KEY_TYPE type;

    if (pTemplate == NULL)
        return CKR_ARGUMENTS_BAD;
    if (!WP11_Session_IsRW(session))
        return CKR_SESSION_READ_ONLY;

    rv = CheckAttributes(pTemplate, ulCount, 1);
    if (rv != CKR_OK)
        return rv;

    /* Get the value and length of key specific attribute types. */
    type = WP11_Object_GetType(obj);
    switch (type) {
#ifndef NO_RSA
        case CKK_RSA:
            attrs = rsaKeyParams;
            cnt = RSA_KEY_PARAMS_CNT;
            break;
#endif
#ifdef HAVE_ECC
        case CKK_EC:
            attrs = ecKeyParams;
            cnt = EC_KEY_PARAMS_CNT;
            break;
#endif
#ifndef NO_DH
        case CKK_DH:
            attrs = dhKeyParams;
            cnt = DH_KEY_PARAMS_CNT;
            break;
#endif
#ifndef NO_AES
        case CKK_AES:
#endif
#if defined(HAVE_ECC) || !defined(NO_DH)
        case CKK_GENERIC_SECRET:
#endif
#if !defined(NO_AES) || defined(HAVE_ECC) || !defined(NO_DH)
            attrs = secretKeyParams;
            cnt = SECRET_KEY_PARAMS_CNT;
            break;
#endif
        default:
            return CKR_OBJECT_HANDLE_INVALID;
   }

    for (i = 0; i < cnt; i++) {
        for (j = 0; j < (int)ulCount; j++) {
            if (attrs[i] == pTemplate[j].type) {
                data[i] = pTemplate[j].pValue;
                if (data[i] == NULL)
                    return CKR_ATTRIBUTE_VALUE_INVALID;
                len[i] = (int)pTemplate[j].ulValueLen;
                break;
            }
        }
    }

    /* Set the value and length of key specific attributes
     * Old key data is cleared.
     */
    switch (type) {
#ifndef NO_RSA
        case CKK_RSA:
            ret = WP11_Object_SetRsaKey(obj, data, len);
            break;
#endif
#ifdef HAVE_ECC
        case CKK_EC:
            ret = WP11_Object_SetEcKey(obj, data, len);
            break;
#endif
#ifndef NO_DH
        case CKK_DH:
            ret = WP11_Object_SetDhKey(obj, data, len);
            break;
#endif
#ifndef NO_AES
        case CKK_AES:
#endif
#if defined(HAVE_ECC) || !defined(NO_DH)
        case CKK_GENERIC_SECRET:
#endif
#if !defined(NO_AES) || defined(HAVE_ECC) || !defined(NO_DH)
            ret = WP11_Object_SetSecretKey(obj, data, len);
            break;
#endif
        default:
            break;
    }
    if (ret == MEMORY_E)
        return CKR_DEVICE_MEMORY;
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    /* Set remaining attributes - key specific attributes ignored. */
    for (i = 0; i < (int)ulCount; i++) {
        attr = &pTemplate[i];
        ret = WP11_Object_SetAttr(obj, attr->type, attr->pValue,
                                                              attr->ulValueLen);
        if (ret == BAD_FUNC_ARG)
            return CKR_ATTRIBUTE_VALUE_INVALID;
        else if (ret == BUFFER_E)
            return CKR_BUFFER_TOO_SMALL;
        else if (ret != 0)
            return CKR_FUNCTION_FAILED;
    }

    return CKR_OK;
}

/**
 * New Object object.
 *
 * @param  session    [in]   Session object.
 * @param  keyType    [in]   Type of key object.
 * @param  keyClass   [in]   Class of key object.
 * @param  pTemplate  [in]   Array of attributes to create object with.
 * @param  ulCount    [in]   Count of elements in array.
 * @param  object     [out]  New Object object.
 * @return  CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when setting an attribute fails.
 *          CKR_OK on success.
 */
static CK_RV NewObject(WP11_Session* session, CK_KEY_TYPE keyType,
                       CK_OBJECT_CLASS keyClass, CK_ATTRIBUTE_PTR pTemplate,
                       CK_ULONG ulCount, WP11_Object** object)
{
    int ret;
    CK_RV rv;
    WP11_Object* obj = NULL;

    ret = WP11_Object_New(session, keyType, &obj);
    if (ret == MEMORY_E)
        return CKR_DEVICE_MEMORY;
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    ret = WP11_Object_SetClass(obj, keyClass);
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    rv = SetAttributeValue(session, obj, pTemplate, ulCount);
    if (rv != CKR_OK) {
        WP11_Object_Free(obj);
        return rv;
    }

    *object = obj;

    return CKR_OK;
}

/**
 * Add an object to the session.
 *
 * @param  session    [in]   Session object.
 * @param  object     [in]   Object object.
 * @param  pTemplate  [in]   Array of attributes.
 * @param  ulCount    [in]   Count of elements in array.
 * @param  phKey      [out]  Handle to new key object.
 * @return  CKR_ATTRIBUTE_VALUE_INVALID when attribute value is not valid for
 *          data type.
 *          CKR_FUNCTION_FAILED when setting an attribute fails.
 *          CKR_OK on success.
 */
static CK_RV AddObject(WP11_Session* session, WP11_Object* object,
                       CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                       CK_OBJECT_HANDLE_PTR phKey)
{
    int ret;
    CK_ATTRIBUTE* attr;
    int onToken = 0;

    FindAttributeType(pTemplate, ulCount, CKA_TOKEN, &attr);
    if (attr != NULL) {
        if (attr->pValue == NULL)
            return CKR_ATTRIBUTE_VALUE_INVALID;
        if (attr->ulValueLen != sizeof(CK_BBOOL))
            return CKR_ATTRIBUTE_VALUE_INVALID;
        onToken = *(CK_BBOOL*)attr->pValue;
    }

    ret = WP11_Session_AddObject(session, onToken, object);
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    *phKey = WP11_Object_GetHandle(object);

    return CKR_OK;
}

/**
 * Create an object in the session or on the token associated with the session.
 *
 * @param  hSession   [in]   Handle of session.
 * @param  pTemplate  [in]   Template of attributes for object.
 * @param  ulCount    [in]   Number of attribute triplets in template.
 * @param  object     [out]  New Object object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate or phObject is NULL.
 *          CKR_SESSION_READ_ONLY when the session cannot create objects.
 *          CKR_TEMPLATE_INCOMPLETE when CKA_KEY_TYPE is missing.
 *          CKR_ATTRIBUTE_VALUE_INVALID when an attribute has invalid value or
 *          length.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when creating the object fails.
 *          CKR_OK on success.
 */
static CK_RV CreateObject(WP11_Session* session, CK_ATTRIBUTE_PTR pTemplate,
                          CK_ULONG ulCount, WP11_Object** object)
{
    CK_RV rv;
    CK_KEY_TYPE keyType = -1;
    CK_OBJECT_CLASS keyClass = -1;
    CK_ATTRIBUTE* attr;

    FindAttributeType(pTemplate, ulCount, CKA_KEY_TYPE, &attr);
    if (attr == NULL)
        return CKR_TEMPLATE_INCOMPLETE;
    if (attr->pValue == NULL)
        return CKR_ATTRIBUTE_VALUE_INVALID;
    if (attr->ulValueLen != sizeof(CK_KEY_TYPE))
        return CKR_ATTRIBUTE_VALUE_INVALID;
    keyType = *(CK_KEY_TYPE*)attr->pValue;

    if (keyType != CKK_RSA && keyType != CKK_EC && keyType != CKK_DH &&
                         keyType != CKK_AES &&  keyType != CKK_GENERIC_SECRET) {
        return CKR_ATTRIBUTE_VALUE_INVALID;
    }

    FindAttributeType(pTemplate, ulCount, CKA_CLASS, &attr);
    if (attr != NULL) {
        if (attr->pValue == NULL)
            return CKR_ATTRIBUTE_VALUE_INVALID;
        if (attr->ulValueLen != sizeof(CK_OBJECT_CLASS))
            return CKR_ATTRIBUTE_VALUE_INVALID;
        keyClass = *(CK_OBJECT_CLASS*)attr->pValue;
    }

    rv = NewObject(session, keyType, keyClass, pTemplate, ulCount, object);

    return rv;
}

/**
 * Create an object in the session or on the token associated with the session.
 *
 * @param  hSession   [in]   Handle of session.
 * @param  pTemplate  [in]   Template of attributes for object.
 * @param  ulCount    [in]   Number of attribute triplets in template.
 * @param  phObject   [out]  Handle of object created.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate or phObject is NULL.
 *          CKR_SESSION_READ_ONLY when the session cannot create objects.
 *          CKR_TEMPLATE_INCOMPLETE when CKA_KEY_TYPE is missing.
 *          CKR_ATTRIBUTE_VALUE_INVALID when an attribute has invalid value or
 *          length.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when creating the object fails.
 *          CKR_OK on success.
 */
CK_RV C_CreateObject(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate,
                     CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phObject)
{
    CK_RV rv;
    WP11_Session* session;
    WP11_Object* object;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pTemplate == NULL || phObject == NULL)
        return CKR_ARGUMENTS_BAD;
    if (!WP11_Session_IsRW(session))
        return CKR_SESSION_READ_ONLY;

    rv = CreateObject(session, pTemplate, ulCount, &object);
    if (rv != CKR_OK)
        return rv;
    rv = AddObject(session, object, pTemplate, ulCount, phObject);
    if (rv != CKR_OK)
        WP11_Object_Free(object);

    return rv;
}

/**
 * Copy the object in the session or on the token associated with the session.
 *
 * @param  hSession      [in]   Handle of session.
 * @param  hObject       [in]   Handle of object to copy.
 * @param  pTemplate     [in]   Template of attributes to copy.
 * @param  ulCount       [in]   Number of attribute triplets in template.
 * @param  phNewObject   [out]  Handle of object created.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate or phNewObject is NULL.
 *          CKR_SESSION_READ_ONLY when the session cannot create objects.
 *          CKR_OBJECT_HANDLE_INVALID when handle is not to a valid object.
 *          CKR_TEMPLATE_INCOMPLETE when CKA_KEY_TYPE is missing.
 *          CKR_ATTRIBUTE_VALUE_INVALID when an attribute has invalid value or
 *          length.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when creating the object fails.
 *          CKR_BUFFER_TOO_SMALL when an attributes length is too small for the
 *          value.
 *          CK_UNAVAILABLE_INFORMATION when an attribute type is not supported.
 *          CKR_OK on success.
 */
CK_RV C_CopyObject(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject,
                   CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                   CK_OBJECT_HANDLE_PTR phNewObject)
{
    int ret;
    CK_RV rv;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    WP11_Object* newObj = NULL;
    CK_ATTRIBUTE* attr;
    CK_KEY_TYPE keyType;
    int onToken = 0;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pTemplate == NULL || phNewObject == NULL)
        return CKR_ARGUMENTS_BAD;
    if (!WP11_Session_IsRW(session))
        return CKR_SESSION_READ_ONLY;

    /* Need key type and whether object is to be on the token to create a new
     * object. Get the object type from original object and where to store
     * new object from template.
     */
    ret = WP11_Object_Find(session, hObject, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;
    keyType = WP11_Object_GetType(obj);

    FindAttributeType(pTemplate, ulCount, CKA_TOKEN, &attr);
    if (attr != NULL) {
        if (attr->pValue == NULL)
            return CKR_ATTRIBUTE_VALUE_INVALID;
        if (attr->ulValueLen != sizeof(CK_BBOOL))
            return CKR_ATTRIBUTE_VALUE_INVALID;
        onToken = *(CK_BBOOL*)attr->pValue;
    }

    ret = WP11_Object_New(session, keyType, &newObj);
    if (ret == MEMORY_E)
        return CKR_DEVICE_MEMORY;
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    /* Use get and set attribute value to fill in object. */
    rv = C_GetAttributeValue(hSession, hObject, pTemplate, ulCount);
    if (rv != CKR_OK) {
        WP11_Object_Free(newObj);
        return rv;
    }
    rv = SetAttributeValue(session, newObj, pTemplate, ulCount);
    if (rv != CKR_OK) {
        WP11_Object_Free(newObj);
        return rv;
    }

    ret = WP11_Session_AddObject(session, onToken, newObj);
    if (ret != 0) {
        WP11_Object_Free(newObj);
        return CKR_FUNCTION_FAILED;
    }

    *phNewObject = WP11_Object_GetHandle(newObj);

    return CKR_OK;
}

/**
 * Destroy object in session or on token.
 *
 * @param  hSession  [in]  Handle of session.
 * @param  hObject   [in]  Handle of object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_SESSION_READ_ONLY when the session cannot create objects.
 *          CKR_OBJECT_HANDLE_INVALID when handle is not to a valid object.
 *          CKR_OK on success.
 */
CK_RV C_DestroyObject(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (!WP11_Session_IsRW(session))
        return CKR_SESSION_READ_ONLY;

    ret = WP11_Object_Find(session, hObject, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    WP11_Session_RemoveObject(session, obj);
    WP11_Object_Free(obj);

    return CKR_OK;
}

/**
 * Get the size of an specific object.
 * Not supported.
 *
 * @param  hSession  [in]   Handle of session.
 * @param  hObject   [in]   Handle of object.
 * @param  pulSize   [out]  Size in bytes of object on the token.
 *                          CK_UNAVAILABLE_INFORMATION is returned to indicate
 *                          this operation is not supported.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pulSize is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when handle is not to a valid object.
 *          CKR_OK on success.
 */
CK_RV C_GetObjectSize(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulSize == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hObject, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    *pulSize = CK_UNAVAILABLE_INFORMATION;

    return CKR_OK;
}


/**
 * Get the values of the attributes from the object.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  pTemplate  [in]  Template of attributes for object.
 * @param  ulCount    [in]  Number of attribute triplets in template.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when handle is not to a valid object.
 *          CKR_ATTRIBUTE_TYPE_INVALID if the attribute type is not supported.
 *          CKR_ATTRIBUTE_VALUE_INVALID if value is not valid for data type.
 *          CKR_BUFFER_TOO_SMALL if an attribute length is too short.
 *          CK_UNAVAILABLE_INFORMATION when an attribute type is not supported
 *          for retrieval.
 *          CKR_FUNCTION_FAILED when getting a value fails.
 *          CKR_OK on success.
 */
CK_RV C_GetAttributeValue(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    int ret;
    CK_RV rv;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_ATTRIBUTE* attr;
    int i;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pTemplate == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hObject, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    /* Check the value and lengths of attributes based on data type. */
    rv = CheckAttributes(pTemplate, ulCount, 0);
    if (rv != CKR_OK)
        return rv;

    for (i = 0; i < (int)ulCount; i++) {
        attr = &pTemplate[i];

        ret = WP11_Object_GetAttr(obj, attr->type, attr->pValue,
                                                             &attr->ulValueLen);
        if (ret == BAD_FUNC_ARG)
            return CKR_ATTRIBUTE_TYPE_INVALID;
        else if (ret == BUFFER_E)
            return CKR_BUFFER_TOO_SMALL;
        else if (ret == NOT_AVAILABE_E)
            return CK_UNAVAILABLE_INFORMATION;
        else if (ret != 0)
            return CKR_FUNCTION_FAILED;
    }

    return CKR_OK;
}

/**
 * Set the values of the attributes into the object.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  hObject    [in]  Handle of object to set value agqainst.
 * @param  pTemplate  [in]  Template of attributes set against object.
 * @param  ulCount    [in]  Number of attribute triplets in template.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate is NULL.
 *          CKR_SESSION_READ_ONLY when the session cannot modify objects.
 *          CKR_OBJECT_HANDLE_INVALID when handle is not to a valid object.
 *          CKR_ATTRIBUTE_TYPE_INVALID if the attribute type is not supported.
 *          CKR_ATTRIBUTE_VALUE_INVALID if value is not valid for data type.
 *          CKR_BUFFER_TOO_SMALL if an attribute length is too short.
 *          CK_UNAVAILABLE_INFORMATION when an attribute type is not supported
 *          for modification.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when getting a value fails.
 *          CKR_OK on success.
 */
CK_RV C_SetAttributeValue(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pTemplate == NULL)
        return CKR_ARGUMENTS_BAD;
    if (!WP11_Session_IsRW(session))
        return CKR_SESSION_READ_ONLY;

    ret = WP11_Object_Find(session, hObject, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    return SetAttributeValue(session, obj, pTemplate, ulCount);
}

/**
 * Initialize the finding of an object associated with the session.
 * All matching objects are found, up to a limit, by this call.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  pTemplate  [in]  Template of attributes match against object.
 * @param  ulCount    [in]  Number of attribute triplets in template.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pTemplate is NULL.
 *          CKR_OPERATION_ACTIVE when last find operation on session has not
 *          been finalized.
 *          CKR_ATTRIBUTE_VALUE_INVALID when attribute value is not valid for
 *          data type.
 *          CKR_OK on success.
 */
CK_RV C_FindObjectsInit(CK_SESSION_HANDLE hSession,
                        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    WP11_Session* session;
    CK_ATTRIBUTE* attr;
    int onToken = 1;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pTemplate == NULL)
        return CKR_ARGUMENTS_BAD;

    if (WP11_Session_FindInit(session) != 0)
        return CKR_OPERATION_ACTIVE;

    FindAttributeType(pTemplate, ulCount, CKA_TOKEN, &attr);
    if (attr != NULL) {
        if (attr->pValue == NULL)
            return CKR_ATTRIBUTE_VALUE_INVALID;
        if (attr->ulValueLen != sizeof(CK_BBOOL))
            return CKR_ATTRIBUTE_VALUE_INVALID;
        onToken = *(CK_BBOOL*)attr->pValue;
    }

    WP11_Session_Find(session, onToken, pTemplate, ulCount);

    return CKR_OK;
}

/**
 * Return next handles to found objects.
 * Object match the criteria set in the initialization call.
 *
 * @param  hSession          [in]   Handle of session.
 * @param  phObject          [in]   Array to hold object handles.
 * @param  ulMaxObjectCount  [in]   Number of entries in array.
 * @param  pulObjectCount    [out]  Number of handles set into array.
 *                                  0 when no more handles available.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when phObject or pulObjectCount is NULL.
 *          CKR_OK on success.
 */
CK_RV C_FindObjects(CK_SESSION_HANDLE hSession,
                    CK_OBJECT_HANDLE_PTR phObject,
                    CK_ULONG ulMaxObjectCount,
                    CK_ULONG_PTR pulObjectCount)
{
    int i;
    CK_OBJECT_HANDLE handle;
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (phObject == NULL || pulObjectCount == NULL)
        return CKR_ARGUMENTS_BAD;

    for (i = 0; i < (int)ulMaxObjectCount; i++) {
        if (WP11_Session_FindGet(session, &handle) == FIND_NO_MORE_E)
            break;
        phObject[i] = handle;
    }
    *pulObjectCount = i;

    return CKR_OK;
}

/**
 * Finalize the object finding operation.
 * Must be called before another find operation on the session is initialized.
 *
 * @param  hSession  [in]   Handle of session.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_OK on success.
 */
CK_RV C_FindObjectsFinal(CK_SESSION_HANDLE hSession)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;

    WP11_Session_FindFinal(session);

    return CKR_OK;
}


/**
 * Initialize encryption operation.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_KEY_TYPE_INCONSISTENT when the key type is not valid for the
 *          mechanism (operation).
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when initializing fails.
 *          CKR_OK on success.
 */
CK_RV C_EncryptInit(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_KEY_TYPE type;
    int init;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    type = WP11_Object_GetType(obj);
    switch (pMechanism->mechanism) {
#ifndef NO_RSA
        case CKM_RSA_X_509:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_X_509_ENC;
            break;

        case CKM_RSA_PKCS:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_PKCS_ENC;
            break;

    #ifndef WC_NO_RSA_OAEP
        case CKM_RSA_PKCS_OAEP: {
            CK_RSA_PKCS_OAEP_PARAMS* params;

            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_RSA_PKCS_OAEP_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_RSA_PKCS_OAEP_PARAMS*)pMechanism->pParameter;
            if (params->source != CKZ_DATA_SPECIFIED)
                return CKR_MECHANISM_PARAM_INVALID;

            ret = WP11_Session_SetOaepParams(session, params->hashAlg,
                                              params->mgf, params->pSourceData,
                                              params->ulSourceDataLen);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_RSA_PKCS_OAEP_ENC;
            break;
        }
    #endif
#endif

#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (type != CKK_AES)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != AES_IV_SIZE)
                return CKR_MECHANISM_PARAM_INVALID;
            ret = WP11_Session_SetCbcParams(session, pMechanism->pParameter, 1,
                                                                           obj);
            if (ret == MEMORY_E)
                return CKR_DEVICE_MEMORY;
            if (ret != 0)
                return CKR_FUNCTION_FAILED;
            init = WP11_INIT_AES_CBC_ENC;
            break;
    #endif

    #ifdef HAVE_AESGCM
        case CKM_AES_GCM: {
             CK_GCM_PARAMS* params;

            if (type != CKK_AES)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_GCM_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_GCM_PARAMS*)pMechanism->pParameter;
            ret = WP11_Session_SetGcmParams(session, params->pIv,
                                                  params->ulIvLen, params->pAAD,
                                                  params->ulAADLen,
                                                  params->ulTagBits);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_AES_GCM_ENC;
            break;
        }
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    WP11_Session_SetMechanism(session, pMechanism->mechanism);
    WP11_Session_SetObject(session, obj);
    WP11_Session_SetOpInitialized(session, init);

    return CKR_OK;
}

/**
 * Encrypt single-part data.
 *
 * @param  hSession             [in]      Handle of session.
 * @param  pData                [in]      Data to encrypt.
 * @param  ulDataLen            [in]      Length of data in bytes.
 * @param  pEncryptedData       [in]      Buffer to hold encrypted data.
 *                                        NULL indicates length required.
 * @param  pulEncryptedDataLen  [in,out]  On in, length of buffer in bytes.
 *                                        On out, length of encrypted data in
 *                                        bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData or pulEncryptedDataLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_EncryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          encrypted data.
 *          CKR_FUNCTION_FAILED when encrypting failed.
 *          CKR_OK on success.
 */
CK_RV C_Encrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
                CK_ULONG ulDataLen, CK_BYTE_PTR pEncryptedData,
                CK_ULONG_PTR pulEncryptedDataLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 encDataLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pData == NULL || pulEncryptedDataLen == NULL)
        return CKR_ARGUMENTS_BAD;

    /* Key the key for the encryption operation. */
    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_RSA
        case CKM_RSA_X_509:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_RSA_X_509_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encDataLen = WP11_Rsa_KeyLen(obj);
            if (pEncryptedData == NULL) {
                *pulEncryptedDataLen = encDataLen;
                return CKR_OK;
            }
            if (encDataLen > (word32)*pulEncryptedDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_Rsa_PublicEncrypt(pData, ulDataLen, pEncryptedData,
                                                 &encDataLen, obj,
                                                 WP11_Session_GetSlot(session));
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedDataLen = encDataLen;
            break;
        case CKM_RSA_PKCS:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_RSA_PKCS_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encDataLen = WP11_Rsa_KeyLen(obj);
            if (pEncryptedData == NULL) {
                *pulEncryptedDataLen = encDataLen;
                return CKR_OK;
            }
            if (encDataLen > (word32)*pulEncryptedDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaPkcs15_PublicEncrypt(pData, ulDataLen, pEncryptedData,
                                                 &encDataLen, obj,
                                                 WP11_Session_GetSlot(session));
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedDataLen = encDataLen;
            break;
    #ifndef WC_NO_RSA_OAEP
        case CKM_RSA_PKCS_OAEP:
            if (!WP11_Session_IsOpInitialized(session,
                                                 WP11_INIT_RSA_PKCS_OAEP_ENC)) {
                return CKR_OPERATION_NOT_INITIALIZED;
            }

            encDataLen = WP11_Rsa_KeyLen(obj);
            if (pEncryptedData == NULL) {
                *pulEncryptedDataLen = encDataLen;
                return CKR_OK;
            }
            if (encDataLen > (word32)*pulEncryptedDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaOaep_PublicEncrypt(pData, ulDataLen, pEncryptedData,
                                                     &encDataLen, obj, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedDataLen = encDataLen;
            break;
    #endif
#endif
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encDataLen = ulDataLen;
            if (pEncryptedData == NULL) {
                *pulEncryptedDataLen = encDataLen;
                return CKR_OK;
            }
            if (encDataLen > (word32)*pulEncryptedDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesCbc_Encrypt(pData, ulDataLen, pEncryptedData,
                                                          &encDataLen, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedDataLen = encDataLen;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encDataLen = ulDataLen + WP11_AesGcm_GetTagBits(session) / 8;
            if (pEncryptedData == NULL) {
                *pulEncryptedDataLen = encDataLen;
                return CKR_OK;
            }
            if (encDataLen > (word32)*pulEncryptedDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesGcm_Encrypt(pData, ulDataLen, pEncryptedData,
                                                     &encDataLen, obj, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedDataLen = encDataLen;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Continue encrypting multi-part data.
 *
 * @param  hSession             [in]      Handle of session.
 * @param  pPart                [in]      Data to encrypt.
 * @param  ulPartLen            [in]      Length of data in bytes.
 * @param  pEncryptedPart       [in]      Buffer to hold encrypted data.
 *                                        NULL indicates length required.
 * @param  pulEncryptedPartLen  [in,out]  On in, length of buffer in bytes.
 *                                        On out, length of encrypted data in
 *                                        bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart or pulEncryptedPartLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_EncryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          encrypted data.
 *          CKR_FUNCTION_FAILED when encrypting failed.
 *          CKR_OK on success.
 */
CK_RV C_EncryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                      CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart,
                      CK_ULONG_PTR pulEncryptedPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 encPartLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL || pulEncryptedPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encPartLen = ulPartLen + WP11_AesCbc_PartLen(session);
            encPartLen &= ~0xf;
            if (pEncryptedPart == NULL) {
                *pulEncryptedPartLen = encPartLen;
                return CKR_OK;
            }
            if (encPartLen > (word32)*pulEncryptedPartLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesCbc_EncryptUpdate(pPart, ulPartLen, pEncryptedPart,
                                                          &encPartLen, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedPartLen = encPartLen;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encPartLen = ulPartLen;
            if (pEncryptedPart == NULL) {
                *pulEncryptedPartLen = encPartLen;
                return CKR_OK;
            }
            if (encPartLen > (word32)*pulEncryptedPartLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesGcm_EncryptUpdate(pPart, ulPartLen, pEncryptedPart,
                                                     &encPartLen, obj, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulEncryptedPartLen = encPartLen;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Finishes encrypting multi-part data.
 *
 * @param  hSession                 [in]      Handle of session.
 * @param  pLastEncryptedPart       [in]      Buffer to hold encrypted data.
 *                                            NULL indicates length required.
 * @param  pulLastEncryptedPartLen  [in,out]  On in, length of buffer in bytes.
 *                                            On out, length of encrypted data
 *                                            in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart or pulEncryptedPartLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_EncryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          encrypted data.
 *          CKR_FUNCTION_FAILED when encrypting failed.
 *          CKR_OK on success.
 */
CK_RV C_EncryptFinal(CK_SESSION_HANDLE hSession,
                     CK_BYTE_PTR pLastEncryptedPart,
                     CK_ULONG_PTR pulLastEncryptedPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 encPartLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulLastEncryptedPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encPartLen = WP11_AesCbc_PartLen(session);
            if (encPartLen != 0) {
                WP11_AesCbc_EncryptFinal(session);
                return CKR_DATA_LEN_RANGE;
            }
            *pulLastEncryptedPartLen = 0;
            if (pLastEncryptedPart == NULL)
                return CKR_OK;

            ret = WP11_AesCbc_EncryptFinal(session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_ENC))
                return CKR_OPERATION_NOT_INITIALIZED;

            encPartLen = WP11_AesGcm_GetTagBits(session) / 8;
            if (pLastEncryptedPart == NULL) {
                *pulLastEncryptedPartLen = encPartLen;
                return CKR_OK;
            }
            if (encPartLen > (word32)*pulLastEncryptedPartLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesGcm_EncryptFinal(pLastEncryptedPart, &encPartLen,
                                                                       session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulLastEncryptedPartLen = encPartLen;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Initialize decryption operation.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_KEY_TYPE_INCONSISTENT when the key type is not valid for the
 *          mechanism (operation).
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when initializing fails.
 *          CKR_OK on success.
 */
CK_RV C_DecryptInit(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_KEY_TYPE type;
    int init;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    type = WP11_Object_GetType(obj);
    switch (pMechanism->mechanism) {
#ifndef NO_RSA
        case CKM_RSA_X_509:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_X_509_DEC;
            break;
        case CKM_RSA_PKCS:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_PKCS_DEC;
            break;
    #ifndef WC_NO_RSA_OAEP
        case CKM_RSA_PKCS_OAEP: {
            CK_RSA_PKCS_OAEP_PARAMS* params;

            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_RSA_PKCS_OAEP_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_RSA_PKCS_OAEP_PARAMS*)pMechanism->pParameter;
            if (params->source != CKZ_DATA_SPECIFIED)
                return CKR_MECHANISM_PARAM_INVALID;

            ret = WP11_Session_SetOaepParams(session, params->hashAlg,
                                               params->mgf, params->pSourceData,
                                               params->ulSourceDataLen);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_RSA_PKCS_OAEP_DEC;
            break;
        }
    #endif
#endif
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (type != CKK_AES)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != AES_IV_SIZE)
                return CKR_MECHANISM_PARAM_INVALID;
            ret = WP11_Session_SetCbcParams(session, pMechanism->pParameter, 0,
                                                                           obj);
            if (ret == MEMORY_E)
                return CKR_DEVICE_MEMORY;
            if (ret != 0)
                return CKR_FUNCTION_FAILED;
            init = WP11_INIT_AES_CBC_DEC;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM: {
            CK_GCM_PARAMS* params;

            if (type != CKK_AES)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_GCM_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_GCM_PARAMS*)pMechanism->pParameter;
            ret = WP11_Session_SetGcmParams(session, params->pIv,
                                                  params->ulIvLen, params->pAAD,
                                                  params->ulAADLen,
                                                  params->ulTagBits);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_AES_GCM_DEC;
            break;
        }
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    WP11_Session_SetMechanism(session, pMechanism->mechanism);
    WP11_Session_SetObject(session, obj);
    WP11_Session_SetOpInitialized(session, init);

    return CKR_OK;
}

/**
 * Decrypt single-part data.
 *
 * @param  hSession            [in]      Handle of session.
 * @param  pEncryptedData      [in]      Data to decrypt.
 * @param  ulEncryptedDataLen  [in]      Length of data in bytes.
 * @param  pData               [in]      Buffer to hold decrypted data.
 *                                       NULL indicates length required.
 * @param  pulDataLen          [in,out]  On in, length of buffer in bytes.
 *                                       On out, length of decrypted data in
 *                                       bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pEncryptedData or pulDataLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DecryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          decrypted data.
 *          CKR_FUNCTION_FAILED when decrypting failed.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_Decrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedData,
                CK_ULONG ulEncryptedDataLen, CK_BYTE_PTR pData,
                CK_ULONG_PTR pulDataLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 decDataLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pEncryptedData == NULL || pulDataLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_RSA
        case CKM_RSA_X_509:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_RSA_X_509_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decDataLen = WP11_Rsa_KeyLen(obj);
            if (pData == NULL) {
                *pulDataLen = decDataLen;
                return CKR_OK;
            }
            if (decDataLen > (word32)*pulDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_Rsa_PrivateDecrypt(pEncryptedData, ulEncryptedDataLen,
                                                 pData, &decDataLen, obj,
                                                 WP11_Session_GetSlot(session));
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulDataLen = decDataLen;
            break;
        case CKM_RSA_PKCS:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_RSA_PKCS_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decDataLen = WP11_Rsa_KeyLen(obj);
            if (pData == NULL) {
                *pulDataLen = decDataLen;
                return CKR_OK;
            }
            if (decDataLen > (word32)*pulDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaPkcs15_PrivateDecrypt(pEncryptedData,
                                                 ulEncryptedDataLen, pData,
                                                 &decDataLen, obj,
                                                 WP11_Session_GetSlot(session));
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulDataLen = decDataLen;
            break;
    #ifndef WC_NO_RSA_OAEP
        case CKM_RSA_PKCS_OAEP:
            if (!WP11_Session_IsOpInitialized(session,
                                                 WP11_INIT_RSA_PKCS_OAEP_DEC)) {
                return CKR_OPERATION_NOT_INITIALIZED;
            }

            decDataLen = WP11_Rsa_KeyLen(obj);
            if (pData == NULL) {
                *pulDataLen = decDataLen;
                return CKR_OK;
            }
            if (decDataLen > (word32)*pulDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaOaep_PrivateDecrypt(pEncryptedData,
                                                     ulEncryptedDataLen, pData,
                                                     &decDataLen, obj, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulDataLen = decDataLen;
            break;
    #endif
#endif
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decDataLen = ulEncryptedDataLen;
            if (pData == NULL) {
                *pulDataLen = decDataLen;
                return CKR_OK;
            }
            if (decDataLen > (word32)*pulDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesCbc_Decrypt(pEncryptedData, ulEncryptedDataLen,
                                              pData, &decDataLen, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulDataLen = decDataLen;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decDataLen = ulEncryptedDataLen -
                                            WP11_AesGcm_GetTagBits(session) / 8;
            if (pData == NULL) {
                *pulDataLen = decDataLen;
                return CKR_OK;
            }
            if (decDataLen > (word32)*pulDataLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesGcm_Decrypt(pEncryptedData, ulEncryptedDataLen,
                                              pData, &decDataLen, obj, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulDataLen = decDataLen;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Continue decrypting multi-part data.
 *
 * @param  hSession            [in]      Handle of session.
 * @param  pEncryptedPart      [in]      Data to decrypt.
 * @param  ulEncryptedPartLen  [in]      Length of data in bytes.
 * @param  pPart               [in]      Buffer to hold decrypted data.
 *                                       NULL indicates length required.
 * @param  pulPartLen          [in,out]  On in, length of buffer in bytes.
 *                                       On out, length of decrypted data in
 *                                       bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pEncryptedData or pulDataLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DecryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          decrypted data.
 *          CKR_FUNCTION_FAILED when decrypting failed.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_DecryptUpdate(CK_SESSION_HANDLE hSession,
                      CK_BYTE_PTR pEncryptedPart,
                      CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart,
                      CK_ULONG_PTR pulPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 decPartLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pEncryptedPart == NULL || pulPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decPartLen = ulEncryptedPartLen + WP11_AesCbc_PartLen(session);
            decPartLen &= ~0xf;
            if (pPart == NULL) {
                *pulPartLen = decPartLen;
                return CKR_OK;
            }
            if (decPartLen > (word32)*pulPartLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesCbc_DecryptUpdate(pEncryptedPart, ulEncryptedPartLen,
                                                   pPart, &decPartLen, session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulPartLen = decPartLen;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            *pulPartLen = 0;
            if (pPart == NULL)
                return CKR_OK;

            ret = WP11_AesGcm_DecryptUpdate(pEncryptedPart, ulEncryptedPartLen,
                                                                       session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Finishes decrypting multi-part data.
 *
 * @param  hSession        [in]      Handle of session.
 * @param  plastPart       [in]      Buffer to hold decrypted data.
 *                                   NULL indicates length required.
 * @param  pulLastPartLen  [in,out]  On in, length of buffer in bytes.
 *                                   On out, length of decrypted data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pulLastPartLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DecryptInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          decrypted data.
 *          CKR_FUNCTION_FAILED when decrypting failed.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_DecryptFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
                     CK_ULONG_PTR pulLastPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 decPartLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulLastPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_AES
    #ifdef HAVE_AES_CBC
        case CKM_AES_CBC:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_CBC_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decPartLen = WP11_AesCbc_PartLen(session);
            if (decPartLen != 0) {
                WP11_AesCbc_DecryptFinal(session);
                return CKR_DATA_LEN_RANGE;
            }
            *pulLastPartLen = 0;
            if (pLastPart == NULL)
                return CKR_OK;

            ret = WP11_AesCbc_DecryptFinal(session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            break;
    #endif
    #ifdef HAVE_AESGCM
        case CKM_AES_GCM:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_AES_GCM_DEC))
                return CKR_OPERATION_NOT_INITIALIZED;

            decPartLen = WP11_AesGcm_EncDataLen(session) -
                                            WP11_AesGcm_GetTagBits(session) / 8;
            if (pLastPart == NULL) {
                *pulLastPartLen = decPartLen;
                return CKR_OK;
            }
            if (decPartLen > (word32)*pulLastPartLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_AesGcm_DecryptFinal(pLastPart, &decPartLen, obj,
                                                                       session);
            if (ret < 0)
                return CKR_FUNCTION_FAILED;
            *pulLastPartLen = decPartLen;
            break;
    #endif
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return CKR_OK;
}

/**
 * Initialize digest operation.
 * No digest mechanisms are supported.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 */
CK_RV C_DigestInit(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    return CKR_MECHANISM_INVALID;
}

/**
 * Digest single-part data.
 * No digest mechanisms are supported.
 *
 * @param  hSession      [in]      Handle of session.
 * @param  pData         [in]      Data to be digested.
 * @param  ulDataLen     [in]      Length of data in bytes.
 * @param  pDigest       [in]      Buffer to hold digest output.
 *                                 NULL indicates length required.
 * @param  pulDigestLen  [in,out]  On in, length of the buffer.
 *                                 On out, length of the digest data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData, ulDataLen or pulDigestLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DigestInit has not been
 *          successfully called.
 */
CK_RV C_Digest(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
                  CK_ULONG ulDataLen, CK_BYTE_PTR pDigest,
                  CK_ULONG_PTR pulDigestLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pData == NULL || ulDataLen == 0 || pulDigestLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pDigest;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Continue digesting multi-part data.
 * No digest mechanisms are supported.
 *
 * @param  hSession      [in]      Handle of session.
 * @param  pPart         [in]      Data to be digested.
 * @param  ulPartLen     [in]      Length of data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart is NULL or ulPartLen is 0.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DigestInit has not been
 *          successfully called.
 */
CK_RV C_DigestUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                        CK_ULONG ulPartLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL || ulPartLen == 0)
        return CKR_ARGUMENTS_BAD;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Finished digesting multi-part data and places digest value in the key.
 * No digest mechanisms are supported.
 *
 * @param  hSession  [in]  Handle of session.
 * @param  hKey      [in]  Handle of a key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DigestInit has not been
 *          successfully called.
 */
CK_RV C_DigestKey(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Finished digesting multi-part data.
 * No digest mechanisms are supported.
 *
 * @param  hSession      [in]      Handle of session.
 * @param  pDigest       [in]      Buffer to hold digest output.
 *                                 NULL indicates length required.
 * @param  pulDigestLen  [in,out]  On in, length of the buffer.
 *                                 On out, length of the digest data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pulDigestLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DigestInit has not been
 *          successfully called.
 */
CK_RV C_DigestFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest,
                       CK_ULONG_PTR pulDigestLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulDigestLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pDigest;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Initialize signing operation.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_KEY_TYPE_INCONSISTENT when the key type is not valid for the
 *          mechanism (operation).
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_FUNCTION_FAILED when initializing fails.
 *          CKR_OK on success.
 */
CK_RV C_SignInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism,
                    CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_KEY_TYPE type;
    int init;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    type = WP11_Object_GetType(obj);
    switch (pMechanism->mechanism) {
#ifndef NO_RSA
        case CKM_RSA_PKCS:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_PKCS_SIGN;
            break;
    #ifdef WC_RSA_PSS
        case CKM_RSA_PKCS_PSS: {
            CK_RSA_PKCS_PSS_PARAMS* params;

            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_RSA_PKCS_PSS_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_RSA_PKCS_PSS_PARAMS*)pMechanism->pParameter;
            ret = WP11_Session_SetPssParams(session, params->hashAlg,
                                                     params->mgf, params->sLen);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_RSA_PKCS_PSS_SIGN;
            break;
        }
    #endif
#endif
#ifdef HAVE_ECC
        case CKM_ECDSA:
            if (type != CKK_EC)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_ECDSA_SIGN;
            break;
#endif
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (type != CKK_GENERIC_SECRET)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            ret = WP11_Hmac_Init(pMechanism->mechanism, obj, session);
            if (ret != 0)
                return CKR_FUNCTION_FAILED;
            init = WP11_INIT_HMAC_SIGN;
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    WP11_Session_SetMechanism(session, pMechanism->mechanism);
    WP11_Session_SetObject(session, obj);
    WP11_Session_SetOpInitialized(session, init);

    return CKR_OK;
}

/**
 * Sign the single-part data.
 *
 * @param  hSession         [in]      Handle of session.
 * @param  pData            [in]      Data to sign.
 * @param  ulDataLen        [in]      Length of data in bytes.
 * @param  pSignature       [in]      Buffer to hold signature.
 *                                    NULL indicates length required.
 * @param  pulSignatureLen  [in,out]  On in, length of buffer in bytes.
 *                                    On out, length of signature in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData or pulSignatureLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_SignInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          signature data.
 *          CKR_FUNCTION_FAILED when signing fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_Sign(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
             CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
             CK_ULONG_PTR pulSignatureLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    word32 sigLen;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pData == NULL || pulSignatureLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_RSA
        case CKM_RSA_PKCS:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_RSA_PKCS_SIGN))
                return CKR_OPERATION_NOT_INITIALIZED;

            sigLen = WP11_Rsa_KeyLen(obj);
            if (pSignature == NULL) {
                *pulSignatureLen = sigLen;
                return CKR_OK;
            }
            if (sigLen > (word32)*pulSignatureLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaPkcs15_Sign(pData, ulDataLen, pSignature, &sigLen,
                                            obj, WP11_Session_GetSlot(session));
            *pulSignatureLen = sigLen;
            break;
    #ifdef WC_RSA_PSS
        case CKM_RSA_PKCS_PSS:
            if (!WP11_Session_IsOpInitialized(session,
                                                 WP11_INIT_RSA_PKCS_PSS_SIGN)) {
                return CKR_OPERATION_NOT_INITIALIZED;
            }

            sigLen = WP11_Rsa_KeyLen(obj);
            if (pSignature == NULL) {
                *pulSignatureLen = sigLen;
                return CKR_OK;
            }
            if (sigLen > (word32)*pulSignatureLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_RsaPKCSPSS_Sign(pData, ulDataLen, pSignature, &sigLen,
                                                                  obj, session);
            *pulSignatureLen = sigLen;
            break;
    #endif
#endif
#ifdef HAVE_ECC
        case CKM_ECDSA:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_ECDSA_SIGN))
                return CKR_OPERATION_NOT_INITIALIZED;

            sigLen = WP11_Ec_SigLen(obj);
            if (pSignature == NULL) {
                *pulSignatureLen = sigLen;
                return CKR_OK;
            }
            if (sigLen > (word32)*pulSignatureLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_Ec_Sign(pData, ulDataLen, pSignature, &sigLen, obj,
                                                 WP11_Session_GetSlot(session));
            *pulSignatureLen = sigLen;
            break;
#endif
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_SIGN))
                return CKR_OPERATION_NOT_INITIALIZED;

            sigLen = WP11_Hmac_SigLen(session);
            if (pSignature == NULL) {
                *pulSignatureLen = sigLen;
                return CKR_OK;
            }
            if (sigLen > (word32)*pulSignatureLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_Hmac_Sign(pData, ulDataLen, pSignature, &sigLen,
                                                                       session);
            *pulSignatureLen = sigLen;
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Continue signing the multi-part data.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  pPart      [in]  Data to sign.
 * @param  ulPartLen  [in]  Length of data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_SignInit has not been
 *          successfully called.
 *          CKR_FUNCTION_FAILED when signing fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_SignUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                   CK_ULONG ulPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_SIGN))
                return CKR_OPERATION_NOT_INITIALIZED;

            ret = WP11_Hmac_Update(pPart, ulPartLen, session);
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Finish signing the multi-part data.
 *
 * @param  hSession         [in]      Handle of session.
 * @param  pSignature       [in]      Buffer to hold signature.
 *                                    NULL indicates length required.
 * @param  pulSignatureLen  [in,out]  On in, length of buffer in bytes.
 *                                    On out, length of signature in
 *                                    bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData or pulSignatureLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_SignInit has not been
 *          successfully called.
 *          CKR_BUFFER_TOO_SMALL when the output length is too small for
 *          signature data.
 *          CKR_FUNCTION_FAILED when signing fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_SignFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature,
                  CK_ULONG_PTR pulSignatureLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_MECHANISM_TYPE mechanism;
    word32 sigLen;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pulSignatureLen == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_SIGN))
                return CKR_OPERATION_NOT_INITIALIZED;

            sigLen = WP11_Hmac_SigLen(session);
            if (pSignature == NULL) {
                *pulSignatureLen = sigLen;
                return CKR_OK;
            }
            if (sigLen > (word32)*pulSignatureLen)
                return CKR_BUFFER_TOO_SMALL;

            ret = WP11_Hmac_SignFinal(pSignature, &sigLen, session);
            *pulSignatureLen = sigLen;
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Initialize signing operation that recovers data from signature.
 * No mechanisms are supported.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 */
CK_RV C_SignRecoverInit(CK_SESSION_HANDLE hSession,
                        CK_MECHANISM_PTR pMechanism,
                        CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    return CKR_MECHANISM_INVALID;
}

/**
 * Sign the data such that the data can be recovered from the signature.
 *
 * @param  hSession         [in]      Handle of session.
 * @param  pData            [in]      Data to sign.
 * @param  ulDataLen        [in]      Length of data in bytes.
 * @param  pSignature       [in]      Buffer to hold signature.
 *                                    NULL indicates length required.
 * @param  pulSignatureLen  [in,out]  On in, length of buffer in bytes.
 *                                    On out, length of signature in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData or pulSignatureLen is NULL, or
 *          ulDataLen is 0.
 *          CKR_OPERATION_NOT_INITIALIZED when C_SignRecoverInit has not been
 *          successfully called.
 */
CK_RV C_SignRecover(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
                    CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
                    CK_ULONG_PTR pulSignatureLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pData == NULL || ulDataLen == 0 || pulSignatureLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pSignature;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Initialize verification operation.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_KEY_TYPE_INCONSISTENT when the key type is not valid for the
 *          mechanism (operation).
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_FUNCTION_FAILED when initializing fails.
 *          CKR_OK on success.
 */
CK_RV C_VerifyInit(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_KEY_TYPE type;
    int init;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    type = WP11_Object_GetType(obj);
    switch (pMechanism->mechanism) {
#ifndef NO_RSA
        case CKM_RSA_PKCS:
            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_RSA_PKCS_VERIFY;
            break;
    #ifdef WC_RSA_PSS
        case CKM_RSA_PKCS_PSS: {
            CK_RSA_PKCS_PSS_PARAMS* params;

            if (type != CKK_RSA)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_RSA_PKCS_PSS_PARAMS))
                return CKR_MECHANISM_PARAM_INVALID;

            params = (CK_RSA_PKCS_PSS_PARAMS*)pMechanism->pParameter;
            ret = WP11_Session_SetPssParams(session, params->hashAlg,
                                                     params->mgf, params->sLen);
            if (ret != 0)
                return CKR_MECHANISM_PARAM_INVALID;
            init = WP11_INIT_RSA_PKCS_PSS_VERIFY;
            break;
        }
    #endif
#endif
#ifdef HAVE_ECC
        case CKM_ECDSA:
            if (type != CKK_EC)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            init = WP11_INIT_ECDSA_VERIFY;
            break;
#endif
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (type != CKK_GENERIC_SECRET)
                return CKR_KEY_TYPE_INCONSISTENT;
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }
            ret = WP11_Hmac_Init(pMechanism->mechanism, obj, session);
            if (ret != 0)
                return CKR_FUNCTION_FAILED;
            init = WP11_INIT_HMAC_VERIFY;
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    WP11_Session_SetMechanism(session, pMechanism->mechanism);
    WP11_Session_SetObject(session, obj);
    WP11_Session_SetOpInitialized(session, init);

    return CKR_OK;
}

/**
 * Verify the single-part data.
 *
 * @param  hSession        [in]  Handle of session.
 * @param  pData           [in]  Data to verify.
 * @param  ulDataLen       [in]  Length of data in bytes.
 * @param  pSignature      [in]  Signature data.
 * @param  ulSignatureLen  [in]  Length of signature in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pData or pSignature is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_VerifyInit has not been
 *          successfully called.
 *          CKR_FUNCTION_FAILED when verification fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_SIGNATURE_INVALID when the signature does not verify the data.
 *          CKR_OK on success.
 */
CK_RV C_Verify(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData,
               CK_ULONG ulDataLen, CK_BYTE_PTR pSignature,
               CK_ULONG ulSignatureLen)
{
    int ret;
    int stat;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pData == NULL || pSignature == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_RSA
        case CKM_RSA_PKCS:
            if (!WP11_Session_IsOpInitialized(session,
                                                   WP11_INIT_RSA_PKCS_VERIFY)) {
                return CKR_OPERATION_NOT_INITIALIZED;
            }

            ret = WP11_RsaPkcs15_Verify(pSignature, ulSignatureLen, pData,
                                                         ulDataLen, &stat, obj);
            break;
    #ifdef WC_RSA_PSS
        case CKM_RSA_PKCS_PSS:
            if (!WP11_Session_IsOpInitialized(session,
                                               WP11_INIT_RSA_PKCS_PSS_VERIFY)) {
                return CKR_OPERATION_NOT_INITIALIZED;
            }

            ret = WP11_RsaPKCSPSS_Verify(pSignature, ulSignatureLen, pData,
                                                ulDataLen, &stat, obj, session);
            break;
    #endif
#endif
#ifdef HAVE_ECC
        case CKM_ECDSA:
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_ECDSA_VERIFY))
                return CKR_OPERATION_NOT_INITIALIZED;

            ret = WP11_Ec_Verify(pSignature, ulSignatureLen, pData, ulDataLen,
                                                                    &stat, obj);
            break;
#endif
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_VERIFY))
                return CKR_OPERATION_NOT_INITIALIZED;

            ret = WP11_Hmac_Verify(pSignature, ulSignatureLen, pData, ulDataLen,
                                                                &stat, session);
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;
    if (stat == 0)
        return CKR_SIGNATURE_INVALID;

    return CKR_OK;
}

/**
 * Continue verifying the multi-part data.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  pPart      [in]  Data to verify.
 * @param  ulPartLen  [in]  Length of data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_VerifyInit has not been
 *          successfully called.
 *          CKR_FUNCTION_FAILED when verification fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_OK on success.
 */
CK_RV C_VerifyUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                     CK_ULONG ulPartLen)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_VERIFY))
                return CKR_OPERATION_NOT_INITIALIZED;

            ret = WP11_Hmac_Update(pPart, ulPartLen, session);
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Finishes verifying the multi-part data.
 *
 * @param  hSession        [in]  Handle of session.
 * @param  pSignature      [in]  Signature data.
 * @param  ulSignatureLen  [in]  Length of signature in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pSignature is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_VerifyInit has not been
 *          successfully called.
 *          CKR_FUNCTION_FAILED when verification fails.
 *          CKR_MECHANISM_INVALID when wrong initialization function was used.
 *          CKR_SIGNATURE_INVALID when the signature does not verify the data.
 *          CKR_OK on success.
 */
CK_RV C_VerifyFinal(CK_SESSION_HANDLE hSession,
                    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
    int ret;
    int stat;
    WP11_Session* session;
    WP11_Object* obj = NULL;
    CK_MECHANISM_TYPE mechanism;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pSignature == NULL)
        return CKR_ARGUMENTS_BAD;

    WP11_Session_GetObject(session, &obj);
    if (obj == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    mechanism = WP11_Session_GetMechanism(session);
    switch (mechanism) {
#ifndef NO_HMAC
    #ifndef NO_MD5
        case CKM_MD5_HMAC:
    #endif
    #ifndef NO_SHA
        case CKM_SHA1_HMAC:
    #endif
    #ifdef WOLFSSL_SHA224
        case CKM_SHA224_HMAC:
    #endif
    #ifndef NO_SHA256
        case CKM_SHA256_HMAC:
    #endif
    #ifdef WOLFSSL_SHA384
        case CKM_SHA384_HMAC:
    #endif
    #ifdef WOLFSSL_SHA512
        case CKM_SHA512_HMAC:
    #endif
            if (!WP11_Session_IsOpInitialized(session, WP11_INIT_HMAC_VERIFY))
                return CKR_OPERATION_NOT_INITIALIZED;

            ret = WP11_Hmac_VerifyFinal(pSignature, ulSignatureLen, &stat,
                                                                       session);
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }
    if (ret < 0)
        return CKR_FUNCTION_FAILED;
    if (stat == 0)
        return CKR_SIGNATURE_INVALID;

    return CKR_OK;
}

/**
 * Initialize verification operation where data is recovered from the signature.
 * No mechanisms are supported.
 *
 * @param  hSession    [in]  Handle of session.
 * @param  pMechanism  [in]  Type of operation to perform with parameters.
 * @param  hKey        [in]  Handle to key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 */
CK_RV C_VerifyRecoverInit(CK_SESSION_HANDLE hSession,
                          CK_MECHANISM_PTR pMechanism,
                          CK_OBJECT_HANDLE hKey)
{
    int ret;
    WP11_Session* session;
    WP11_Object* obj;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    return CKR_MECHANISM_INVALID;
}

/**
 * Verify the signature where the data is recovered from the signature.
 * No mechanisms are supported.
 *
 * @param  hSession        [in]      Handle of session.
 * @param  pSignature      [in]      Signature data.
 * @param  ulSignatureLen  [in]      Length of signature in bytes.
 * @param  pData           [in]      Buffer to hold data that was verified.
 * @param  ulDataLen       [in,out]  On in, length of buffer in bytes.
 *                                   On out, length of data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pSignature or pulDataLen is NULL, or
 *          ulSignatureLen is 0.
 *          CKR_OPERATION_NOT_INITIALIZED when C_VerifyInit has not been
 *          successfully called.
 *          CKR_OK on success.
 */
CK_RV C_VerifyRecover(CK_SESSION_HANDLE hSession,
                      CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen,
                      CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pSignature == NULL || ulSignatureLen == 0 || pulDataLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pData;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Continue digesting and encrypting multi-part data.
 *
 * @param  hSession             [in]      Handle of session.
 * @param  pPart                [in]      Data to digest and encrypt.
 * @param  ulPartLen            [in]      Length of data in bytes.
 * @param  pEncryptedPart       [in]      Buffer to hold encrypted data.
 *                                        NULL indicates length required.
 * @param  pulEncryptedPartLen  [in,out]  On in, length of buffer in bytes.
 *                                        On out, length of encrypted data in
 *                                        bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart or pulEncryptedPartLen is NULL or
 *          ulPartLen is 0.
 *          CKR_OPERATION_NOT_INITIALIZED when C_EncryptInit and C_DigestInit
 *          have not been successfully called.
 */
CK_RV C_DigestEncryptUpdate(CK_SESSION_HANDLE hSession,
                            CK_BYTE_PTR pPart, CK_ULONG ulPartLen,
                            CK_BYTE_PTR pEncryptedPart,
                            CK_ULONG_PTR pulEncryptedPartLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL || ulPartLen == 0 || pulEncryptedPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pEncryptedPart;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Continue decrypting and digesting multi-part data.
 *
 * @param  hSession            [in]      Handle of session.
 * @param  pEncryptedPart      [in]      Data to decrypt and digest.
 * @param  ulEncryptedPartLen  [in]      Length of data in bytes.
 * @param  pPart               [in]      Buffer to hold decrypted data.
 *                                       NULL indicates length required.
 * @param  pulPartLen          [in,out]  On in, length of buffer in bytes.
 *                                       On out, length of decrypted data in
 *                                       bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pEncryptedPart or pulDataLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DecryptInit and C_DigestInit
 *          have not been successfully called.
 */
CK_RV C_DecryptDigestUpdate(CK_SESSION_HANDLE hSession,
                            CK_BYTE_PTR pEncryptedPart,
                            CK_ULONG ulEncryptedPartLen,
                            CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pEncryptedPart == NULL || ulEncryptedPartLen == 0 ||
                                                           pulPartLen == NULL) {
        return CKR_ARGUMENTS_BAD;
    }

    (void)pPart;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Continue signing and encrypting multi-part data.
 *
 * @param  hSession             [in]      Handle of session.
 * @param  pPart                [in]      Data to sign and encrypt.
 * @param  ulPartLen            [in]      Length of data in bytes.
 * @param  pEncryptedPart       [in]      Buffer to hold encrypted data.
 *                                        NULL indicates length required.
 * @param  pulEncryptedPartLen  [in,out]  On in, length of buffer in bytes.
 *                                        On out, length of encrypted data in
 *                                        bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pPart or pulEncryptedPartLen is NULL or
 *          ulPartLen is 0.
 *          CKR_OPERATION_NOT_INITIALIZED when C_EncryptInit and C_SignInit
 *          have not been successfully called.
 */
CK_RV C_SignEncryptUpdate(CK_SESSION_HANDLE hSession,
                          CK_BYTE_PTR pPart, CK_ULONG ulPartLen,
                          CK_BYTE_PTR pEncryptedPart,
                          CK_ULONG_PTR pulEncryptedPartLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pPart == NULL || ulPartLen == 0 || pulEncryptedPartLen == NULL)
        return CKR_ARGUMENTS_BAD;

    (void)pEncryptedPart;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Continue decrypting and verify multi-part data.
 *
 * @param  hSession            [in]      Handle of session.
 * @param  pEncryptedPart      [in]      Data to decrypt and verify.
 * @param  ulEncryptedPartLen  [in]      Length of data in bytes.
 * @param  pPart               [in]      Buffer to hold decrypted data.
 *                                       NULL indicates length required.
 * @param  pulPartLen          [in,out]  On in, length of buffer in bytes.
 *                                       On out, length of decrypted data in
 *                                       bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pEncryptedPart or pulDataLen is NULL.
 *          CKR_OPERATION_NOT_INITIALIZED when C_DecryptInit and C_VerifyInit
 *          have not been successfully called.
 */
CK_RV C_DecryptVerifyUpdate(CK_SESSION_HANDLE hSession,
                            CK_BYTE_PTR pEncryptedPart,
                            CK_ULONG ulEncryptedPartLen,
                            CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
    WP11_Session* session;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pEncryptedPart == NULL || ulEncryptedPartLen == 0 ||
                                                           pulPartLen == NULL) {
        return CKR_ARGUMENTS_BAD;
    }

    (void)pPart;

    return CKR_OPERATION_NOT_INITIALIZED;
}

/**
 * Generate a symmetric key into a new key object.
 *
 * @param  hSession    [in]   Handle of session.
 * @param  pMechanism  [in]   Type of operation to perform with parameters.
 * @param  pTemplate   [in]   Array of attributes to create key object with.
 * @param  ulCount     [in]   Count of array elements.
 * @param  phKey       [out]  Handle to new key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism, pTemplate or phKey is NULL.
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_ATTRIBUTE_VALUE_INVALID when attribute value is not valid for
 *          data type.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when setting an attribute fails.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_OK on success.
 */
CK_RV C_GenerateKey(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism,
                    CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                    CK_OBJECT_HANDLE_PTR phKey)
{
    int ret;
    CK_RV rv;
    WP11_Session* session = NULL;
    WP11_Object* key = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL || pTemplate == NULL || phKey == NULL)
        return CKR_ARGUMENTS_BAD;

    switch (pMechanism->mechanism) {
#ifndef NO_AES
        case CKM_AES_KEY_GEN:
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }

            rv = NewObject(session, CKK_AES, CKO_SECRET_KEY, pTemplate, ulCount,
                                                                          &key);
            if (rv == CKR_OK) {
                ret = WP11_AesGenerateKey(key, WP11_Session_GetSlot(session));
                if (ret != 0) {
                    WP11_Object_Free(key);
                    rv = CKR_FUNCTION_FAILED;
                }
                else
                   rv = AddObject(session, key, pTemplate, ulCount, phKey);
                   if (rv != CKR_OK)
                       WP11_Object_Free(key);
            }
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    return rv;
}


/**
 * Generate a public/private key pair into new key objects.
 *
 * @param  hSession                    [in]   Handle of session.
 * @param  pMechanism                  [in]   Type of operation to perform with
 *                                            parameters.
 * @param  pPublicKeyTemplate          [in]   Array of attributes to create
 *                                            public key object with.
 * @param  ulPublicKeyAttributeCount   [in]   Count of public key attriubues in
 *                                            the array.
 * @param  pPrivateKeyTemplate         [in]   Array of attributes to create
 *                                            private key object with.
 * @param  ulPrivateKeyAttributeCount  [in]   Count of private key attriubues in
 *                                            the array.
 * @param  phPublicKey                 [out]  Handle to new public key object.
 * @param  phPrivateKey                [out]  Handle to new private key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism, pPublicKeyTemplate,
 *          pPrivateKeyTemplate, phPublicKey or phPrivateKey is NULL.
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_ATTRIBUTE_VALUE_INVALID when attribute value is not valid for
 *          data type.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when setting an attribute fails.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_OK on success.
 */
CK_RV C_GenerateKeyPair(CK_SESSION_HANDLE hSession,
                        CK_MECHANISM_PTR pMechanism,
                        CK_ATTRIBUTE_PTR pPublicKeyTemplate,
                        CK_ULONG ulPublicKeyAttributeCount,
                        CK_ATTRIBUTE_PTR pPrivateKeyTemplate,
                        CK_ULONG ulPrivateKeyAttributeCount,
                        CK_OBJECT_HANDLE_PTR phPublicKey,
                        CK_OBJECT_HANDLE_PTR phPrivateKey)
{
    int ret;
    CK_RV rv;
    WP11_Session* session = NULL;
    WP11_Object* pub = NULL;
    WP11_Object* priv = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL || pPublicKeyTemplate == NULL ||
                           pPrivateKeyTemplate == NULL || phPublicKey == NULL ||
                           phPrivateKey == NULL) {
        return CKR_ARGUMENTS_BAD;
    }

    switch (pMechanism->mechanism) {
#if !defined(NO_RSA) && defined(WOLFSSL_KEY_GEN)
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }

            *phPublicKey = *phPrivateKey = CK_INVALID_HANDLE;

            rv = NewObject(session, CKK_RSA, CKO_PUBLIC_KEY, pPublicKeyTemplate,
                           ulPublicKeyAttributeCount, &pub);
            if (rv == CKR_OK) {
                rv = NewObject(session, CKK_RSA, CKO_PRIVATE_KEY,
                                pPrivateKeyTemplate, ulPrivateKeyAttributeCount,
                                &priv);
            }
            if (rv == CKR_OK) {
                ret = WP11_Rsa_GenerateKeyPair(pub, priv,
                                                 WP11_Session_GetSlot(session));
                if (ret != 0)
                    rv = CKR_FUNCTION_FAILED;
            }

            if (rv == CKR_OK) {
                rv = AddObject(session, pub, pPublicKeyTemplate,
                                        ulPublicKeyAttributeCount, phPublicKey);
            }
            if (rv == CKR_OK) {
                rv = AddObject(session, priv, pPrivateKeyTemplate,
                                      ulPrivateKeyAttributeCount, phPrivateKey);
            }
            break;
#endif
#ifdef HAVE_ECC
       case CKM_EC_KEY_PAIR_GEN:
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }

            *phPublicKey = *phPrivateKey = CK_INVALID_HANDLE;

            rv = NewObject(session, CKK_EC, CKO_PUBLIC_KEY, pPublicKeyTemplate,
                                               ulPublicKeyAttributeCount, &pub);
             if (rv == CKR_OK) {
                rv = NewObject(session, CKK_EC, CKO_PRIVATE_KEY,
                                pPrivateKeyTemplate, ulPrivateKeyAttributeCount,
                                &priv);
            }
            if (rv == CKR_OK) {
                ret = WP11_Ec_GenerateKeyPair(pub, priv,
                                                 WP11_Session_GetSlot(session));
                if (ret != 0)
                    rv = CKR_FUNCTION_FAILED;
            }

            if (rv == CKR_OK) {
                rv = AddObject(session, pub, pPublicKeyTemplate,
                                        ulPublicKeyAttributeCount, phPublicKey);
            }
            if (rv == CKR_OK) {
                rv = AddObject(session, priv, pPrivateKeyTemplate,
                                      ulPrivateKeyAttributeCount, phPrivateKey);
            }
            break;
#endif
#ifndef NO_DH
        case CKM_DH_PKCS_KEY_PAIR_GEN:
            if (pMechanism->pParameter != NULL ||
                                              pMechanism->ulParameterLen != 0) {
                return CKR_MECHANISM_PARAM_INVALID;
            }

            *phPublicKey = *phPrivateKey = CK_INVALID_HANDLE;

            rv = NewObject(session, CKK_DH, CKO_PUBLIC_KEY, pPublicKeyTemplate,
                                               ulPublicKeyAttributeCount, &pub);
            if (rv == CKR_OK) {
                rv = NewObject(session, CKK_DH, CKO_PRIVATE_KEY,
                                pPrivateKeyTemplate, ulPrivateKeyAttributeCount,
                                &priv);
            }
            if (rv == CKR_OK) {
                ret = WP11_Dh_GenerateKeyPair(pub, priv,
                                                 WP11_Session_GetSlot(session));
                if (ret != 0)
                    rv = CKR_FUNCTION_FAILED;
            }

            if (rv == CKR_OK) {
                rv = AddObject(session, pub, pPublicKeyTemplate,
                                        ulPublicKeyAttributeCount, phPublicKey);
            }
            if (rv == CKR_OK) {
                rv = AddObject(session, priv, pPrivateKeyTemplate,
                                      ulPrivateKeyAttributeCount, phPrivateKey);
            }
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

    if (rv != CKR_OK && pub != NULL)
        WP11_Object_Free(pub);
    if (rv != CKR_OK && priv != NULL)
        WP11_Object_Free(priv);

    return rv;
}

/**
 * Wrap a key using another key.
 * No mechanisms are supported.
 *
 * @param  hSession          [in]      Handle of session.
 * @param  pMechanism        [in]      Type of operation to perform with
 *                                     parameters.
 * @param  hWrappingKey      [in]      Handle of key to wrap with.
 * @param  hKey              [in]      Handle of key to wrap.
 * @param  pWrappedKey       [in]      Buffer to hold wrapped key.
 * @param  pulWrappedKeyLen  [in,out]  On in, length of buffer.
 *                                     On out, length of wrapped key in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism or pulWrappedKeyLen is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when a key object handle is not valid.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 */
CK_RV C_WrapKey(CK_SESSION_HANDLE hSession,
                CK_MECHANISM_PTR pMechanism,
                CK_OBJECT_HANDLE hWrappingKey, CK_OBJECT_HANDLE hKey,
                CK_BYTE_PTR pWrappedKey,
                CK_ULONG_PTR pulWrappedKeyLen)
{
    int ret;
    WP11_Session* session = NULL;
    WP11_Object* key = NULL;
    WP11_Object* wrappingKey = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL || pulWrappedKeyLen == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hWrappingKey, &wrappingKey);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;
    ret = WP11_Object_Find(session, hKey, &key);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    (void)pWrappedKey;

    return CKR_MECHANISM_INVALID;
}

/**
 * Unwrap a key using a key.
 * No mechanisms are supported.
 *
 * @param  hSession          [in]   Handle of session.
 * @param  pMechanism        [in]   Type of operation to perform with
 *                                  parameters.
 * @param  hUnwrappingKey    [in]   Handle of key to unwrap with.
 * @param  pWrappedKey       [in]   Buffer to hold wrapped key.
 * @param  pulWrappedKeyLen  [in]   Length of wrapped key in bytes.
 * @param  pTemplate         [in]   Array of attributes to create key object
 *                                  with.
 * @param  ulAttributeCount  [in]   Count of array elements.
 * @param  phKey             [out]  Handle of unwrapped key.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism or pulWrappedKeyLen is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when a key object handle is not valid.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 */
CK_RV C_UnwrapKey(CK_SESSION_HANDLE hSession,
                  CK_MECHANISM_PTR pMechanism,
                  CK_OBJECT_HANDLE hUnwrappingKey,
                  CK_BYTE_PTR pWrappedKey, CK_ULONG ulWrappedKeyLen,
                  CK_ATTRIBUTE_PTR pTemplate,
                  CK_ULONG ulAttributeCount,
                  CK_OBJECT_HANDLE_PTR phKey)
{
    int ret;
    WP11_Session* session = NULL;
    WP11_Object* unwrappingKey = NULL;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL || pWrappedKey == NULL || ulWrappedKeyLen == 0 ||
                                           pTemplate == NULL || phKey == NULL) {
        return CKR_ARGUMENTS_BAD;
    }

    ret = WP11_Object_Find(session, hUnwrappingKey, &unwrappingKey);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    (void)ulAttributeCount;

    return CKR_MECHANISM_INVALID;
}

#if defined(HAVE_ECC) || !defined(NO_DH)
/**
 * Determine the key length of the object.
 *
 * @param  obj         [in]   Symmetric key object.
 * @param  len         [in]   Length of data to make key from.
 * @param  symmKeyLen  [out]  Length of symmetric key in bytes.
 * @return  0 on success.
 */
static int SymmKeyLen(WP11_Object* obj, word32 len, word32* symmKeyLen)
{
    int ret;
    word32 valueLen = 0;
    byte data[sizeof(CK_ULONG)];
    CK_ULONG dataLen = sizeof(data);

    ret = WP11_Object_GetAttr(obj, CKA_VALUE_LEN, data, &dataLen);
    if (ret != 0)
        return ret;

    valueLen = *(CK_ULONG*)data;

    switch (WP11_Object_GetType(obj)) {
        case CKK_AES:
        case CKK_GENERIC_SECRET:
        default:
            if (valueLen > 0 && valueLen <= len)
                len = valueLen;
            *symmKeyLen = len;
            break;
    }

    return ret;
}
#endif

/**
 * Generate a symmetric key into a new key object.
 *
 * @param  hSession    [in]   Handle of session.
 * @param  pMechanism  [in]   Type of operation to perform with parameters.
 * @param  hBaseKey    [in]   Handle to base key object.
 * @param  pTemplate   [in]   Array of attributes to create key object with.
 * @param  ulCount     [in]   Count of array elements.
 * @param  phKey       [out]  Handle to new key object.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pMechanism, pTemplate or phKey is NULL.
 *          CKR_OBJECT_HANDLE_INVALID when key object handle is not valid.
 *          CKR_MECHANISM_PARAM_INVALID when mechanism's parameters are not
 *          valid for the operation.
 *          CKR_ATTRIBUTE_VALUE_INVALID when attribute value is not valid for
 *          data type.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when setting an attribute fails.
 *          CKR_MECHANISM_INVALID when the mechanism is not supported with this
 *          type of operation.
 *          CKR_OK on success.
 */
CK_RV C_DeriveKey(CK_SESSION_HANDLE hSession,
                  CK_MECHANISM_PTR pMechanism,
                  CK_OBJECT_HANDLE hBaseKey,
                  CK_ATTRIBUTE_PTR pTemplate,
                  CK_ULONG ulAttributeCount,
                  CK_OBJECT_HANDLE_PTR phKey)
{
    int ret;
    CK_RV rv;
    WP11_Session* session;
    WP11_Object* obj = NULL;
#if defined(HAVE_ECC) || !defined(NO_DH)
    byte* derivedKey = NULL;
    word32 keyLen;
    word32 symmKeyLen;
    unsigned char* secretKeyData[2] = { NULL, NULL };
    CK_ULONG secretKeyLen[2] = { 0, 0 };
#endif

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pMechanism == NULL || pTemplate == NULL || phKey == NULL)
        return CKR_ARGUMENTS_BAD;

    ret = WP11_Object_Find(session, hBaseKey, &obj);
    if (ret != 0)
        return CKR_OBJECT_HANDLE_INVALID;

    switch (pMechanism->mechanism) {
#ifdef HAVE_ECC
        case CKM_ECDH1_DERIVE: {
            CK_ECDH1_DERIVE_PARAMS* params;

            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen != sizeof(CK_ECDH1_DERIVE_PARAMS))
                 return CKR_MECHANISM_PARAM_INVALID;
            params = (CK_ECDH1_DERIVE_PARAMS*)pMechanism->pParameter;
            if (params->pPublicData == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (params->ulPublicDataLen == 0)
                return CKR_MECHANISM_PARAM_INVALID;
            if (params->kdf != CKD_NULL)
                return CKR_MECHANISM_PARAM_INVALID;

            keyLen = params->ulPublicDataLen / 2;
            derivedKey = XMALLOC(keyLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            if (derivedKey == NULL)
                return CKR_DEVICE_MEMORY;

            ret = WP11_EC_Derive(params->pPublicData, params->ulPublicDataLen,
                                                       derivedKey, keyLen, obj);
            if (ret != 0)
                rv = CKR_FUNCTION_FAILED;
            break;
        }
#endif
#ifndef NO_DH
        case CKM_DH_PKCS_DERIVE:
            if (pMechanism->pParameter == NULL)
                return CKR_MECHANISM_PARAM_INVALID;
            if (pMechanism->ulParameterLen == 0)
                return CKR_MECHANISM_PARAM_INVALID;

            keyLen = pMechanism->ulParameterLen;
            derivedKey = XMALLOC(keyLen, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            if (derivedKey == NULL)
                return CKR_DEVICE_MEMORY;

            ret = WP11_Dh_Derive(pMechanism->pParameter,
                                         pMechanism->ulParameterLen, derivedKey,
                                         &keyLen, obj);
            if (ret != 0)
                rv = CKR_FUNCTION_FAILED;
            break;
#endif
        default:
            return CKR_MECHANISM_INVALID;
    }

#if defined(HAVE_ECC) || !defined(NO_DH)
    if (ret == 0) {
        rv = CreateObject(session, pTemplate, ulAttributeCount, &obj);
        if (rv == CKR_OK) {
            ret = SymmKeyLen(obj, keyLen, &symmKeyLen);
            if (ret == 0) {
                /* Only use the bottom part of the secret for the key. */
                secretKeyData[1] = derivedKey + (keyLen - symmKeyLen);
                secretKeyLen[1] = keyLen;
                ret = WP11_Object_SetSecretKey(obj, secretKeyData,
                                                                  secretKeyLen);
                if (ret != 0)
                    rv = CKR_FUNCTION_FAILED;
                if (ret == 0) {
                    rv = AddObject(session, obj, pTemplate, ulAttributeCount,
                                                                         phKey);
                }
            }
        }
    }

    if (derivedKey != NULL) {
        XMEMSET(derivedKey, 0, keyLen);
        XFREE(derivedKey, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }
#endif

    return rv;
}

/**
 * Seed the token's random number generator.
 *
 * @param  hSession   [in]  Handle of session.
 * @param  pSeed      [in]  Seed data.
 * @param  ulSeedLen  [in]  Length of seed data in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pSeed is NULL.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when seeding the random fails.
 *          CKR_OK on success.
 */
CK_RV C_SeedRandom(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed,
                   CK_ULONG ulSeedLen)
{
    int ret;
    WP11_Session* session;
    WP11_Slot* slot;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pSeed == NULL)
        return CKR_ARGUMENTS_BAD;

    slot = WP11_Session_GetSlot(session);
    ret = WP11_Slot_SeedRandom(slot, pSeed, ulSeedLen);
    if (ret == MEMORY_E)
        return CKR_DEVICE_MEMORY;
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

/**
 * Generate random data using token's random number generator.
 *
 * @param  hSession     [in]  Handle of session.
 * @param  pRandomData  [in]  Buffer to hold random data.
 * @param  ulRandomLen  [in]  Length of buffer in bytes.
 * @return  CKR_CRYPTOKI_NOT_INITIALIZED when library not initialized.
 *          CKR_SESSION_HANDLE_INVALID when session handle is not valid.
 *          CKR_ARGUMENTS_BAD when pRandomData is NULL.
 *          CKR_DEVICE_MEMORY when dynamic memory allocation fails.
 *          CKR_FUNCTION_FAILED when generating random data fails.
 *          CKR_OK on success.
 */
CK_RV C_GenerateRandom(CK_SESSION_HANDLE hSession,
                       CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen)
{
    int ret;
    WP11_Session* session;
    WP11_Slot* slot;

    if (!WP11_Library_IsInitialized())
        return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (WP11_Session_Get(hSession, &session) != 0)
        return CKR_SESSION_HANDLE_INVALID;
    if (pRandomData == NULL)
        return CKR_ARGUMENTS_BAD;

    slot = WP11_Session_GetSlot(session);
    ret = WP11_Slot_GenerateRandom(slot, pRandomData, ulRandomLen);
    if (ret == MEMORY_E)
        return CKR_DEVICE_MEMORY;
    if (ret != 0)
        return CKR_FUNCTION_FAILED;

    return CKR_OK;
}

