/* app_mcxn.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/random.h"
#endif

#ifdef WOLFCRYPT_TZ_PSA
#include "psa/crypto.h"
#include "psa/error.h"
#include "psa/initial_attestation.h"
#include "wolfssl/wolfcrypt/types.h"
#endif

extern void hal_init(void);

static void gpio_init_output(GPIO_Type *gpio, PORT_Type *port,
                             clock_ip_name_t gpio_clock,
                             clock_ip_name_t port_clock, uint32_t pin,
                             uint8_t initial_level)
{
    const port_pin_config_t pin_config = {
        .pullSelect = kPORT_PullDisable,
#if defined(FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE) && FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE
        .pullValueSelect = kPORT_LowPullResistor,
#endif
#if defined(FSL_FEATURE_PORT_HAS_SLEW_RATE) && FSL_FEATURE_PORT_HAS_SLEW_RATE
        .slewRate = kPORT_FastSlewRate,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PASSIVE_FILTER) && FSL_FEATURE_PORT_HAS_PASSIVE_FILTER
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_OPEN_DRAIN) && FSL_FEATURE_PORT_HAS_OPEN_DRAIN
        .openDrainEnable = kPORT_OpenDrainDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH
        .driveStrength = kPORT_LowDriveStrength,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1
        .driveStrength1 = kPORT_LowDriveStrength,
#endif
        .mux = kPORT_MuxAlt0,
#if defined(FSL_FEATURE_PORT_HAS_INPUT_BUFFER) && FSL_FEATURE_PORT_HAS_INPUT_BUFFER
        .inputBuffer = kPORT_InputBufferEnable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_INVERT_INPUT) && FSL_FEATURE_PORT_HAS_INVERT_INPUT
        .invertInput = kPORT_InputNormal,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK) && FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK
        .lockRegister = kPORT_UnlockRegister
#endif
    };
    const gpio_pin_config_t gpio_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = initial_level,
    };

    CLOCK_EnableClock(gpio_clock);
    CLOCK_EnableClock(port_clock);
    GPIO_PinInit(gpio, pin, &gpio_config);
    PORT_SetPinConfig(port, pin, &pin_config);
}

#ifdef WOLFCRYPT_TZ_PSA
#define LINE_LEN 16

static void print_hex(const uint8_t *buffer, uint32_t length, int dumpChars)
{
    uint32_t i, sz;

    if (!buffer) {
        wolfBoot_printf("\tNULL\r\n");
        return;
    }

    while (length > 0) {
        sz = length;
        if (sz > LINE_LEN)
            sz = LINE_LEN;

        wolfBoot_printf("\t");
        for (i = 0; i < LINE_LEN; i++) {
            if (i < sz)
                wolfBoot_printf("%02x ", buffer[i]);
            else
                wolfBoot_printf("   ");
        }
        if (dumpChars) {
            wolfBoot_printf("| ");
            for (i = 0; i < sz; i++) {
                if (buffer[i] > 31 && buffer[i] < 127)
                    wolfBoot_printf("%c", buffer[i]);
                else
                    wolfBoot_printf(".");
            }
        }
        wolfBoot_printf("\r\n");

        buffer += sz;
        length -= sz;
    }
}
#endif

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)

/* Read a CBOR bstr at *offset; sets *data and *data_len, advances *offset. */
static int cbor_get_bstr(const uint8_t *buf, size_t buf_len, size_t *offset,
                          const uint8_t **data, size_t *data_len)
{
    uint8_t b;
    size_t  len;
    if (*offset >= buf_len) return -1;
    b = buf[(*offset)++];
    if ((b >> 5) != 2) return -1; /* must be CBOR major type 2 (bstr) */
    switch (b & 0x1F) {
        case 24:
            if (*offset >= buf_len) return -1;
            len = buf[(*offset)++]; break;
        case 25:
            if (*offset + 2 > buf_len) return -1;
            len = ((size_t)buf[*offset] << 8) | buf[*offset + 1];
            *offset += 2; break;
        default:
            if ((b & 0x1F) > 23) return -1; /* rejects AI>=26 */
            len = b & 0x1F;
    }
    if (*offset + len > buf_len) return -1;
    *data = buf + *offset;
    *data_len = len;
    *offset += len;
    return 0;
}

