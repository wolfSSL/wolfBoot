/* otp-keystore-primer.c
 *
 * Command line utility to create a OTP image
 *
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define OTP_SIZE 4096

#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "otp_keystore.h"

extern struct keystore_slot PubKeys[];

const char outfile[] = "otp.bin";

int main(void)
{
    int n_keys = keystore_num_pubkeys();
    int i;
    struct wolfBoot_otp_hdr hdr;
    uint32_t tot_len;
    int ofd;
    int slot_size;

    memcpy(hdr.keystore_hdr_magic, KEYSTORE_HDR_MAGIC, 8);
    hdr.item_count = n_keys;
    hdr.flags = 0;
    hdr.version = WOLFBOOT_VERSION;

    /* Sanity check to avoid writing an empty keystore */
    if (n_keys < 1) {
        fprintf(stderr, "Error: too few keys (%d), refusing to create %s\n", n_keys, outfile);
        exit(1);
    }
    
    slot_size = keystore_get_size(0);
    slot_size += KEYSTORE_HDR_SIZE;
    fprintf(stderr, "Slot size: %d\n", slot_size);
    fprintf(stderr, "Number of slots: %d\n", n_keys);
    fprintf(stderr, "%s size: %d\n", outfile, slot_size * n_keys + sizeof(struct wolfBoot_otp_hdr));

    ofd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (ofd < 0) {
        perror("opening output file");
        exit(2);
    }
     
    /* Write the header to the beginning of the OTP binary file */
    if (write(ofd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fprintf(stderr, "Error writing to %s: %s\n", outfile, strerror(errno));
    }

    for (i = 0; i < n_keys; i++) {
        /* Write each public key to its slot in OTP */
        if (write(ofd, &PubKeys[i],
                slot_size) < 0) {
        fprintf(stderr, "Error adding key %d to %s: %s\n", i, outfile, strerror(errno));
        exit(3);
        }
    }
    fprintf(stderr, "%s successfully created.\nGoodbye.\n", outfile);
    close(ofd);
    return 0;
}
