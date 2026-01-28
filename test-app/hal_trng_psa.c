/* hal_trng_psa.c
 *
 * PSA-backed entropy for bare-metal test-app.
 * Copyright (C) 2026 wolfSSL Inc.
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
#include "psa/crypto.h"

int hal_trng_get_entropy(unsigned char *out, unsigned len)
{
    psa_status_t status;

    if (out == NULL || len == 0) {
        return -1;
    }

    status = psa_generate_random(out, len);
    return (status == PSA_SUCCESS) ? 0 : -1;
}