/* Maximum fixed bytes in a COSE_Sign1 Sig_Structure before variable content:
 *   1 (array(4) tag) + 1 (tstr(10) tag) + 10 ("Signature1") +
 *   3 (max bstr hdr for prot) + 1 (empty-AAD 0x40) + 3 (max bstr hdr for payload)
 */
#define COSE_SIGN1_TBS_FIXED_OVERHEAD  (1 + 1 + 10 + 3 + 1 + 3)

/* Write a CBOR bstr length header into buf; returns number of bytes written. */
static int cbor_put_bstr_hdr(uint8_t *buf, size_t len)
{
    if (len <= 23) {
        buf[0] = 0x40 | (uint8_t)len;
        return 1;
    }
    if (len <= 0xFF) {
        buf[0] = 0x58; buf[1] = (uint8_t)len;
        return 2;
    }
    if (len > 0xFFFF)
        return -1;
    buf[0] = 0x59; buf[1] = (uint8_t)(len >> 8);
    buf[2] = (uint8_t)len;
    return 3;
}

static int run_attest_verify_test(void)
{
    static const uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
    };
    static uint8_t token[1024];
    static uint8_t tbs[1024]; /* Sig_Structure buffer */
    uint8_t        pubkey[65];
    uint8_t        hash[32];
    size_t         token_len = sizeof(token);
    size_t         pubkey_len = sizeof(pubkey);
    size_t         hash_len = 0;
    size_t         tbs_len = 0;
    const uint8_t *prot_bytes, *payload_bytes, *sig_bytes;
    size_t         prot_len, payload_len, sig_len;
    size_t         off = 0;
    int            hdr;
    psa_status_t   st;
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t   key_id;

    wolfBoot_printf("Start attestation verify test\r\n");

    /* 1. Generate token — also caches the IAK public key on the secure side */
    st = psa_initial_attest_get_token(challenge, sizeof(challenge),
                                      token, sizeof(token), &token_len);
    if (st != PSA_SUCCESS) {
        wolfBoot_printf("attest_verify: get_token failed (%d)\r\n", (int)st);
        return -1;
    }
    wolfBoot_printf("attest_verify: token_len=%lu\r\n", (unsigned long)token_len);
    print_hex(token, (uint32_t)token_len, 1);

    /* 2. Retrieve IAK public key via PSA attestation service (read-once) */
    st = psa_initial_attest_get_iak_pubkey(pubkey, sizeof(pubkey), &pubkey_len);
    if (st != PSA_SUCCESS) {
        wolfBoot_printf("attest_verify: get_iak_pubkey failed (%d)\r\n", (int)st);
        return -1;
    }
    wolfBoot_printf("attest_verify: IAK pubkey (%lu bytes):\r\n",
                    (unsigned long)pubkey_len);
    print_hex(pubkey, (uint32_t)pubkey_len, 1);

    /* 3. Parse COSE_Sign1: 84 | prot_bstr | A0 | payload_bstr | sig_bstr */
    if (off >= token_len || token[off++] != 0x84) {
        wolfBoot_printf("attest_verify: bad array tag\r\n"); return -1;
    }
    if (cbor_get_bstr(token, token_len, &off, &prot_bytes, &prot_len) != 0 ||
        off >= token_len || token[off++] != 0xA0 ||
        cbor_get_bstr(token, token_len, &off, &payload_bytes, &payload_len) != 0 ||
        cbor_get_bstr(token, token_len, &off, &sig_bytes, &sig_len) != 0 ||
        sig_len != 64) {
        wolfBoot_printf("attest_verify: COSE parse failed\r\n"); return -1;
    }

    /* 4. Reconstruct Sig_Structure = ["Signature1", prot_bstr, b"", payload_bstr] */
    if (COSE_SIGN1_TBS_FIXED_OVERHEAD + prot_len + payload_len > sizeof(tbs)) {
        wolfBoot_printf("attest_verify: tbs buffer too small\r\n");
        return -1;
    }
    tbs[tbs_len++] = 0x84;                                       /* array(4) */
    tbs[tbs_len++] = 0x6A;                                       /* tstr(10) */
    memcpy(tbs + tbs_len, "Signature1", 10); tbs_len += 10;
    hdr = cbor_put_bstr_hdr(tbs + tbs_len, prot_len);
    if (hdr < 0) { wolfBoot_printf("attest_verify: cbor hdr failed\r\n"); return -1; }
    tbs_len += hdr;
    memcpy(tbs + tbs_len, prot_bytes, prot_len);
    tbs_len += prot_len;
    tbs[tbs_len++] = 0x40;                                       /* bstr(0): empty AAD */
    hdr = cbor_put_bstr_hdr(tbs + tbs_len, payload_len);
    if (hdr < 0) { wolfBoot_printf("attest_verify: cbor hdr failed\r\n"); return -1; }
    tbs_len += hdr;
    memcpy(tbs + tbs_len, payload_bytes, payload_len);
    tbs_len += payload_len;

    /* 5. SHA-256 hash of Sig_Structure */
    if (psa_hash_compute(PSA_ALG_SHA_256, tbs, tbs_len,
                         hash, sizeof(hash), &hash_len) != PSA_SUCCESS) {
        wolfBoot_printf("attest_verify: hash failed\r\n");
        return -1;
    }

    /* 6. Import IAK public key and verify ES256 signature (raw R||S, 64 bytes) */
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attrs, 256);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    if (psa_import_key(&attrs, pubkey, pubkey_len, &key_id) != PSA_SUCCESS) {
        psa_reset_key_attributes(&attrs);
        wolfBoot_printf("attest_verify: import_key failed\r\n");
        return -1;
    }
    psa_reset_key_attributes(&attrs);
    st = psa_verify_hash(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                         hash, hash_len, sig_bytes, sig_len);
    psa_destroy_key(key_id);

    if (st != PSA_SUCCESS) {
        wolfBoot_printf("attest_verify: FAILED (%d)\r\n", (int)st);
        return -1;
    }
    wolfBoot_printf("attest_verify: IAK signature verified OK\r\n");
    return 0;
}
#endif

