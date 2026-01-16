#include "wolfboot/wolfboot.h"
#include "image.h"

int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    int i;
    /* Test key: "0123456789abcdef0123456789abcdef" (32 bytes for AES-256) */
    const char test_key[] = "0123456789abcdef0123456789abcdef";
    /* Test nonce: "0123456789abcdef" (16 bytes) */
    const char test_nonce[] = "0123456789abcdef";

    for (i = 0; i < ENCRYPT_KEY_SIZE && i < (int)sizeof(test_key); i++) {
        key[i] = (uint8_t)test_key[i];
    }
    for (i = 0; i < ENCRYPT_NONCE_SIZE && i < (int)sizeof(test_nonce); i++) {
        nonce[i] = (uint8_t)test_nonce[i];
    }
    return 0;
}