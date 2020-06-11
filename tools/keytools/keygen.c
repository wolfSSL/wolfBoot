/* keygen.c
 *
 * C native key generation tool
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
 */

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
//#define DEBUG_SIGNTOOL

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <wolfssl/wolfcrypt/settings.h>
#ifndef NO_RSA
#include <wolfssl/wolfcrypt/rsa.h>
#endif
#ifdef HAVE_ECC
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn.h>

#endif
#ifdef HAVE_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>
#endif

#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef DEBUG_SIGNTOOL
#include <wolfssl/wolfcrypt/logging.h>
#endif

#if defined(_WIN32) && !defined(PATH_MAX)
#define PATH_MAX 256
#endif

#define KEYGEN_ED25519 0
#define KEYGEN_ECC256  1
#define KEYGEN_RSA2048 2
#define KEYGEN_RSA4096 3

const char Ed25519_pub_key_define[] = "const uint8_t ed25519_pub_key[32] = {";
const char Ecc256_pub_key_define[] = "const uint8_t ecc256_pub_key[64] = {";
const char Rsa_2048_pub_key_define[] = "const uint8_t rsa2048_pub_key[%d] = {";
const char Rsa_4096_pub_key_define[] = "const uint8_t rsa4096_pub_key[%d] = {";

const char Cfile_Banner[] = "/* Public-key file for wolfBoot, automatically generated. Do not edit.  */\n" \
                             "/*\n" \
                             " * This file has been generated and contains the public key which is\n" \
                             " * used by wolfBoot to verify the updates.\n" \
                             " */" \
                             "\n#include <stdint.h>\n\n";



static void usage(const char *pname) /* implies exit */
{
    printf("Usage: %s [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ]  pub_key_file.c\n", pname);
    exit(125);
}

static void fwritekey(uint8_t *key, int len, FILE *f)
{
    int i;
    for (i = 0; i < len; i++)
    {
        if ((i % 8) == 0) {
            if (i != 0)
                fprintf(f, ",");
            fprintf(f, "\n\t");
        }
        else {
            fprintf(f, ", ");
        }
        fprintf(f, "0x%02x", key[i]);
    }
}


#if !defined(NO_RSA) && defined(WOLFSSL_KEY_GEN)
static void keygen_rsa(WC_RNG *rng, char *pubkeyfile, int size)
{
    RsaKey k;
    uint8_t priv_der[4096], pub_der[2048];
    int privlen, publen;
    FILE *fpub, *fpriv;
    char priv_fname[40];

    if (wc_MakeRsaKey(&k, size, 65537, rng) != 0) {
        fprintf(stderr, "Unable to create RSA%d key\n", size);
        exit(1);
    }
    privlen = wc_RsaKeyToDer(&k, priv_der, size);
    if (privlen <= 0) {
        fprintf(stderr, "Unable to export private key to DER\n");
        exit(2);
    }
    publen = wc_RsaKeyToPublicDer(&k, pub_der, size);
    if (publen <= 0) {
        fprintf(stderr, "Unable to export public key\n");
        exit(3);
    }
    sprintf(priv_fname, "rsa%d.der", size);
    fpriv = fopen(priv_fname, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", priv_fname, strerror(errno));
        exit(4);
    }
    fwrite(priv_der, privlen, 1, fpriv);
    fclose(fpriv);

    fpub = fopen(pubkeyfile, "w");
    if (fpub == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", pubkeyfile, strerror(errno));
        exit(4);
    }
    fprintf(fpub, "%s", Cfile_Banner);
    if (size == 2048)
        fprintf(fpub, Rsa_2048_pub_key_define, publen);
    else
        fprintf(fpub, Rsa_4096_pub_key_define, publen);

    fwritekey(pub_der, publen, fpub);
    fprintf(fpub, "\n};\n");
    fprintf(fpub, "const uint32_t rsa%d_pub_key_len = %d;\n", size, publen);
    fclose(fpub);
}
#endif

#ifdef HAVE_ECC
#define ECC256_KEY_SIZE 32
static void keygen_ecc256(WC_RNG *rng, char *pubkfile)
{
    ecc_key k;
    uint8_t Qx[ECC256_KEY_SIZE], Qy[ECC256_KEY_SIZE], d[ECC256_KEY_SIZE];
    uint32_t qxsize = ECC256_KEY_SIZE,
             qysize = ECC256_KEY_SIZE,
             dsize = ECC256_KEY_SIZE;
    FILE *fpriv, *fpub;
    char priv_fname[20] = "";


    if (wc_ecc_make_key(rng, ECC256_KEY_SIZE, &k) != 0) {
        fprintf(stderr, "Unable to create ecc%d key\n", ECC256_KEY_SIZE << 3);
        exit(1);
    }

    if (wc_ecc_export_private_raw(&k, Qx, &qxsize, Qy, &qysize, d, &dsize) != 0) {
        fprintf(stderr, "Unable to export private key to DER\n");
        exit(2);
    }

    if (wc_ecc_export_public_raw(&k, Qx, &qxsize, Qy, &qysize ) != 0) {
        fprintf(stderr, "Unable to export public key\n");
        exit(3);
    }

    sprintf(priv_fname, "ecc%d.der", (ECC256_KEY_SIZE << 3));

    fpriv = fopen(priv_fname, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", priv_fname,  strerror(errno));
        exit(3);
    }

    fwrite(Qx, qxsize, 1, fpriv);
    fwrite(Qy, qysize, 1, fpriv);
    fwrite(d, dsize, 1, fpriv);
    fclose(fpriv);
    fpub = fopen(pubkfile, "w");
    if (fpub == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", pubkfile, strerror(errno));
        exit(4);
    }
    fprintf(fpub, "%s", Cfile_Banner);
    fprintf(fpub, "%s", Ecc256_pub_key_define);
    fwritekey(Qx, qxsize, fpub);
    fprintf(fpub, ",");
    fwritekey(Qy, qysize, fpub);
    fprintf(fpub, "\n};\n");
    fprintf(fpub, "const uint32_t ecc256_pub_key_len = 64;\n");
    fclose(fpub);
}
#endif


