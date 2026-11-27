/* nxp_hse.h
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

/* Host interface to the NXP HSE firmware (S32G, S32R, S32K3 families).
 *
 * The platform provides, before this header is included:
 *   NXP_HSE_MU_BASE    host side of the Messaging Unit used
 *   NXP_HSE_DESC_BASE  memory HSE reads service descriptors from, 256 bytes
 *                      per channel
 *   NXP_HSE_ADDR64     defined when the HSE variant takes 64-bit addresses
 *                      in its requests (S32G2, S32G3, S32R45); without it
 *                      the requests carry 32-bit addresses (S32K3)
 *   NXP_HSE_MU_CHANNELS  service channels per Messaging Unit when not 16
 *                      (S32K3 has 4) */

#ifndef NXP_HSE_H
#define NXP_HSE_H

#include <stdint.h>

#define NXP_HSE_DESC_SIZE           256U
#ifndef NXP_HSE_MU_CHANNELS
#define NXP_HSE_MU_CHANNELS         16U
#endif
#define NXP_HSE_ATTR_MAX            64U

/* Global status, the upper half of the MU flag status register. */
#define NXP_HSE_STATUS_RNG_OK       (1U << 5)
#define NXP_HSE_STATUS_INIT_OK      (1U << 8)
#define NXP_HSE_STATUS_INSTALL_OK   (1U << 9)   /* system image installed */
#define NXP_HSE_STATUS_BOOT_OK      (1U << 10)
#define NXP_HSE_STATUS_SYSIMG_PRIMARY (1U << 14)

#define NXP_HSE_RSP_OK              0x55A5AA33UL
#define NXP_HSE_RSP_VERIFY_FAILED   0x55A5A164UL

/* HSE accepts only administration services on channel 0 and any service
 * on the others. */
#define NXP_HSE_CHANNEL_ATTR        0U
#define NXP_HSE_CHANNEL_ADMIN       1U

#define NXP_HSE_ATTR_FW_VERSION     1U
#define NXP_HSE_ATTR_SMR_CORE_BOOT  3U      /* five 32-bit maps */
#define NXP_HSE_ATTR_LIFECYCLE      12U     /* one byte */
#define NXP_HSE_ATTR_ROLLBACK_LEFT  101U    /* 32 bits, fuse counter steps */

/* Key handle of a slot in the non-volatile key catalog */
#define NXP_HSE_KEY_NVM(group, slot) \
    (0x010000UL | ((uint32_t)(group) << 8) | (uint32_t)(slot))
#define NXP_HSE_KEY_OWNER_ANY       0U
#define NXP_HSE_KEY_OWNER_CUST      1U
#define NXP_HSE_KEY_TYPE_AES        0x12U
#define NXP_HSE_KEY_TYPE_ECC_PUB    0x88U
#define NXP_HSE_CURVE_P256          1U

/* Where an SMR is loaded from */
#define NXP_HSE_MEDIUM_QSPI         0U
#define NXP_HSE_MEDIUM_SD           2U
#define NXP_HSE_MEDIUM_MMC          3U

/* Blocks the caller provides memory for */
#define NXP_HSE_KEY_GROUP_SIZE      8U
#define NXP_HSE_KEY_INFO_SIZE       16U
#ifdef NXP_HSE_ADDR64
#define NXP_HSE_SMR_ENTRY_SIZE      72U
#else
#define NXP_HSE_SMR_ENTRY_SIZE      64U
#endif
#define NXP_HSE_CR_ENTRY_SIZE       32U

/* Firmware version attribute, as HSE returns it. */
struct nxp_hse_fw_version {
    uint8_t  reserved;
    uint8_t  soc_type;
    uint16_t fw_type;   /* 0 standard, 1 premium */
    uint8_t  major;
    uint8_t  minor;
    uint16_t patch;
};

/* Each returns 0 on success and a negative value otherwise; *rsp holds the
 * HSE response word when one was received. Addresses are physical: the
 * caller cleans its buffers out of the data cache before a request and
 * invalidates the ones HSE writes afterwards. */
uint32_t nxp_hse_status(void);
int nxp_hse_wait_init(uint32_t timeout_us);
int nxp_hse_request(uint32_t channel, const uint8_t *desc, uint32_t len,
    uint32_t *rsp);
int nxp_hse_get_attr(uint16_t id, uint8_t *out, uint32_t len,
    uint32_t *rsp);
int nxp_hse_format_catalogs(uint64_t nvm, uint64_t ram, uint32_t *rsp);
int nxp_hse_import_ecc_pub(uint32_t handle, uint8_t curve, uint16_t bits,
    uint64_t info, uint64_t pub, uint32_t *rsp);
int nxp_hse_smr_install(uint8_t index, uint64_t entry, uint32_t src,
    uint64_t dest, uint32_t size, uint8_t medium, uint32_t key,
    uint32_t tag_r, uint32_t tag_s, uint64_t data, uint64_t r, uint64_t s,
    uint16_t half, uint32_t *rsp);
int nxp_hse_cr_install(uint8_t index, uint64_t entry, uint8_t core,
    uint32_t smr_map, uint32_t reset_addr, uint32_t *rsp);
int nxp_hse_smr_verify(uint8_t index, uint32_t *rsp);
int nxp_hse_sysimg_size(uint64_t size, uint32_t *rsp);
int nxp_hse_sysimg_publish(uint64_t offset, uint64_t len, uint64_t buf,
    uint32_t *rsp);
void nxp_hse_report(void);

#endif /* NXP_HSE_H */
