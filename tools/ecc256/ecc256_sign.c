/*
 * ecc256_sign.c
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
 *
 */
#include <stdint.h>
#include <fcntl.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <sys/stat.h>    

#define ECC_KEY_SIZE  32 
#define ECC_SIG_SIZE  (2 * ECC_KEY_SIZE)
#define WOLFBOOT_SIGN_ECC256
#define VERIFY_SIGNATURE_TEST

#include "target.h"
#include "image.h"
#include "loader.h"

static uint8_t key_buffer[2 * sizeof(ecc_key)];

void print_buf(uint8_t *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        int p = i % 8;
        if (p == 0)
            printf("\t");
        printf("0x%02X", buf[i]);
        if (i < len - 1)
            printf(",");
        if (p == 7)
            printf("\n");
        else
            printf(" ");
    }
    printf("\n");
}

void print_mp(mp_int *m)
{
    uint8_t buf[32];
    mp_to_unsigned_bin(m, buf);
    print_buf(buf, 32);
}

void print_key(void *key_in)
{
    uint8_t * key = key_in;
    print_buf(key, ECC_KEY_SIZE);
}

int main(int argc, char *argv[])
{
    uint8_t inkey[ECC_KEY_SIZE * 4];
    int in_fd, key_fd, out_fd, r;
    int outlen = ECC_SIG_SIZE;
    char in_name[PATH_MAX];
    char signed_name[PATH_MAX];
    char *dot;
    ecc_key *key = (ecc_key *)key_buffer;
    Sha256 sha;
    Sha256 keyhash;
    uint8_t shabuf[SHA256_DIGEST_SIZE];
    uint8_t signature[ECC_SIG_SIZE] = { };
    uint8_t *final;
    int total_size;
    uint8_t hdr[IMAGE_HEADER_SIZE];
    uint8_t *ptr = hdr;
    struct stat st;
    int version;
    int padsize = 0;
    int i;
    int sigsize = 0;
    int res;
    mp_int mpr, mps;
    mp_init(&mpr);
    mp_init(&mps);
    RNG rng;
    wc_InitRng(&rng);
    if (argc != 4 && argc!=5) {
        fprintf(stderr, "Usage: %s image key.der fw_version [padsize]\n", argv[0]);
        exit(1);
    }
    version = atoi(argv[3]);
    if (version < 0) {
        fprintf(stderr, "%s: invalid version '%s'.\n", argv[0], argv[3]);
        exit(1); 
    }
    if (argc > 4) {
        padsize = atoi(argv[4]);
        if (padsize < 1024) {
            fprintf(stderr, "%s: invalid padding size '%s'.\n", argv[0], argv[4]);
            exit(1); 
        }
    }

    strcpy(in_name, argv[1]);
    snprintf(signed_name, PATH_MAX, "%s.v%s.signed", argv[1], argv[3]);

    in_fd = open(in_name, O_RDONLY);
    if (in_fd < 0) {
        perror(in_name);
        exit(2);
    }
    out_fd = open(signed_name, O_WRONLY|O_CREAT|O_TRUNC, 0660);
    if (out_fd < 0) {
        perror(signed_name);
        exit(2);
    }

    key_fd = open(argv[2], O_RDONLY);
    if (key_fd < 0)  {
        perror(argv[2]);
        exit(2);
    }
    wc_ecc_init(key);
    r = read(key_fd, inkey, 3 * ECC_KEY_SIZE);
    if (r < 0) {
        perror("read");
        exit(3);
    }
    r = wc_ecc_import_unsigned(key, inkey, inkey + ECC_KEY_SIZE, inkey + 2 * ECC_KEY_SIZE, ECC_SECP256R1);
    if (r < 0) {
        printf("Errror importing key\n");
    }
    printf("key.type = %d\n", key->type);
    close(key_fd);

    
    /* Create header */
    r = stat(in_name, &st);
    if (r < 0) {
        perror(in_name);
        exit(2);
    }
    memset(hdr, 0xFF, IMAGE_HEADER_SIZE);
    *((uint32_t *)ptr) = WOLFBOOT_MAGIC;
    ptr += (sizeof(uint32_t));
    *((uint32_t *)(ptr)) = st.st_size;
    ptr += (sizeof(uint32_t));

    ptr += (sizeof(uint16_t));
    *(ptr++) = HDR_VERSION;
    *(ptr++) = 4;
    *((uint32_t *)(ptr)) = version;
    ptr += (sizeof(uint32_t));
    
    ptr += (sizeof(uint16_t) + sizeof(uint32_t));
    *(ptr++) = HDR_TIMESTAMP;
    *(ptr++) = 8;
    *((uint64_t *)(ptr)) = st.st_mtime;
    ptr += sizeof(uint64_t);
     

    /* Sha256 */
    wc_InitSha256(&sha);
    wc_Sha256Update(&sha, hdr, ptr - hdr);
    while(1) {
        r = read(in_fd, shabuf, SHA256_DIGEST_SIZE);
        if (r <= 0)
            break;
        wc_Sha256Update(&sha, shabuf, r);
        if (r < 32)
            break;
    }
    wc_Sha256Final(&sha, shabuf);
    if (wc_ecc_sign_hash_ex(shabuf, SHA256_DIGEST_SIZE, &rng, key, &mpr, &mps) != MP_OKAY) {
        printf("Error signing hash\n");
        exit(1);
    }
    if (wc_ecc_verify_hash_ex(&mpr, &mps, shabuf, SHA256_DIGEST_SIZE, &res, key) != MP_OKAY) {
        printf("Error verifying hash\n");
        exit(1);
    }
    if (res == 0) {
        printf("Bad signature.\n");
        exit(1);
    }
    printf("shabuf\n");
    print_buf(shabuf, SHA256_DIGEST_SIZE);
    mp_to_unsigned_bin(&mpr, signature);
    mp_to_unsigned_bin(&mps, signature + ECC_KEY_SIZE);
    sigsize = 2 * ECC_KEY_SIZE;
    printf("signature (%d bytes)\n", sigsize);
    print_buf(shabuf, sigsize);

#ifdef VERIFY_SIGNATURE_TEST
    {
        ecc_key *pubk = (ecc_key *)key_buffer;
        int ret;
        int fd;
        mp_int r, s;
        uint8_t pubk_buf[2 * ECC_KEY_SIZE];
        mp_init(&r);
        mp_init(&s);

        mp_read_unsigned_bin(&r, signature, ECC_KEY_SIZE);
        mp_read_unsigned_bin(&s, signature + ECC_KEY_SIZE, ECC_KEY_SIZE);

        ret = wc_ecc_init(pubk);
        if (ret < 0)
        {
            perror ("initializing ecc key");
            exit(2);
        }

        fd = open("ecc-pub.der", O_RDONLY);
        if (fd < 0) {
            perror ("cannot verify signature: opening ecc-pub.der");
            exit(2);
        }

        if (read(fd, pubk_buf, 2 * ECC_KEY_SIZE) != (2 * ECC_KEY_SIZE)) {
            perror ("cannot verify signature: error reading ecc-pub.der");
            exit(2);
        }

        ret = wc_ecc_import_unsigned(pubk, pubk_buf, pubk_buf + ECC_KEY_SIZE, NULL, ECC_SECP256R1);
        if (ret != MP_OKAY) {
            perror ("importing public key");
            exit(2);
        }
        printf("pubkey.type = %d\n", pubk->type);
        ret = wc_ecc_verify_hash_ex(&r, &s, shabuf, SHA256_DIGEST_SIZE, &res, pubk);
        if (ret != MP_OKAY) {
            printf("Verify operation failed.\n");
        } else if (res == 0) {
            printf("Bad signature.\n");
        } else {
            printf("Signature verified OK \n");
        }
    }
#endif
    *(ptr++) = HDR_SHA256;
    *(ptr++) = SHA256_DIGEST_SIZE;
    memcpy(ptr, shabuf, SHA256_DIGEST_SIZE);
    ptr += SHA256_DIGEST_SIZE;
    
    wc_InitSha256(&keyhash);
    wc_Sha256Update(&keyhash, inkey, 2 * ECC_KEY_SIZE);
    wc_Sha256Final(&keyhash, shabuf);
    *(ptr++) = HDR_PUBKEY;
    *(ptr++) = SHA256_DIGEST_SIZE;
    memcpy(ptr, shabuf, SHA256_DIGEST_SIZE);
    wc_Sha256Free(&keyhash);
    ptr += SHA256_DIGEST_SIZE;
    
    *(ptr++) = HDR_SIGNATURE;
    *(ptr++) = ECC_SIG_SIZE;
    memcpy(ptr, signature, ECC_SIG_SIZE);
    ptr += ECC_SIG_SIZE;
    *(ptr++) = HDR_END;

    printf("\n\n");
    print_buf(hdr, IMAGE_HEADER_SIZE);

    /* Write header */
    write(out_fd, hdr, IMAGE_HEADER_SIZE);

    /* Write image payload */
    lseek(in_fd, 0, SEEK_SET);

    while(1) {
        uint8_t tmpbuf[32];
        r = read(in_fd, tmpbuf, 32);
        if (r <= 0)
            break;
        write(out_fd, tmpbuf, r); 
        if (r < 32)
            break;
    }

    /* Pad if needed */
    r = stat(signed_name, &st);
    if ((r == 0) && st.st_size < padsize) {
        size_t fill = padsize - st.st_size;
        uint8_t padbyte = 0xFF;
        out_fd = open(signed_name, O_WRONLY|O_APPEND|O_EXCL);
        if (out_fd > 0) {
            while(fill--)
                write(out_fd, &padbyte, 1);
        }
        close(out_fd);
    }
    exit(0);
}

