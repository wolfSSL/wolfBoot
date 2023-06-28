/* hash.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/hash.h>

static void usage()
{
    printf("NOTE currently policy sealing only supports sha256");
    printf("NOTE add SIM flag to build for simulator");
    printf("Expected usage: ./hash hashAlg wolfBoot\n");
    printf("hashAlg: the hashing algorithm [--sha256]\n");
    printf("wolfBoot: wolfBoot image to hash\n");
}

int main(int argc, char** argv)
{
    int fd;
    int ret;
    struct stat st[1];
    uint8_t* wolfBoot;
    uint8_t wolfBootDigest[WC_MAX_DIGEST_SIZE];
    int wolfBootDigestSz;
    int startWolfboot;
    int endWolfboot;

    if (argc != 3) {
        usage();
        return 0;
    }

/* if we're not using the simulator use the text section */
#ifndef PRESEAL_SIM
    int i, j;
    uint8_t* map;

    /* find _start_wolfboot and _end_wolfboot */
    ret = stat("wolfboot.map", st);
    if (ret < 0)
        return 1;

    fd = open("wolfboot.map", O_RDONLY);
    if (fd < 0)
        return 1;

    map = malloc(st->st_size);
    ret = read(fd, map, st->st_size);
    if (ret != st->st_size)
        return 1;

    close(fd);

    /* find _start_wolfboot */
    for (i = 0; i < st->st_size; i++) {
        if (memcmp("_start_wolfboot", map + i,
            strlen("_start_wolfboot")) == 0) {
            /* parse the offset */
            for (j = i; j >= 0; j--) {
                if (map[j] == 'x') {
                    sscanf((char*)map + j + 1, "%x", &startWolfboot);
                    break;
                }
            }

            if (j < 0)
                return 1;

            break;
        }
    }

    for (; i < st->st_size; i++) {
        if (memcmp("_end_wolfboot", map + i,
            strlen("_end_wolfboot")) == 0) {
            /* parse the offset */
            for (j = i; j >= 0; j--) {
                if (map[j] == 'x') {
                    sscanf((char*)map + j + 1, "%x", &endWolfboot);
                    break;
                }
            }

            if (j < 0)
                return 1;

            break;
        }
    }

    /* if we got to the end of the file we didn't find it */
    if (i >= st->st_size)
        return 1;

    free(map);
#endif /* !SIM */

    /* read the wolfBoot image */
    ret = stat(argv[2], st);
    if (ret < 0)
        return 1;

/* if we're using the simulator */
#ifdef PRESEAL_SIM
    startWolfboot = 0;
    endWolfboot = st->st_size;
#endif

    fd = open(argv[2], O_RDONLY);
    if (fd < 0)
        return 1;

    wolfBoot = malloc(st->st_size);
    ret = read(fd, wolfBoot, st->st_size);
    if (ret != st->st_size)
        return 1;

    close(fd);

    if (strcmp(argv[1], "--sha256") == 0) {
        wc_Sha256 sha[1];

        wolfBootDigestSz = WC_SHA256_DIGEST_SIZE;

        wc_InitSha256(sha);

        /* hash the wolfboot executable code */
        wc_Sha256Update(sha, wolfBoot + startWolfboot,
            endWolfboot - startWolfboot);

        wc_Sha256Final(sha, wolfBootDigest);

 	      wc_Sha256Free(sha);
    }
    else {
        usage();
        return 1;
    }

    /* write the hash */
    fd = open("wolfBootDigest.bin", O_CREAT | O_TRUNC | O_WRONLY, 0666);

    ret = write(fd, wolfBootDigest, wolfBootDigestSz);

    if (ret != wolfBootDigestSz)
        return 1;

    free(wolfBoot);

    printf("Digest output file: wolfBootDigest.bin\n");

    (void)wolfBootDigest;

    return 0;
}
