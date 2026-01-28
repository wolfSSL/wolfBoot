/* otp-keystore-gen.c
 *
 * Command line utility to create a OTP image
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

/* Define a generic max OTP size to appease otp_keystore.h when no target is set. */
#if !defined(OTP_SIZE) && !defined(TARGET_stm32h7) && !defined(TARGET_stm32h5)
#define OTP_SIZE 4096
#endif

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
    uint8_t *otp_buf = NULL;
    uint8_t uds[OTP_UDS_LEN];
    size_t offset;
    int rand_fd;
    ssize_t rlen;

    memcpy(hdr.keystore_hdr_magic, KEYSTORE_HDR_MAGIC, 8);
    hdr.item_count = n_keys;
    hdr.flags = 0;
    hdr.version = WOLFBOOT_VERSION;

    /* Sanity check to avoid writing an empty keystore */
    if (n_keys < 1) {
        fprintf(stderr, "Error: too few keys (%d), refusing to create %s\n",
            n_keys, outfile);
        exit(1);
    }

    slot_size = keystore_get_size(0);
    slot_size += KEYSTORE_HDR_SIZE;
    fprintf(stderr, "Slot size: %d\n", slot_size);
    fprintf(stderr, "Number of slots: %d\n", n_keys);
    tot_len = (uint32_t)sizeof(struct wolfBoot_otp_hdr) +
        (uint32_t)(slot_size * n_keys);
    fprintf(stderr, "%s keystore size: %u\n", outfile, tot_len);
    if (tot_len > OTP_UDS_OFFSET) {
        fprintf(stderr,
            "Error: keystore size %u exceeds OTP UDS offset %u\n",
            tot_len, (unsigned)OTP_UDS_OFFSET);
        exit(1);
    }

    otp_buf = (uint8_t *)malloc(OTP_SIZE);
    if (otp_buf == NULL) {
        fprintf(stderr, "Error: out of memory allocating OTP buffer\n");
        exit(1);
    }
    memset(otp_buf, 0xFF, OTP_SIZE);

    memcpy(otp_buf, &hdr, sizeof(hdr));

    ofd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (ofd < 0) {
        perror("opening output file");
        free(otp_buf);
        exit(2);
    }

    for (i = 0; i < n_keys; i++) {
        /* Write each public key to its slot in OTP */
        offset = sizeof(hdr) + (size_t)i * (size_t)slot_size;
        memcpy(otp_buf + offset, &PubKeys[i], (size_t)slot_size);
    }

    rand_fd = open("/dev/urandom", O_RDONLY);
    if (rand_fd < 0) {
        perror("opening /dev/urandom");
        close(ofd);
        free(otp_buf);
        exit(4);
    }
    rlen = read(rand_fd, uds, sizeof(uds));
    close(rand_fd);
    if (rlen != (ssize_t)sizeof(uds)) {
        fprintf(stderr, "Error: failed to read random UDS (%zd)\n", rlen);
        close(ofd);
        free(otp_buf);
        exit(5);
    }

    memcpy(otp_buf + OTP_UDS_OFFSET, uds, sizeof(uds));

    if (write(ofd, otp_buf, OTP_SIZE) != OTP_SIZE) {
        fprintf(stderr, "Error writing to %s: %s\n", outfile, strerror(errno));
        close(ofd);
        free(otp_buf);
        exit(3);
    }
    fprintf(stderr, "%s successfully created.\nGoodbye.\n", outfile);
    close(ofd);
    free(otp_buf);

    return 0;
}
