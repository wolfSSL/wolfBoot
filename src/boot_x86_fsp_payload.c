/* boot_x86_fsp_payload.c
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

#include <string.h>
#include <x86/fsp.h>
#include <x86/linux.h>

static struct stage2_parameter stage2_params;

void set_stage2_parameter(struct stage2_parameter *p)
{
    memcpy(&stage2_params, p, sizeof(stage2_params));
}

void do_boot(const uint32_t *app)
{
    int r;
#ifdef LINUX_PAYLOAD
    load_linux((uint8_t *)app, &stage2_params, NULL);
#else
    wolfBoot_printf("not supported application\n");
    panic();
#endif /* LINUX_PAYLOAD */
}
