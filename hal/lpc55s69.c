/* lpc55s69.c
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

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "fsl_common.h"
#include "image.h"

#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_iap.h"
#include "fsl_iap_ffr.h"
#include "fsl_iocon.h"
#include "fsl_puf.h"
#include "fsl_reset.h"
#include "fsl_rng.h"
#include "fsl_usart.h"
#include "hal.h"
#include "loader.h"
#include "printf.h"
#include "lpc55s69.h"

#include <wolfssl/wolfcrypt/types.h>

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

#ifdef WOLFSSL_NXP_LPC55S6X
#include <wolfssl/wolfcrypt/port/nxp/casper_port.h>
#include <wolfssl/wolfcrypt/port/nxp/hashcrypt_port.h>
#endif

#if (WOLFBOOT_SECTOR_SIZE % LPC55S69_FLASH_PAGE_SIZE) != 0
#error WOLFBOOT_SECTOR_SIZE must be a multiple of the 512-byte flash page
#endif

#ifdef WOLFSSL_NXP_HWPUF
#include <wolfssl/wolfcrypt/port/nxp/hwpuf_port.h>
#endif

#if defined(WOLFCRYPT_TZ_PSA)
#include <wolfssl/wolfcrypt/hmac.h>

# if defined(WOLFBOOT_HASH_SHA256)
#   include <wolfssl/wolfcrypt/sha256.h>
#   define HAL_KDF_HASH_TYPE WC_HASH_TYPE_SHA256
# elif defined(WOLFBOOT_HASH_SHA384)
#   include <wolfssl/wolfcrypt/sha512.h>
#   define HAL_KDF_HASH_TYPE WC_HASH_TYPE_SHA384
# elif defined(WOLFBOOT_HASH_SHA3_384)
#   include <wolfssl/wolfcrypt/sha3.h>
#   define HAL_KDF_HASH_TYPE WC_HASH_TYPE_SHA3_384
# else
#   error "No supported hash type for HAL_KDF"
# endif
#endif /* WOLFCRYPT_TZ_PSA */

static flash_config_t pflash;
static int flash_ready;

#if defined(TZEN) && defined(__WOLFBOOT) && !defined(LPC55S69_KEYVAULT_ADDR)
extern uint32_t _flash_keyvault;
extern uint32_t _flash_keyvault_size;
#define LPC55S69_KEYVAULT_ADDR ((uint32_t)(uintptr_t)&_flash_keyvault)
#define LPC55S69_KEYVAULT_LEN  ((uint32_t)(uintptr_t)&_flash_keyvault_size)
#endif
uint32_t SystemCoreClock; /* set in clock_config.c */

static void flashInit(void)
{
    if (!flash_ready) {
        XMEMSET(&pflash, 0, sizeof(pflash));
        if (FLASH_Init(&pflash) == kStatus_FLASH_Success) {
            (void)FFR_Init(&pflash);
            flash_ready = 1;
        }
    }
}

static NOINLINEFUNCTION void hal_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0U) {
        *p++ = 0U;
    }
}


#if defined(__WOLFBOOT) && defined(WOLFSSL_NXP_HWPUF) && \
    (defined(WOLFCRYPT_TZ_PSA) || defined(WOLFBOOT_HWPUF_PROVISION))
#ifndef WOLFBOOT_HWPUF_PROV_ADDRESS
#define WOLFBOOT_HWPUF_PROV_ADDRESS 0x80000ul
#endif
#define HWPUF_PROV_MAGIC            0x564f5250ul    /* "PROV" */
#define HWPUF_PROV_FLASH_LEN        0x1000ul
#define HWPUF_PROV_PAGE_SIZE        512ul
/* The commit word lives alone in the last page of the region and is written
 * only after the payload has been written and verified, so an interrupted
 * provisioning never leaves a valid-looking record behind. */
#define HWPUF_PROV_COMMIT_ADDRESS   (WOLFBOOT_HWPUF_PROV_ADDRESS + \
                                     HWPUF_PROV_FLASH_LEN - HWPUF_PROV_PAGE_SIZE)
#define HWPUF_PROV_UDS_KEY_INDEX    14              /* NOT the one in PFR */
#define HWPUF_PROV_UDS_KEY_SIZE     32

#if (WOLFBOOT_HWPUF_PROV_ADDRESS % HWPUF_PROV_PAGE_SIZE) != 0
#error "WOLFBOOT_HWPUF_PROV_ADDRESS must be aligned to the flash page size"
#endif
#define HWPUF_PROV_OVERLAPS(base, len) \
    ((WOLFBOOT_HWPUF_PROV_ADDRESS < ((base) + (len))) && \
     ((base) < (WOLFBOOT_HWPUF_PROV_ADDRESS + HWPUF_PROV_FLASH_LEN)))
