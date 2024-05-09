/* ata.c
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
 *
 */
/**
 * @file ata.c
 * @brief This file contains the implementation of the ATA
 * (Advanced Technology Attachment) driver used for interacting with AHCI
 * (Advanced Host Controller Interface) based SATA drives.
 * It supports functions for drive initialization, read and write operations,
 * and device identification.
 */
#ifndef ATA_C
#define ATA_C
#include <stdint.h>
#include <x86/common.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <printf.h>
#include <string.h>

#define MAX_ATA_DRIVES 4
#define MAX_SECTOR_SIZE 512

#define CACHE_INVALID 0xBADF00DBADC0FFEEULL

#ifdef DEBUG_ATA
/**
 * @brief This macro is used to conditionally print debug messages for the ATA
 * driver. If DEBUG_ATA is defined, it calls the wolfBoot_printf function with
 * the specified format and arguments. Otherwise, it is defined as an empty
 * do-while loop.
 *
 * @param[in] ... Format and arguments for the debug printf.
 */
#define ATA_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define ATA_DEBUG_PRINTF(...) do {} while(0)
#endif /* DEBUG_ATA */


static int ata_drive_count = -1;
struct ata_async_info{
    int in_progress;
    int drv;
    int slot;
};

static struct ata_async_info ata_async_info;
/**
 * @brief This structure holds the necessary information for an ATA drive,
 * including AHCI base address, AHCI port number, and sector cache.
 */
struct ata_drive {
    uint32_t ahci_base;
    unsigned ahci_port;
    uint32_t clb_port;
    uint32_t ctable_port;
    uint32_t fis_port;
    uint32_t sector_size_shift;
    uint8_t  sector_cache[MAX_SECTOR_SIZE];
    uint64_t cached;
    enum ata_security_state sec;
};

/**
 * @brief This array holds instances of the `struct ata_drive` representing
 * multiple ATA drives (up to `MAX_ATA_DRIVES`).
 */
struct ata_drive ATA_Drv[MAX_ATA_DRIVES];

/**
 * @brief This packed structure defines a single entry in the HBA
 * (Host Bus Adapter) HBA PRDT (Physical Region Descriptor Table) used for
 * DMA data transfer.
 */
struct __attribute__((packed)) hba_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t _res0;
    uint32_t dbc:22, _res1:9, i:1;
};

/**
 * @brief This packed structure defines the layout of an HBA command table,
 * which contains command headers and PRDT entries for DMA data transfer.
 */
struct __attribute__((packed)) hba_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t _res[48];
    struct hba_prdt_entry prdt_entry[8];
};

/**
 * @brief This packed structure defines the format of an H2D Register FIS
 * used for ATA commands.
 */
struct __attribute__((packed)) fis_reg_h2d {
    uint8_t fis_type;

    uint8_t pmport:4, _res0:3, c:1;

    uint8_t command;
    uint8_t feature_l;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_h;

    uint16_t count;
    uint8_t icc;
    uint8_t control;

    uint32_t _res1;
};

/**
 * @brief This packed structure defines the format of a D2H Register FIS
 * used for ATA commands completion.
 */
struct __attribute__((packed)) fis_reg_d2h {
    uint8_t fis_type;

    uint8_t pmport:4, _res0:2, i:1, _res1:1;

    uint8_t status;
    uint8_t error;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t _res2;

    uint16_t count;
    uint16_t _res3;

    uint32_t _res4;
};


/**
 * @brief This packed structure defines the format of a Data FIS used for
 * data transfer.
 */
struct __attribute__((packed)) fis_data {
    uint8_t fis_type;
    uint8_t pmport:4, _res0:4;
    uint16_t res1;
    uint32_t data[0];
};

/**
 * @brief This function initializes a new ATA drive instance with the provided
 * parameters and stores it in the ATA_Drv array.
 *
 * @param[in] ahci_base The base address of the AHCI controller.
 * @param[in] ahci_port The port number of the AHCI controller.
 * @param[in] clb The command list base address for the drive.
 * @param[in] ctable The command table base address for the drive.
 * @param[in] fis The FIS base address for the drive.
 *
 * @return The index of the new ATA drive if successful, or -1 if the maximum
 * number of drives has been reached.
 */
