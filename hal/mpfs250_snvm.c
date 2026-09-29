/* mpfs250_snvm.c
 *
 * PolarFire SoC secure NVM (sNVM) and SRAM-PUF key material for wolfBoot: a
 * keystore backend that serves the trust anchor from sNVM, a PUF-derived key
 * encryption key with RFC 3394 AES key-wrap, and the image-encryption key
 * provider that unwraps a PUF-wrapped AES key stored in sNVM.
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
#include <string.h>
#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "hal.h"
#include "hal/mpfs250.h"
#include "hal/mpfs250_snvm.h"
#include "printf.h"
#include "encrypt.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/memory.h> /* wc_ForceZero */

#if defined(SNVM_KEYSTORE) && !defined(WOLFBOOT_NO_SIGN)

static uint8_t snvm_page_cache[SNVM_KEYSTORE_PAGE_DATA];
static uint8_t snvm_slot_cache[SIZEOF_KEYSTORE_SLOT];

/* Read from the logical keystore image (header + slots) spanning consecutive
 * sNVM modules.  0 on success, -1 on read error or out-of-range module. */
static int snvm_keystore_read(uint32_t offset, uint8_t *out, uint32_t len)
{
    uint32_t got = 0;
    uint32_t mod, moff, n;

    while (got < len) {
        mod  = (uint32_t)SNVM_KEYSTORE_MODULE +
               ((offset + got) / SNVM_KEYSTORE_PAGE_DATA);
        moff = (offset + got) % SNVM_KEYSTORE_PAGE_DATA;
        if (mod >= (uint32_t)MPFS_SNVM_MODULE_MAX) {
            return -1;
        }
        if (mpfs_snvm_read((uint8_t)mod, NULL, NULL, snvm_page_cache,
                SNVM_KEYSTORE_PAGE_DATA) != 0) {
            return -1;
        }
        n = SNVM_KEYSTORE_PAGE_DATA - moff;
        if (n > (len - got)) {
            n = len - got;
        }
        memcpy(out + got, snvm_page_cache + moff, n);
        got += n;
    }
    return 0;
}

int keystore_num_pubkeys(void)
{
    uint8_t hdr_buf[SNVM_KEYSTORE_HDR_SIZE];
    struct snvm_keystore_hdr *hdr = (struct snvm_keystore_hdr *)hdr_buf;

    if (snvm_keystore_read(0, hdr_buf, SNVM_KEYSTORE_HDR_SIZE) != 0) {
        return 0;
    }
    if (memcmp(hdr->magic, SNVM_KEYSTORE_MAGIC, 8) != 0) {
        return 0;
    }
    if (hdr->item_count > SNVM_KEYSTORE_MAX_PUBKEYS) {
        return 0;
    }
    return (int)hdr->item_count;
}

/* Load slot id into the static cache; returns NULL on any error. */
static struct keystore_slot *snvm_load_slot(int id)
{
    if ((id < 0) || (id >= keystore_num_pubkeys())) {
        return (struct keystore_slot *)0;
    }
    if (snvm_keystore_read(SNVM_KEYSTORE_HDR_SIZE +
            ((uint32_t)id * SIZEOF_KEYSTORE_SLOT), snvm_slot_cache,
            SIZEOF_KEYSTORE_SLOT) != 0) {
        return (struct keystore_slot *)0;
    }
    return (struct keystore_slot *)snvm_slot_cache;
}

uint8_t *keystore_get_buffer(int id)
{
    struct keystore_slot *slot = snvm_load_slot(id);
    if (slot == NULL) {
        return (uint8_t *)0;
    }
    return slot->pubkey;
}

int keystore_get_size(int id)
{
    struct keystore_slot *slot = snvm_load_slot(id);
    if (slot == NULL) {
        return -1;
    }
    /* Reject an over-large size from a corrupt/mis-provisioned slot so callers
     * cannot read past the fixed-size cache. */
    if (slot->pubkey_size > KEYSTORE_PUBKEY_SIZE) {
        return -1;
    }
    return (int)slot->pubkey_size;
}

