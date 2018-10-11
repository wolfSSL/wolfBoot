/*
 * ed25519_sign.c
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
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <bootutil/image.h>
#include <sys/stat.h>    

#define IMAGE_FIRMWARE_OFFSET 256


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

int main(int argc, char *argv[])
{
    uint8_t inkey[ED25519_PRV_KEY_SIZE];
    int in_fd, key_fd, out_fd, r;
    int outlen = ED25519_SIG_SIZE;
    char in_name[PATH_MAX];
    char signed_name[PATH_MAX];
    char *dot;
    ed25519_key key;
    Sha256 sha;
    Sha256 keyhash;
    uint8_t shabuf[32];
    uint8_t signature[ED25519_SIG_SIZE];
    uint8_t *final;
    int total_size;
    struct image_header *hdr;
    uint8_t header_buffer[IMAGE_FIRMWARE_OFFSET];
    struct image_tlv_info info;
    struct image_tlv tlv;
    struct stat st;
    int version;
    int padsize = 0;
    int i;

    hdr = (struct image_header *)header_buffer;

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
    wc_ed25519_init(&key);
    wc_InitSha256(&sha);

    r = read(key_fd, inkey, ED25519_PRV_KEY_SIZE);
    if (r < 0) {
        perror("read");
        exit(3);
    }
    r = wc_ed25519_import_private_key(inkey, ED25519_KEY_SIZE, inkey + ED25519_KEY_SIZE, ED25519_KEY_SIZE, &key);
    if (r < 0) {
        perror("importing private key");
    }
    close(key_fd);

    print_key(inkey);
    print_key(inkey + 32);
    
    /* Create header */
    r = stat(in_name, &st);
    if (r < 0) {
        perror(in_name);
        exit(2);
    }
    memset(hdr, 0x00, IMAGE_FIRMWARE_OFFSET);
    hdr->ih_magic = IMAGE_MAGIC; 
    hdr->ih_load_addr = 0x10100;
    hdr->ih_hdr_size = IMAGE_FIRMWARE_OFFSET;
    hdr->ih_img_size = st.st_size;
    hdr->ih_ver.iv_major = version;

    /* Sha256 */
    wc_Sha256Update(&sha, (uint8_t *)hdr, IMAGE_FIRMWARE_OFFSET);
    while(1) {
        r = read(in_fd, shabuf, 32);
        if (r <= 0)
            break;
        wc_Sha256Update(&sha, shabuf, r);
        if (r < 32)
            break;
    }
    wc_Sha256Final(&sha, shabuf);
    wc_ed25519_sign_msg(shabuf, 32, signature, &outlen, &key);
    wc_Sha256Free(&sha);
    
    /* Write header */
    write(out_fd, hdr, IMAGE_FIRMWARE_OFFSET);

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

    /* TLV INFO hdr */
    memset(&info, 0, sizeof(info));
    info.it_magic = IMAGE_TLV_INFO_MAGIC;
    info.it_tlv_tot = sizeof(info) + 3 * sizeof(tlv) + 2 * SHA256_DIGEST_SIZE + ED25519_SIG_SIZE;
    write(out_fd, &info, sizeof(info));

    /* TLV 0: SHA DIGEST */
    memset(&tlv, 0, sizeof(tlv));
    tlv.it_type = IMAGE_TLV_SHA256;
    tlv.it_len = SHA256_DIGEST_SIZE; 
    write(out_fd, &tlv, sizeof(tlv));
    write(out_fd, shabuf, SHA256_DIGEST_SIZE);

    /* TLV 1: KEYHASH */
    wc_InitSha256(&keyhash);
    wc_Sha256Update(&keyhash, inkey + ED25519_KEY_SIZE, ED25519_KEY_SIZE);
    wc_Sha256Final(&keyhash, shabuf);
    memset(&tlv, 0, sizeof(tlv));
    tlv.it_type = IMAGE_TLV_KEYHASH;
    tlv.it_len = SHA256_DIGEST_SIZE;
    write(out_fd, &tlv, sizeof(tlv));
    write(out_fd, shabuf, SHA256_DIGEST_SIZE);
    wc_Sha256Free(&keyhash);

    /* TLV 2: SIGNATURE */
    memset(&tlv, 0, sizeof(tlv));
    tlv.it_type = IMAGE_TLV_ED25519;
    tlv.it_len = ED25519_SIG_SIZE;
    write(out_fd, &tlv, sizeof(tlv));
    write(out_fd, signature, ED25519_SIG_SIZE);
    close(out_fd);

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