int ata_drive_new(uint32_t ahci_base, unsigned ahci_port, uint32_t clb,
                  uint32_t ctable, uint32_t fis)
{
    struct ata_drive *ata = (void *)0;
    if (++ata_drive_count >= MAX_ATA_DRIVES)
        return -1;

    ata = &ATA_Drv[ata_drive_count];
    ata->ahci_base = ahci_base;
    ata->ahci_port = ahci_port;
    ata->clb_port = clb;
    ata->ctable_port = ctable;
    ata->fis_port = fis;
    ata->sector_size_shift = 9; /* 512 */
    ata->cached = CACHE_INVALID;
    return ata_drive_count;
}

/**
 * @brief This static function finds an available command slot for the specified
 * ATA drive and returns the slot number.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 *
 * @return The index of the available command slot if found, or -1 if no slot is
 * available.
 */
static int find_cmd_slot(int drv)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    uint32_t slots, sact, ci;
    uint32_t i;
    sact = mmio_read32((AHCI_PxSACT(ata->ahci_base, ata->ahci_port)));
    ci = mmio_read32((AHCI_PxCI(ata->ahci_base, ata->ahci_port)));
    slots = sact | ci;
    for (i = 0; i < 32; i++) {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    wolfBoot_printf("ATA: Cannot allocate command slot\r\n");
    return -1;
}

/**
 * @brief This static function prepares a command slot for DMA data transfer by
 * initializing the command header and command table entries.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] buf The buffer containing the data to be transferred.
 * @param[in] sz The size of the data to be transferred in bytes.
 *
 * @return The index of the prepared command slot if successful, or -1 if an error
 * occurs.
 */
static int prepare_cmd_h2d_slot(int drv, const uint8_t *buf, int sz, int w)
{
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct ata_drive *ata = &ATA_Drv[drv];
    int slot = find_cmd_slot(drv);
    if (slot < 0) {
        wolfBoot_printf("ATA: Operation aborted: no free command slot\r\n");
        return -1;
    }
    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;
    memset(cmd, 0, sizeof(struct hba_cmd_header));
    cmd->cfl = FIS_LEN_H2D / 4;
    cmd->ctba = (uint32_t)(ata->ctable_port);
    tbl = (struct hba_cmd_table *)(uintptr_t)(ata->ctable_port);
    memset(tbl, 0, sizeof(struct hba_cmd_table));
    cmd->prdtl = 1;
    cmd->w = w;
    tbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buf;
    tbl->prdt_entry[0].dbc = sz - 1;
    return slot;
}

/**
 * @brief Check the completion status of an asynchronous ATA command.
 *
 * This function checks the completion status of an asynchronous ATA command
 * that was previously initiated.
 *
 * @return
 *   - 0: Asynchronous ATA command completed successfully.
 *   - ATA_ERR_OP_NOT_IN_PROGRESS: No asynchronous operation in progress.
 *   - ATA_ERR_BUSY: The ATA operation is still in progress.
 *   - -1: ATA Task File error.
 */
int ata_cmd_complete_async()
{
    struct ata_drive *ata;
    int slot;

    if (!ata_async_info.in_progress)
        return ATA_ERR_OP_NOT_IN_PROGRESS;
    ata = &ATA_Drv[ata_async_info.drv];
    if (mmio_read32(AHCI_PxIS(ata->ahci_base, ata->ahci_port)) & AHCI_PORT_IS_TFES) {
        ata_async_info.in_progress = 0;
        return -1;
    }

    slot = ata_async_info.slot;
    if ((mmio_read32(AHCI_PxCI(ata->ahci_base, ata->ahci_port)) & (1 << slot)) != 0)
        return ATA_ERR_BUSY;

    ata_async_info.in_progress = 0;
    return 0;
}

