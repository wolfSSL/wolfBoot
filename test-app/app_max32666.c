/* app_max32666.c
 *
 * Test application for wolfBoot on MAX32666FTHR
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "hal/max32666.h"

#ifdef WOLFCRYPT_MAX32666_TEST
/* MSDK's mxc_delay.c references SystemCoreClock (CMSIS).
 * wolfBoot configures HIRC96M = 96 MHz in hal_init(). */
uint32_t SystemCoreClock = 96000000;

/* MAX32665/MAX32666 TRNG does not implement on-demand health test in hardware.
 * The MSDK trng_revb driver assumes a 3-register layout (ctrl/status/data) but
 * the actual silicon only has 2 registers (cn/data). Provide a stub so that
 * wolfSSL's wc_GenerateSeed() can proceed. */
int MXC_TRNG_HealthTest(void) { return 0; }

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/port/maxim/max3266x.h>

/*
 * AES-GCM 256-bit known-answer test
 * Test Case 16 from McGrew & Viega GCM spec (same as wolfcrypt test.c)
 */
static int test_aes_gcm_256(void)
{
    Aes aes;
    int ret;
    byte resultC[60];
    byte resultT[16];
    byte resultP[60];

    static const byte key[] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08
    };
    static const byte iv[] = {
        0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
        0xde, 0xca, 0xf8, 0x88
    };
    static const byte plain[] = {
        0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
        0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
        0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
        0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
        0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
        0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
        0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
        0xba, 0x63, 0x7b, 0x39
    };
    static const byte aad[] = {
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xab, 0xad, 0xda, 0xd2
    };
    static const byte expC[] = {
        0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07,
        0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
        0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9,
        0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
        0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d,
        0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
        0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a,
        0xbc, 0xc9, 0xf6, 0x62
    };
    static const byte expT[] = {
        0x76, 0xfc, 0x6e, 0xce, 0x0f, 0x4e, 0x17, 0x68,
        0xcd, 0xdf, 0x88, 0x53, 0xbb, 0x2d, 0x55, 0x1b
    };

    wolfBoot_printf("AES-GCM-256 test: ");

    memset(&aes, 0, sizeof(aes));
    ret = wc_AesGcmSetKey(&aes, key, sizeof(key));
    if (ret != 0) {
        wolfBoot_printf("FAIL (SetKey: %d)\n", ret);
        return ret;
    }

    memset(resultC, 0, sizeof(resultC));
    memset(resultT, 0, sizeof(resultT));
    ret = wc_AesGcmEncrypt(&aes, resultC, plain, sizeof(plain),
                           iv, sizeof(iv), resultT, sizeof(resultT),
                           aad, sizeof(aad));
    if (ret != 0) {
        wolfBoot_printf("FAIL (Encrypt: %d)\n", ret);
        return ret;
    }
    if (memcmp(resultC, expC, sizeof(expC)) != 0) {
        wolfBoot_printf("FAIL (ciphertext mismatch)\n");
        return -1;
    }
    if (memcmp(resultT, expT, sizeof(expT)) != 0) {
        wolfBoot_printf("FAIL (tag mismatch)\n");
        return -1;
    }

    memset(resultP, 0, sizeof(resultP));
    ret = wc_AesGcmDecrypt(&aes, resultP, resultC, sizeof(resultC),
                           iv, sizeof(iv), resultT, sizeof(resultT),
                           aad, sizeof(aad));
    if (ret != 0) {
        wolfBoot_printf("FAIL (Decrypt: %d)\n", ret);
        return ret;
    }
    if (memcmp(resultP, plain, sizeof(plain)) != 0) {
        wolfBoot_printf("FAIL (plaintext mismatch)\n");
        return -1;
    }

    wolfBoot_printf("PASS\n");
    return 0;
}

/*
 * ECDHE P-256 known-answer test
 * Two fixed key pairs; verify shared secret matches expected value.
 * Keys from NIST ECDH test vectors (KAS_ECC_CDH_PrimitiveTest).
 */
