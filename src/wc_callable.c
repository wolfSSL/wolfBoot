#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfboot/wolfboot.h"
#include "wolfboot/wc_secure.h"
#include "hal.h"
#include <stdint.h>


#ifdef CRYPTO_CB_HSM

/* From linker script, dedicated RAM area in secure mode */
extern uint32_t _keyvault_origin, _keyvault_size;

#ifndef WCS_SLOTS
    #define WCS_SLOTS (8)
#endif

#ifndef WCS_KEY_VAULT_SIZE
#   define WCS_KEY_VAULT_SIZE ((uint32_t)(&_keyvault_size))
#endif

#define KEYVAULT_ALIGN (8)


#define EXAMPLE_KEYVAULT

#ifdef EXAMPLE_KEYVAULT
    #define USE_CERT_BUFFERS_256
    #include <wolfssl/certs_test.h>
#endif


#define WCS_TYPE_FILE 0 /* Generic file support (e.g. TLS Cert) */
#define WCS_TYPE_AES  1 /* AES symmetric key */
#define WCS_TYPE_ECC  2 /* ECC key/keypair */

#define WCS_ACCESS_ENCDEC            (1 << 0)
#define WCS_ACCESS_SIGN              (1 << 1)
#define WCS_ACCESS_VERIFY            (1 << 2)
#define WCS_ACCESS_DERIVE            (1 << 3)
#define WCS_ACCESS_EXPORT_PUBLIC     (1 << 4)
#define WCS_ACCESS_EXPORT_PRIVATE    (1 << 5)
#define WCS_ACCESS_WRITE             (1 << 6)
#define WCS_ACCESS_USAGE_COUNTER     (1 << 7)
#define WCS_ACCESS_VALID_DATE        (1 << 8)
#define WCS_ACCESS_READ              (1 << 9)


#define KEYVAULT_INVALID_ADDRESS ((void *)(0xFFFFFFFF))

#define WCS_MAX_DERIVED_KEY_SIZE (256)

struct wcs_keyvault
{
    uint32_t keyvault_base;
    uint32_t keyvault_off[WCS_SLOTS];
    int slot_used[WCS_SLOTS];
    uint32_t keyvault_top;
};

static struct wcs_keyvault WCS_KV;

struct wcs_key
{
    uint32_t type;
    uint32_t size;
    uint32_t access_flags;
    uint32_t key_size;
    int      provisioned;
    union wcs_key_type_u {
        uint8_t raw[0];
        ecc_key ecc;
        /*  ....  */
    } key;
};

static struct wcs_key *keyvault_get_slot(int i)
{
    uint32_t key_base = WCS_KV.keyvault_base;
    uint32_t key_off;
    struct wcs_key *item;
    if (i > WCS_SLOTS)
        return (void*)KEYVAULT_INVALID_ADDRESS;
    if (WCS_KV.slot_used[i] == 0)
        return (void *)KEYVAULT_INVALID_ADDRESS;
    key_off = WCS_KV.keyvault_off[i];
    if (key_off == (uint32_t)KEYVAULT_INVALID_ADDRESS)
        return (void *)KEYVAULT_INVALID_ADDRESS;
    return (struct wcs_key *)(key_base + key_off);
}

