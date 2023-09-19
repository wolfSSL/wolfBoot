/* ahci.c
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

/**
 * @brief Enables SATA ports and detects connected SATA disks.
 *
 * This function enables SATA ports in the AHCI controller and detects connected SATA disks.
 * It initializes the ATA drives for the detected disks.
 *
 * @param base The AHCI Base Address Register (ABAR) for accessing AHCI registers.
 */
void sata_enable(uint32_t base) {
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


    mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_AE);

    /* Wait until enabled. */
    while ((mmio_read32(AHCI_HBA_GHC(base)) & HBA_GHC_AE) == 0)
        ;

    AHCI_DEBUG_PRINTF("AHCI memory mapped at %08x\r\n", base);

    /* Resetting the controller */
    mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_HR | HBA_GHC_IE);

    /* Wait until reset is complete */
    while ((mmio_read32(AHCI_HBA_GHC(base)) & HBA_GHC_HR) != 0)
        ;

    /* Wait until enabled. */
    if ((mmio_read32(AHCI_HBA_GHC(base)) & HBA_GHC_AE) == 0)
          mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_AE);;

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
#ifdef WOLFBOOT_ATA_DISK_LOCK
            const char user_passphrase[] = WOLFBOOT_ATA_DISK_LOCK_PASSWORD;
#endif

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
            //mmio_write32(AHCI_PxIE(base, i), 0);

            count = 0;
            while (1) {
                ssts = mmio_read32(AHCI_PxSSTS(base, i));
                ipm = (ssts >> 8) & 0x0F;
                ssts &= AHCI_SSTS_DET_MASK;
                if (ssts == AHCI_PORT_SSTS_DET_PCE)
                    break;
                if (count++ > 5) {
                    AHCI_DEBUG_PRINTF("AHCI port %d: Timeout occurred.\r\n", i);
                    break;
                }
                delay(1000);
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
                count = 0;
                do {
                    reg = mmio_read32(AHCI_PxCMD(base, i));
                    if (count++ > 5) {
                        AHCI_DEBUG_PRINTF("AHCI Error: Port did not clear CR!\r\n");
                        break;
                    }
                    delay(1000);
                } while ((reg & AHCI_PORT_CMD_CR) != 0);
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
                count = 0;
                do {
                    reg = mmio_read32(AHCI_PxCMD(base, i));
                    if (count++ > 5) {
                        wolfBoot_printf("AHCI Error: Port did not clear FR!\r\n");
                        break;
                    }
                    delay(1000);
                } while ((reg & AHCI_PORT_CMD_FR) != 0);
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
                do {
                    reg = mmio_read32(AHCI_PxCMD(base, i));
                } while(reg & AHCI_PORT_CMD_CR);

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
                    int drv;
                    wolfBoot_printf("SATA disk drive detected on AHCI port %d\r\n",
                            i);
                    drv = ata_drive_new(base, i, clb, tbl, fis);
                    if (drv < 0) {
                        wolfBoot_printf("Failed to associate ATA drive to disk\r\n");
                    } else {
                        char buf[512] ="";
                        int r;
                        enum ata_security_state ata_st;
                        AHCI_DEBUG_PRINTF("ATA%d associated to AHCI port %d\r\n",
                                drv, i);
                        r = ata_identify_device(drv);
                        AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);


#ifdef WOLFBOOT_ATA_DISK_LOCK
                        ata_st = ata_security_get_state(drv);
                        wolfBoot_printf("ATA: Security state SEC%d\r\n", ata_st);
                        if (ata_st == ATA_SEC1) {
                            AHCI_DEBUG_PRINTF("ATA identify: calling freeze lock\r\n", r);
                            r = ata_security_freeze_lock(drv);
                            AHCI_DEBUG_PRINTF("ATA security freeze lock: returned %d\r\n", r);
                            r = ata_identify_device(drv);
                            AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
                            ata_st = ata_security_get_state(drv);
                            wolfBoot_printf("ATA: Security disabled. State SEC%d\r\n", ata_st);
                        }
                        else if (ata_st == ATA_SEC4) {
                            AHCI_DEBUG_PRINTF("ATA identify: calling device unlock\r\n", r);
                            r = ata_security_unlock_device(drv, user_passphrase);
                            AHCI_DEBUG_PRINTF("ATA device unlock: returned %d\r\n", r);
                            r = ata_identify_device(drv);
                            AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
                            ata_st = ata_security_get_state(drv);
                            if (ata_st == ATA_SEC5) {
                                AHCI_DEBUG_PRINTF("ATA identify: calling device freeze\r\n", r);
                                r = ata_security_freeze_lock(drv);
                                AHCI_DEBUG_PRINTF("ATA device freeze: returned %d\r\n", r);
                                r = ata_identify_device(drv);
                                AHCI_DEBUG_PRINTF("ATA identify: returned %d\r\n", r);
                            }
                            ata_st = ata_security_get_state(drv);
                            if (ata_st != ATA_SEC6) {
                                panic();
                            }
                            ata_st = ata_security_get_state(drv);
                            wolfBoot_printf("ATA: Security enabled. State SEC%d\r\n", ata_st);
                        }
#endif
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
    uint32_t i, reg;
    volatile uint32_t count;

    for (i = 0; i < AHCI_MAX_PORTS; i++) {
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
        count = 0;
        do {
            reg = mmio_read32(AHCI_PxCMD(base, i));
            if (count++ > 5) {
                break;
            }
            delay(1000);
        } while ((reg & AHCI_PORT_CMD_CR) != 0);

        /* Disable FIS RX */
        reg = mmio_read32(AHCI_PxCMD(base, i));
        if (reg & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_START)) {
            wolfBoot_printf("AHCI Error: Could not disable FIS while DMA is running\r\n");
        } else if ((reg & AHCI_PORT_CMD_FR) != 0) {
            mmio_write32(AHCI_PxCMD(base, i),
                    reg & (~AHCI_PORT_CMD_FRE));
        }

        /* Wait for FR to be cleared */
        count = 0;
        do {
            reg = mmio_read32(AHCI_PxCMD(base, i));
            if (count++ > 5) {
                wolfBoot_printf("AHCI Error: Port did not clear FR!\r\n"); 
                break;
            }
            delay(1000);
        } while ((reg & AHCI_PORT_CMD_FR) != 0);
        reg = mmio_read32(AHCI_PxCMD(base, i));
        mmio_write32(AHCI_PxCMD(base, i),
                reg & (~AHCI_PORT_CMD_ICC_ACTIVE));

    }
    /* reg = mmio_read32(AHCI_HBA_GHC(base)); */
    /* mmio_write32(AHCI_HBA_GHC(base), reg & (~HBA_GHC_AE)); */
    /* mmio_or32(AHCI_HBA_GHC(base), HBA_GHC_HR | HBA_GHC_IE); */
    /* memset((void *)SATA_BASE, 0, 0x1000000); */
}
#endif /* AHCI_H_ */

