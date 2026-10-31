/* fsp_nvs.h
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
#ifndef FSP_NVS_H
#define FSP_NVS_H

#include <stdint.h>

/* Flash record holding the FSP-M non-volatile storage (memory training)
 * data. The key covers the FSP-M image and its UPDs, so a change to either
 * invalidates the record. */
#define FSP_NVS_MAGIC 0x5356464BU /* "KFVS" */

struct fsp_nvs_record {
    uint32_t magic;
    uint32_t key;
    uint32_t len;
    uint32_t crc;
    uint32_t reserved[4];
    uint8_t data[];
} __attribute__((packed));

uint32_t fsp_nvs_crc32(uint32_t crc, const uint8_t *buf, uint32_t len);
int fsp_nvs_record_valid(const struct fsp_nvs_record *rec, uint32_t key,
                         uint32_t area_size);
int fsp_nvs_apply(uint8_t *udp_m, uint32_t udp_size, const uint8_t *fsp_m,
                  uint32_t fsp_m_size);
void fsp_nvs_skip(int skip);
void fsp_nvs_save(void);

#endif /* FSP_NVS_H */