static int keyvault_alloc_slot(int slot, uint32_t wcs_type, uint32_t size)
{
    uint32_t key_base = WCS_KV.keyvault_base;
    uint32_t key_off;
    struct wcs_key *key_in_slot;
    if (slot > WCS_SLOTS)
        return -1;
    key_off = WCS_KV.keyvault_off[slot];
    if (key_off == (uint32_t)KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (WCS_KV.slot_used[slot] != 0)
        return -1;
    WCS_KV.slot_used[slot]++;
    while (WCS_KV.keyvault_top % KEYVAULT_ALIGN)
        WCS_KV.keyvault_top++;
    WCS_KV.keyvault_off[slot] = WCS_KV.keyvault_top;

    if (WCS_KV.keyvault_top + size + sizeof(struct wcs_key) >
            WCS_KEY_VAULT_SIZE)
        return -1;
    key_in_slot = (struct wcs_key *)((WCS_KV.keyvault_base + WCS_KV.keyvault_top));
    key_in_slot->size = size;
    key_in_slot->type = wcs_type;
    WCS_KV.keyvault_top += key_in_slot->size + sizeof(struct wcs_key);
    return 0;
}

static int keyvault_new(uint32_t wcs_type, uint32_t size, uint32_t flags)
{
    int key_slot = 0;
    while (WCS_KV.slot_used[key_slot]) {
        key_slot++;
        if (key_slot >= WCS_SLOTS)
            return -1;
    }
    if (keyvault_alloc_slot(key_slot, wcs_type, size) == 0) {
        struct wcs_key *key_in_slot;
        key_in_slot = keyvault_get_slot(key_slot);
        if (key_in_slot == (void *)KEYVAULT_INVALID_ADDRESS)
            return -1;
        key_in_slot->access_flags = flags;
        return key_slot;
    }
    else
        return -1;
}


static int keyvault_init(void)
{
    int i;
    ecc_key ecc;
    struct wcs_key *item;
    word32 buffer_len;
    (void)item;
    XMEMSET(&WCS_KV, 0, sizeof(WCS_KV));
    WCS_KV.keyvault_base = (uint32_t)&_keyvault_origin;

#ifdef EXAMPLE_KEYVAULT
    /* Slot 0: Certificate stored as raw file, Read-only */
    if (keyvault_alloc_slot(0, WCS_TYPE_FILE, sizeof(ca_ecc_cert_der_256)) != 0) {
        return -1;
    }
    item = keyvault_get_slot(0);
    if (item != (void*)KEYVAULT_INVALID_ADDRESS) {
        item->access_flags = WCS_ACCESS_READ; /* Read only */
    } else {
        return -1;
    }
    XMEMCPY(item->key.raw, ca_ecc_cert_der_256, sizeof(ca_ecc_cert_der_256));
    item->provisioned = 1;
    WCS_KV.slot_used[0]++;


    /* Slot 1: Server key stored as ecc object, can sign and export its public
     * part to NSC
     */
    if (keyvault_alloc_slot(1, WCS_TYPE_ECC, sizeof(ecc_key)) != 0) {
        return -1;
    }
    item = keyvault_get_slot(1);
    if (item != KEYVAULT_INVALID_ADDRESS) {
        item->access_flags = WCS_ACCESS_SIGN | WCS_ACCESS_DERIVE |
            WCS_ACCESS_EXPORT_PUBLIC;
    }else {
        return -1;
    }
    wc_ecc_init(&ecc);
    buffer_len = 0;
    if (wc_EccPrivateKeyDecode(ecc_key_der_256, &buffer_len, &ecc, sizeof_ecc_key_der_256) == 0)
    {
        XMEMCPY(&item->key.ecc, &ecc, sizeof(ecc_key));
        item->provisioned = 1;
        WCS_KV.slot_used[1]++;
    }
    else {
        return -1;
    }

    /* Slot 2: Server certificate (.der file)
     */
    if (keyvault_alloc_slot(2, WCS_TYPE_FILE, sizeof(ca_ecc_cert_der_256)) != 0) {
        return -1;
    }
    item = keyvault_get_slot(0);
    if (item != KEYVAULT_INVALID_ADDRESS) {
        item->access_flags = WCS_ACCESS_READ; /* Read only */
    }
    else {
        return -1;
    }
    XMEMCPY(item->key.raw, serv_ecc_der_256, sizeof(serv_ecc_der_256));
    item->provisioned = 1;
    WCS_KV.slot_used[2]++;

    /* Slot 3: Client public key to authenticate client
     */
    /* TODO */

    /* Slot 4: Pre-allocated, empty ECC key slot, used for public key derivation
     */
    if (keyvault_alloc_slot(4, WCS_TYPE_ECC, sizeof(ecc_key)) != 0) {
        return -1;
    }
    item = keyvault_get_slot(4);
    if (item != KEYVAULT_INVALID_ADDRESS) {
        item->access_flags = WCS_ACCESS_DERIVE | WCS_ACCESS_WRITE |
            WCS_ACCESS_EXPORT_PUBLIC;
    }
    else {
        return -1;
    }
    wc_ecc_init(&ecc);
    item->provisioned = 0;
    WCS_KV.slot_used[4]++;

#endif
    return 0;

}


/* Non-secure callable interface, for access from non-secure domain.
 * This is the base API to access crypto functions from the application
 * using WCS.
 */


int __attribute__((cmse_nonsecure_entry))
wcs_ecc_import_public(int curve_id, uint8_t *pubkey, uint32_t key_size)
{
    int slot_id;
    struct wcs_key *wk;
    int ret;
    ecc_key new_key;

    if ((curve_id < 0) || (wc_ecc_is_valid_idx(curve_id) == 0))
        return ECC_BAD_ARG_E;

    slot_id = keyvault_new(WCS_TYPE_ECC, sizeof(ecc_key),
            WCS_ACCESS_WRITE |
            WCS_ACCESS_DERIVE | WCS_ACCESS_VERIFY | WCS_ACCESS_EXPORT_PUBLIC);
    if (slot_id < 0)
        return -1;
    if (slot_id >= WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;
    wk = keyvault_get_slot(slot_id);
    if (wk == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (wk->type != WCS_TYPE_ECC)
        return -1;
    if ((wk->access_flags & WCS_ACCESS_WRITE) == 0)
        return -1;
    if (wk->size != sizeof(ecc_key))
        return -1;
    if (wc_ecc_init(&new_key) != 0)
        return -1;
    ret = wc_ecc_import_unsigned(&new_key, pubkey, pubkey + key_size,
            NULL, curve_id);
    if (ret < 0)
        return -1;

    memcpy(&wk->key.ecc, &new_key, sizeof(ecc_key));
    wk->size = key_size;
    wk->provisioned++;
    wk->access_flags &= (~WCS_ACCESS_WRITE);
    return slot_id;
}

int __attribute__((cmse_nonsecure_entry))
wcs_ecc_keygen(uint32_t key_size, int ecc_curve)
{
    int slot_id;
    struct wcs_key *wk;
    int ret;
    WC_RNG wcs_rng;
    ecc_key new_key;
    slot_id = keyvault_new(WCS_TYPE_ECC, sizeof(ecc_key),
            WCS_ACCESS_WRITE |
            WCS_ACCESS_DERIVE | WCS_ACCESS_SIGN | WCS_ACCESS_EXPORT_PUBLIC);
    if (slot_id < 0)
        return -1;
    if (slot_id >= WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;
    wk = keyvault_get_slot(slot_id);
    if (wk == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (wk->type != WCS_TYPE_ECC)
        return -1;
    if ((wk->access_flags & WCS_ACCESS_WRITE) == 0)
        return -1;

    if (wk->size != sizeof(ecc_key))
        return -1;
    if (wc_ecc_init(&new_key) != 0)
        return -1;
    wc_InitRng(&wcs_rng);
    ret = wc_ecc_make_key_ex(&wcs_rng, key_size, &new_key, ecc_curve);
    wc_FreeRng(&wcs_rng);
    if (ret < 0)
        return -1;
    memcpy(&wk->key.ecc, &new_key, sizeof(ecc_key));
    wk->size = key_size;
    wk->provisioned++;
    wk->access_flags &= (~WCS_ACCESS_WRITE);
    return slot_id;
}


int __attribute__((cmse_nonsecure_entry))
wcs_ecc_sign_call(struct wcs_sign_call_params *p)
{
    int slot_id = p->slot_id;
    int ret;
    WC_RNG wcs_rng;
    struct wcs_key *sign_key;

    /* TODO: sanity check memory range for param pointer */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;

    sign_key = keyvault_get_slot(slot_id);
    if (sign_key == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (sign_key->type != WCS_TYPE_ECC)
        return -1;
    if ((sign_key->access_flags & WCS_ACCESS_SIGN) == 0)
        return -1;
    wc_InitRng(&wcs_rng);
    ret = wc_ecc_sign_hash(p->in, p->inSz, p->out, (word32 *)&p->outSz, &wcs_rng,
            &sign_key->key.ecc);
    wc_FreeRng(&wcs_rng);
    return ret;
}

int __attribute__((cmse_nonsecure_entry))
wcs_ecc_verify_call(struct wcs_verify_call_params *p)
{
    int slot_id = p->slot_id;
    int ret;
    struct wcs_key *verify_key;

    /* TODO: sanity check memory range for param pointer */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;
    verify_key = keyvault_get_slot(slot_id);
    if (verify_key == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (verify_key->provisioned == 0)
        return -1;
    if (verify_key->type != WCS_TYPE_ECC)
        return -1;
    if (verify_key->size != sizeof(ecc_key))
        return -1;
    if ((verify_key->access_flags & WCS_ACCESS_VERIFY) == 0)
        return -1;
    ret = wc_ecc_verify_hash(p->sig, p->sigSz, p->hash, p->hashSz,
            p->verify_res, &verify_key->key.ecc);
    return ret;
}

int __attribute__((cmse_nonsecure_entry))
wcs_ecc_getpublic(int slot_id, uint8_t *pubkey, uint32_t *pubkeySz)
{
    int ret;
    uint32_t x_sz, y_sz;
    struct wcs_key *ecckey;
    x_sz = *pubkeySz / 2;
    y_sz = x_sz;

    /* TODO: sanity check memory range for pubkey/pubkeySz pointers */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;
    ecckey = keyvault_get_slot(slot_id);
    if (ecckey == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (ecckey->provisioned == 0)
        return -1;
    if (ecckey->type != WCS_TYPE_ECC)
        return -1;
    if (ecckey->size != sizeof(ecc_key))
        return -1;
    if ((ecckey->access_flags & WCS_ACCESS_EXPORT_PUBLIC) == 0)
        return -1;

    /* TODO: check bidirectional argument pubkeySz for valid ecc key size */

    ret = wc_ecc_export_public_raw(&ecckey->key.ecc, pubkey,
            (word32 *)&x_sz, pubkey + x_sz, (word32 *)&y_sz);
    if (ret == 0) {
        *pubkeySz = x_sz + y_sz;
    }
    return ret;
}


int __attribute__((cmse_nonsecure_entry))
wcs_ecdh_shared(int privkey_slot_id, int pubkey_slot_id, word32 outlen)
{
    struct wcs_key *priv, *pub, *shared;
    int shared_slot_id = -1;
    byte outkey[WCS_MAX_DERIVED_KEY_SIZE];

    if (privkey_slot_id > WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[pubkey_slot_id] == 0)
        return -1;
    if (WCS_KV.slot_used[privkey_slot_id] == 0)
        return -1;

    priv = keyvault_get_slot(privkey_slot_id);
    pub = keyvault_get_slot(pubkey_slot_id);


    if ((priv == KEYVAULT_INVALID_ADDRESS) || (pub == KEYVAULT_INVALID_ADDRESS))
        return -1;
    if ((priv->provisioned == 0) || (pub->provisioned == 0))
        return -1;
    if ((priv->type != WCS_TYPE_ECC) || (pub->type != WCS_TYPE_ECC))
        return -1;
    if ((priv->size != sizeof(ecc_key)) || (pub->size != sizeof(ecc_key)))
        return -1;
    if (((priv->access_flags & WCS_ACCESS_DERIVE) == 0) ||
            ((pub->access_flags & WCS_ACCESS_DERIVE) == 0)) {
        return -1;
    }

    shared_slot_id = keyvault_new(WCS_TYPE_AES, outlen,
            WCS_ACCESS_WRITE | WCS_ACCESS_READ | WCS_ACCESS_ENCDEC
            );

    if (shared_slot_id < 0)
        return -1;

    if (WCS_KV.slot_used[shared_slot_id] == 0)
        return -1;

    shared = keyvault_get_slot(shared_slot_id);
    if (shared == KEYVAULT_INVALID_ADDRESS)
        return -1;
    if (shared->provisioned != 0)
        return -1;
    if ((shared->access_flags & WCS_ACCESS_WRITE) == 0)
        return -1;
    if ((shared->access_flags & WCS_ACCESS_ENCDEC) == 0)
        return -1;
    if (shared->size < outlen)
        return -1;
    if (WCS_MAX_DERIVED_KEY_SIZE < outlen)
        return -1;
    if (wc_ecc_shared_secret(&priv->key.ecc, &pub->key.ecc, outkey, &outlen) != 0)
        return -1;

    XMEMCPY(&shared->key.raw, outkey, outlen);
    shared->provisioned = 1;
    return shared_slot_id;
}

int __attribute__((cmse_nonsecure_entry))
wcs_slot_read(int slot_id, uint8_t *buffer, uint32_t len)
{
    struct wcs_key *item;
    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_KV.slot_used[slot_id] == 0)
        return -1;

    /* TODO: sanity check memory range for param buffer */

    item = keyvault_get_slot(slot_id);
    if (item == KEYVAULT_INVALID_ADDRESS)
        return -1;

    if (item->provisioned == 0)
        return -1;

    if (item->type != WCS_TYPE_FILE)
        return -1;

    if ((item->access_flags & WCS_ACCESS_READ) == 0)
        return -1;

    if (item->size > len)
        return -1;
    if (item->size < len)
        len = item->size;

    XMEMCPY(buffer, &item->key.raw, len);
    return len;
}
#endif /* CRYPTO_CB_HSM */

int __attribute__((cmse_nonsecure_entry))
wcs_get_random(uint8_t *rand, uint32_t size)
{
    int ret;
    WC_RNG wcs_rng;
    wc_InitRng(&wcs_rng);
    ret = wc_RNG_GenerateBlock(&wcs_rng, rand, size);
    wc_FreeRng(&wcs_rng);
    return ret;
}


void wcs_Init(void)
{
    hal_trng_init();
#ifdef CRYPTO_CB_HSM
    keyvault_init();
#endif
}

#endif
