/* ahci.c
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
 * @file ahci.c
 *
 * @brief AHCI (Advanced Host Controller Interface) Implementation.
 *
 * This file contains the implementation of the AHCI (Advanced Host Controller
 * Interface) driver. It includes functions to enable and disable the AHCI
 * controller, detect SATA disks, and initialize ATA drives for detected disks.
 */

#ifndef AHCI_H_
#define AHCI_H_
#include <stdint.h>

#include <x86/common.h>
#include <printf.h>
#include <pci.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <string.h>

#if defined(WOLFBOOT_TPM_SEAL)
#include <image.h>
#include <tpm.h>

#if defined(WOLFBOOT_FSP)
#include <stage2_params.h>
#endif

#include <wolfssl/wolfcrypt/coding.h>
/* hardcode to ecc256 algo for now */
#define TPM_MAX_POLICY_SIZE (512)
#ifndef ATA_UNLOCK_DISK_KEY_NV_INDEX
#define ATA_UNLOCK_DISK_KEY_NV_INDEX 0
#endif

#define ATA_SECRET_RANDOM_BYTES 21

#endif /* WOLFBOOT_TPM_SEAL */

#if defined(WOLFBOOT_ATA_DISK_LOCK_PASSWORD) || defined(WOLFBOOT_TPM_SEAL)
#ifndef ATA_UNLOCK_DISK_KEY_SZ
#define ATA_UNLOCK_DISK_KEY_SZ 32
#endif
#ifndef WOLFBOOT_TPM_SEAL_KEY_ID
#define WOLFBOOT_TPM_SEAL_KEY_ID (0)
#endif
#endif /* defined(WOLFBOOT_ATA_DISK_LOCK_PASSWORD) || defined(WOLFBOOT_TPM_SEAL) */

#if defined(WOLFBOOT_ATA_DISK_LOCK_PASSWORD) && defined(WOLFBOOT_TPM_SEAL)
#error "The secret to unlock the disk must be either a password or sealed with TPM, not both"
#endif

#define AHCI_ABAR_OFFSET     0x24
#ifdef TARGET_x86_fsp_qemu
#define SATA_BASE 0x02200000
#elif TARGET_kontron_vx3060_s2
#define SATA_BASE 0x02200000
#endif /* TARGET_qemu_fsp */


#define HBA_FIS_SIZE 0x100
#define HBA_CLB_SIZE 0x400
#define HBA_TBL_SIZE 0x800
#define HBA_TBL_ALIGN 0x80

static uint8_t ahci_hba_fis[HBA_FIS_SIZE * AHCI_MAX_PORTS]
__attribute__((aligned(HBA_FIS_SIZE)));
static uint8_t ahci_hba_clb[HBA_CLB_SIZE * AHCI_MAX_PORTS]
__attribute__((aligned(HBA_CLB_SIZE)));
static uint8_t ahci_hba_tbl[HBA_TBL_SIZE * AHCI_MAX_PORTS]
__attribute__((aligned(HBA_TBL_ALIGN)));

#define PCI_REG_PCS 0x92
#define PCI_REG_CLK 0x94
#define PCI_REG_PCS_PORT_ENABLE_MASK 0x3f
#define PCI_REG_PCS_OOB 1 << 15
#define PCI_REG_MAP 0x90
#define PCI_REG_MAP_AHCI_MODE (0x1 << 6)
#define PCI_REG_MAP_ALL_PORTS (0x1 << 5)

#define SATA_MAX_TRIES (5)
#define SATA_DELAY (100)

#ifdef DEBUG_AHCI
#define AHCI_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define AHCI_DEBUG_PRINTF(...) do {} while(0)
#endif /* DEBUG_AHCI */

/**
 * @brief Sets the AHCI Base Address Register (ABAR) for the given device.
 *
 * @param bus The PCI bus number of the AHCI device.
 * @param dev The PCI device number of the AHCI device.
 * @param fun The PCI function number of the AHCI device.
 * @param addr The address to set as the ABAR.
 */
