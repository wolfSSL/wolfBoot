/* fsp.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#ifndef FSP_H
#define FSP_H

#include <x86/fsp/FspCommon.h>

int fsp_info_header_is_ok(struct fsp_info_header *hdr);
int fsp_get_image_revision(struct fsp_info_header *h, int *build,
                                  int *rev, int *maj, int *min);
void print_fsp_image_revision(struct fsp_info_header *h);
void fsp_init_silicon(void);

#endif /* FSP_H */