#if HWPUF_PROV_OVERLAPS(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE)
#error "WOLFBOOT_HWPUF_PROV_ADDRESS overlaps the boot partition"
#endif
#if HWPUF_PROV_OVERLAPS(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE)
#error "WOLFBOOT_HWPUF_PROV_ADDRESS overlaps the update partition"
#endif
#if HWPUF_PROV_OVERLAPS(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE)
#error "WOLFBOOT_HWPUF_PROV_ADDRESS overlaps the swap partition"
#endif
/* wolfBoot itself, the keyvault and the NSC region sit below the boot
 * partition */
#if WOLFBOOT_HWPUF_PROV_ADDRESS < WOLFBOOT_PARTITION_BOOT_ADDRESS
#error "WOLFBOOT_HWPUF_PROV_ADDRESS must be above the wolfBoot image"
#endif
/* Protected flash region (PFR) starts at 0x9DE00 */
#if (WOLFBOOT_HWPUF_PROV_ADDRESS + HWPUF_PROV_FLASH_LEN) > 0x9DE00ul
#error "WOLFBOOT_HWPUF_PROV_ADDRESS overlaps the protected flash region"
#endif
#ifdef WOLFBOOT_KEYVAULT_ADDRESS
#if HWPUF_PROV_OVERLAPS(WOLFBOOT_KEYVAULT_ADDRESS & ~LPC55S69_FLASH_SECURE_ALIAS, \
        WOLFBOOT_KEYVAULT_SIZE)
#error "WOLFBOOT_HWPUF_PROV_ADDRESS overlaps the keyvault"
#endif
#endif

typedef struct hwpuf_prov {
    byte ac[PUF_ACTIVATION_CODE_SIZE];
    byte uds_kc[PUF_GET_KEY_CODE_SIZE_FOR_KEY_SIZE(HWPUF_PROV_UDS_KEY_SIZE)];
} hwpuf_prov;

/* The payload must end before the commit page */
typedef char hwpuf_prov_size_check[
    (sizeof(hwpuf_prov) <= HWPUF_PROV_FLASH_LEN - HWPUF_PROV_PAGE_SIZE) ?
    1 : -1];

/* Holds no secret; word aligned because the PUF driver streams it by word */
static hwpuf_prov prov __attribute__((aligned(4)));

#define HWPUF_PROV_BLANK    1

