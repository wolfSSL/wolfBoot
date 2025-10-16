/* app_nxp_p1021.c
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
#include "../hal/nxp_ppc.h"
#include "printf.h"

static const char* hex_lut = "0123456789abcdef";

void main(void)
{
    int i = 0;
    int j = 0;
    int k = 0;
    char snum[8];

    uart_init();

    uart_write("Test App\n", 9);

    /* Wait for reboot */
    while(1) {
        for (j=0; j<1000000; j++)
            ;
        i++;

        uart_write("\n0x", 3);
        for (k=0; k<8; k++) {
            snum[7 - k] = hex_lut[(i >> 4*k) & 0xf];
        }
        uart_write(snum, 8);
    }
}
