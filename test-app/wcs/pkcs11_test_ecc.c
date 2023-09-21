/* pkcs11_test_ecc.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
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


#include "user_settings.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_pkcs11.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/logging.h>

#define wolfBoot_printf(...) do{}while(0)

static WC_RNG rng;

#ifdef HAVE_ECC
/* ./certs/ecc-client-key.der, ECC */
static const unsigned char ecc_clikey_der_256[] =
{
    0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20, 0xF8,
    0xCF, 0x92, 0x6B, 0xBD, 0x1E, 0x28, 0xF1, 0xA8,
    0xAB, 0xA1, 0x23, 0x4F, 0x32, 0x74, 0x18, 0x88,
    0x50, 0xAD, 0x7E, 0xC7, 0xEC, 0x92, 0xF8, 0x8F,
    0x97, 0x4D, 0xAF, 0x56, 0x89, 0x65, 0xC7, 0xA0,
    0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D,
    0x03, 0x01, 0x07, 0xA1, 0x44, 0x03, 0x42, 0x00,
    0x04, 0x55, 0xBF, 0xF4, 0x0F, 0x44, 0x50, 0x9A,
    0x3D, 0xCE, 0x9B, 0xB7, 0xF0, 0xC5, 0x4D, 0xF5,
    0x70, 0x7B, 0xD4, 0xEC, 0x24, 0x8E, 0x19, 0x80,
    0xEC, 0x5A, 0x4C, 0xA2, 0x24, 0x03, 0x62, 0x2C,
    0x9B, 0xDA, 0xEF, 0xA2, 0x35, 0x12, 0x43, 0x84,
    0x76, 0x16, 0xC6, 0x56, 0x95, 0x06, 0xCC, 0x01,
    0xA9, 0xBD, 0xF6, 0x75, 0x1A, 0x42, 0xF7, 0xBD,
    0xA9, 0xB2, 0x36, 0x22, 0x5F, 0xC7, 0x5D, 0x7F,
    0xB4
};
static const int sizeof_ecc_clikey_der_256 = sizeof(ecc_clikey_der_256);

/* ./certs/ecc-client-keyPub.der, ECC */
static const unsigned char ecc_clikeypub_der_256[] =
{
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE,
        0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D,
        0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x55, 0xBF, 0xF4,
        0x0F, 0x44, 0x50, 0x9A, 0x3D, 0xCE, 0x9B, 0xB7, 0xF0, 0xC5,
        0x4D, 0xF5, 0x70, 0x7B, 0xD4, 0xEC, 0x24, 0x8E, 0x19, 0x80,
        0xEC, 0x5A, 0x4C, 0xA2, 0x24, 0x03, 0x62, 0x2C, 0x9B, 0xDA,
        0xEF, 0xA2, 0x35, 0x12, 0x43, 0x84, 0x76, 0x16, 0xC6, 0x56,
        0x95, 0x06, 0xCC, 0x01, 0xA9, 0xBD, 0xF6, 0x75, 0x1A, 0x42,
        0xF7, 0xBD, 0xA9, 0xB2, 0x36, 0x22, 0x5F, 0xC7, 0x5D, 0x7F,
        0xB4
};
static const int sizeof_ecc_clikeypub_der_256 = sizeof(ecc_clikeypub_der_256);


int decode_private_key(ecc_key* key, int devId)
{
    int    ret;
    word32 idx;

    wolfBoot_printf( "Decode ECC Private Key\n");
    ret = wc_ecc_init_ex(key, NULL, devId);
    if (ret != 0) {
        wolfBoot_printf( "Failed to initialize ECC key: %d\n", ret);
    }
    if (ret == 0) {
        idx = 0;
        ret = wc_EccPrivateKeyDecode(ecc_clikey_der_256, &idx, key,
                                     (word32)sizeof_ecc_clikey_der_256);
        if (ret != 0)
            wolfBoot_printf( "Failed to decode private key: %d\n", ret);
    }

    return ret;
}

