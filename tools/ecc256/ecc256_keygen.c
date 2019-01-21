/* ecc_keys.h
 *
 * Copyright (C) 2006-2015 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/ecc.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>

#define ECC_KEY_SIZE  32 

void create_pubkey_cfile(const char *fname, uint8_t *key_in)
{
    char buf[4192] = { };
    char keybyte[5] = {};
    int i;
    int fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0660);
    if (fd < 0) {
        perror("creating c file");
        exit(1);
    }
    strcat(buf,"/* This file is automatically generate by ecc256 keygen. DO NOT EDIT. */\n");
    strcat(buf,"#include <stdint.h>\n");
    strcat(buf,"const uint8_t ecc256_pub_key[64] = {\n");
    for (i = 0; i < 2 * ECC_KEY_SIZE; i++) {
        int p = i % 8;
        if (p == 0)
            strcat(buf,"\t");
        snprintf(keybyte, 5, "0x%02X", key_in[i]);
        strcat(buf, keybyte);
        if (i < 2 * ECC_KEY_SIZE - 1)
            strcat(buf, ",");
        if (p == 7)
            strcat(buf, "\n");
        else
            strcat(buf," ");
    }
    strcat(buf,"};\n");
    strcat(buf,"const uint32_t ecc256_pub_key_len = 64;\n");
    write(fd, buf, strlen(buf));
    close(fd);
}

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

int main(int argc, char *argv[])
{
    ecc_key key;
    uint8_t der[3 * ECC_KEY_SIZE];
    uint8_t buf[2 * ECC_KEY_SIZE];
    FILE* derFile;
    size_t sz;
    uint32_t qxlen = ECC_KEY_SIZE;
    uint32_t qylen = ECC_KEY_SIZE;
    uint32_t dlen = ECC_KEY_SIZE;
    int fd;

    RNG rng;

    wc_InitRng(&rng);
    wc_ecc_init(&key);

    if (wc_ecc_make_key(&rng, ECC_KEY_SIZE, &key) != 0) {
        printf("error making ecc key\n");
        return -1;
    }
    print_mp(&key.k);
    /* write private key */
    if (wc_ecc_export_private_raw(&key, 
                der, &qxlen, 
                der + ECC_KEY_SIZE, &qylen, 
                der + 2 * ECC_KEY_SIZE, &dlen
                ) == 0) {
        printf("Created private key: %d bytes\n", qxlen + qylen + dlen);
    }

    /* Store private key */
    fd = open("ecc256.der", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("open");
        exit(2);
    }
    write(fd, der, qxlen + qylen + dlen);
    close(fd);


    /* Store public key */
    fd = open("ecc-pub.der", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("open");
        exit(2);
    }
    write(fd, der, qxlen + qylen);
    close(fd);
    if (argc > 1) {
        printf("Generating .c code for public key...\n");
        create_pubkey_cfile(argv[1], der);
    }

    /* close stuff up */
    wc_ecc_free(&key);
    wc_FreeRng(&rng);
    return 0;
}