static int test_ecdhe_p256(void)
{
    ecc_key keyA, keyB;
    WC_RNG rng;
    int ret;
    byte secretA[32], secretB[32];
    word32 secretALen = sizeof(secretA);
    word32 secretBLen = sizeof(secretB);
    int i;

    /* Party A key pair (dA, QAx, QAy) where QA = dA * G */
    static const char dA[] =
        "c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049c62a9c57862d1433";
    static const char QAx[] =
        "dad0b65394221cf9b051e1feca5787d098dfe637fc90b9ef945d0c3772581180";
    static const char QAy[] =
        "5271a0461cdb8252d61f1c456fa3e59ab1f45b33accf5f58389e0577b8990bb3";

    /* Party B key pair (dB, QBx, QBy) where QB = dB * G */
    static const char dB[] =
        "c6ef9c5d78ae012a011164acb397ce2088685d8f06bf9be0b283ab46476bee53";
    static const char QBx[] =
        "d12dfb5289c8d4f81208b70270398c342296970a0bccb74c736fc7554494bf63";
    static const char QBy[] =
        "56fbf3ca366cc23e8157854c13c58d6aac23f046ada30f8353e74f33039872ab";

    /* Expected shared secret: dA * QB = dB * QA */
    static const byte expSecret[] = {
        0xd6, 0x84, 0x0f, 0x6b, 0x42, 0xf6, 0xed, 0xaf,
        0xd1, 0x31, 0x16, 0xe0, 0xe1, 0x25, 0x65, 0x20,
        0x2f, 0xef, 0x8e, 0x9e, 0xce, 0x7d, 0xce, 0x03,
        0x81, 0x24, 0x64, 0xd0, 0x4b, 0x94, 0x42, 0xde,
    };

    wolfBoot_printf("ECDHE P-256 test: ");

    /* Enable TRNG peripheral clock before wc_InitRng seeds from hardware */
    GCR_PERCKCN1 &= ~GCR_PERCKCN1_TRNGD;

    ret = wc_InitRng(&rng);
    if (ret != 0) {
        wolfBoot_printf("FAIL (InitRng: %d)\n", ret);
        return ret;
    }

    ret = wc_ecc_init(&keyA);
    if (ret != 0) {
        wolfBoot_printf("FAIL (init A: %d)\n", ret);
        wc_FreeRng(&rng);
        return ret;
    }
    ret = wc_ecc_init(&keyB);
    if (ret != 0) {
        wolfBoot_printf("FAIL (init B: %d)\n", ret);
        wc_ecc_free(&keyA);
        wc_FreeRng(&rng);
        return ret;
    }

    wc_ecc_set_rng(&keyA, &rng);
    wc_ecc_set_rng(&keyB, &rng);

    /* Import key A (private + public) */
    ret = wc_ecc_import_raw(&keyA, QAx, QAy, dA, "SECP256R1");
    if (ret != 0) {
        wolfBoot_printf("FAIL (import A: %d)\n", ret);
        goto cleanup;
    }

    /* Import key B (private + public) */
    ret = wc_ecc_import_raw(&keyB, QBx, QBy, dB, "SECP256R1");
    if (ret != 0) {
        wolfBoot_printf("FAIL (import B: %d)\n", ret);
        goto cleanup;
    }

    /* A computes shared secret: dA * QB */
    ret = wc_ecc_shared_secret(&keyA, &keyB, secretA, &secretALen);
    if (ret != 0) {
        wolfBoot_printf("FAIL (shared A: %d)\n", ret);
        goto cleanup;
    }

    /* B computes shared secret: dB * QA */
    ret = wc_ecc_shared_secret(&keyB, &keyA, secretB, &secretBLen);
    if (ret != 0) {
        wolfBoot_printf("FAIL (shared B: %d)\n", ret);
        goto cleanup;
    }

    /* Both sides must agree */
    if (secretALen != secretBLen ||
        memcmp(secretA, secretB, secretALen) != 0) {
        wolfBoot_printf("FAIL (secrets differ)\n");
        ret = -1;
        goto cleanup;
    }

    /* Verify against expected value */
    if (secretALen != sizeof(expSecret) ||
        memcmp(secretA, expSecret, sizeof(expSecret)) != 0) {
        wolfBoot_printf("FAIL (expected mismatch)\n");
        wolfBoot_printf("  got: ");
        for (i = 0; i < (int)secretALen; i++)
            wolfBoot_printf("%02x", secretA[i]);
        wolfBoot_printf("\n");
        ret = -1;
        goto cleanup;
    }

    wolfBoot_printf("PASS\n");
    ret = 0;

cleanup:
    wc_ecc_free(&keyB);
    wc_ecc_free(&keyA);
    wc_FreeRng(&rng);
    return ret;
}
#endif /* WOLFCRYPT_MAX32666_TEST */

#define LED_RED_PIN     (1UL << 29)
#define LED_BLUE_PIN    (1UL << 30)
#define LED_GREEN_PIN   (1UL << 31)

void main(void)
{
    uint32_t version;

    hal_init();

    version = wolfBoot_current_firmware_version();

    if (version == 1) {
        /* Turn on blue LED */
        GPIO0_EN0_SET = LED_BLUE_PIN;    /* configure as GPIO */
        GPIO0_OUT_EN |= LED_BLUE_PIN;    /* enable output */
        GPIO0_OUT_CLR = LED_BLUE_PIN;    /* drive low (LED on) */
    } else {
        /* Turn on green LED */
        GPIO0_EN0_SET = LED_GREEN_PIN;   /* configure as GPIO */
        GPIO0_OUT_EN |= LED_GREEN_PIN;   /* enable output */
        GPIO0_OUT_CLR = LED_GREEN_PIN;   /* drive low (LED on) */
    }

    wolfBoot_printf("MAX32666 Test App v%lu\n", (unsigned long)version);

    /* Mark boot successful to prevent rollback */
    wolfBoot_success();

    wolfBoot_printf("Boot success marked. Version: %lu\n",
        (unsigned long)version);

#ifdef WOLFCRYPT_MAX32666_TEST
    /* Initialize MAX32666 TPU hardware */
    if (wc_MXC_TPU_Init() != 0) {
        wolfBoot_printf("TPU init failed!\n");
    } else {
        wolfBoot_printf("TPU initialized.\n");
        test_aes_gcm_256();
        test_ecdhe_p256();
        wc_MXC_TPU_Shutdown();
    }
#endif

    /* Main loop */
    while (1) {
        __asm__ volatile ("nop");
    }
}