static inline void ahci_set_bar(uint32_t bus, uint32_t dev,
                                uint32_t func, uint32_t addr)
{
    pci_config_write32(bus, dev, func, AHCI_ABAR_OFFSET, addr);
}

/**
 * @brief Initializes the SATA controller for the given device.
 *
 * This function initializes the SATA controller for the specified AHCI device
 * and detects connected SATA disks. It sets up the necessary registers and
 * configurations for the controller to function properly.
 *
 * @param bus The PCI bus number of the AHCI device.
 * @param dev The PCI device number of the AHCI device.
 * @param fun The PCI function number of the AHCI device.
 * @return 0 on success, or a negative value on failure.
 */
int init_sata_controller(uint32_t bus, uint32_t dev, uint32_t fun)
{
    uint16_t reg16;
    uint32_t reg;

    reg16 = pci_config_read16(bus, dev, fun, PCI_REG_PCS);
    /* enable all ports */
    reg16 |= 0x3f;
    reg16 |= PCI_REG_PCS_OOB;
    pci_config_write16(bus, dev, fun,
                       PCI_REG_PCS, reg16);

    reg = pci_config_read32(bus, dev, fun, PCI_REG_CLK);
    reg |= 0x193;
    pci_config_write32(bus, dev, fun, PCI_REG_CLK, reg);

    wolfBoot_printf("Device detected: %x\r\n",
                    reg16 & ~PCI_REG_PCS_PORT_ENABLE_MASK);

    return 0;
}

/**
 * @brief Enables the AHCI controller for the given device.
 *
 * This function enables the AHCI controller for the specified AHCI device
 * and returns the AHCI Base Address Register (ABAR) for accessing AHCI registers.
 *
 * @param bus The PCI bus number of the AHCI device.
 * @param dev The PCI device number of the AHCI device.
 * @param fun The PCI function number of the AHCI device.
 * @return The ABAR address on success, or 0 on failure.
 */
uint32_t ahci_enable(uint32_t bus, uint32_t dev, uint32_t fun)
{
    uint16_t reg16;
    uint32_t iobar;
    uint32_t reg;
    uint32_t bar;

    AHCI_DEBUG_PRINTF("ahci: enabling %x:%x.%x\r\n", bus, dev, fun);
    reg = pci_config_read16(bus, dev, fun, PCI_COMMAND_OFFSET);

    bar = pci_config_read32(bus, dev, fun, AHCI_ABAR_OFFSET);
    AHCI_DEBUG_PRINTF("PCI BAR: %08x\r\n", bar);
    iobar = pci_config_read32(bus, dev, fun, AHCI_AIDPBA_OFFSET);
    AHCI_DEBUG_PRINTF("PCI I/O space: %08x\r\n", iobar);

    reg |= PCI_COMMAND_BUS_MASTER;
    reg |= PCI_COMMAND_MEM_SPACE;
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, reg);

    reg = pci_config_read32(bus, dev, fun, PCI_INTR_OFFSET);
    AHCI_DEBUG_PRINTF("Interrupt pin for AHCI controller: %02x\r\n",
                    (reg >> 8) & 0xFF);
    pci_config_write32(bus, dev, fun, PCI_INTR_OFFSET,
                       (reg & 0xFFFFFF00 | 0x0a));
    AHCI_DEBUG_PRINTF("Setting interrupt line: 0x0A\r\n");

    return bar;
}

/**
 * @brief Dumps the status of the specified AHCI port.
 *
 * This function dumps the status of the AHCI port with the given index.
 * It prints the status of various port registers for debugging purposes.
 *
 * @param base The AHCI Base Address Register (ABAR) for accessing AHCI registers.
 * @param i The index of the AHCI port to dump status for.
 */