/**
 * @brief This static function executes the command in the specified command
 * slot for the ATA drive, if async = 0 waits for the command to complete
 * otherwise it immediately returns ATA_ERR_BUSY. Software must call
 * `ata_cmd_complete_async()` to check for operation completion.  Only one
 * operation can run at a time, so this function returns ATA_OP_IN_PROGRESS if
 * another async operation is already in progress. Software must invoke
 * ata_cmd_complete_async() until it returns 0 or -1. Please note that the
 * function is NOT thread safe and is NOT safe if ATA functions are supposed to
 * interrupt the normal execution flow.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] slot The index of the command slot to execute.
 * @param[in] async Flag indicating whether the operation should be asynchronous (1) or synchronous (0).

 * @return:
 *   - -1: if an error occurs during command execution.
 *   - ATA_ERR_OP_IN_PROGRESS: if async  = 1 and another asynchronous operation is already in progress.
 *   - ATA_ERR_BUSY: If async = 1. To get cmd status invoke `ata_cmd_complete_async()`.
 *   - 0: success.
 */
static int exec_cmd_slot_ex(int drv, int slot, int async)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    uint32_t reg;

    if (ata_async_info.in_progress)
        return ATA_ERR_OP_IN_PROGRESS;

    /* Clear IS */
    reg = mmio_read32(AHCI_PxIS(ata->ahci_base, ata->ahci_port));
    mmio_write32(AHCI_PxIS(ata->ahci_base, ata->ahci_port), reg);

    /* Wait until port not busy */
    while (mmio_read32(AHCI_PxTFD(ata->ahci_base, ata->ahci_port)) & (ATA_DEV_BUSY | ATA_DEV_DRQ))
        ;

    mmio_write32((AHCI_PxCI(ata->ahci_base, ata->ahci_port)), 1 << slot);
    if (async) {
        ata_async_info.in_progress = 1;
        ata_async_info.drv = drv;
        ata_async_info.slot = slot;
        return ATA_ERR_BUSY;
    }

    while ((mmio_read32(AHCI_PxCI(ata->ahci_base, ata->ahci_port)) & (1 << slot)) != 0) {
        if (mmio_read32(AHCI_PxIS(ata->ahci_base, ata->ahci_port)) & AHCI_PORT_IS_TFES) {
            wolfBoot_printf("ATA: port error\r\n");
            return -1;
        }
    }
    return 0;
}

/**
 * @brief This static function executes the command in the specified command
 * slot for the ATA drive and waits for the command to complete.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] slot The index of the command slot to execute.
 *
 * @return 0 on success, or -1 if an error occurs during command execution.
 */
static int exec_cmd_slot(int drv, int slot)
{
    return exec_cmd_slot_ex(drv, slot, 0);
}

static void invert_buf(uint8_t *src, uint8_t *dst, unsigned len)
{
    unsigned i;
    memset(dst, 0, len);
    for (i = 0; i < len; i+=2) {
       dst[i] = src[i + 1];
       dst[i + 1] = src[i];
    }
    dst[len - 1]  = 0;
}

static void noninvert_buf(uint8_t *src, uint8_t *dst, unsigned len)
{
    memcpy(dst, src, len);
}

#ifndef ATA_BUF_SIZE
#define ATA_BUF_SIZE 8192
#endif
static uint8_t buffer[ATA_BUF_SIZE];

#define ATA_ID_SERIAL_NO_POS 10 * 2
#define ATA_ID_SERIAL_NO_LEN 20
#define ATA_ID_MODEL_NO_POS  27 * 2
#define ATA_ID_MODEL_NO_LEN  40


#define ATA_ID_COMMAND_SET_SUPPORTED_POS 82 * 2
#define ATA_ID_SECURITY_STATUS_POS 128 * 2


#ifdef WOLFBOOT_ATA_DISK_LOCK

/**
 * @brief Helper function to execute an ATA command from the security set.
 * This function groups the common code among all the API in the ATA security
 * set sending the same arguments.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] ata_cmd The ATA command to execute from the ATA security set.
 *
 * @return 0 on success, or -1 if an error occurs while preparing or executing
 * the ATA command.
 *
 */
