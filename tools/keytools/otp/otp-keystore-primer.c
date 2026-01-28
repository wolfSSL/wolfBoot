/* otp-keystore-primer.c
 *
 * Primer app to provision public keys into OTP flash
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
#include <string.h>
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "otp_keystore.h"

extern struct keystore_slot PubKeys[];

void main(void)
{
    int n_keys = keystore_num_pubkeys();
    int i;
    struct wolfBoot_otp_hdr hdr;
    uint32_t tot_len;
    uint8_t uds[OTP_UDS_LEN];
    uint32_t wp_len;

    hal_init();
    hal_trng_init();

    memcpy(hdr.keystore_hdr_magic, KEYSTORE_HDR_MAGIC, 8);
    hdr.item_count = n_keys;
    hdr.flags = 0;
    hdr.version = WOLFBOOT_VERSION;

    /* Sanity check to avoid writing an empty keystore */
    if (n_keys < 1) {
        while(1)
            ;
    }

    /* Write the header to the beginning of the OTP memory */
    hal_flash_otp_write(FLASH_OTP_BASE, (uint16_t *)&hdr, sizeof(hdr));

    for (i = 0; i < n_keys; i++) {
        /* Write each public key to its slot in OTP */
        hal_flash_otp_write(FLASH_OTP_BASE +
                OTP_HDR_SIZE + i * SIZEOF_KEYSTORE_SLOT, (uint16_t *)&PubKeys[i],
                sizeof(struct keystore_slot));
    }

    tot_len = OTP_HDR_SIZE + n_keys * SIZEOF_KEYSTORE_SLOT;
    if (tot_len > OTP_UDS_OFFSET) {
        /* Not enough room for UDS without overlapping keystore. */
        while (1)
            ;
    }

    if (hal_trng_get_entropy(uds, sizeof(uds)) != 0) {
        while (1)
            ;
    }
    hal_flash_otp_write(FLASH_OTP_BASE + OTP_UDS_OFFSET,
                        (uint16_t *)uds, sizeof(uds));

#ifdef ENABLE_OTP_WP
    /* Protect the OTP areas just written (keystore + UDS) */
    wp_len = OTP_UDS_OFFSET + OTP_UDS_LEN;
    if (wp_len < tot_len) {
        wp_len = tot_len;
    }
    hal_flash_otp_set_readonly(FLASH_OTP_BASE, wp_len);
#endif
    (void)tot_len;
    (void)wp_len;

    /* Done! */
    while(1)
        ;

}
