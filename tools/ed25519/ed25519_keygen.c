/*
 * ed25519_keygen.c
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
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/asn_public.h>

#define PEMSIZE 1024

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
}

void print_key(void *key_in)
{
    uint8_t * key = key_in;
    print_buf(key, ED25519_KEY_SIZE);
}

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
    strcat(buf,"/* This file is automatically generate by ed25519 keygen. DO NOT EDIT. */\n");
    strcat(buf,"#include <stdint.h>\n");
    strcat(buf,"const uint8_t ed25519_pub_key[32] = {\n");
    for (i = 0; i < ED25519_KEY_SIZE; i++) {
        int p = i % 8;
        if (p == 0)
            strcat(buf,"\t");
        snprintf(keybyte, 5, "0x%02X", key_in[i]);
        strcat(buf, keybyte);
        if (i < ED25519_KEY_SIZE - 1)
            strcat(buf, ",");
        if (p == 7)
            strcat(buf, "\n");
        else
            strcat(buf," ");
    }
    strcat(buf,"};\n");
    strcat(buf,"const uint32_t ed25519_pub_key_len = 32;\n");
    write(fd, buf, strlen(buf));
    close(fd);
}

void print_sig(void *sig_in)
{
    uint8_t * sig = sig_in;
    print_buf(sig, ED25519_SIG_SIZE);
}



int main(int argc, char *argv[])
{
    uint8_t priv[ED25519_KEY_SIZE], pub[ED25519_KEY_SIZE], full[2*ED25519_KEY_SIZE];
    uint8_t sig[ED25519_SIG_SIZE];
    uint32_t outlen;
    char outkey[PEMSIZE];
    int fd;
    RNG rng;
    ed25519_key key;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s cfile\n", argv[0]);
        exit(1);
    }

    wc_ed25519_init(&key);
    wc_InitRng(&rng);
    if (wc_ed25519_make_key(&rng, ED25519_KEY_SIZE, &key) != 0) {
        printf("Failed to create ED25519 key!\n");
        exit(1);
    }

    outlen = ED25519_KEY_SIZE;
    wc_ed25519_export_private_only(&key, priv, &outlen); 
    printf("const uint8_t ed_private_key[ED25519_KEY_SIZE] = {\n");
    print_key(priv);
    printf("};\n\n");
    
    outlen = ED25519_PRV_KEY_SIZE;
    wc_ed25519_export_private(&key, full, &outlen); 
    print_key(full);
    print_key(full + 32);

    fd = open("ed25519.der", O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd < 0) {
        perror("exporting key (der)");
    }
    write(fd, full, ED25519_PRV_KEY_SIZE);
    close(fd);
    memset(outkey, 0, PEMSIZE);
    wc_DerToPem(priv, outlen, outkey, PEMSIZE, ED25519_TYPE);
    printf("%s\n", outkey);
    fd = open("ed25519.pem", O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd < 0) {
        perror("exporting key (pem)");
    }
    write(fd, outkey, strlen(outkey));
    close(fd);

    
    outlen = ED25519_KEY_SIZE;
    wc_ed25519_export_public(&key, pub, &outlen); 
    printf("const uint8_t ed_public_key[ED25519_KEY_SIZE] = {\n");
    print_key(pub);
    printf("};\n\n");
    memset(outkey, 0, PEMSIZE);
    wc_DerToPem(pub, 32, outkey, PEMSIZE, PUBLICKEY_TYPE);
    printf("%s\n", outkey);
    fd = open("ed25519_pub.pem", O_WRONLY|O_CREAT|O_TRUNC, 0660);
    if (fd < 0) {
        perror("creating key\n");
    }
    write(fd, outkey, strlen(outkey));
    close(fd);
    if (argc > 1) {
        printf("Generating .c code for public key...\n");
        create_pubkey_cfile(argv[1], pub);
    }
    exit(0);
}

