/* app_imx8mm.c
 *
 * Test bare-metal boot application
 *
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

#include "wolfboot/wolfboot.h"
#include "printf.h"

#ifdef TARGET_imx8mm

#include "hal/imx8mm.h"
#include "aarch64_arch.h"

/* Console: uart_write() in hal/imx8mm.c, linked into the test-app
 * (DEBUG_UART=1, the config default) */

/* FDT magic 0xd00dfeed read little-endian */
#define FDT_MAGIC_LE 0xedfe0dd0u

/* dtb: x0 from wolfBoot (arm64 boot protocol) */
void __attribute__((section(".boot"))) main(uint64_t dtb) {
    uint32_t magic;

    wolfBoot_printf("\n*** wolfBoot i.MX 8M Mini payload: verified + booted ***\n");
    wolfBoot_printf("  CurrentEL = EL%d\n", (int)current_el());
    wolfBoot_printf("  DTB (x0)  = 0x%08x%08x\n",
        (uint32_t)(dtb >> 32), (uint32_t)dtb);

    /* MMU off: check range and alignment before reading */
    if (dtb < IMX8MM_DRAM_BASE || dtb > (IMX8MM_DRAM_END - 4) ||
            (dtb & 0x3) != 0) {
        wolfBoot_printf("  DTB magic = (pointer not in DRAM or misaligned)\n");
    }
    else {
        magic = *((volatile uint32_t*)(uintptr_t)dtb);
        if (magic == FDT_MAGIC_LE)
            wolfBoot_printf("  DTB magic = OK (0xd00dfeed)\n");
        else
            wolfBoot_printf("  DTB magic = 0x%08x (BAD)\n", magic);
    }

    wolfBoot_printf("*** payload complete: parked ***\n");

    /* Wait for reboot */
    while(1)
        ;
}
#endif /** TARGET_imx8mm **/