uint32_t keystore_get_mask(int id)
{
    struct keystore_slot *slot = snvm_load_slot(id);
    if (slot == NULL) {
        return 0;
    }
    return slot->part_id_mask;
}

uint32_t keystore_get_key_type(int id)
{
    struct keystore_slot *slot = snvm_load_slot(id);
    if (slot == NULL) {
        return (uint32_t)-1;
    }
    return slot->key_type;
}

#endif /* SNVM_KEYSTORE && !WOLFBOOT_NO_SIGN */

#if defined(SNVM_KEYSTORE_PROVISION) && !defined(WOLFBOOT_NO_SIGN)

/* Write the compiled-in keystore into sNVM page by page.  Run once; an
 * SNVM_KEYSTORE build then serves the trust anchor from sNVM.  Public keys only. */
int snvm_keystore_provision(void)
{
    static uint8_t img[SNVM_KEYSTORE_MAX_MODULES * SNVM_KEYSTORE_PAGE_DATA];
    struct snvm_keystore_hdr *hdr = (struct snvm_keystore_hdr *)img;
    struct keystore_slot slot;
    uint8_t *pub;
    uint32_t total, modules, m, off;
    int n, i, sz;

    n = keystore_num_pubkeys();
    if (n <= 0) {
        wolfBoot_printf("snvm provision: no compiled keys\n");
        return -1;
    }
    total = SNVM_KEYSTORE_HDR_SIZE + ((uint32_t)n * SIZEOF_KEYSTORE_SLOT);
    if (total > sizeof(img)) {
        wolfBoot_printf("snvm provision: image too large (%u)\n",
            (unsigned)total);
        return -1;
    }

    memset(img, 0, sizeof(img));
    memcpy(hdr->magic, SNVM_KEYSTORE_MAGIC, 8);
    hdr->item_count = (uint16_t)n;
    hdr->flags = 0;
    hdr->version = 0;

    for (i = 0; i < n; i++) {
        memset(&slot, 0, sizeof(slot));
        sz = keystore_get_size(i);
        pub = keystore_get_buffer(i);
        if ((sz < 0) || (pub == NULL)) {
            return -1;
        }
        slot.slot_id      = (uint32_t)i;
        slot.key_type     = keystore_get_key_type(i);
        slot.part_id_mask = keystore_get_mask(i);
        slot.pubkey_size  = (uint32_t)sz;
        memcpy(slot.pubkey, pub, (uint32_t)sz);
        memcpy(&img[SNVM_KEYSTORE_HDR_SIZE + ((uint32_t)i * SIZEOF_KEYSTORE_SLOT)],
            &slot, SIZEOF_KEYSTORE_SLOT);
    }

    modules = (total + SNVM_KEYSTORE_PAGE_DATA - 1u) / SNVM_KEYSTORE_PAGE_DATA;
    for (m = 0; m < modules; m++) {
        off = m * SNVM_KEYSTORE_PAGE_DATA;
        if (mpfs_snvm_write(SYS_SERV_CMD_SNVM_WRITE_PLAIN,
                (uint8_t)(SNVM_KEYSTORE_MODULE + m), &img[off], NULL) != 0) {
            wolfBoot_printf("snvm provision: write module %u failed\n",
                (unsigned)(SNVM_KEYSTORE_MODULE + m));
            return -1;
        }
    }
    wolfBoot_printf("snvm provision: wrote %d key(s) in %u module(s)\n", n,
        (unsigned)modules);
    return 0;
}

#endif /* SNVM_KEYSTORE_PROVISION && !WOLFBOOT_NO_SIGN */

#ifdef SNVM_KEK

/* Fixed PUF challenge.  Any constant works: the same challenge returns the
 * same device-unique response across cold boots, anchoring a stable KEK. */
static const uint8_t snvm_kek_challenge[MPFS_PUF_CHALLENGE_LEN] = {
    0x77, 0x6f, 0x6c, 0x66, 0x42, 0x6f, 0x6f, 0x74,
    0x4b, 0x45, 0x4b, 0x76, 0x31, 0x00, 0x00, 0x00
};
/* Domain-separation label hashed with the PUF response. */
static const char snvm_kek_label[] = "wolfBoot-sNVM-KEK-v1";

