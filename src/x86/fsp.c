/* fsp_tgl.c
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
#include <x86/fsp/FspCommon.h>
#include <stage2_params.h>
#include <x86/common.h>
#include <pci.h>
#include <printf.h>

#define FSP_INFO_HEADER_OFFSET 0x94
#define EFI_SUCCESS 0x0

#ifndef FSP_S_PARAM_SIZE
#define FSP_S_PARAM_SIZE 0xee0
#endif

#define PCI_DEVICE_CONTROLLER_TO_PEX 0x6


int fsp_machine_update_s_parameters(uint8_t *default_s_params);
int fsp_pre_silicon_init_cb(void);
/*!
 * \brief Check if the FSP info header is valid.
 *
 * This static function checks if the given FSP info header is valid by verifying
 * its signature.
 *
 * \param hdr Pointer to the FSP info header structure.
 * \return 1j if the FSP info header is valid, 0 otherwise.
 */
int fsp_info_header_is_ok(struct fsp_info_header *hdr)
{
    uint8_t *raw_signature;

    raw_signature = (uint8_t *)&hdr->Signature;
    if (raw_signature[0] != 'F' || raw_signature[1] != 'S' ||
        raw_signature[2] != 'P' || raw_signature[3] != 'H') {
        return 0;
    }
    return 1;
}

int fsp_get_image_revision(struct fsp_info_header *h, int *build,
                                  int *rev, int *maj, int *min)
{
    uint16_t ext_revision;
    uint32_t revision;

    if (!fsp_info_header_is_ok(h)) {
        wolfBoot_printf("Wrong FSP Header\r\n");
        return -1;
    }

    revision = h->ImageRevision;

    *build = revision & 0xff;
    *rev = (revision >> 8) & 0xff;
    *min = (revision >> 16) & 0xff;
    *maj = (revision >> 24) & 0xff;

    if (h->HeaderRevision >= 6) {
        *build = *build | ((h->ExtendedImageRevision & 0xff) << 8);
        *rev = *rev | (h->ExtendedImageRevision & 0xff00);
    }

    return 0;
}

void print_fsp_image_revision(struct fsp_info_header *h)
{
    int build, rev, maj, min;
    int r;
    r = fsp_get_image_revision(h, &build, &rev, &maj, &min);
    if (r != 0) {
        wolfBoot_printf("failed to get fsp image revision\r\n");
        return;
    }
    wolfBoot_printf("%x.%x.%x build %x\r\n", maj, min, rev, build);
}
