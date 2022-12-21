#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "wolfboot/wolfboot.h"
#include <stdint.h>


#ifndef WCSM_SLOTS
    #define WCSM_SLOTS (4)
#endif

struct wcsm_key
{
    uint32_t id;
    uint32_t type;
    int in_use;
    size_t size;
    union wcsm_key_type_u {
        Aes aes;
        ecc_key ecc;
    } key;
};

struct wcsm_key WCSM_Keys[WCSM_SLOTS]  = { };

static int new_slot(void)
{
    int key_slot = 0;
    while (WCSM_Keys[key_slot].in_use) {
        key_slot++;
        if (key_slot >= WCSM_SLOTS)
            return -1;
    }
    return key_slot;
}


int __attribute__((cmse_nonsecure_entry)) wcsm_ecc_keygen(size_t key_size,
        int ecc_curve)
{
    int slot_id;
    struct wcsm_key *wk;
    int ret;
    ecc_key *new_key = NULL;
    WC_RNG *rng = NULL;
    slot_id = new_slot();
    if (slot_id < 0)
        return -1;
    if (slot_id >= WCSM_SLOTS)
        return -1;

    /* TODO: important: arguments check */

    wk = &WCSM_Keys[slot_id];
    if (wc_ecc_init(new_key) != 0)
        return -1;
    ret = wc_ecc_make_key_ex(rng, key_size, new_key, ecc_curve);
    if (ret < 0)
        return -1;
    wk->in_use++;
    memcpy(&wk->key.ecc, new_key, sizeof(ecc_key));
    wk->size = key_size;
    return slot_id;
}

/*
int wcsm_ecc_sign();
int wcsm_ecc_verify();
int wcsm_ecc_getpublic();
int wcsm_ecdh();
int wcsm_aes_encrypt();
int wcsm_aes_decrypt();
*/



int __attribute__((cmse_nonsecure_entry)) nsc_test(void)
{
    return 0;
}



#endif