int mpfs_puf_kek(uint8_t *kek)
{
    uint8_t   resp[MPFS_PUF_RESPONSE_LEN];
    uint8_t   digest[WC_SHA384_DIGEST_SIZE];
    wc_Sha384 sha;
    int       ret;

    if (kek == NULL) {
        return -1;
    }
    ret = mpfs_puf_emulation(snvm_kek_challenge, 0, resp);
    if (ret != 0) {
        return -1;
    }
    ret = wc_InitSha384(&sha);
    if (ret == 0) {
        ret = wc_Sha384Update(&sha, (const byte *)snvm_kek_label,
            (word32)(sizeof(snvm_kek_label) - 1));
        if (ret == 0) {
            ret = wc_Sha384Update(&sha, resp, (word32)sizeof(resp));
        }
        if (ret == 0) {
            ret = wc_Sha384Final(&sha, digest);
        }
        wc_Sha384Free(&sha);
        /* the context buffered the whole sub-block message, PUF response included */
        wc_ForceZero(&sha, sizeof(sha));
    }
    /* KEK = first 256 bits of SHA-384(label || PUF response). */
    if (ret == 0) {
        memcpy(kek, digest, SNVM_KEK_LEN);
    }
    wc_ForceZero(resp, sizeof(resp));
    wc_ForceZero(digest, sizeof(digest));
    return (ret == 0) ? 0 : -1;
}

int snvm_kek_wrap(const uint8_t *key, uint32_t keysz, uint8_t *out,
    uint32_t outsz)
{
    uint8_t kek[SNVM_KEK_LEN];
    int     ret;

    if ((key == NULL) || (out == NULL)) {
        return -1;
    }
    if (mpfs_puf_kek(kek) != 0) {
        return -1;
    }
    ret = wc_AesKeyWrap(kek, (word32)sizeof(kek), key, keysz, out, outsz, NULL);
    wc_ForceZero(kek, sizeof(kek));
    return ret;
}

int snvm_kek_unwrap(const uint8_t *in, uint32_t insz, uint8_t *key,
    uint32_t keysz)
{
    uint8_t kek[SNVM_KEK_LEN];
    int     ret;

    if ((in == NULL) || (key == NULL)) {
        return -1;
    }
    if (mpfs_puf_kek(kek) != 0) {
        return -1;
    }
    ret = wc_AesKeyUnWrap(kek, (word32)sizeof(kek), in, insz, key, keysz, NULL);
    wc_ForceZero(kek, sizeof(kek));
    return ret;
}


#endif /* SNVM_KEK */

#if defined(CUSTOM_ENCRYPT_KEY) && defined(SNVM_KEK)

/* sNVM module SNVM_ENCKEY_MODULE holds: [wrapped AES key (40)][nonce (N)]. */
#define SNVM_ENCKEY_BLOB_LEN (SNVM_KEK_WRAPPED_LEN + ENCRYPT_NONCE_SIZE)
#if SNVM_ENCKEY_BLOB_LEN > MPFS_SNVM_PLAIN_DATA_LEN
#error "wrapped key + nonce does not fit one plaintext sNVM page"
#endif

#if (ENCRYPT_KEY_SIZE != SNVM_KEK_AESKEY_LEN)
#error "SNVM PUF encrypt-key provider expects a 256-bit (AES-256) image key"
#endif

/* Read the wrapped AES key + nonce from sNVM, unwrap the key with the PUF KEK
 * and return both.  Returns 0 on success. */