void ahci_dump_port(uint32_t base, int i)
{
    uint32_t cmd, ci, is, tfd, serr, ssst;

    cmd = mmio_read32(AHCI_PxCMD(base, i));
    ci = mmio_read32(AHCI_PxCI(base, i));
    is = mmio_read32(AHCI_PxIS(base, i));
    tfd = mmio_read32(AHCI_PxTFD(base, i));
    serr = mmio_read32(AHCI_PxSERR(base, i));
    ssst = mmio_read32(AHCI_PxSSTS(base, i));
    AHCI_DEBUG_PRINTF("%d: cmd:0x%x ci:0x%x is: 0x%x tfd: 0x%x serr: 0x%x ssst: 0x%x\r\n",
                    i, cmd, ci, is, tfd, serr, ssst);
}

#ifdef WOLFBOOT_ATA_DISK_LOCK
#ifdef WOLFBOOT_ATA_DISK_LOCK_PASSWORD
static int sata_get_unlock_secret(uint8_t *secret, int *secret_size)
{
    int password_len;

    password_len = strlen(WOLFBOOT_ATA_DISK_LOCK_PASSWORD);
    if (*secret_size < password_len)
        return -1;
    *secret_size = password_len;
    memcpy(secret, (uint8_t*)WOLFBOOT_ATA_DISK_LOCK_PASSWORD, *secret_size);
    return 0;
}
#endif /* WOLFBOOT_ATA_DISK_LOCK_PASSWORD */

#ifdef WOLFBOOT_TPM_SEAL

/**
 * @brief Calculate the SHA256 hash of the key.
 *
 * @param key_slot The key slot ID to calculate the hash for.
 * @param hash A pointer to store the resulting SHA256 hash.
 * @return 0 on success, -1 on failure
 */
static int get_key_sha256(uint8_t key_slot, uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha256 sha256_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return -1;

    wc_InitSha256(&sha256_ctx);
    while (i < (uint32_t)pubkey_sz) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > (uint32_t)pubkey_sz)
            blksz = pubkey_sz - i;
        wc_Sha256Update(&sha256_ctx, (pubkey + i), blksz);
        i += blksz;
    }
    wc_Sha256Final(&sha256_ctx, hash);
    return 0;
}

static int sata_get_random_base64(uint8_t *out, int *out_size)
{
    uint8_t rand[ATA_SECRET_RANDOM_BYTES];
    word32 base_64_len;
    int ret;

    ret = wolfBoot_get_random(rand, ATA_SECRET_RANDOM_BYTES);
    if (ret != 0)
        return ret;
    base_64_len = *out_size;
    ret = Base64_Encode_NoNl(rand, ATA_SECRET_RANDOM_BYTES, out, &base_64_len);
    if (ret != 0)
        return ret;

    /* double check we have a NULL-terminated string */
    if ((int)base_64_len < *out_size) {
        out[base_64_len] = '\0';
        base_64_len += 1;
    } else {
        out[base_64_len-1] = '\0';
    }
    *out_size = (int)base_64_len;
    return 0;
}

static int sata_create_and_seal_unlock_secret(const uint8_t *pubkey_hint,
                                              const uint8_t *policy,
                                              int policy_size,
                                              uint8_t *secret,
                                              int *secret_size)
{
    uint8_t secret_check[WOLFBOOT_MAX_SEAL_SZ];
    int secret_check_sz;
    int ret;

    if (*secret_size < ATA_UNLOCK_DISK_KEY_SZ)
        return -1;

    ret = sata_get_random_base64(secret, secret_size);
    if (ret == 0) {
        wolfBoot_printf("Creating new secret (%d bytes)\r\n", *secret_size);
        wolfBoot_printf("%s\r\n", secret);

        /* seal new secret */
        ret = wolfBoot_seal(pubkey_hint, policy, policy_size,
                            ATA_UNLOCK_DISK_KEY_NV_INDEX,
                            secret, *secret_size);
    }

    if (ret == 0) {
        /* unseal again to make sure it works */
        memset(secret_check, 0, sizeof(secret_check));
        ret = wolfBoot_unseal(pubkey_hint, policy, policy_size,
                              ATA_UNLOCK_DISK_KEY_NV_INDEX,
                              secret_check, &secret_check_sz);
        if (ret == 0) {
            if (*secret_size != secret_check_sz ||
                memcmp(secret, secret_check, secret_check_sz) != 0)
                {
                    wolfBoot_printf("secret check mismatch!\n");
                    ret = -1;
                }
        }

        wolfBoot_printf("Secret Check %d bytes\n", secret_check_sz);
        wolfBoot_printf("%s\r\n", secret_check);
        TPM2_ForceZero(secret_check, sizeof(secret_check));
    }

    if (ret == 0) {
        wolfBoot_printf("Secret %d bytes\n", *secret_size);
        wolfBoot_printf("%s\r\n", secret);
    }

    return ret;
}

