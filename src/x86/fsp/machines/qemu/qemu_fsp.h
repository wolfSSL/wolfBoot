/* qemu_fsp.h
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
 * Machine dependent header for qemu x86 with FSP
 */

#include <stdint.h>

#define FSPM_UPD_SIGNATURE 0x4D5F4450554D4551 /* 'QEMUPD_M' */
#define FSPT_UPD_SIGNATURE 0x545F4450554D4551 /* 'QEMUPD_T' */

struct fsp_upd_header {
    uint64_t Signature;
    uint8_t Revision;
    uint8_t Reserved[23];
} __attribute__((packed));

struct fspt_common_upd {
    uint8_t Revision;
    uint8_t Reserved[3];
    uint32_t MicrocodeRegionBase;
    uint32_t MicrocodeRegionLength;
    uint32_t CodeRegionBase;
    uint32_t CodeRegionLength;
    uint8_t Reserved1[12];
} __attribute__((packed));

struct fspt_upd {
    struct fsp_upd_header FspUpdHeader;
    struct fspt_common_upd FsptCommonUpd;
    uint8_t ReservedFsptUpd1[32];
    uint8_t UnusedUpdSpace0[48];
    uint16_t UpdTerminator;
} __attribute__((packed));

struct fspm_arch_upd {
    uint8_t Revision;
    uint8_t Reserved[3];
    uint32_t NvsBufferPtr;
    uint32_t StackBase;
    uint32_t StackSize;
    uint32_t BootLoaderTolumSize;
    uint32_t BootMode;
    uint32_t FspEventHandler;
    uint8_t Reserved1[4];
} __attribute__((packed));

struct fsp_m_config {
    uint32_t SerialDebugPortAddress;
    uint8_t SerialDebugPortType;
    uint8_t SerialDebugPortDevice;
    uint8_t SerialDebugPortStrideSize;
    uint8_t UnusedUpdSpace0[49];
    uint8_t ReservedFspmUpd[4];
} __attribute__((packed));

struct fspm_upd {
    struct fsp_upd_header FspUpdHeader;
    struct fspm_arch_upd FspmArchUpd;
    struct fsp_m_config FspmConfig;
    uint8_t UnusedUpdSpace1[2];
    uint16_t UpdTerminator;
} __attribute__((packed));
