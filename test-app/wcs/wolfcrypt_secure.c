/* wolfcrypt-secure.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include "wolfboot/wc_secure.h"
#define WCS_ECC_MAX_KEY_LEN (66 * 2) /* Up to ECC-521 */
#define WCS_ECC_MAX_SIGN_LEN (66 * 2)

#ifdef HAVE_PK_CALLBACKS
/**
 * \brief Key Gen Callback (used by TLS server)
 */
int wcs_tls_ecc_keygen(WOLFSSL* ssl, ecc_key* key, word32 keySz,
    int ecc_curve, void* ctx)
{
    int err;
    byte pubKeyRaw[WCS_ECC_MAX_KEY_LEN];
    int slot_id;
    int ecc_curve, key_sz;
    (void)ctx; /* not used in kg */

    WOLFSSL_MSG("CreateKeyCb: WC-S");
    /* get curve */
    ecc_curve = info->pk.eckg.curveId;
    key_sz = info->pk.eckg.keySz;

    /* generate new key in secure mode */
    err = wcs_ecc_keygen(ecc_curve, key_sz);
    if (err < 0) {
        WOLFSSL_MSG_EX("wcs_tls_ecc_keygen error: %d\n", rc);
        return WC_HW_E;
    }
    slot_id = err;
    rc = wcs_ecc_getpublic(slot_id, pubKeyRaw, WCS_MAX_PUBKEY_RAW_LEN);
    if (rc < 0) {
        WOLFSSL_MSG_EX("wcs_ecc_getpublic error: %d\n", rc);
        /* TODO: drop key just created */
        //wcs_drop(slot_id);
        rc = WC_HW_E;
        return rc;
    }
    /* load generated public key into key, used by wolfSSL */
    err = wc_ecc_import_unsigned(key, &pubKeyRaw[0], &pubKeyRaw[keySz],
        NULL, ecc_curve);
    return err;
}



static int wcs_tls_ecc_sign(WOLFSSL* ssl,
       const unsigned char* in, unsigned int inSz,
       unsigned char* out, word32* outSz,
       const unsigned char* keyDer, unsigned int keySz,
       void* ctx)
{

    int slot_id = (int)ctx;
    (void)keyDer;
    (void)keySz;
    if (slot_id < 0)
        return BAD_FUNC_ARG;
    ret = wcs_ecc_sign(slot_id, in, inSz, out, outSz);
    return ret;
}

static int wcs_tls_ecc_verify(WOLFSSL *ssl,
       const unsigned char* sig, unsigned int sigSz,
       const unsigned char* hash, unsigned int hashSz,
       const unsigned char* keyDer, unsigned int keySz,
       int* result, void* ctx)
{

    int slot_id = (int)ctx;
    (void)keyDer;
    (void)keySz;
    if (slot_id < 0)
        return BAD_FUNC_ARG;
    ret = wcs_ecc_verify(slot_id, sig, sigSz, hash, hashSz,
            result);
    return ret;
}

static int wcs_tls_ecc_shared_secret(WOLFSSL* ssl, struct ecc_key* otherKey,
        unsigned char* pubKeyDer, word32* pubKeySz,
        unsigned char* out, word32* outlen,
        int side, void* ctx)
{
    int sk_id, pk_id, shared_id;
    ecc_key tmpKey;
    uint8_t pubKey_buf[WCS_ECC_MAX_KEY_LEN];
    int curve_id, keySz;


    if (!otherKey)
        return BAD_FUNC_ARG;

    curve_id = otherKey->dp.id;
    keySz = otherKey->dp.keySz;

    sk_id = (int)ctx;

    /* for client: create and export public key */
    if (side == WOLFSSL_CLIENT_END) {
        /* TLS v1.3 calls key gen already, so don't do it here */
        if (wolfSSL_GetVersion(ssl) < WOLFSSL_TLSV1_3) {
            ret = wcs_ecc_keygen(otherKey->dp.id, otherKey->dp.keySz);
            if (ret < 0)
                return WC_HW_E;
            sk_id = ret;
        } else {
            sk_id = (int)ctx;
        }
        if (wc_ecc_export_public_raw(otherKey, pubKey_buf, keySz,
                    pubKey_buf + keySz, keySz) != 0)
            return BAD_FUNC_ARG;
        ret = wcs_ecc_import_public(otherKey->dp,id, pubKey_buf, keySz);
        if (ret < 0)
            return WC_HW_E;
        pk_id = ret;
    }
    else { /* Server side */
        /* Private key is already present */
        sk_id = (int)ctx;
        /* Import x963 pubkey into tmpKey */
        ret = wc_ecc_import_x963_ex(pubKeyDer, *pubKeySz, &tmpKey,
            otherKey->dp->id);
        if (ret != 0) {
            return BAD_FUNC_ARG;
        }

        ret = wc_ecc_export_public_raw(&tmpKey, pubKey_buf, keySz, pubKey_buf + keySz, keySz);
        if (ret != 0) {
            return WC_HW_E;
        }
        ret = wcs_ecc_import_public(curve_id, pubKey_buf, keySz);
        if (ret < 0) {
            return WC_HW_E;
        }
        pk_id = ret;
        ret = 0;
    }

    ret = wcs_ecdh_shared(sk_id, pk_id, *outlen);
    if (ret < 0)
        return ret;

    shared_id = ret;
    ret = wcs_slot_read(shared_id, out, *outlen);
    if (ret < 0)
        return ret;
    else
        *outlen = ret;
    return 0;
}


