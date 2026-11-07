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

#include <stddef.h>
#include <stdint.h>

#include "wolfboot/wolfboot.h"
#include "printf.h"

#ifdef TARGET_imx8mm

#include "hal/imx8mm.h"
#include "aarch64_arch.h"
#include "fdt.h"

/* Console: uart_write() in hal/imx8mm.c, linked into the test-app
 * (DEBUG_UART=1, the config default). DTB parser: src/fdt.c. */

/* Print a string property, if present and NUL-terminated */
static void print_prop(const fdt_ctx* ctx, const char* path,
    const char* name, const char* label)
{
    const char* s = NULL;
    int len = 0;
    int off = fdt_path_offset(ctx, path);

    if (off >= 0)
        s = (const char*)fdt_getprop(ctx, off, name, &len);
    if (s != NULL && len > 0 && s[len - 1] == '\0')
        wolfBoot_printf("  %s = %s\n", label, s);
    else
        wolfBoot_printf("  %s = (none)\n", label);
}

/* Parse the DTB with src/fdt.c */
static void check_dtb(uint64_t dtb)
{
    fdt_ctx ctx;
    uint32_t size = 0;
    int rc;

    /* MMU off: check range and alignment before reading */
    if (dtb < IMX8MM_DRAM_BASE ||
            dtb > (IMX8MM_DRAM_END - FDT_HEADER_SIZE) || (dtb & 0x3) != 0) {
        wolfBoot_printf("  DTB       = pointer not in DRAM or misaligned\n");
        return;
    }
    rc = fdt_peek_size((const void*)(uintptr_t)dtb, FDT_HEADER_SIZE, &size);
    if (rc == 0 && size > (IMX8MM_DRAM_END - dtb))
        rc = -FDT_ERR_BADLAYOUT;
    if (rc == 0)
        rc = fdt_open(&ctx, (void*)(uintptr_t)dtb, size);
    if (rc != 0) {
        wolfBoot_printf("  DTB       = BAD (%d)\n", rc);
        return;
    }
    wolfBoot_printf("  DTB       = OK, %u bytes\n", (unsigned)fdt_size(&ctx));
    print_prop(&ctx, "/", "model", "model    ");
    print_prop(&ctx, "/chosen", "bootargs", "bootargs ");
}

/* dtb: x0 from wolfBoot (arm64 boot protocol) */
void __attribute__((section(".boot"))) main(uint64_t dtb) {
    wolfBoot_printf("\n*** wolfBoot i.MX 8M Mini payload: verified + booted ***\n");
    wolfBoot_printf("  CurrentEL = EL%d\n", (int)current_el());
    wolfBoot_printf("  DTB (x0)  = 0x%08x%08x\n",
        (uint32_t)(dtb >> 32), (uint32_t)dtb);
    check_dtb(dtb);
    wolfBoot_printf("*** payload complete: parked ***\n");

    /* Wait for reboot */
    while(1)
        ;
}
#endif /** TARGET_imx8mm **/