#ifdef HAVE_ED25519
static void keygen_ed25519(WC_RNG *rng, char *pubkfile)
{
    ed25519_key k;
    uint8_t priv[32], pub[32];
    FILE *fpriv, *fpub;
    uint32_t outlen = ED25519_KEY_SIZE;
    if (wc_ed25519_make_key(rng, ED25519_KEY_SIZE, &k) != 0) {
        fprintf(stderr, "Unable to create ed25519 key\n");
        exit(1);
    }
    if (wc_ed25519_export_private_only(&k, priv, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed25519 private key\n");
        exit(2);
    }
    if (wc_ed25519_export_public(&k, pub, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed25519 public key\n");
        exit(2);
    }
    fpriv = fopen("ed25519.der", "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file 'ed25519.der' for writing: %s", strerror(errno));
        exit(3);
    }
    fwrite(priv, 32, 1, fpriv);
    fwrite(pub, 32, 1, fpriv);
    fclose(fpriv);
    fpub = fopen(pubkfile, "w");
    if (fpub == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", pubkfile, strerror(errno));
        exit(4);
    }
    fprintf(fpub, "%s", Cfile_Banner);
    fprintf(fpub, "%s", Ed25519_pub_key_define);
    fwritekey(pub, 32, fpub);
    fprintf(fpub, "\n};\n");
    fprintf(fpub, "const uint32_t ed25519_pub_key_len = 32;\n");
    fclose(fpub);
}
#endif

int main(int argc, char** argv)
{
    int i;
    int force = 0;
    int  keytype = 0;
    const char *kfilename = NULL;
    char *output_pubkey_file;
    WC_RNG rng;
    FILE *f;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    /* Check arguments and print usage */
    if (argc < 2 || argc > 4)
        usage(argv[0]);

    for (i = 1; i < argc - 1; i++) {
        /* Parse Arguments */ 
        if (strcmp(argv[i], "--ed25519") == 0) {
            keytype = KEYGEN_ED25519;
            kfilename = strdup("ed25519.der");
        }
        else if (strcmp(argv[i], "--ecc256") == 0) {
            keytype = KEYGEN_ECC256;
            kfilename = strdup("ecc256.der");
        }
        else if (strcmp(argv[i], "--rsa2048") == 0) {
            keytype = KEYGEN_RSA2048;
            kfilename = strdup("rsa2048.der");
        }
        else if (strcmp(argv[i], "--rsa4096") == 0) {
            keytype = KEYGEN_RSA4096;
            kfilename = strdup("rsa4096.der");
        }
        else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        }
        else {
            fprintf(stderr, "Invalid argument '%s'.", argv[i]);
            usage(argv[0]);
        }
    }
    output_pubkey_file = strdup(argv[argc - 1]);

    f = fopen(kfilename, "rb");
    if (!force && (f != NULL)) {
        char reply[40];
        fclose(f);
        printf("** Warning: key file already exist! Are you sure you want to  generate a new key and overwrite the existing key? [Type 'Yes, I am sure!']: ");
        fflush(stdout);
        scanf("%s", reply);
        printf("Reply is [%s]\n", reply);
        if (strcmp(reply, "Yes, I am sure!") != 0) {
            printf("Operation aborted by user.");
            exit(5);
        }
    }

    printf("Private key:           %s\n", kfilename);
    printf("Generated public key C file:        %s\n", output_pubkey_file);
    printf("Generating key...\n");
    fflush(stdout);
    wc_InitRng(&rng);

    switch (keytype) {
#ifdef HAVE_ED25519
        case KEYGEN_ED25519:
            {
                keygen_ed25519(&rng, output_pubkey_file);
                break;
            }
#endif

#ifdef HAVE_ECC
        case KEYGEN_ECC256:
            {
                keygen_ecc256(&rng, output_pubkey_file);
                break;
            }
#endif

#ifndef NO_RSA
        case KEYGEN_RSA2048:
            {
                keygen_rsa(&rng, output_pubkey_file, 2048);
                break;
            }
        case KEYGEN_RSA4096:
            {
                keygen_rsa(&rng, output_pubkey_file, 4096);
                break;
            }
#endif
    } /* end switch */

    printf("Done.\n");
    return 0;
}