static int sata_get_unlock_secret(uint8_t *secret, int *secret_size)
{
    uint8_t pubkey_hint[WOLFBOOT_SHA_DIGEST_SIZE];
    uint8_t policy[TPM_MAX_POLICY_SIZE];
    uint16_t policy_size;
    const uint8_t *pol;
    int secretCheckSz;
    int ret;

    if (*secret_size < ATA_UNLOCK_DISK_KEY_SZ)
        return -1;

#if defined(WOLFBOOT_FSP)
    ret = stage2_get_tpm_policy(&pol, &policy_size);
#else
#error "implement get_tpm_policy "
#endif

    if (policy_size > TPM_MAX_POLICY_SIZE)
        return -1;

    memcpy(policy, pol, policy_size);
    ret = get_key_sha256(WOLFBOOT_TPM_SEAL_KEY_ID, pubkey_hint);
    if (ret != 0) {
        wolfBoot_printf("failed to find key id %d\r\n", WOLFBOOT_TPM_SEAL_KEY_ID);
        return ret;
    }
    ret = wolfBoot_unseal(pubkey_hint, policy, policy_size,
                          ATA_UNLOCK_DISK_KEY_NV_INDEX,
                          secret, secret_size);
    if (ret != 0 && ((ret & RC_MAX_FMT1) == TPM_RC_HANDLE)) {
            wolfBoot_printf("Sealed secret does not exist!\r\n");
            ret = sata_create_and_seal_unlock_secret(pubkey_hint, policy, policy_size, secret,
                                                     secret_size);
    }
#if defined(WOLFBOOT_DEBUG_REMOVE_SEALED_ON_ERROR)
    if (ret != 0) {
        wolfBoot_printf("deleting secret and panic!\r\n");
        wolfBoot_delete_seal(ATA_UNLOCK_DISK_KEY_NV_INDEX);
        panic();
    }
#endif
    if (ret != 0) {
        wolfBoot_printf("get sealed unlock secret failed! %d (%s)\n", ret,
                        wolfTPM2_GetRCString(ret));
        return ret;
    }
    return ret;
}
#endif /* WOLFBOOT_TPM_SEAL */

#ifdef WOLFBOOT_ATA_DISABLE_USER_PASSWORD
static int sata_disable_password(int drv)
{
    enum ata_security_state ata_st;
    int r;
    wolfBoot_printf("DISK DISABLE PASSWORD\r\n");
    ata_st = ata_security_get_state(drv);
    wolfBoot_printf("ATA: State SEC%d\r\n", ata_st);
    if (ata_st == ATA_SEC4) {
        r = ata_security_unlock_device(drv, ATA_MASTER_PASSWORD, 1);
        wolfBoot_printf("ATA device unlock: returned %d\r\n", r);
        if (r == 0) {
            r = ata_security_disable_password(drv, ATA_MASTER_PASSWORD, 1);
            wolfBoot_printf("ATA disable password: returned %d\r\n", r);
        }
    }
    panic();
    return 0;
}
#endif

/**
 * @brief Unlocks a SATA disk for a given drive.
 *
 * This function unlocks a SATA disk identified by the specified drive number.
 * If the SATA disk has no user password set, this function locks the disk.
 *
 * @param drv The drive number of the SATA disk to be unlocked.
 * @return An integer indicating the success or failure of the unlocking
 * operation.
 *         - 0: Success (disk unlocked).
 *         - -1: Failure (unable to unlock the disk).
 */
