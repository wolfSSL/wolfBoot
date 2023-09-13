/* wc_callable.c
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
#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfboot/wolfboot.h"
#include "wolfboot/wc_secure.h"
#include "hal.h"
#include <stdint.h>

int __attribute__((cmse_nonsecure_entry))
wcs_get_random(uint8_t *rand, uint32_t size)
{
    int ret;
    WC_RNG wcs_rng;
    wc_InitRng(&wcs_rng);
    ret = wc_RNG_GenerateBlock(&wcs_rng, rand, size);
    wc_FreeRng(&wcs_rng);
    return ret;
}

void wcs_Init(void)
{
    hal_trng_init();
}

#endif
