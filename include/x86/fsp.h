/* fsp.h
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
 * Definition of headers used in Intel FSP binaries
 */

#include <stdint.h>
#include <x86/hob.h>

#ifndef FSP_H
#define FSP_H

struct fsp_info_header {
    uint32_t Signature;
    uint32_t HeaderLength;
    uint8_t Reserved1[2];
    uint8_t SpecVersion;
    uint8_t HeaderRevision;
    uint32_t ImageRevision;
    uint8_t ImageId[8];
    uint32_t ImageSize;
    uint32_t ImageBase;
    uint16_t ImageAttribute;
    uint16_t ComponentAttribute;
    uint32_t CfgRegionOffset;
    uint32_t CfgRegionSize;
    uint32_t Reserved2;
    uint32_t TempRamInitEntryOffset;
    uint32_t Reserved3;
    uint32_t NotifyPhaseEntryOffset;
    uint32_t FspMemoryInitEntryOffset;
    uint32_t TempRamExitEntryOffset;
    uint32_t FspSiliconInitEntryOffset;
    uint32_t FspMultiPhaseSiInitEntryOffset;
    uint16_t ExtendedImageRevision;
    uint16_t Reserved4;
} __attribute__((packed));

struct stage2_parameter {
    struct efi_hob *hobList;
};

void set_stage2_parameter(struct stage2_parameter *p);

#endif /* FSP_H */
