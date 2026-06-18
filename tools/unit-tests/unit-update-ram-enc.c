/* unit-update-ram-enc.c
 *
 * Unit tests for wolfBoot_ram_decrypt() (EXT_ENCRYPTED + MMU ramboot path).
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

/*
 * Regression coverage for the bounds check in wolfBoot_ram_decrypt().
 *
 * EXT_ENCRYPTED uses an unauthenticated stream cipher (no MAC), so the image
 * length field, read from the decrypted-but-not-yet-authenticated header, is
 * attacker-influenceable: a single flipped ciphertext bit changes the decrypted
 * 'len'. These tests confirm an oversized 'len' (including the 32-bit (len +
 * IMAGE_HEADER_SIZE) wrap value) is rejected, while a legitimately-sized image
 * still decrypts correctly.
 */

#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#include "target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "encrypt.h"
#include "libwolfboot.c"
#include <check.h>
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/chacha.h>

Suite *wolfboot_suite(void);

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

/* Needed only to satisfy the non-fixed-partition dualboot path libwolfboot.c
 * compiles under WOLFBOOT_NO_PARTITIONS (unit-update-ram-enc-nopart build).
 * The tests call wolfBoot_ram_decrypt() directly and never reach these. */
void* hal_get_primary_address(void)
{
    return (void *)(uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void *)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}

/* Provision a deterministic, valid (not all-0x00 / all-0xFF) key + nonce and
 * initialize the crypto so that encrypt_iv_nonce mirrors the boot-time state. */
static void setup_crypto_key(void)
{
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    unsigned int i;

    for (i = 0; i < ENCRYPT_KEY_SIZE; i++)
        key[i] = (uint8_t)(i + 1);
    for (i = 0; i < ENCRYPT_NONCE_SIZE; i++)
        nonce[i] = (uint8_t)(0xA0 + i);

    encrypt_initialized = 0;
    ck_assert_int_eq(wolfBoot_set_encrypt_key(key, nonce), 0);
    ck_assert_int_eq(crypto_init(), 0);
}

/* Encrypt 'size' bytes of 'plain' into 'enc' using exactly the per-block IV
 * schedule that decrypt_header()/wolfBoot_ram_decrypt() use to decrypt, so the
 * produced ciphertext round-trips through the function under test. 'size' must
 * be a multiple of ENCRYPT_BLOCK_SIZE. */
static void encrypt_blob(uint8_t *enc, const uint8_t *plain, uint32_t size)
{
    uint32_t off;
    for (off = 0; off < size; off += ENCRYPT_BLOCK_SIZE) {
        wolfBoot_crypto_set_iv(encrypt_iv_nonce, off / ENCRYPT_BLOCK_SIZE);
        crypto_encrypt(enc + off, plain + off, ENCRYPT_BLOCK_SIZE);
    }
}

/* Build a plaintext header (magic + chosen length field), zero-padded to one
 * IMAGE_HEADER_SIZE, then encrypt it into 'enc'. Enough to reach the length
 * check in wolfBoot_ram_decrypt(); the body is irrelevant for the reject path. */
static void make_encrypted_header(uint8_t *enc, uint32_t len)
{
    uint8_t plain[IMAGE_HEADER_SIZE];
    uint32_t magic = WOLFBOOT_MAGIC;

    memset(plain, 0, sizeof(plain));
    memcpy(plain, &magic, sizeof(magic));
    memcpy(plain + sizeof(uint32_t), &len, sizeof(len));
    encrypt_blob(enc, plain, IMAGE_HEADER_SIZE);
}

/* Attacker primitive, NO key required. XOR a bit-mask into the ciphertext bytes
 * that cover the length field (header offset sizeof(uint32_t)). The cipher is a
 * stream cipher (ciphertext = keystream XOR plaintext), so each flipped
 * ciphertext bit flips the same bit of the decrypted length: the decrypted
 * 'len' becomes (original_len XOR mask), with no knowledge of the key, the
 * keystream, or even the original length. 'mask' is applied in the same byte
 * order the header stores the length, so (len ^ mask) holds on any endianness. */
static void flip_len_ciphertext_bits(uint8_t *enc, uint32_t mask)
{
    uint8_t mask_bytes[sizeof(uint32_t)];
    unsigned int i;

    memcpy(mask_bytes, &mask, sizeof(mask));
    for (i = 0; i < sizeof(uint32_t); i++)
        enc[sizeof(uint32_t) + i] ^= mask_bytes[i];
}

#define DST_CANARY 0x5A
/* A valid, in-bounds image decrypts successfully and matches the plaintext, and
 * the copy must touch ONLY the image region: everything past it is canaried
 * before the call and verified untouched afterwards. */