int decode_public_key(ecc_key* key, int devId)
{
    int ret;
    word32 idx;

    wolfBoot_printf( "Decode ECC Public Key\n");
    ret = wc_ecc_init_ex(key, NULL, devId);
    if (ret != 0) {
        wolfBoot_printf( "Failed to initialize ECC key: %d\n", ret);
    }
    if (ret == 0) {
        idx = 0;
        ret = wc_EccPublicKeyDecode(ecc_clikeypub_der_256, &idx, key,
                                    (word32)sizeof_ecc_clikeypub_der_256);
        if (ret != 0)
            wolfBoot_printf( "Failed to decode public key: %d\n", ret);
    }

    return ret;
}

int ecdsa_sign_verify(int devId)
{
    int     ret = 0;
    byte    hash[32], sig[128];
    word32  hashSz, sigSz;
    int     verify;
    ecc_key eccPriv;
    ecc_key eccPub;

    memset(hash, 9, sizeof(hash));
    hashSz = sizeof(hash);
    sigSz = sizeof(sig);

    ret = decode_private_key(&eccPriv, devId);
    if (ret == 0) {
        wolfBoot_printf( "Sign with ECC Keys\n");
        ret = wc_ecc_sign_hash(hash, hashSz, sig, &sigSz, &rng, &eccPriv);
        if (ret < 0)
            wolfBoot_printf( "Failed to sign: %d\n", ret);
        wc_ecc_free(&eccPriv);
    }

    if (ret == 0) {
        ret = decode_public_key(&eccPub, devId);
        if (ret == 0) {
            wolfBoot_printf( "Verify with ECC Keys\n");
            ret = wc_ecc_verify_hash(sig, sigSz, hash, (int)hashSz, &verify,
                                                                       &eccPub);
            if (ret < 0 || !verify)
                wolfBoot_printf( "Failed to verify: %d (%d)\n", ret, verify);
            if (!verify)
                ret = -1;
            wc_ecc_free(&eccPub);
        }
    }

    return ret;
}
#endif

#if 0
int main(int argc, char* argv[])
{
    int ret;
    const char* library;
    const char* slot;
    const char* tokenName;
    const char* userPin;
    Pkcs11Dev dev;
    Pkcs11Token token;
    int slotId;
    int devId = 1;

    if (argc != 4 && argc != 5) {
        wolfBoot_printf(
                "Usage: pkcs11_ecc <libname> <slot> <tokenname> [userpin]\n");
        return 1;
    }

    library = argv[1];
    slot = argv[2];
    tokenName = argv[3];
    userPin = (argc == 4) ? NULL : argv[4];
    slotId = atoi(slot);

#if defined(DEBUG_WOLFSSL)
    wolfSSL_Debugging_ON();
#endif
    wolfCrypt_Init();

    ret = wc_Pkcs11_Initialize(&dev, library, NULL);
    if (ret != 0) {
        wolfBoot_printf( "Failed to initialize PKCS#11 library\n");
        ret = 2;
    }
    if (ret == 0) {
        ret = wc_Pkcs11Token_Init(&token, &dev, slotId, tokenName,
            (byte*)userPin, userPin == NULL ? 0 : strlen(userPin));
        if (ret != 0) {
            wolfBoot_printf( "Failed to initialize PKCS#11 token\n");
            ret = 2;
        }
        if (ret == 0) {
            ret = wc_CryptoDev_RegisterDevice(devId, wc_Pkcs11_CryptoDevCb,
                                              &token);
            if (ret != 0) {
                wolfBoot_printf( "Failed to register PKCS#11 token\n");
                ret = 2;
            }
            if (ret == 0) {
                wc_InitRng_ex(&rng, NULL, devId);

            #ifdef HAVE_ECC
                ret = ecdsa_sign_verify(devId);
                if (ret != 0)
                    ret = 1;
            #endif

                wc_FreeRng(&rng);
            }
            wc_Pkcs11Token_Final(&token);
        }
        wc_Pkcs11_Finalize(&dev);
    }

    wolfCrypt_Cleanup();

    return ret;
}
#endif

