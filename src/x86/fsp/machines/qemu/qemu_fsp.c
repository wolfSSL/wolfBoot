/* qemu_fsp.c
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
 *
 *
 * Machine dependent code for qemu x86 with FSP
 */

#include <stdint.h>
#include <x86/hob.h>
#include "qemu_fsp.h"

const
struct fspt_upd TempRamInitParams = {
    .FspUpdHeader = {
        .Signature = FSPT_UPD_SIGNATURE,
        .Revision  = 1,
        .Reserved  = {0},
    },
    .FsptCommonUpd = {
        .Revision              = 1,
        .MicrocodeRegionBase   = 0,
        .MicrocodeRegionLength = 0,
        .CodeRegionBase        = 0xFF000000,
        .CodeRegionLength      = 0x00000000,
    },
    .UpdTerminator = 0x55AA,
};

int fsp_machine_update_m_parameters(uint8_t *default_m_params,
                                    uint32_t mem_base,
                                    uint32_t mem_size)
{
    struct fspm_upd *new_udp;

    new_udp = (struct fspm_upd*)default_m_params;
    new_udp->FspmArchUpd.BootLoaderTolumSize = 0;
    new_udp->FspmArchUpd.BootMode = BOOT_WITH_FULL_CONFIGURATION;

    /* TODO: support permanent flash area to store NVS*/
    new_udp->FspmArchUpd.NvsBufferPtr        = 0;
    new_udp->FspmArchUpd.StackBase = mem_base;
    new_udp->FspmArchUpd.StackSize = mem_size;

    return 0;
}
