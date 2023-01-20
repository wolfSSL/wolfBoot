#ifndef WOLFBOOT_SECURE_CALLS_INCLUDED
#define WOLFBOOT_SECURE_CALLS_INCLUDED
#include <stdint.h>


/* Data types shared between wolfBoot and the non-secure application */

#ifdef WOLFCRYPT_SECURE_MODE
struct wcs_sign_call_params
{
    int slot_id;
    const uint8_t *in;
    uint32_t inSz;
    uint8_t *out;
    uint32_t outSz;
    int verify_res;
};

struct wcs_verify_call_params
{
    int slot_id;
    const uint8_t *sig;
    uint32_t sigSz;
    uint8_t *hash;
    uint32_t hashSz;
    int verify_res;
};
#endif


#ifdef WOLFBOOT_SECURE_CALLS

/* Secure calls prototypes for the non-secure world */
int __attribute__((cmse_nonsecure_entry)) wcs_ecc_import_public(int slot_id,
        uint8_t *pubkey, uint32_t key_size, int curve_id);

int __attribute__((cmse_nonsecure_entry)) wcs_ecc_keygen(uint32_t key_size,
        int ecc_curve);
int __attribute__((cmse_nonsecure_entry)) wcs_ecc_getpublic(int slot_id,
        uint8_t *pubkey, uint32_t *pubkeySz);
int __attribute__((cmse_nonsecure_entry)) wcs_ecdh_shared(int privkey_slot_id,
        int pubkey_slot_id, int shared_slot_id);
int __attribute__((cmse_nonsecure_entry)) wcs_get_random(uint8_t *rand,
        uint32_t size);

int __attribute__((cmse_nonsecure_entry))
    wcs_ecc_sign_call(struct wcs_sign_call_params *p);
int __attribute__((cmse_nonsecure_entry))
    wcs_ecc_verify_call(struct wcs_verify_call_params *p);

#endif /* WOLFBOOT_SECURE_CALLS */


#endif