static int security_command(int drv, uint8_t ata_cmd)
{
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct fis_reg_h2d *cmdfis;
    struct ata_drive *ata = &ATA_Drv[drv];
    int ret;
    int slot = prepare_cmd_h2d_slot(drv, buffer, ATA_SECURITY_COMMAND_LEN, 0);
    if (slot < 0) {
        return slot;
    }
    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;
    tbl = (struct hba_cmd_table *)(uintptr_t)cmd->ctba;
    cmdfis = (struct fis_reg_h2d *)(&tbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ata_cmd;
    ret = exec_cmd_slot(drv, slot);
    return ret;
}

/**
 * @brief Helper function to execute an ATA command from the security set that
 * require transmitting a passphrase.
 * This function groups the common code among all the API in the ATA security
 * set sending the same arguments.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] ata_cmd The ATA command to execute from the ATA security set.
 * @param[in] passphrase The passphrase to transmit as argument in the buffer
 * entry.
 * @param[in] async if the command will be executed in asynchronous mode
 * @param[in] master if the passphrase should compare with master
 * @return
 *   - 0: ATA command completed successfully.
 *   - ATA_ERR_OP_NOT_IN_PROGRESS: If async = 1 but no asynchronous operation in progress.
 *   - ATA_ERR_BUSY: If async = 1. To get cmd status invoke `ata_cmd_complete_async()`.
 */
static int security_command_passphrase(int drv, uint8_t ata_cmd,
                                       const char *passphrase, int async, int master)
{
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct fis_reg_h2d *cmdfis;
    struct ata_drive *ata = &ATA_Drv[drv];
    int ret;
    int slot = prepare_cmd_h2d_slot(drv, buffer,
            ATA_SECURITY_COMMAND_LEN, 1);
    memset(buffer, 0, ATA_SECURITY_COMMAND_LEN);
    if (master)
        buffer[0] = 0x1;
    memcpy(buffer + ATA_SECURITY_PASSWORD_OFFSET, passphrase, strlen(passphrase));
    if (slot < 0) {
        return slot;
    }
    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;
    tbl = (struct hba_cmd_table *)(uintptr_t)cmd->ctba;
    cmdfis = (struct fis_reg_h2d *)(&tbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ata_cmd;
    cmdfis->count = 1;
    ret = exec_cmd_slot_ex(drv, slot, async);
    return ret;
}


/**
 * @brief This function initiates the ATA command SECURITY FREEZE LOCK
 * as defined in specs ATA8-ACS Sec. 7.47.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_freeze_lock(int drv)
{
    return security_command(drv, ATA_CMD_SECURITY_FREEZE_LOCK);
}

/**
 * @brief This function initiates the ATA command SECURITY ERASE PREPARE
 * as defined in specs ATA8-ACS Sec. 7.45
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_erase_prepare(int drv)
{
    return security_command(drv, ATA_CMD_SECURITY_ERASE_PREPARE);
}

/**
 * @brief This function unlocks the access to the disk, using the the ATA
 * command SECURITY UNLOCK, as defined in specs ATA8-ACS Sec. 7.49
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] passphrase The passphrase of the disk unit.
 * @param[in] master if true compare with MASTER otherwise USER
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_unlock_device(int drv, const char *passphrase, int master)
{
    return security_command_passphrase(drv, ATA_CMD_SECURITY_UNLOCK, passphrase, 0, master);
}

/**
 * @brief This function enables security features on the disk, by setting a new
 * USER password to access the disk unit. This function uses the the ATA
 * command SECURITY SET PASSWORD, as defined in specs ATA8-ACS Sec. 7.48
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] passphrase The new USER passphrase for the disk unit.
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_set_password(int drv, int master, const char *passphrase)
{
    return security_command_passphrase(drv, ATA_CMD_SECURITY_SET_PASSWORD, passphrase, 0, master);
}

/**
 * @brief This function disables security features on the disk, by resetting
 * the USER password used to access the disk unit. This function uses the the ATA
 * command SECURITY DISABLE PASSWORD, as defined in specs ATA8-ACS Sec. 7.44
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] passphrase The old USER passphrase for the disk unit to reset.
 * @param[in] master if true compare with MASTER otherwise USER
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_disable_password(int drv, const char *passphrase, int master)
{
    return security_command_passphrase(drv, ATA_CMD_SECURITY_DISABLE_PASSWORD, passphrase, 0, master);
}

/**
 * @brief This function initiates the ATA command SECURITY ERASE UNIT, as defined
 * in specs ATA8-ACS Sec. 7.46
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] passphrase The USER/MASTER passphrase for the disk unit to erase.
 * @param[in] master if 1 compare againts the MASTER password instead of USER
 * password
 *
 * @return 0 on success, or -1 if an error occurred.
 */
int ata_security_erase_unit(int drv, const char *passphrase, int master)
{
    return security_command_passphrase(drv, ATA_CMD_SECURITY_ERASE_UNIT,
                                       passphrase, 1, master);
}

#endif /* WOLFBOOT_SATA_DISK_LOCK */

/**
 * @brief This function identifies the ATA device connected to the specified
 * ATA drive and retrieves its device information, such as serial number and
 * model name.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 *
 * @return 0 on success, or -1 if an error occurs during device identification.
 */
int ata_identify_device(int drv)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct fis_reg_h2d *cmdfis;
    uint8_t serial_no[ATA_ID_SERIAL_NO_LEN];
    uint8_t model_no[ATA_ID_MODEL_NO_LEN];
    int ret = 0;
    int slot = prepare_cmd_h2d_slot(drv, buffer,
            ATA_IDENTIFY_DEVICE_COMMAND_LEN, 0);
    int s_locked, s_frozen, s_enabled, s_supported;

    if (slot < 0)
        return slot;

    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;

    tbl = (struct hba_cmd_table *)(uintptr_t)cmd->ctba;
    cmdfis = (struct fis_reg_h2d *)(&tbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_IDENTIFY_DEVICE;
    ret = exec_cmd_slot(drv, slot);
    if (ret == 0) {
        uint16_t *id_buf = (uint16_t *)buffer;
        uint16_t cmd_set_supported;
        uint16_t sec_status;
        ATA_DEBUG_PRINTF("Device identified\r\n");
        ATA_DEBUG_PRINTF("Cylinders: %d\r\n", id_buf[1]);
        ATA_DEBUG_PRINTF("Heads: %d\r\n", id_buf[3]);
        ATA_DEBUG_PRINTF("Sectors per track: %d\r\n", id_buf[6]);
        ATA_DEBUG_PRINTF("Vendor: %04x:%04x:%04x\r\n", id_buf[7], id_buf[8],
                id_buf[9]);
        invert_buf(&buffer[ATA_ID_SERIAL_NO_POS], serial_no,
                ATA_ID_SERIAL_NO_LEN);
        ATA_DEBUG_PRINTF("Serial n.: %s\r\n", serial_no);

        invert_buf(&buffer[ATA_ID_MODEL_NO_POS], model_no,
                ATA_ID_MODEL_NO_LEN);
        ATA_DEBUG_PRINTF("Model: %s\r\n", model_no);


        ATA_DEBUG_PRINTF("Security mode information:\r\n");
        memcpy(&cmd_set_supported, buffer + ATA_ID_COMMAND_SET_SUPPORTED_POS, 2);

        s_supported = cmd_set_supported & (1 << 1);

        ATA_DEBUG_PRINTF(" - Security Mode feature set: %ssupported\r\n",
                s_supported ? "":"not ");

        ata->sec = ATA_SEC0;
        if (s_supported) {
            memcpy(&sec_status, buffer + ATA_ID_SECURITY_STATUS_POS, 2);
            ATA_DEBUG_PRINTF(" = Security Level: %s\r\n",
                    sec_status & (1 << 8) ? "Maximum":"High");

            if (sec_status & (1 << 5)) {
                ATA_DEBUG_PRINTF("Enhanced security erase supported\r\n");
            }
            if (sec_status & (1 << 4)) {
                ATA_DEBUG_PRINTF("Security: count expired\r\n");
            }
            if (sec_status & (1 << 3)) {
                ATA_DEBUG_PRINTF("Security: frozen\r\n");
                s_frozen = 1;
            }
            if (sec_status & (1 << 2)) {
                ATA_DEBUG_PRINTF("Security: locked\r\n");
                s_locked = 1;
            }
            if (sec_status & (1 << 1)) {
                ATA_DEBUG_PRINTF("Security: enabled\r\n");
                s_enabled = 1;
            }
            if (sec_status & (1 << 0)) {
                ATA_DEBUG_PRINTF("Security: supported\r\n");
                s_supported = 1;
            }
            if (!s_enabled && !s_frozen)
                ata->sec = ATA_SEC1;
            else if (!s_enabled && s_frozen)
                ata->sec = ATA_SEC2;
            else if (s_enabled && s_locked)
                ata->sec = ATA_SEC4;
            else if (!s_locked && !s_frozen)
                ata->sec = ATA_SEC5;
            else if (!s_locked && s_frozen)
                ata->sec = ATA_SEC6;
        }
        ATA_DEBUG_PRINTF(" - Security state: SEC%d\r\n",
                (int)ata->sec);
    }
    return ret;
}

enum ata_security_state ata_security_get_state(int drv)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    return ata->sec;
}

static int ata_drive_read_sector(int drv, uint64_t start, uint32_t count,
        uint8_t *buf)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct fis_reg_h2d *cmdfis;
    int i;
    int slot = prepare_cmd_h2d_slot(drv, buf, count << ata->sector_size_shift, 0);
    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;

    tbl = (struct hba_cmd_table *)(uintptr_t)cmd->ctba;
    cmdfis = (struct fis_reg_h2d *)(&tbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_READ_DMA_EX;
    cmdfis->lba0 = (uint8_t)(start & 0xFF);
    cmdfis->lba1 = (uint8_t)((start >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((start >> 16) & 0xFF);
    cmdfis->lba3 = (uint8_t)((start >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((start >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((start >> 40) & 0xFF);
    cmdfis->device = (1 << 6); /* LBA mode */
    cmdfis->count = (uint16_t)(count & 0xFFFF);
    exec_cmd_slot(drv, slot);
    return count << ata->sector_size_shift;
}

static int ata_drive_write_sector(int drv, uint64_t start, uint32_t count,
        const uint8_t *buf)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    struct hba_cmd_header *cmd;
    struct hba_cmd_table *tbl;
    struct fis_reg_h2d *cmdfis;
    uint8_t *buf_ptr;
    int i;
    int slot = prepare_cmd_h2d_slot(drv, buf, count << ata->sector_size_shift, 1);
    cmd = (struct hba_cmd_header *)(uintptr_t)ata->clb_port;
    cmd += slot;
    tbl = (struct hba_cmd_table *)(uintptr_t)cmd->ctba;
    cmdfis = (struct fis_reg_h2d *)(&tbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_WRITE_DMA;
    cmdfis->lba0 = (uint8_t)(start & 0xFF);
    cmdfis->lba1 = (uint8_t)((start >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((start >> 16) & 0xFF);
    cmdfis->lba3 = (uint8_t)((start >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((start >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((start >> 40) & 0xFF);
    cmdfis->device = (1 << 6); /* LBA mode */
    cmdfis->count = (uint16_t)(count & 0xFFFF);

    exec_cmd_slot(drv, slot);
    return count << ata->sector_size_shift;
}

static void ata_invalidate_cache(int drv)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    ata->cached = CACHE_INVALID;
}

static void ata_cache_pull(int drv, uint64_t sector)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    if (ata->cached == sector)
        return;
    if (ata_drive_read_sector(drv, sector, 1, ata->sector_cache) < 0) {
        ata_invalidate_cache(drv);
        return;
    }
    ata->cached = sector;
}

static void ata_cache_commit(int drv)
{
    struct ata_drive *ata = &ATA_Drv[drv];
    if (ata->cached == CACHE_INVALID)
        return;
    ata_drive_write_sector(drv, ata->cached, 1, ata->sector_cache);
}

/**
 * @brief This function reads data from the specified ATA drive starting from
 * the given sector and copies it into the provided buffer. It handles partial
 * reads and multiple sector transfers.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] start The starting sector number to read from.
 * @param[in] size The size of the data to read in bytes.
 * @param[out] buf The buffer to store the read data.
 *
 * @return The number of bytes read into the buffer, or -1 if an error occurs.
 */
int ata_drive_read(int drv, uint64_t start, uint32_t size, uint8_t *buf)
{
    uint64_t sect_start, sect_off;
    uint32_t count = 0;
    struct ata_drive *ata = &ATA_Drv[drv];
    uint32_t buffer_off = 0;
    sect_start = start >> ata->sector_size_shift;
    sect_off = start - (sect_start << ata->sector_size_shift);

    if (drv > ata_drive_count)
        return -1;

    if (sect_off > 0) {
        uint32_t len = MAX_SECTOR_SIZE - sect_off;
        if (len > size)
            len = size;
        ata_cache_pull(drv, sect_start);
        if (ata->cached != sect_start)
            return -1;
        memcpy(buf, ata->sector_cache + sect_off, len);
        size -= len;
        buffer_off += len;
        sect_start++;
    }
    if (size > 0)
        count = size >> ata->sector_size_shift;
    if (count > 0) {
        if (ata_drive_read_sector(drv, sect_start, count, buf + buffer_off) < 0)
            return -1;
        size -= (count << ata->sector_size_shift);
        buffer_off += (count << ata->sector_size_shift);
        sect_start += count;
    }
    if (size > 0) {
        ata_cache_pull(drv, sect_start);
        memcpy(buf + buffer_off, ata->sector_cache, size);
        buffer_off += size;
    }
    return buffer_off;
}

/**
 * @brief This function writes data from the provided buffer to the specified ATA
 * drive starting from the given sector. It handles partial writes and multiple
 * sector transfers.
 *
 * @param[in] drv The index of the ATA drive in the ATA_Drv array.
 * @param[in] start The starting sector number to write to.
 * @param[in] size The size of the data to write in bytes.
 * @param[in] buf The buffer containing the data to write.
 *
 * @return The number of bytes written to the drive, or -1 if an error occurs.
 */
int ata_drive_write(int drv, uint64_t start, uint32_t size,
        const uint8_t *buf)
{
    uint64_t sect_start, sect_off;
    uint32_t count;
    struct ata_drive *ata = &ATA_Drv[drv];
    uint32_t buffer_off = 0;
    sect_start = start >> ata->sector_size_shift;
    sect_off = start - (sect_start << ata->sector_size_shift);

    if (sect_off > 0) {
        uint32_t len = MAX_SECTOR_SIZE - sect_off;
        if (len > size)
            len = size;
        ata_cache_pull(drv, sect_start);
        if (ata->cached != sect_start)
            return -1;
        memcpy(ata->sector_cache + sect_off, buf, len);
        ata_cache_commit(drv);
        size -= len;
        buffer_off += len;
        sect_start++;
    }
    if (size > 0)
        count = size >> ata->sector_size_shift;
    if (count > 0) {
        if (ata_drive_write_sector(drv, sect_start, count, buf + buffer_off) < 0)
            return -1;
        size -= (count << ata->sector_size_shift);
        buffer_off += (count << ata->sector_size_shift);
        sect_start += count;
    }
    if (size > 0) {
        ata_cache_pull(drv, sect_start);
        memcpy(ata->sector_cache, buf + buffer_off, size);
        ata_cache_commit(drv);
        buffer_off += size;
    }
    return buffer_off;
}
#endif /* ATA_C */
