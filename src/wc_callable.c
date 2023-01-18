#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfboot/wolfboot.h"
#include <stdint.h>


#ifndef WCS_SLOTS
    #define WCS_SLOTS (4)
#endif

#define WCS_MAX_RAW_KEY 256

#define WCS_TYPE_AES 1
#define WCS_TYPE_ECC 2

#define ACCESS_ENCDEC            (1 << 0)
#define ACCESS_SIGN              (1 << 1)
#define ACCESS_VERIFY            (1 << 2)
#define ACCESS_DERIVE            (1 << 3)
#define ACCESS_EXPORT_PUBLIC     (1 << 4)
#define ACCESS_EXPORT_PRIVATE    (1 << 5)
#define ACCESS_WRITE             (1 << 6)
#define ACCESS_USAGE_COUNTER     (1 << 7)
#define ACCESS_VALID_DATE        (1 << 8)

struct wcs_key
{
    word32 id;
    word32 type;
    int in_use;
    size_t size;
    word32 access_flags;
    word32 key_size;
    union wcs_key_type_u {
        byte raw[WCS_MAX_RAW_KEY];
        ecc_key ecc;
        /*  ....  */
    } key;
};

static struct wcs_key WCS_Keys[WCS_SLOTS]  = { };
static WC_RNG wcs_rng;

static int new_slot(void)
{
    int key_slot = 0;
    while (WCS_Keys[key_slot].in_use) {
        key_slot++;
        if (key_slot >= WCS_SLOTS)
            return -1;
    }
    return key_slot;
}


int __attribute__((cmse_nonsecure_entry)) wcs_ecc_keygen(size_t key_size,
        int ecc_curve)
{
    int slot_id;
    struct wcs_key *wk;
    int ret;
    ecc_key *new_key = NULL;
    slot_id = new_slot();
    if (slot_id < 0)
        return -1;
    if (slot_id >= WCS_SLOTS)
        return -1;
    if (WCS_Keys[slot_id].in_use != 0)
        return -1;
    if (WCS_Keys[slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[slot_id].access_flags & ACCESS_WRITE) == 0)
        return -1;

    /* TODO: important: arguments check: key_size/ecc curve */

    wk = &WCS_Keys[slot_id];
    if (wc_ecc_init(new_key) != 0)
        return -1;
    ret = wc_ecc_make_key_ex(&wcs_rng, key_size, new_key, ecc_curve);
    if (ret < 0)
        return -1;
    wk->in_use++;
    memcpy(&wk->key.ecc, new_key, sizeof(ecc_key));
    wk->size = key_size;
    return slot_id;
}

struct wcs_sign_call_params
{
    int slot_id;
    const byte *in;
    word32 inSz;
    byte *out;
    word32 outSz;
    int verify_res;
};

struct wcs_verify_call_params
{
    int slot_id;
    const byte *sig;
    word32 sigSz;
    byte *hash;
    word32 hashSz;
    int verify_res;
};

int __attribute__((cmse_nonsecure_entry)) wcs_ecc_sign_call(struct wcs_sign_call_params *p)
{
    int slot_id = p->slot_id;
    int ret;

    /* TODO: sanity check memory range for param pointer */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[slot_id].in_use == 0)
        return -1;
    if (WCS_Keys[slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[slot_id].access_flags & ACCESS_SIGN) == 0)
        return -1;
    ret = wc_ecc_sign_hash(p->in, p->inSz, p->out, &p->outSz, &wcs_rng, &WCS_Keys[slot_id].key.ecc);
    return ret;
}

int __attribute__((cmse_nonsecure_entry)) wcs_ecc_verify_call(struct wcs_verify_call_params *p)
{
    int slot_id = p->slot_id;
    int ret;

    /* TODO: sanity check memory range for param pointer */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[slot_id].in_use == 0)
        return -1;
    if (WCS_Keys[slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[slot_id].access_flags & ACCESS_VERIFY) == 0)
        return -1;
    ret = wc_ecc_verify_hash(p->sig, p->sigSz, p->hash, p->hashSz, p->verify_res, &WCS_Keys[slot_id].key.ecc);
    return ret;
}

int __attribute__((cmse_nonsecure_entry)) wcs_ecc_getpublic_call(int slot_id, byte *pubkey, word32 *pubkeySz)
{
    int ret;
    word32 x_sz, y_sz;
    x_sz = *pubkeySz / 2;
    y_sz = x_sz;

    /* TODO: sanity check memory range for pubkey/pubkeySz pointers */

    if (slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[slot_id].in_use == 0)
        return -1;
    if (WCS_Keys[slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[slot_id].access_flags & ACCESS_EXPORT_PUBLIC) == 0)
        return -1;

    /* TODO: check bidirectional argument pubkeySz for valid ecc key size */

    ret = wc_ecc_export_public_raw(&WCS_Keys[slot_id].key.ecc, pubkey, &x_sz, pubkey + x_sz, &y_sz);
    if (ret == 0) {
        *pubkeySz = x_sz + y_sz;
    }
    return ret;
}

int __attribute__((cmse_nonsecure_entry)) wcs_ecdh_shared(int privkey_slot_id, int pubkey_slot_id, int shared_slot_id)
{
    int outlen = 256;
    ecc_key *priv, *pub;
    byte outkey[256];
    
    if (privkey_slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[privkey_slot_id].in_use == 0)
        return -1;
    if (WCS_Keys[privkey_slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[privkey_slot_id].access_flags & ACCESS_DERIVE) == 0)
        return -1;

    if (pubkey_slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[pubkey_slot_id].in_use == 0)
        return -1;
    if (WCS_Keys[pubkey_slot_id].type != WCS_TYPE_ECC)
        return -1;
    if ((WCS_Keys[pubkey_slot_id].access_flags & ACCESS_DERIVE) == 0)
        return -1;
    
    if (shared_slot_id > WCS_SLOTS)
        return -1;
    if (WCS_Keys[shared_slot_id].in_use != 0)
        return -1;
    if ((WCS_Keys[shared_slot_id].access_flags & ACCESS_WRITE) == 0)
        return -1;
    if ((WCS_Keys[shared_slot_id].access_flags & ACCESS_ENCDEC) == 0)
        return -1;

    priv = &WCS_Keys[privkey_slot_id].key.ecc;
    pub  = &WCS_Keys[pubkey_slot_id].key.ecc;

    if (wc_ecc_shared_secret(priv, pub, outkey, &outlen) != 0)
        return -1;

    if (outlen > WCS_MAX_RAW_KEY)
        return -1;

    WCS_Keys[shared_slot_id].in_use++;
    WCS_Keys[shared_slot_id].key_size = outlen;
    XMEMCPY(WCS_Keys[shared_slot_id].key.raw, outkey, outlen);
    return 0;
}

int __attribute__((cmse_nonsecure_entry)) wcs_get_random_call(byte *rand, size_t size)
{

}

void wsc_Init(void)
{
    wc_InitRng(&wcs_rng);
}

#endif