int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    uint8_t page[MPFS_SNVM_PLAIN_DATA_LEN];
    int     ret;

    if ((key == NULL) || (nonce == NULL)) {
        return -1;
    }
    ret = mpfs_snvm_read(SNVM_ENCKEY_MODULE, NULL, NULL, page,
        MPFS_SNVM_PLAIN_DATA_LEN);
    if (ret != 0) {
        wolfBoot_printf("encrypt-key: sNVM read failed (%d)\n", ret);
        return -1;
    }
    ret = snvm_kek_unwrap(page, SNVM_KEK_WRAPPED_LEN, key, ENCRYPT_KEY_SIZE);
    if (ret != (int)ENCRYPT_KEY_SIZE) {
        wolfBoot_printf("encrypt-key: PUF unwrap failed (%d)\n", ret);
        wc_ForceZero(page, sizeof(page));
        return -1;
    }
    memcpy(nonce, page + SNVM_KEK_WRAPPED_LEN, ENCRYPT_NONCE_SIZE);
    wc_ForceZero(page, sizeof(page));
    return 0;
}

/* Provisioned out-of-band, so runtime set/erase are unused by the decrypt
 * path.  Neither can be honoured at runtime, so both fail closed: an update
 * flow that relies on them must not believe a key was stored or erased. */
int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce)
{
    (void)key;
    (void)nonce;
    return -1;
}

int wolfBoot_erase_encrypt_key(void)
{
    return -1;
}

#ifdef SNVM_ENCKEY_PROVISION
/* One-time: wrap the AES key with the device PUF KEK and store
 * [wrapped key][nonce] in sNVM.  It must match the sign tool's key file.
 * The key comes from snvm_enckey_prov_key/nonce, defined by the integrator in
 * another object (SNVM_ENCKEY_PROVISION_EXTERN), or from the built-in public
 * test vector, which needs SNVM_ENCKEY_INSECURE_TEST_KEY as acknowledgement. */
#if defined(SNVM_ENCKEY_PROVISION_EXTERN)
extern const uint8_t snvm_enckey_prov_key[SNVM_KEK_AESKEY_LEN];
extern const uint8_t snvm_enckey_prov_nonce[ENCRYPT_NONCE_SIZE];
#define prov_aes_key   snvm_enckey_prov_key
#define prov_aes_nonce snvm_enckey_prov_nonce
#elif !defined(SNVM_ENCKEY_INSECURE_TEST_KEY)
#error "SNVM_ENCKEY_PROVISION writes a key into sNVM permanently. Define \
SNVM_ENCKEY_PROVISION_EXTERN and provide snvm_enckey_prov_key/nonce, or define \
SNVM_ENCKEY_INSECURE_TEST_KEY to provision the built-in public test vector."
#else
/* Publicly known test vector.  A device provisioned with this has no image
 * confidentiality, and sNVM cannot be rewritten to undo it. */
static const uint8_t prov_aes_key[SNVM_KEK_AESKEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};
static const uint8_t prov_aes_nonce[ENCRYPT_NONCE_SIZE] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};
#endif

int snvm_enckey_provision(void)
{
    uint8_t page[MPFS_SNVM_PLAIN_DATA_LEN];
    int     ret, i;

    for (i = 0; i < (int)sizeof(page); i++) {
        page[i] = 0;
    }
    ret = snvm_kek_wrap(prov_aes_key, (uint32_t)sizeof(prov_aes_key), page,
        SNVM_KEK_WRAPPED_LEN);
    if (ret != SNVM_KEK_WRAPPED_LEN) {
        wolfBoot_printf("enckey provision: wrap failed (%d)\n", ret);
        wc_ForceZero(page, sizeof(page));
        return -1;
    }
    memcpy(page + SNVM_KEK_WRAPPED_LEN, prov_aes_nonce, ENCRYPT_NONCE_SIZE);
    ret = mpfs_snvm_write(SYS_SERV_CMD_SNVM_WRITE_PLAIN, SNVM_ENCKEY_MODULE,
        page, NULL);
    wolfBoot_printf("enckey provision: sNVM write[%d] ret=%d\n",
        SNVM_ENCKEY_MODULE, ret);
    wc_ForceZero(page, sizeof(page));
    return ret;
}
#endif /* SNVM_ENCKEY_PROVISION */

#endif /* CUSTOM_ENCRYPT_KEY && SNVM_KEK */