#endif /* HAVE_PK_CALLBACKS */

#ifdef WOLF_CRYPTO_CB
#define WCS_DEVID  0x57432D53 /* WC-S */

int wolfSSL_WCS_CryptoDevCb(int devId, wc_CryptoInfo* info, void* ctx)
{
    int rc = CRYPTOCB_UNAVAILABLE;
    int slot_id = (uint32_t) ((uintptr_t)ctx & 0xFFFFFFFF);

    if (info == NULL)
        return BAD_FUNC_ARG;

    if (slot_id < 0)
        return BAD_FUNC_ARG;

    if (devId != WCS_DEVID)
        return BAD_FUNC_ARG;


    if (info->algo_type == WC_ALGO_TYPE_SEED) {
        /* use the WCS hardware for RNG seed */
    #if !defined(WC_NO_RNG) && defined(USE_WCS_RNG_SEED)
        while (info->seed.sz > 0) {
            rc = wcs_get_random(info->seed.seed, info->seed.sz);
            if (rc < 0) {
                return rc;
            }
            info->seed.seed += rc;
            info->seed.sz -= rc;
        }
        rc = 0;
    #else
        rc = CRYPTOCB_UNAVAILABLE;
    #endif
    }
#ifdef HAVE_ECC
    else if (info->algo_type == WC_ALGO_TYPE_PK) {
    #ifdef USE_WCS_VERBOSE
        WCS_INTERFACE_PRINTF("WCS Pk: Type %d\n", info->pk.type);
    #endif
        if (info->pk.type == WC_PK_TYPE_EC_KEYGEN) {
            byte pubKeyRaw[WCS_MAX_PUBKEY_RAW_LEN];
            int ecc_curve, key_sz;

            WOLFSSL_MSG("WCS: ECC KeyGen");

            /* get curve */
            ecc_curve = info->pk.eckg.curveId;
            key_sz = info->pk.eckg.keySz;
            
            /* generate new ephemeral key on device */
            rc = wcs_ecc_keygen(ecc_curve, key_sz);
            if (rc < 0) {
            #ifdef USE_WCS_VERBOSE
                WCS_INTERFACE_PRINTF("wcs_ecc_keygen error: %d\n", rc);
            #endif
                rc = WC_HW_E;
                return rc;
            }
            
            slot_id = rc;

            rc = wcs_ecc_getpublic(slot_id, pubKeyRaw, WCS_MAX_PUBKEY_RAW_LEN);

            if (rc < 0) {
            #ifdef USE_WCS_VERBOSE
                WCS_INTERFACE_PRINTF("wcs_ecc_getpublic error: %d\n", rc);
            #endif
                /* TODO: drop key just created */
                //wcs_drop(slot_id);
                rc = WC_HW_E;
                return rc;
            }
            rc = wc_ecc_import_unsigned(info->pk.eckg.key, pubKeyRaw,
                    pubKeyRaw + key_sz, NULL, ecc_curve);

            if (rc < 0) {
            #ifdef USE_WCS_VERBOSE
                WCS_INTERFACE_PRINTF("wc_ecc_import_unsigned error: %d\n", rc);
            #endif
                return rc;
            }
        }
        else if (info->pk.type == WC_PK_TYPE_ECDSA_SIGN) {
            byte sigRS[WCS_ECC_MAX_SIGN_LEN];
            byte *r, *s;
            word32 inSz = info->pk.eccsign.inlen;
            int key_sz;

            WOLFSSL_MSG("WCS: ECC Sign");

            key_sz = info->pk.eccsign.keySz;

            /* truncate input to match key size */
            if (inSz > key_sz)
                inSz = key_sz;

            rc = wcs_ecc_sign(slot_id, info->pk.eccsign.in, inSz, sigRS,
                    WCS_ECC_MAX_SIGN_LEN);
            if (rc < 0) {
            #ifdef USE_WCS_VERBOSE
                WCS_INTERFACE_PRINTF("wc_ecc_sign error: %d\n", rc);
            #endif
                rc = WC_HW_E;
                return rc;
            }

            /* Convert R and S to signature */
            r = &sigRS[0];
            s = &sigRS[key_sz];
            rc = wc_ecc_rs_raw_to_sig((const byte*)r, key_sz, (const byte*)s,
                key_sz, info->pk.eccsign.out, info->pk.eccsign.outlen);
            if (rc != 0) {
                WOLFSSL_MSG("Error converting RS to Signature");
                return rc;
            }
        }
        else if (info->pk.type == WC_PK_TYPE_ECDSA_VERIFY) {
            /* TODO */
            return CRYPTOCB_UNAVAILABLE;
        }
        else if (info->pk.type == WC_PK_TYPE_ECDH) {
            /* TODO */
            return CRYPTOCB_UNAVAILABLE;
        }
    }
#endif /* HAVE_ECC */

    /* need to return negative here for error */
    if (rc != 0 && rc != CRYPTOCB_UNAVAILABLE) {
        WOLFSSL_MSG("WCS: CryptoCb failed");
    #ifdef USE_WCS_VERBOSE
        WCS_INTERFACE_PRINTF("WCS: CryptoCb failed %d\n", rc);
    #endif
        rc = WC_HW_E;
    }

    return rc;
}

#endif /* WOLF_CRYPTO_CB */