int sata_unlock_disk(int drv, int freeze)
{
    int secret_size = ATA_UNLOCK_DISK_KEY_SZ;
    uint8_t secret[ATA_UNLOCK_DISK_KEY_SZ];
    enum ata_security_state ata_st;
    int r;

#ifdef WOLFBOOT_ATA_DISABLE_USER_PASSWORD
    sata_disable_password(0);
#endif
    r = sata_get_unlock_secret(secret, &secret_size);
    if (r != 0)
        return r;
#ifdef TARGET_x86_fsp_qemu
    wolfBoot_printf("DISK LOCK SECRET: %s\r\n", secret);
#endif

    ata_st = ata_security_get_state(drv);
    wolfBoot_printf("ATA: Security state SEC%d\r\n", ata_st);
#if defined(TARGET_x86_fsp_qemu)
    if (ata_st == ATA_SEC0)
        return 0;
#endif
    if (ata_st == ATA_SEC1) {
        if (freeze) {
            AHCI_DEBUG_PRINTF("ATA identify: calling freeze lock\r\n", r);
            r = ata_security_freeze_lock(drv);
            AHCI_DEBUG_PRINTF("ATA security freeze lock: returned %d\r\n", r);
            if (r != 0)
                return -1;
        } else {
            AHCI_DEBUG_PRINTF("ATA security freeze skipped\r\n");
        }
        r = ata_identify_device(drv);
        AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
        ata_st = ata_security_get_state(drv);
        wolfBoot_printf("ATA: Security disabled. State SEC%d\r\n", ata_st);
    }
    else if (ata_st == ATA_SEC4) {
        AHCI_DEBUG_PRINTF("ATA identify: calling device unlock\r\n", r);
        r = ata_security_unlock_device(drv, (char*)secret, 0);
        AHCI_DEBUG_PRINTF("ATA device unlock: returned %d\r\n", r);
        r = ata_identify_device(drv);
        AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
        ata_st = ata_security_get_state(drv);
        if (ata_st == ATA_SEC5) {
            if (freeze) {
                AHCI_DEBUG_PRINTF("ATA identify: calling freeze lock\r\n", r);
                r = ata_security_freeze_lock(drv);
                AHCI_DEBUG_PRINTF("ATA security freeze lock: returned %d\r\n",
                                  r);
                if (r != 0)
                    return -1;
            } else {
                AHCI_DEBUG_PRINTF("ATA security freeze skipped\r\n");
            }
            r = ata_identify_device(drv);
            AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
        }
        ata_st = ata_security_get_state(drv);
        AHCI_DEBUG_PRINTF("ATA: Security enabled. State SEC%d\r\n", ata_st);
        if ((freeze && ata_st != ATA_SEC6) || (!freeze && ata_st != ATA_SEC5)) {
            panic();
        }
        ata_st = ata_security_get_state(drv);
        wolfBoot_printf("ATA: Security enabled. State SEC%d\r\n", ata_st);
    }
    return 0;
}
#endif /* WOLFBOOT_ATA_DISK_LOCK */

/**
 * @brief Waits until a specific address is cleared by a given mask.
 *
 * This function waits until a specific 32-bit PCI memory address is cleared by a given
 * mask. After SATA_DELAY * SATA_MAX_TRIES ms, if the address in memory is not
 * cleared, the function returns -1. *
 * @param[in] address The memory address to monitor.
 * @param[in] mask The mask to apply to the value at the address.
 *
 * @return 0 if the masked bits are cleared within the specified number of
 * tries, 1 otherwise.
 */
static int sata_wait_until_clear(uint32_t address, uint32_t mask)
{
    int count = SATA_MAX_TRIES;
    uint32_t reg;
    int ret = -1;

    while (1) {
        if (count-- == 0)
            break;

        reg = mmio_read32(address);
        if ((reg & mask) == 0) {
            ret = 0;
            break;
        }
        delay(SATA_DELAY);
    }
    
    return ret;
}

