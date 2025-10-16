/* ahci.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

#define AHCI_CLASS_ID       0x01
#define AHCI_SUBCLASS_ID    0x06
#define AHCI_CLASS_CODE     0x0601
#define AHCI_PROG_IF        0x01

#define AHCI_ID_OFFSET       0x00
#define AHCI_CMD_OFFSET      0x04
#define AHCI_STS_OFFSET      0x06
#define AHCI_AIDPBA_OFFSET   0x20
#define AHCI_ABAR_OFFSET     0x24

#define AHCI_MAX_PORTS 32

#define AHCI_HBA_CAP(base)  (base + 0x00)
#define AHCI_HBA_GHC(base)  (base + 0x04)
#define AHCI_HBA_IS(base)  (base + 0x08)
#define AHCI_HBA_PI(base)  (base + 0x0C)
#define AHCI_HBA_VS(base)  (base + 0x10)
#define AHCI_HBA_CCC_CTL(base)  (base + 0x14)
#define AHCI_HBA_CCC_PORTS (base + 0x18)
#define AHCI_HBA_EM_LOC    (base + 0x1C)
#define AHCI_HBA_EM_CTL    (base + 0x20)
#define AHCI_HBA_CAP2      (base + 0x24)
#define AHCI_HBA_BOHC      (base + 0x28)

#define AHCI_PORT_START 0x100
#define AHCI_PORT_SIZE  0x80

#define AHCI_PORT_CLB_OFFSET 0x00
#define AHCI_PORT_CLBH_OFFSET 0x04
#define AHCI_PORT_FB_OFFSET 0x08
#define AHCI_PORT_FBH_OFFSET 0x0C
#define AHCI_PORT_IS_OFFSET 0x10
#define AHCI_PORT_IE_OFFSET 0x14
#define AHCI_PORT_CMD_OFFSET 0x18
#define AHCI_PORT_TFD_OFFSET 0x20
#define AHCI_PORT_SIG_OFFSET 0x24
#define AHCI_PORT_SSTS_OFFSET 0x28
#define AHCI_PORT_SCTL_OFFSET 0x2C
#define AHCI_PORT_SERR_OFFSET 0x30
#define AHCI_PORT_SACT_OFFSET 0x34
#define AHCI_PORT_CI_OFFSET 0x38

#define AHCI_PORT_REG_START(base, port) \
    ((base + AHCI_PORT_START + (port * AHCI_PORT_SIZE)))

#define AHCI_PxSSTS(base, port) \
    (base + AHCI_PORT_START +                                   \
     (port * AHCI_PORT_SIZE) + AHCI_PORT_SSTS_OFFSET)

#define AHCI_PxFB(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_FB_OFFSET)

#define AHCI_PxFBH(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_FBH_OFFSET)

#define AHCI_PxCLB(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_CLB_OFFSET)

#define AHCI_PxCLBH(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_CLBH_OFFSET)

#define AHCI_PxCMD(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_CMD_OFFSET)

#define AHCI_PxIE(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_IE_OFFSET)

#define AHCI_PxIS(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_IS_OFFSET)

#define AHCI_PxSCTL(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_SCTL_OFFSET)

#define AHCI_PxSERR(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_SERR_OFFSET)

#define AHCI_PxTFD(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_TFD_OFFSET)

#define AHCI_PxSIG(base, port) \
    (base + AHCI_PORT_START +                                   \
     (port * AHCI_PORT_SIZE) + AHCI_PORT_SIG_OFFSET)

#define AHCI_PxSACT(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_SACT_OFFSET)

#define AHCI_PxCI(base, port) (base + AHCI_PORT_START + \
            (port * AHCI_PORT_SIZE) + AHCI_PORT_CI_OFFSET)

#define HBA_GHC_AE     (1 << 31) /* AHCI ENABLE */
#define HBA_GHC_HR     (1 << 0)  /* HARD RESET */
#define HBA_GHC_IE     (1 << 1)  /* INT ENABLE */

#define AHCI_CAP_SSS  (1 << 27)      /* Staggered spin-up mode supported */
#define AHCI_CAP_SAM (1 << 18)

#define AHCI_PORT_CMD_CPD  (1 << 20) /* Cold-presence detection */
#define AHCI_PORT_CMD_POD  (1 << 2)  /* Power On Device */
#define AHCI_PORT_CMD_SUD  (1 << 1)  /* Spin-up device */
#define AHCI_PORT_CMD_FR   (1 << 14) /* FR */
#define AHCI_PORT_CMD_FRE  (1 << 4)  /* FIS receive enabled */
#define AHCI_PORT_CMD_START (1 << 0)  /* Start processing the command list */
#define AHCI_PORT_CMD_CR    (1 << 15) /* CR */
#define AHCI_PORT_CMD_ALPE  (1 << 26) /* Aggressive link power management enable */
#define AHCI_PORT_CMD_ICC_ACTIVE (0x1 << 28)
#define AHCI_PORT_CMD_ICC_MASK (0xf << 28)

#define AHCI_SSTS_DET_MASK 0xf
#define AHCI_PORT_SSTS_DET 0x01
#define AHCI_PORT_SSTS_DET_PCE 0x03

#define AHCI_PORT_TFD_BSY (1 << 7)
#define AHCI_PORT_TFD_DRQ (1 << 3)
#define AHCI_PORT_TFD_ERR (1 << 0)

#define AHCI_PORT_SIG_SATA 0x00000101
#define AHCI_PORT_SIG_ATAPI 0xEB140101
#define AHCI_PORT_SIG_SEMB  0xC33C0101
#define AHCI_PORT_SIG_PM    0x96690101

#define AHCI_PORT_IS_TFES   (1 << 30)


struct ahci_received_fis {
    /* 0x00 */
    uint8_t ahci_dma_setup_fis[0x1C];
    uint8_t _res0[0x04];

    /* 0x20 */
    uint8_t ahci_pio_setup_fis[0x14];
    uint8_t _res1[0x0C];

    /* 0x40 */
    uint8_t ahci_d2h_reg_fis[0x14];
    uint8_t _res2[0x04];

    /* 0x58 */
    uint64_t ahci_set_device_bits_fis;

    /* 0x60 */
    uint8_t ahci_unk_fis[0x40];

    /* 0xA0 */
    uint8_t _res_f[0x60];
};

uint32_t ahci_enable(uint32_t bus, uint32_t dev, uint32_t fun);
void sata_enable(uint32_t base);
void sata_disable(uint32_t base);
int sata_unlock_disk(int drv, int freeze);

#endif /* AHCI_H */