START_TEST(test_ram_decrypt_valid)
{
    const uint32_t len = 1024;
    const uint32_t total = IMAGE_HEADER_SIZE + len;
    uint8_t *plain = malloc(total);
    uint8_t *enc = malloc(total);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    uint32_t magic = WOLFBOOT_MAGIC;
    uint32_t touched_past = 0;
    uint32_t i;
    int ret;

    ck_assert_ptr_nonnull(plain);
    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);
    ck_assert_uint_eq(total % ENCRYPT_BLOCK_SIZE, 0);

    for (i = 0; i < total; i++)
        plain[i] = (uint8_t)(i * 7 + 3);
    memcpy(plain, &magic, sizeof(magic));
    memcpy(plain + sizeof(uint32_t), &len, sizeof(len));

    setup_crypto_key();
    encrypt_blob(enc, plain, total);

    /* Canary the whole destination so any write past the image is detectable. */
    memset(dst, DST_CANARY, WOLFBOOT_PARTITION_SIZE);

    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, 0);
    ck_assert_mem_eq(dst, plain, total);

    /* Memory after the IMAGE_HEADER_SIZE + len image bytes must be untouched. */
    for (i = total; i < WOLFBOOT_PARTITION_SIZE; i++) {
        if (dst[i] != DST_CANARY)
            touched_past++;
    }
    ck_assert_uint_eq(touched_past, 0);

    free(plain);
    free(enc);
    free(dst);
}
END_TEST

/* An oversized length must be rejected before the copy loop runs. Without the
 * bounds check this writes IMAGE_HEADER_SIZE + len bytes into a dst sized for a
 * maximum valid image, overrunning it (caught by ASan under ASAN=1). */
START_TEST(test_ram_decrypt_oversize_rejected)
{
    const uint32_t len = WOLFBOOT_PARTITION_SIZE; /* > max valid payload */
    /* src large enough to feed the (unfixed) loop without a source overread,
     * so the destination overflow is the signal ASan would report. */
    uint8_t *enc = malloc(WOLFBOOT_PARTITION_SIZE + 2 * IMAGE_HEADER_SIZE);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    int ret;

    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);
    memset(enc, 0, WOLFBOOT_PARTITION_SIZE + 2 * IMAGE_HEADER_SIZE);

    setup_crypto_key();
    make_encrypted_header(enc, len);

    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, -1);

    free(enc);
    free(dst);
}
END_TEST

/* The specific length that makes (len + IMAGE_HEADER_SIZE) wrap to 0 on 32-bit
 * unsigned math must also be rejected (previously it returned 0 having copied
 * nothing, reporting success on a bogus image). */
START_TEST(test_ram_decrypt_overflow_len_rejected)
{
    const uint32_t len = (uint32_t)(0u - IMAGE_HEADER_SIZE); /* len + HDR == 0 */
    uint8_t *enc = malloc(2 * IMAGE_HEADER_SIZE);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    int ret;

    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);
    memset(enc, 0, 2 * IMAGE_HEADER_SIZE);

    setup_crypto_key();
    make_encrypted_header(enc, len);

    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, -1);

    free(enc);
    free(dst);
}
END_TEST

/* Realistic threat model: the attacker cannot encrypt (no key) but can write to
 * the encrypted partition. Starting from a legitimately-encrypted, correctly-
 * sized image, they flip ciphertext bits in the length field to forge an
 * arbitrarily large decrypted 'len'.  */
START_TEST(test_ram_decrypt_len_bitflip_rejected)
{
    const uint32_t real_len = 1024;            /* legitimate image length */
    const uint32_t mask = 0x80000000u;         /* flip only the top bit of len */
    const uint32_t total = IMAGE_HEADER_SIZE + real_len;
    uint8_t *plain = malloc(total);
    uint8_t *enc = malloc(total);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    uint32_t magic = WOLFBOOT_MAGIC;
    uint32_t decoded_len = 0;
    uint32_t i;
    int ret;

    ck_assert_ptr_nonnull(plain);
    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);

    /* Owner builds a legitimate, correctly-sized encrypted image. */
    for (i = 0; i < total; i++)
        plain[i] = (uint8_t)(i * 7 + 3);
    memcpy(plain, &magic, sizeof(magic));
    memcpy(plain + sizeof(uint32_t), &real_len, sizeof(real_len));

    setup_crypto_key();
    encrypt_blob(enc, plain, total);

    /* Attacker tampers with the ciphertext only (no key, no plaintext). */
    flip_len_ciphertext_bits(enc, mask);

    /* Verify the malleation worked (this decrypt uses the key purely to inspect
     * the result; the attack above used only the ciphertext): the magic is
     * still valid and the decrypted length is now the forged, oversized value. */
    ck_assert_int_eq(decrypt_header(enc), 0);
    decoded_len = *((uint32_t*)(dec_hdr + sizeof(uint32_t)));
    ck_assert_uint_eq(decoded_len, real_len ^ mask);
    ck_assert(decoded_len > (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE));

    /* The forged length must be rejected before the copy loop runs. */
    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, -1);

    free(plain);
    free(enc);
    free(dst);
}
END_TEST