/**
 * @brief Enables SATA ports and detects connected SATA disks.
 *
 * This function enables SATA ports in the AHCI controller and detects connected SATA disks.
 * It initializes the ATA drives for the detected disks.
 *
 * @param base The AHCI Base Address Register (ABAR) for accessing AHCI registers.
 */
void sata_enable(uint32_t base)
{
    volatile uint32_t count;
    uint32_t cap, ports_impl;
    uint32_t fis, clb, tbl;
    uint8_t sata_only;
    uint8_t cap_sud;
    uint32_t n_ports;
    uint32_t i, j;
    uint64_t data64;
    uint32_t data;
    uint32_t reg;
    int drv;
    int r;

    mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_AE);

    /* Wait until enabled. */
    while ((mmio_read32(AHCI_HBA_GHC(base)) & HBA_GHC_AE) == 0)
        ;

    AHCI_DEBUG_PRINTF("AHCI memory mapped at %08x\r\n", base);

    /* Resetting the controller */
    mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_HR | HBA_GHC_IE);

    /* Wait until reset is complete */
    r = sata_wait_until_clear(AHCI_HBA_GHC(base), HBA_GHC_HR);
    if (r != 0)  {
        wolfBoot_printf("ACHI: timeout waiting reset\r\n");
        panic();
    }
    
    /* Wait until enabled. */
    if ((mmio_read32(AHCI_HBA_GHC(base)) & HBA_GHC_AE) == 0)
          mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_AE);

    AHCI_DEBUG_PRINTF("AHCI reset complete.\r\n");

    cap = mmio_read32(AHCI_HBA_CAP(base));
    n_ports = (cap & 0x1F) + 1;
    sata_only = (cap & AHCI_CAP_SAM);
    cap_sud = (cap & AHCI_CAP_SSS);

    if (!sata_only)
        mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_AE);

    ports_impl = mmio_read32(AHCI_HBA_PI(base));

    /* Clear global HBA IS */
    reg = mmio_read32(AHCI_HBA_IS(base));
    mmio_write32(AHCI_HBA_IS(base), reg);
    AHCI_DEBUG_PRINTF("AHCI HBA: Cleared IS\r\n");

    AHCI_DEBUG_PRINTF("AHCI: %d ports\r\n", n_ports);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((ports_impl & (1 << i)) != 0) {
            uint32_t reg;
            uint32_t ssts = mmio_read32(AHCI_PxSSTS(base, i));
            uint8_t ipm = (ssts >> 8) & 0xFF;
            uint8_t det = ssts & 0x0F;
            volatile struct hba_cmd_header *hdr;


            data = mmio_read32(AHCI_PxCMD(base, i));
            /* Detect POD */
            if ((data & AHCI_PORT_CMD_CPD) != 0) {
                AHCI_DEBUG_PRINTF("AHCI port %d: POD\r\n", i);
                mmio_or32(AHCI_PxCMD(base, i), AHCI_PORT_CMD_POD);
            }

            /* Detect pre-spinning */
            if (cap_sud != 0) {
                AHCI_DEBUG_PRINTF("AHCI port %d: Spinning\r\n", i);
                mmio_or32(AHCI_PxCMD(base, i), AHCI_PORT_CMD_SUD);
            }

            /* Disable aggressive powersaving */
            mmio_or32(AHCI_PxSCTL(base, i), (0x03 << 8));

            /* Disable interrupt reporting to SW */
            count = 0;
            while (1) {
                ssts = mmio_read32(AHCI_PxSSTS(base, i));
                ipm = (ssts >> 8) & 0x0F;
                ssts &= AHCI_SSTS_DET_MASK;
                if (ssts == AHCI_PORT_SSTS_DET_PCE)
                    break;
                if (count++ > SATA_MAX_TRIES) {
                    AHCI_DEBUG_PRINTF("AHCI port %d: Timeout occurred.\r\n", i);
                    break;
                }
                delay(SATA_DELAY);
            };

            if (ssts == 0) {
                wolfBoot_printf("AHCI port %d: No disk detected\r\n", i);
            } else {
                wolfBoot_printf("AHCI port %d: Disk detected (det: %02x ipm: %02x)\r\n",
                                i, det, ipm);

                /* Clear port SERR */
                reg = mmio_read32(AHCI_PxSERR(base, i));
                mmio_write32(AHCI_PxSERR(base,i), reg);
                AHCI_DEBUG_PRINTF("AHCI port: Cleared SERR\r\n");

                /* Clear port IS */
                reg = mmio_read32(AHCI_PxIS(base, i));
                mmio_write32(AHCI_PxIS(base,i), reg);
                AHCI_DEBUG_PRINTF("AHCI port: Cleared IS\r\n");

                /* Send STOP command */
                reg = mmio_read32(AHCI_PxCMD(base, i));
                if ((reg & (AHCI_PORT_CMD_START | AHCI_PORT_CMD_CR)) != 0) {
                    if (reg & AHCI_PORT_CMD_START)
                        mmio_write32(AHCI_PxCMD(base, i),
                                (reg & (~AHCI_PORT_CMD_START)));
                }
                AHCI_DEBUG_PRINTF("AHCI port: Sending STOP ...\r\n");

                /* Wait for CR to be cleared */
                r = sata_wait_until_clear(AHCI_PxCMD(base, i), AHCI_PORT_CMD_CR);
                if (r != 0) {
                    wolfBoot_printf("AHCI Error: Port did not clear CR!\r\n");
                    panic();
                }
                
                AHCI_DEBUG_PRINTF("AHCI port: Sent STOP.\r\n");

                AHCI_DEBUG_PRINTF("AHCI port: Disabling FIS ...\r\n");
                /* Disable FIS RX */
                reg = mmio_read32(AHCI_PxCMD(base, i));
                if (reg & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_START)) {
                    wolfBoot_printf("AHCI Error: Could not disable FIS while DMA is running\r");
                } else if ((reg & AHCI_PORT_CMD_FR) != 0) {
                    mmio_write32(AHCI_PxCMD(base, i),
                            reg & (~AHCI_PORT_CMD_FRE));
                }

                /* Wait for FR to be cleared */
                r = sata_wait_until_clear(AHCI_PxCMD(base, i), AHCI_PORT_CMD_FR);
                if (r != 0 ) {
                    wolfBoot_printf("AHCI Error: Port did not clear FR!\r\n");
                    panic();
                }
                
                AHCI_DEBUG_PRINTF("AHCI port: FIS disabled.\r\n");

                clb = (uint32_t)(uintptr_t)(ahci_hba_clb + i * HBA_CLB_SIZE);
                fis = (uint32_t)(uintptr_t)(ahci_hba_fis + i * HBA_FIS_SIZE);
                tbl = (uint32_t)(uintptr_t)(ahci_hba_tbl + i * HBA_TBL_SIZE);

                /* Initialize FIS and CLB address */
                mmio_write32(AHCI_PxCLB(base, i),
                             (uint32_t)(uintptr_t)(clb));
                mmio_write32(AHCI_PxCLBH(base, i), 0);

                mmio_write32(AHCI_PxFB(base, i),
                             (uint32_t)(uintptr_t)(fis));
                mmio_write32(AHCI_PxFBH(base, i), 0);

                memset((uint8_t*)(uintptr_t)clb, 0, HBA_CLB_SIZE);
                memset((uint8_t*)(uintptr_t)fis, 0, HBA_FIS_SIZE);

                /* Wait until CR is cleared */
                r = sata_wait_until_clear(AHCI_PxCMD(base, i), AHCI_PORT_CMD_CR);
                if (r != 0) {
                    wolfBoot_printf("AHCI error: CR clear error\r\n");
                    panic();
                }

                reg |= AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_START;
                mmio_write32(AHCI_PxCMD(base, i), reg);

                AHCI_DEBUG_PRINTF("AHCI port %d command engine started\r\n", i);

                /* Put port into active state */
                reg = mmio_read32(AHCI_PxCMD(base, i));
                mmio_write32(AHCI_PxCMD(base, i), reg | AHCI_PORT_CMD_ICC_ACTIVE);

                /* Check device type by signature */
                reg = mmio_read32(AHCI_PxSIG(base, i));
                AHCI_DEBUG_PRINTF("SATA disk drive detected on AHCI. Sign: %x\r\n",
                            reg);
                if (reg == AHCI_PORT_SIG_SATA) {
                    wolfBoot_printf("SATA disk drive detected on AHCI port %d\r\n",
                            i);
                    drv = ata_drive_new(base, i, clb, tbl, fis);
                    if (drv < 0) {
                        wolfBoot_printf("Failed to associate ATA drive to disk\r\n");
                    } else {
                        AHCI_DEBUG_PRINTF("ATA%d associated to AHCI port %d\r\n",
                                drv, i);
                        r = ata_identify_device(drv);
                        AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
                    }
                } else {
                    AHCI_DEBUG_PRINTF("AHCI port %d: device with signature %08x is not supported\r\n",
                            i, reg);
                }
            }
        }
    }
}