#ifdef WOLFCRYPT_SECURE_MODE
static void print_random_number(void)
{
    uint8_t rnd;
    int ret;

    ret = wcs_get_random(&rnd, sizeof(rnd));
    if (ret != 0)
        wolfBoot_printf("Random number: generate failed (%d)\n", ret);
    else
        wolfBoot_printf("Today's lucky number: 0x%02x\n", rnd);
}
#endif

void main(void)
{
    uint32_t boot_ver;

    hal_init();

#ifdef TZEN
    boot_ver = wolfBoot_nsc_current_firmware_version();
#else
    boot_ver = wolfBoot_current_firmware_version();
#endif

    wolfBoot_printf("Hello from firmware version %d\n", boot_ver);

#ifdef WOLFCRYPT_SECURE_MODE
    print_random_number();
#endif

#ifdef WOLFCRYPT_TZ_PSA
    {
        psa_status_t psa_ret = psa_crypto_init();

        if (psa_ret == PSA_SUCCESS)
            wolfBoot_printf("PSA crypto init ok\r\n");
        else
            wolfBoot_printf("PSA crypto init failed (%d)\r\n", (int)psa_ret);
    }
#endif

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)
    (void)run_attest_verify_test();
#endif

    if (boot_ver == 1) {
        /* Red off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 10U, 1U);
        /* Green off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 27U, 1U);
        /* Blue on */
        gpio_init_output(GPIO1, PORT1, kCLOCK_Gpio1, kCLOCK_Port1, 2U, 0U);
    }
    else {
        /* Red off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 10U, 1U);
        /* Green on */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 27U, 0U);
        /* Blue off */
        gpio_init_output(GPIO1, PORT1, kCLOCK_Gpio1, kCLOCK_Port1, 2U, 1U);

#ifdef TZEN
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
    }

    while (1) {
        __asm__ volatile ("wfi");
    }
}
