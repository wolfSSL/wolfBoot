/* ata.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

struct __attribute__((packed)) hba_cmd_header {
    uint8_t cfl:5, a:1, w:1, p:1;
    uint8_t r:1, b:1, c:1, _res0:1, pmp:4;
    uint16_t prdtl;

    volatile uint32_t prbdc;

    uint32_t ctba;
    uint32_t ctbau;
    uint32_t _res1[4];
};

int ata_drive_new(uint32_t ahci_base, unsigned ahci_port, uint32_t clb, uint32_t ctable, uint32_t fis);
int ata_drive_read(int drv, uint64_t start, uint32_t count, uint8_t *buf);
int ata_drive_write(int drv, uint64_t start, uint32_t count,
        const uint8_t *buf);
int ata_identify_device(int drv);
int ata_security_erase_prepare(int drv);
int ata_security_erase_unit(int drv, const char *passphrase);
int ata_security_set_password(int drv, int master, const char *passphrase);
int ata_security_disable_password(int drv, const char *passphrase, int master);

int ata_device_config_identify(int drv);
int ata_security_freeze_lock(int drv);
int ata_security_unlock_device(int drv, const char *passphrase, int master);
int ata_cmd_complete_async();

/* @brief Enum with the possible state for each drive.
 * See ATA/ATAPI Command Set (ATA8-ACS) section 4.7.4
 */
enum ata_security_state {
    ATA_SEC0 = 0,   /* SEC0: Powered down/Security disabled */
    ATA_SEC1,       /* SEC1: Security disabled/not Frozen */
    ATA_SEC2,       /* SEC2: Security disabled/Frozen */
    _ATA_POFF3,     /* SEC3: Powered down/Security enabled */
    ATA_SEC4,       /* SEC4: Security enabled/Locked */
    ATA_SEC5,       /* SEC5: Unlocked/not Frozen */
    ATA_SEC6        /* SEC6: Unlocked/ Frozen    */
};

enum ata_security_state ata_security_get_state(int);

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
#define ATA_CMD_DEVICE_CONFIGURATION_IDENTIFY 0xB1
#define ATA_CMD_WRITE_DMA 0xCA
#define ATA_CMD_IDENTIFY_DEVICE 0xEC

#define ATA_IDENTIFY_DEVICE_COMMAND_LEN          (256 * 2)


/* Security feature set */
#define ATA_CMD_SECURITY_SET_PASSWORD       0xF1
#define ATA_CMD_SECURITY_UNLOCK             0xF2
#define ATA_CMD_SECURITY_ERASE_PREPARE      0xF3
#define ATA_CMD_SECURITY_ERASE_UNIT         0xF4
#define ATA_CMD_SECURITY_FREEZE_LOCK        0xF5
#define ATA_CMD_SECURITY_DISABLE_PASSWORD   0xF6

/* Constants for security set commands */
#define ATA_SECURITY_COMMAND_LEN                (256 * 2)
#define ATA_SECURITY_PASSWORD_OFFSET            (1 * 2)
#define ATA_ERR_BUSY -2
#define ATA_ERR_OP_IN_PROGRESS -3
#define ATA_ERR_OP_NOT_IN_PROGRESS -4
#endif