/**
 * @brief Disables SATA ports in the AHCI controller.
 *
 * This function disables SATA ports in the AHCI controller and stops any DMA operation.
 * It clears status registers and puts the AHCI ports into an inactive state.
 *
 * @param base The AHCI Base Address Register (ABAR) for accessing AHCI registers.
 */
void sata_disable(uint32_t base)
{
    uint32_t ports_impl;
    uint32_t i, reg;
    volatile uint32_t count;
    int r;
    
    AHCI_DEBUG_PRINTF("SATA: disabling sata controller at 0x%x\r\n", base);

    ports_impl = mmio_read32(AHCI_HBA_PI(base));

    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((ports_impl & (1 << i)) == 0)
            continue;
        AHCI_DEBUG_PRINTF("AHCI: disabling port %d\r\n", i);
        /* Clear port SERR */
        reg = mmio_read32(AHCI_PxSERR(base, i));
        mmio_write32(AHCI_PxSERR(base,i), reg);

        /* Clear port IS */
        reg = mmio_read32(AHCI_PxIS(base, i));
        mmio_write32(AHCI_PxIS(base,i), reg);

        /* Send STOP command */
        reg = mmio_read32(AHCI_PxCMD(base, i));
        if ((reg & (AHCI_PORT_CMD_START | AHCI_PORT_CMD_CR)) != 0) {
            if (reg & AHCI_PORT_CMD_START)
                mmio_write32(AHCI_PxCMD(base, i),
                        (reg & (~AHCI_PORT_CMD_START)));
        }

        /* Wait for CR to be cleared */
        r = sata_wait_until_clear(AHCI_PxCMD(base, i), AHCI_PORT_CMD_CR);
        if (r != 0) {
            wolfBoot_printf("AHCI error: CR clear error\r\n");
            panic();
        }
        
        /* Disable FIS RX */
        reg = mmio_read32(AHCI_PxCMD(base, i));
        if (reg & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_START)) {
            wolfBoot_printf("AHCI Error: Could not disable FIS while DMA is running\r\n");
        } else if ((reg & AHCI_PORT_CMD_FR) != 0) {
            mmio_write32(AHCI_PxCMD(base, i),
                    reg & (~AHCI_PORT_CMD_FRE));
        }

        /* Wait for FR to be cleared */
        r = sata_wait_until_clear(AHCI_PxCMD(base, i), AHCI_PORT_CMD_FR);
        if (r != 0) {
            wolfBoot_printf("AHCI error: FR clear error\r\n");
            panic();
        }
        reg = mmio_read32(AHCI_PxCMD(base, i));
        mmio_write32(AHCI_PxCMD(base, i),
                reg & (~AHCI_PORT_CMD_ICC_ACTIVE));

    }

}
#endif /* AHCI_H_ */

