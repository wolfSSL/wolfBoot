/* ata.h
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

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

struct __attribute__((packed)) hba_cmd_header {
    uint8_t cfl:5, a:1, w:1, p:1;
    uint8_t r:1, b:1, c:1, _res0:1, pmp:4;
    uint16_t prdtl;

    uint32_t prbdc;

    uint32_t ctba;
    uint32_t ctbau;
    uint32_t _res1[4];
};

int ata_drive_new(uint32_t ahci_base, unsigned ahci_port, uint32_t clb, uint32_t ctable, uint32_t fis);
int ata_drive_read(int drv, uint64_t start, uint32_t count, uint8_t *buf);
int ata_drive_write(int drv, uint64_t start, uint32_t count,
        const uint8_t *buf);
int ata_identify_device(int drv);

#define ATA_DEV_BUSY (1 << 7)
#define ATA_DEV_DRQ  (1 << 3)

/* FIS types */

#define FIS_TYPE_REG_H2D    0x27
#define FIS_TYPE_REG_D2H    0x34
#define FIS_TYPE_DMA_ACT    0x39
#define FIS_TYPE_DMA_SETUP  0x41
#define FIS_TYPE_DATA       0x46
#define FIS_TYPE_BIST       0x58
#define FIS_TYPE_PIO_SETUP  0x5F
#define FIS_TYPE_DEV_BITS   0xA1

#define FIS_LEN_H2D         20
#define FIS_H2D_CMD         (1 << 7)

/* ATA commands */

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_WRITE_DMA 0xCA
#define ATA_CMD_IDENTIFY_DEVICE 0xEC

#endif