/* The maximum in-bounds payload, matching the branch the bound check uses. */
#ifdef WOLFBOOT_FIXED_PARTITIONS
#  define RAM_DECRYPT_MAX_PAYLOAD (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE)
#elif defined(WOLFBOOT_RAMBOOT_MAX_SIZE)
#  define RAM_DECRYPT_MAX_PAYLOAD WOLFBOOT_RAMBOOT_MAX_SIZE
#endif

/* The exact maximum in-bounds length must decrypt successfully. Paired with the
 * one-block-over test below this brackets the accept/reject boundary, locking
 * the comparison's off-by-one ('>' vs '>=', +/- IMAGE_HEADER_SIZE). */
START_TEST(test_ram_decrypt_max_valid)
{
    const uint32_t len = RAM_DECRYPT_MAX_PAYLOAD;
    const uint32_t total = IMAGE_HEADER_SIZE + len;
    uint8_t *plain = malloc(total);
    uint8_t *enc = malloc(total);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    uint32_t magic = WOLFBOOT_MAGIC;
    uint32_t i;
    int ret;

    ck_assert_ptr_nonnull(plain);
    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);
    ck_assert_uint_eq(total % ENCRYPT_BLOCK_SIZE, 0);
    ck_assert(total <= WOLFBOOT_PARTITION_SIZE);

    for (i = 0; i < total; i++)
        plain[i] = (uint8_t)(i * 7 + 3);
    memcpy(plain, &magic, sizeof(magic));
    memcpy(plain + sizeof(uint32_t), &len, sizeof(len));

    setup_crypto_key();
    encrypt_blob(enc, plain, total);

    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, 0);
    ck_assert_mem_eq(dst, plain, total);

    free(plain);
    free(enc);
    free(dst);
}
END_TEST

/* One block past the maximum in-bounds length must be rejected before the copy
 * loop runs (the reject side of the boundary pair). */
START_TEST(test_ram_decrypt_one_over_rejected)
{
    const uint32_t len = RAM_DECRYPT_MAX_PAYLOAD + ENCRYPT_BLOCK_SIZE;
    uint8_t *enc = malloc(2 * IMAGE_HEADER_SIZE);
    uint8_t *dst = malloc(WOLFBOOT_PARTITION_SIZE);
    int ret;

    ck_assert_ptr_nonnull(enc);
    ck_assert_ptr_nonnull(dst);
    memset(enc, 0, 2 * IMAGE_HEADER_SIZE);

    setup_crypto_key();
    make_encrypted_header(enc, len);

    ret = wolfBoot_ram_decrypt(enc, dst);
    ck_assert_int_eq(ret, -1);

    free(enc);
    free(dst);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-ram-decrypt");
    TCase *valid = tcase_create("ram_decrypt valid image");
    TCase *oversize = tcase_create("ram_decrypt oversize rejected");
    TCase *overflow = tcase_create("ram_decrypt length-overflow rejected");
    TCase *bitflip = tcase_create("ram_decrypt length bit-flip rejected");
    TCase *maxvalid = tcase_create("ram_decrypt exact-max valid");
    TCase *oneover = tcase_create("ram_decrypt one block over rejected");

    tcase_add_test(valid, test_ram_decrypt_valid);
    tcase_add_test(oversize, test_ram_decrypt_oversize_rejected);
    tcase_add_test(overflow, test_ram_decrypt_overflow_len_rejected);
    tcase_add_test(bitflip, test_ram_decrypt_len_bitflip_rejected);
    tcase_add_test(maxvalid, test_ram_decrypt_max_valid);
    tcase_add_test(oneover, test_ram_decrypt_one_over_rejected);

    suite_add_tcase(s, valid);
    suite_add_tcase(s, oversize);
    suite_add_tcase(s, overflow);
    suite_add_tcase(s, bitflip);
    suite_add_tcase(s, maxvalid);
    suite_add_tcase(s, oneover);

    tcase_set_timeout(bitflip, 5);
    tcase_set_timeout(valid, 5);
    tcase_set_timeout(oversize, 5);
    tcase_set_timeout(overflow, 5);
    tcase_set_timeout(maxvalid, 5);
    tcase_set_timeout(oneover, 5);

    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;

    (void)argc;
    argv0 = strdup(argv[0]);
    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
