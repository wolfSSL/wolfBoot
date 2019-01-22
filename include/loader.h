#ifndef LOADER_H
#define LOADER_H

#if defined(WOLFBOOT_SIGN_ED25519)
    extern const unsigned char ed25519_pub_key[];
    extern unsigned int ed25519_pub_key_len;
#   define KEY_BUFFER  ed25519_pub_key
#   define KEY_LEN     ed25519_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (64)
#elif defined(WOLFBOOT_SIGN_ECC256)
    extern const unsigned char ecc256_pub_key[];
    extern unsigned int ecc256_pub_key_len;
#   define KEY_BUFFER  ecc256_pub_key
#   define KEY_LEN     ecc256_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (64)
#else
#   error "No public key available for given signing algorithm."
#endif /* Algorithm selection */


#endif /* LOADER_H */