static uint32_t hwpuf_commit_word(void)
{
    const uint8_t *p = LPC55S69_FLASH_PTR(HWPUF_PROV_COMMIT_ADDRESS);

    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

/* Load the stored record into rec.
 * Returns 0 if a committed record was loaded, HWPUF_PROV_BLANK if the commit
 * page is erased or all 0xFF (never provisioned, or interrupted before the
 * commit), -1 for anything else. A record in any other state may be a
 * committed one that got damaged, so it is never treated as blank: that
 * would silently re-enroll the PUF and change the device identity. */
static int hwpuf_provision_get(hwpuf_prov *rec)
{
    uint32_t addr;
    uint32_t end = WOLFBOOT_HWPUF_PROV_ADDRESS +
        ((sizeof(*rec) + HWPUF_PROV_PAGE_SIZE - 1) &
            ~(HWPUF_PROV_PAGE_SIZE - 1));
    uint32_t commit_page[HWPUF_PROV_PAGE_SIZE / sizeof(uint32_t)];
    uint32_t failed_addr, failed_data;

    flashInit();

    /* Reading a never-programmed page faults on ECC, check before reading */
    if (FLASH_VerifyErase(&pflash, HWPUF_PROV_COMMIT_ADDRESS,
            HWPUF_PROV_PAGE_SIZE) == kStatus_FLASH_Success)
        return HWPUF_PROV_BLANK;

    /* A page whose programming was cut short may fail ECC as well. Compare
     * through the flash controller, which reports errors instead of
     * faulting, and only read the page once it matches. */
    XMEMSET(commit_page, 0xFF, sizeof(commit_page));
    /* Programmed as 0xFF by hal_flash_erase(): erased, not committed */
    if (FLASH_VerifyProgram(&pflash, HWPUF_PROV_COMMIT_ADDRESS,
            HWPUF_PROV_PAGE_SIZE, (const uint8_t *)commit_page,
            &failed_addr, &failed_data) == kStatus_FLASH_Success &&
            hwpuf_commit_word() == 0xFFFFFFFFul)
        return HWPUF_PROV_BLANK;

    commit_page[0] = HWPUF_PROV_MAGIC;
    if (FLASH_VerifyProgram(&pflash, HWPUF_PROV_COMMIT_ADDRESS,
            HWPUF_PROV_PAGE_SIZE, (const uint8_t *)commit_page,
            &failed_addr, &failed_data) != kStatus_FLASH_Success)
        return -1;

    if (hwpuf_commit_word() != HWPUF_PROV_MAGIC)
        return -1;

    for (addr = WOLFBOOT_HWPUF_PROV_ADDRESS; addr < end;
            addr += HWPUF_PROV_PAGE_SIZE) {
        if (FLASH_VerifyErase(&pflash, addr, HWPUF_PROV_PAGE_SIZE)
                == kStatus_FLASH_Success)
            return -1;
    }

    XMEMCPY(rec, LPC55S69_FLASH_PTR(WOLFBOOT_HWPUF_PROV_ADDRESS),
            sizeof(*rec));
    return 0;
}

#ifdef WOLFBOOT_HWPUF_PROVISION
/* Enroll the PUF and store the record, unless one is already stored.
 * Returns 0 when newly or previously provisioned, -1 otherwise. Only a blank
 * region is provisioned: a damaged record is left alone (attestation fails
 * until the region is erased), so the device identity never changes
 * silently. */
static int hwpuf_provision_set(void)
{
    uint32_t magic = HWPUF_PROV_MAGIC;
    int ret;

    ret = hwpuf_provision_get(&prov);
    if (ret != HWPUF_PROV_BLANK)
        return ret;
    ret = -1;

    hal_zeroize(&prov, sizeof(prov));

    if (nxp_hwpuf_Init() != 0)
        goto error_out;

    if (nxp_hwpuf_Enroll(prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    if (nxp_hwpuf_Deinit() != 0)
        goto error_out;

    if (nxp_hwpuf_Init() != 0)
        goto error_out;

    if (nxp_hwpuf_Start(prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    if (nxp_hwpuf_GenerateKey(HWPUF_PROV_UDS_KEY_INDEX, HWPUF_PROV_UDS_KEY_SIZE,
                              prov.uds_kc, sizeof(prov.uds_kc)) != 0)
        goto error_out;

    flashInit();

    hal_flash_unlock();
    if (hal_flash_erase(WOLFBOOT_HWPUF_PROV_ADDRESS, HWPUF_PROV_FLASH_LEN) != 0)
        goto unlock_out;
    if (hal_flash_write(WOLFBOOT_HWPUF_PROV_ADDRESS,
                        (const uint8_t *)&prov, sizeof(prov)) != 0)
        goto unlock_out;
    if (XMEMCMP(LPC55S69_FLASH_PTR(WOLFBOOT_HWPUF_PROV_ADDRESS), &prov,
               sizeof(prov)) != 0)
        goto unlock_out;
    if (hal_flash_write(HWPUF_PROV_COMMIT_ADDRESS,
                        (const uint8_t *)&magic, sizeof(magic)) != 0)
        goto unlock_out;
    if (hwpuf_commit_word() != HWPUF_PROV_MAGIC)
        goto unlock_out;

    ret = 0;

unlock_out:
    hal_flash_lock();
error_out:
    (void)nxp_hwpuf_Deinit();
    return ret;
}
#endif /* WOLFBOOT_HWPUF_PROVISION */

#if defined(WOLFCRYPT_TZ_PSA)
static int uds_from_hwpuf(uint8_t *out, size_t out_len)
{
    int ret = -1;
    byte uds[HWPUF_PROV_UDS_KEY_SIZE];

    if (hwpuf_provision_get(&prov) != 0)
        return -1;

    if (nxp_hwpuf_Init() != 0)
        goto error_out;

    if (nxp_hwpuf_Start(prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    ret = nxp_hwpuf_GetKey(prov.uds_kc, sizeof(prov.uds_kc),
                           uds, sizeof(uds));
    if (ret != 0)
        goto error_out;

    ret = wc_HKDF_ex((int)HAL_KDF_HASH_TYPE, uds, (word32)sizeof(uds),
                     (const byte *)"WOLFBOOT-UDS", (word32)12,
                     (const byte *)"WOLFBOOT-UDS", (word32)12,
                     (byte *)out, (word32)out_len,
                     NULL, INVALID_DEVID);

error_out:
    hal_zeroize(uds, sizeof(uds));
    (void)nxp_hwpuf_Deinit();
    return ret == 0 ? 0 : -1;
}
#endif /* WOLFCRYPT_TZ_PSA */
#endif /* __WOLFBOOT && WOLFSSL_NXP_HWPUF && (TZ_PSA || HWPUF_PROVISION) */

#if defined(__WOLFBOOT) && defined(WOLFCRYPT_TZ_PSA)
static void reverse_array(byte *s, int len)
{
    int up, dn;

    if (s == NULL)
        return;

    up = 0;
    dn = len - 1;
    while (up < dn) {
        byte t = s[up];
        s[up] = s[dn];
        s[dn] = t;
        ++up;
        --dn;
    }
}
#endif

#if defined(__WOLFBOOT) && defined(WOLFCRYPT_TZ_PSA)
#ifdef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
static int uds_from_uid(uint8_t *out, size_t out_len)
{
    uint8_t uid[16];
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256 hash;
    uint8_t digest[SHA256_DIGEST_SIZE];
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384 hash;
    uint8_t digest[SHA384_DIGEST_SIZE];
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3 hash;
    uint8_t digest[SHA3_384_DIGEST_SIZE];
#endif
    size_t copy_len = sizeof(digest);
    int ret;

    flashInit();

    if (FFR_GetUUID(&pflash, uid) != kStatus_Success)
        return -1;
    reverse_array(uid, sizeof(uid));

#if defined(WOLFBOOT_HASH_SHA256)
    ret = wc_InitSha256(&hash);
    if (ret == 0) {
        ret = wc_Sha256Update(&hash, uid, sizeof(uid));
        if (ret == 0)
            ret = wc_Sha256Final(&hash, digest);
        wc_Sha256Free(&hash);
    }
#elif defined(WOLFBOOT_HASH_SHA384)
    ret = wc_InitSha384(&hash);
    if (ret == 0) {
        ret = wc_Sha384Update(&hash, uid, sizeof(uid));
        if (ret == 0)
            ret = wc_Sha384Final(&hash, digest);
        wc_Sha384Free(&hash);
    }
#elif defined(WOLFBOOT_HASH_SHA3_384)
    ret = wc_InitSha3_384(&hash, NULL, INVALID_DEVID);
    if (ret == 0) {
        ret = wc_Sha3_384_Update(&hash, uid, sizeof(uid));
        if (ret == 0)
            ret = wc_Sha3_384_Final(&hash, digest);
        wc_Sha3_384_Free(&hash);
    }
#endif

    if (ret == 0) {
        if (copy_len > out_len)
            copy_len = out_len;
        XMEMCPY(out, digest, copy_len);
    }

    hal_zeroize(uid, sizeof(uid));
    hal_zeroize(digest, sizeof(digest));
    hal_zeroize(&hash, sizeof(hash));
    return ret == 0 ? 0 : -1;
}
#endif /* WOLFBOOT_UDS_UID_FALLBACK_FORTEST */

/* Derive UDS: from the device UID when WOLFBOOT_UDS_UID_FALLBACK_FORTEST (test
 * only), otherwise from the HW PUF. There is no runtime fallback. */
int hal_uds_derive_key(uint8_t *out, size_t out_len)
{
    if (out == NULL || out_len == 0)
        return -1;

#ifdef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
    return uds_from_uid(out, out_len);
#elif defined(WOLFSSL_NXP_HWPUF)
    return uds_from_hwpuf(out, out_len);
#else
    return -1;
#endif
}
#endif /* __WOLFBOOT && WOLFCRYPT_TZ_PSA */

#if defined(TZEN) && defined(WOLFSSL_NXP_LPC55S6X)
/* The HASHCRYPT bus master reaches SRAM only through the non-secure alias */
#error PKA=1 is not supported with TZEN=1
#endif

#if defined(TZEN) && defined(__WOLFBOOT)
#if ((WOLFBOOT_PARTITION_BOOT_ADDRESS % LPC55S69_AHBSC_FLASH_REGION) != 0) || \
    ((WOLFBOOT_PARTITION_SIZE % LPC55S69_AHBSC_FLASH_REGION) != 0)
#error TZEN needs the boot partition aligned to the 32KB AHB secure flash rules
#endif

/* Every rule field set to secure and privileged */
#define LPC55S69_AHBSC_SECURE   0x33333333U
#define LPC55S69_AHBSC_RULE_S   0x3U

/* The SAU only filters the CPU; these AHB rules apply the same split to DMA
 * and the second core. The controller is write-locked until reset. */
static void hal_ahbsc_init(void)
{
    uint32_t boot = WOLFBOOT_PARTITION_BOOT_ADDRESS &
        ~LPC55S69_FLASH_SECURE_ALIAS;
    uint32_t region;
    uint32_t start;
    uint32_t rule;
    uint32_t misc;
    uint32_t i;

    for (i = 0; i < AHB_SECURE_CTRL_SEC_CTRL_FLASH_ROM_SEC_CTRL_FLASH_MEM_RULE_COUNT;
            i++) {
        rule = 0;
        for (region = 0; region < 8U; region++) {
            start = ((i * 8U) + region) * LPC55S69_AHBSC_FLASH_REGION;
            if ((start < boot) || (start >= boot + WOLFBOOT_PARTITION_SIZE)) {
                rule |= LPC55S69_AHBSC_RULE_S << (region * 4U);
            }
        }
        AHB_SECURE_CTRL->SEC_CTRL_FLASH_ROM[0].SEC_CTRL_FLASH_MEM_RULE[i] = rule;
    }
    for (i = 0; i < AHB_SECURE_CTRL_SEC_CTRL_FLASH_ROM_SEC_CTRL_ROM_MEM_RULE_COUNT;
            i++) {
        AHB_SECURE_CTRL->SEC_CTRL_FLASH_ROM[0].SEC_CTRL_ROM_MEM_RULE[i] =
            LPC55S69_AHBSC_SECURE;
    }
    AHB_SECURE_CTRL->SEC_CTRL_FLASH_ROM[0].SLAVE_RULE =
        AHB_SECURE_CTRL_SEC_CTRL_FLASH_ROM_SLAVE_RULE_ROM_RULE(
            LPC55S69_AHBSC_RULE_S);

    AHB_SECURE_CTRL->SEC_CTRL_RAMX[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAMX[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM0[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM0[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM0[0].MEM_RULE[1] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM1[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM1[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM1[0].MEM_RULE[1] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM2[0].SLAVE_RULE = 0U;
    AHB_SECURE_CTRL->SEC_CTRL_RAM2[0].MEM_RULE[0] = 0U;
    AHB_SECURE_CTRL->SEC_CTRL_RAM2[0].MEM_RULE[1] = 0U;
    AHB_SECURE_CTRL->SEC_CTRL_RAM3[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM3[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM3[0].MEM_RULE[1] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM4[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_RAM4[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_USB_HS[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_USB_HS[0].MEM_RULE[0] = LPC55S69_AHBSC_SECURE;

    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SLAVE_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE0_MEM_CTRL0 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE0_MEM_CTRL1 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE0_MEM_CTRL2 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE1_MEM_CTRL0 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE1_MEM_CTRL1 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE1_MEM_CTRL2 =
        LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_APB_BRIDGE[0].SEC_CTRL_APB_BRIDGE1_MEM_CTRL3 =
        LPC55S69_AHBSC_SECURE;

    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT8_SLAVE0_RULE = LPC55S69_AHBSC_SECURE &
        ~AHB_SECURE_CTRL_SEC_CTRL_AHB_PORT8_SLAVE0_RULE_FLEXCOMM0_RULE_MASK;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT8_SLAVE1_RULE = LPC55S69_AHBSC_SECURE &
        ~AHB_SECURE_CTRL_SEC_CTRL_AHB_PORT8_SLAVE1_RULE_GPIO0_RULE_MASK;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT9_SLAVE0_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT9_SLAVE1_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT10[0].SLAVE0_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT10[0].SLAVE1_RULE = LPC55S69_AHBSC_SECURE;
    AHB_SECURE_CTRL->SEC_CTRL_AHB_PORT10[0].SEC_CTRL_AHB_SEC_CTRL_MEM_RULE[0] =
        LPC55S69_AHBSC_SECURE;

    /* Checking on (0b01); the write lock (0b01) takes effect from the DP copy */
    misc = (AHB_SECURE_CTRL->MISC_CTRL_REG &
            ~(AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK_MASK |
              AHB_SECURE_CTRL_MISC_CTRL_REG_ENABLE_SECURE_CHECKING_MASK)) |
        AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK(2U) |
        AHB_SECURE_CTRL_MISC_CTRL_REG_ENABLE_SECURE_CHECKING(1U);
    AHB_SECURE_CTRL->MISC_CTRL_REG = misc;
    AHB_SECURE_CTRL->MISC_CTRL_DP_REG =
        (misc & ~AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK_MASK) |
        AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK(1U);
}

static void hal_sau_init(void)
{
    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area (boot partition) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1, 0);

    /* Non-secure RAM, above the SRAM used by wolfBoot */
    sau_init_region(2, LPC55S69_NS_SRAM_START, LPC55S69_NS_SRAM_END, 0);

    /* Only the console and GPIO; clocks, pin mux, flash, DMA and crypto stay
     * secure */
    sau_init_region(3, LPC55S69_FLEXCOMM0_NS_BASE,
            LPC55S69_FLEXCOMM0_NS_BASE + LPC55S69_FLEXCOMM0_NS_SIZE - 1, 0);
    sau_init_region(4, LPC55S69_GPIO_NS_BASE,
            LPC55S69_GPIO_NS_BASE + LPC55S69_GPIO_NS_SIZE - 1, 0);

    NVIC_SetTargetState(FLEXCOMM0_IRQn);

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
    __DSB();
    __ISB();

    AHB_SECURE_CTRL->CPU0_LOCK_REG = (AHB_SECURE_CTRL->CPU0_LOCK_REG &
            ~(AHB_SECURE_CTRL_CPU0_LOCK_REG_LOCK_SAU_MASK |
              AHB_SECURE_CTRL_CPU0_LOCK_REG_CPU0_LOCK_REG_LOCK_MASK)) |
        AHB_SECURE_CTRL_CPU0_LOCK_REG_LOCK_SAU(1U) |
        AHB_SECURE_CTRL_CPU0_LOCK_REG_CPU0_LOCK_REG_LOCK(1U);

    hal_ahbsc_init();
}

/* Hand the LED pins to the non-secure application: pin mux stays secure */
static void periph_unsecure(void)
{
    const uint32_t led_mux = IOCON_FUNC0 | IOCON_MODE_PULLUP | IOCON_DIGITAL_EN;

    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, LPC55S69_LED_BLUE_PIN, led_mux);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, LPC55S69_LED_RED_PIN, led_mux);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, LPC55S69_LED_GREEN_PIN, led_mux);
}
#endif /* TZEN */

/* PRIMASK saved per ROM call so the caller's interrupt state survives */
static int RAMFUNCTION lpc55s69_page_erase(uint32_t offset)
{
    uint32_t primask = __get_PRIMASK();
    status_t st;

    __disable_irq();
    st = FLASH_Erase(&pflash, offset, LPC55S69_FLASH_PAGE_SIZE,
            kFLASH_ApiEraseKey);
    __set_PRIMASK(primask);
    return (st == kStatus_FLASH_Success) ? 0 : -1;
}

static int RAMFUNCTION lpc55s69_page_program(uint32_t offset,
        const uint8_t *page)
{
    uint32_t primask = __get_PRIMASK();
    status_t st;

    __disable_irq();
    st = FLASH_Program(&pflash, offset, page, LPC55S69_FLASH_PAGE_SIZE);
    __set_PRIMASK(primask);
    return (st == kStatus_FLASH_Success) ? 0 : -1;
}

static int RAMFUNCTION lpc55s69_page_is_blank(uint32_t offset)
{
    uint32_t primask = __get_PRIMASK();
    status_t st;

    __disable_irq();
    st = FLASH_VerifyErase(&pflash, offset, LPC55S69_FLASH_PAGE_SIZE);
    __set_PRIMASK(primask);
    return (st == kStatus_FLASH_Success);
}

/* Accepts the secure (0x1xxxxxxx) and non-secure flash aliases */
static int RAMFUNCTION lpc55s69_flash_offset(uint32_t address, uint32_t len,
        uint32_t *offset)
{
    uint32_t off = address;

    if ((off >= LPC55S69_FLASH_SECURE_ALIAS) &&
            (off < LPC55S69_FLASH_SECURE_ALIAS + LPC55S69_FLASH_SIZE)) {
        off -= LPC55S69_FLASH_SECURE_ALIAS;
    }
    if ((off >= LPC55S69_FLASH_SIZE) || (len > LPC55S69_FLASH_SIZE - off)) {
        return -1;
    }
    *offset = off;
    return 0;
}

static int hal_flash_fix_ecc(uint32_t address, uint32_t len)
{
    uint8_t page_buf[LPC55S69_FLASH_PAGE_SIZE];
    uint32_t offset = 0;
    uint32_t end;
    uint32_t i;
    int ret;

    ret = lpc55s69_flash_offset(address, len, &offset);
    if (ret == 0) {
        for (i = 0; i < sizeof(page_buf); i++) {
            page_buf[i] = 0xFFU;
        }
        end = offset + len;
        while ((ret == 0) && (offset < end)) {
            if (lpc55s69_page_is_blank(offset)) {
                ret = lpc55s69_page_program(offset, page_buf);
            }
            offset += LPC55S69_FLASH_PAGE_SIZE;
        }
    }
    return ret;
}

/* Fill every erased page wolfBoot may read, so reads never hit an ECC fault;
 * DICE attestation hashes the whole bootloader region */
static int hal_flash_fix_ecc_all(void)
{
    int ret = 0;

#ifdef __WOLFBOOT
    ret = hal_flash_fix_ecc(ARCH_FLASH_OFFSET,
            WOLFBOOT_PARTITION_BOOT_ADDRESS - ARCH_FLASH_OFFSET);
#endif
    if (ret == 0) {
        ret = hal_flash_fix_ecc(WOLFBOOT_PARTITION_BOOT_ADDRESS,
                WOLFBOOT_PARTITION_SIZE);
    }
    if (ret == 0) {
        ret = hal_flash_fix_ecc(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
                WOLFBOOT_PARTITION_SIZE);
    }
    if (ret == 0) {
        ret = hal_flash_fix_ecc(WOLFBOOT_PARTITION_SWAP_ADDRESS,
                WOLFBOOT_SECTOR_SIZE);
    }
#if defined(TZEN) && defined(__WOLFBOOT)
    if (ret == 0) {
        ret = hal_flash_fix_ecc(LPC55S69_KEYVAULT_ADDR, LPC55S69_KEYVAULT_LEN);
    }
#endif
    return ret;
}

#if !defined(__WOLFBOOT) && !defined(TZEN)
/* The test-app seeds wolfCrypt from the RNG; wolfBoot itself does not */
static void lpc55s69_app_rng_init(void)
{
    CLOCK_EnableClock(kCLOCK_Rng);
    RESET_PeripheralReset(kRNG_RST_SHIFT_RSTn);
    RNG_Init(RNG);
}
#endif

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* lpc55s69 must run < 100 MHz for flash write/erase to work */
    BOARD_BootClockFROHF96M();

# ifdef DEBUG_UART
    uart_init();
# endif

# ifdef WOLFSSL_NXP_LPC55S6X
    CLOCK_EnableClock(kCLOCK_HashCrypt);
    (void)wc_hashcrypt_init();
    CLOCK_EnableClock(kCLOCK_Casper);
    (void)wc_casper_init();
# endif
#endif /* __WOLFBOOT */

#if !defined(__WOLFBOOT) && !defined(TZEN)
    lpc55s69_app_rng_init();
#endif

#if defined(WOLFSSL_NXP_HWPUF) && (defined(__WOLFBOOT) || !defined(TZEN))
    /* FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL: PUF_Init() won't do this */
    CLOCK_EnableClock(kCLOCK_Puf);
#endif

#if defined(__WOLFBOOT) || !defined(TZEN)
    flashInit();
    if ((flash_ready == 0) || (hal_flash_fix_ecc_all() != 0)) {
        wolfBoot_printf("lpc55s69: flash init failed\n");
    }
#endif

#if defined(TZEN) && defined(__WOLFBOOT)
    hal_sau_init();
#endif

#if defined(__WOLFBOOT) && \
    (defined(WOLFCRYPT_TZ_PSA) || defined(WOLFSSL_NXP_HWPUF))
    if (wolfCrypt_Init() != 0)
        wolfBoot_printf("wolfCrypt_Init failed\n");
#endif

#if defined(__WOLFBOOT) && defined(WOLFBOOT_HWPUF_PROVISION)
    /* Provisioning failure does not stop the boot, will run again next boot */
    if (hwpuf_provision_set() != 0)
        wolfBoot_printf("hwpuf provision failed\n");
#endif
}

#if defined(BOOT_BENCHMARK) && defined(__WOLFBOOT)
/* DWT CYCCNT stops without a debugger; SysTick on the FRO 1MHz ticks in us */
uint64_t hal_get_timer_us(void)
{
    static uint64_t elapsed_us;
    static uint32_t last;
    static int started;
    uint32_t now;

    if (!started) {
        SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_FRO1MHZ_CLK_ENA_MASK;
        CLOCK_AttachClk(kFRO1M_to_SYSTICK0);
        SysTick->LOAD = SysTick_LOAD_RELOAD_Msk;
        SysTick->VAL = 0U;
        SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
        last = SysTick->VAL;
        started = 1;
    }
    now = SysTick->VAL;
    elapsed_us += (last - now) & SysTick_LOAD_RELOAD_Msk;
    last = now;
    return elapsed_us;
}
#endif

#ifdef __WOLFBOOT
/* Assert hook needed by SDK assert() macro. */
void RAMFUNCTION __assert_func(const char *a, int b, const char *c,
        const char *d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    while (1) {
    }
}

void hal_prepare_boot(void)
{
#ifdef BOOT_BENCHMARK
    SysTick->CTRL = 0U;
#endif
#ifdef TZEN
    periph_unsecure();
#endif
}
#endif /* __WOLFBOOT */

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t page_buf[LPC55S69_FLASH_PAGE_SIZE];
    const uint8_t *page_src;
    uint32_t offset = 0;
    uint32_t page_off;
    uint32_t in_page;
    uint32_t chunk;
    uint32_t i;
    int blank;
    int ret = 0;

    if (len == 0) {
        return 0;
    }
    if ((len < 0) || (data == NULL) || (flash_ready == 0) ||
            (lpc55s69_flash_offset(address, (uint32_t)len, &offset) != 0)) {
        return -1;
    }

    while ((ret == 0) && (len > 0)) {
        page_off = offset & ~(LPC55S69_FLASH_PAGE_SIZE - 1U);
        in_page = offset - page_off;
        chunk = LPC55S69_FLASH_PAGE_SIZE - in_page;
        if ((uint32_t)len < chunk) {
            chunk = (uint32_t)len;
        }

        blank = lpc55s69_page_is_blank(page_off);
        if ((chunk != LPC55S69_FLASH_PAGE_SIZE) && blank) {
            for (i = 0; i < LPC55S69_FLASH_PAGE_SIZE; i++) {
                page_buf[i] = 0xFFU;
            }
        }
        else if (chunk != LPC55S69_FLASH_PAGE_SIZE) {
            /* Merge with the page as the caller addresses it (S or NS) */
            page_src = LPC55S69_FLASH_PTR(address - in_page);
            for (i = 0; i < LPC55S69_FLASH_PAGE_SIZE; i++) {
                page_buf[i] = page_src[i];
            }
        }
        for (i = 0; i < chunk; i++) {
            page_buf[in_page + i] = data[i];
        }

        if (!blank) {
            ret = lpc55s69_page_erase(page_off);
        }
        if (ret == 0) {
            ret = lpc55s69_page_program(page_off, page_buf);
        }

        address += chunk;
        offset += chunk;
        data += chunk;
        len -= (int)chunk;
    }

    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* Erased pages are programmed with 0xFF so that later reads do not fault */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint8_t page_buf[LPC55S69_FLASH_PAGE_SIZE];
    uint32_t offset = 0;
    uint32_t end;
    uint32_t i;
    int ret = 0;

    if (len == 0) {
        return 0;
    }
    if ((len < 0) || (flash_ready == 0) ||
            ((address % LPC55S69_FLASH_PAGE_SIZE) != 0U) ||
            (((uint32_t)len % LPC55S69_FLASH_PAGE_SIZE) != 0U) ||
            (lpc55s69_flash_offset(address, (uint32_t)len, &offset) != 0)) {
        return -1;
    }

    for (i = 0; i < sizeof(page_buf); i++) {
        page_buf[i] = 0xFFU;
    }
    end = offset + (uint32_t)len;
    while ((ret == 0) && (offset < end)) {
        ret = lpc55s69_page_erase(offset);
        if (ret == 0) {
            ret = lpc55s69_page_program(offset, page_buf);
        }
        offset += LPC55S69_FLASH_PAGE_SIZE;
    }

    return ret;
}

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
static int trng_ready;

void hal_trng_init(void)
{
    CLOCK_EnableClock(kCLOCK_Rng);
    RESET_PeripheralReset(kRNG_RST_SHIFT_RSTn);
    /* Runs the RNG health check; it has no failure return */
    RNG_Init(RNG);
    trng_ready = 1;
}

void hal_trng_fini(void)
{
    trng_ready = 0;
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    if ((trng_ready == 0) || (out == NULL)) {
        return -1;
    }
    return (RNG_GetRandomData(RNG, out, len) == kStatus_Success) ? 0 : -1;
}
#endif /* WOLFCRYPT_SECURE_MODE */

#ifdef DEBUG_UART
void uart_init(void)
{
    const uint32_t uart_mux = IOCON_FUNC1 | IOCON_MODE_INACT |
        IOCON_DIGITAL_EN;
    usart_config_t config;

    CLOCK_EnableClock(kCLOCK_Iocon);
    IOCON_PinMuxSet(IOCON, LPC55S69_UART_PORT, LPC55S69_UART_RX_PIN, uart_mux);
    IOCON_PinMuxSet(IOCON, LPC55S69_UART_PORT, LPC55S69_UART_TX_PIN, uart_mux);

    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM0);
    CLOCK_EnableClock(kCLOCK_FlexComm0);
    RESET_ClearPeripheralReset(kFC0_RST_SHIFT_RSTn);

    USART_GetDefaultConfig(&config);
    config.baudRate_Bps = LPC55S69_UART_BAUD;
    config.enableTx = true;
    config.enableRx = true;
    (void)USART_Init(USART0, &config, LPC55S69_UART_CLK_HZ);
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;

    if (buf == NULL) {
        return;
    }
    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            (void)USART_WriteBlocking(USART0, (const uint8_t *)"\r", 1U);
        }
        (void)USART_WriteBlocking(USART0, (const uint8_t *)&buf[i], 1U);
    }
}
#endif /* DEBUG_UART */
