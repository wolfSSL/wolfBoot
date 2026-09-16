/* fsp.h
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
#ifndef FSP_H
#define FSP_H

#include <x86/fsp/FspCommon.h>

int fsp_info_header_is_ok(struct fsp_info_header *hdr);
int fsp_get_image_revision(struct fsp_info_header *h, int *build,
                                  int *rev, int *maj, int *min);
void print_fsp_image_revision(struct fsp_info_header *h);
void fsp_init_silicon(void);

/* Reset requests (not failures) from FspMemInit and NotifyPhase. */
#define FSP_STATUS_RESET_REQUIRED_COLD  0x40000001
#define FSP_STATUS_RESET_REQUIRED_WARM  0x40000002
/* Codes 3..8 are platform-defined. On Intel client SoCs FSP-S returns _3 to
 * request a global reset (host + CSME), needed for the ChipsetInit sync. */
#define FSP_STATUS_RESET_REQUIRED_3     0x40000003
#define FSP_STATUS_RESET_REQUIRED_4     0x40000004
#define FSP_STATUS_RESET_REQUIRED_5     0x40000005
#define FSP_STATUS_RESET_REQUIRED_6     0x40000006
#define FSP_STATUS_RESET_REQUIRED_7     0x40000007
#define FSP_STATUS_RESET_REQUIRED_8     0x40000008

#endif /* FSP_H */
