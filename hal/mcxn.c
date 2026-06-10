/* mcxn.c
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
#include "fsl_flash.h"
#include "fsl_gpio.h"
#include "fsl_lpflexcomm.h"
#include "fsl_lpuart.h"
#include "fsl_port.h"
#include "fsl_reset.h"
#include "loader.h"
#include "PERI_AHBSC.h"

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

#if defined(WOLFCRYPT_TZ_PSA)
/* 128-bit device UUID in IPC 1 (SYSCON UUID block) */
#define MCXN_UUID_ADDR  0x01100000U
#endif

#if defined(WOLFCRYPT_TZ_PSA) && defined(WOLFBOOT_DICE_HW)
#include "wolfboot/dice.h"
#include "mcuxClEls.h"
#include "mcuxClEls_Kdf.h"
#include "mcuxClEls_Ecc.h"
#include "mcuxClEls_KeyManagement.h"
#include "mcuxCsslFlowProtection.h"
#include <wolfssl/wolfcrypt/sha256.h>

/* Key slot pre-loaded by ROM DICE: HKDF(UDS, wolfBoot_hash) -> initial CDI. */
#define MCXN_ELS_DICE_CDI_INITIAL_KEYSLOT   7U

/* wolfBoot stores the boot-measurement-derived CDI here. */
#define MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT   11U

/* wolfBoot stores the per-boot IAK (P-256) here.
 * ELS EccKeyGen DETERMINISTIC mode uses privateKeyIdx as both the CDI seed
 * source and the IAK output slot, so the IAK overwrites the CDI in-place. */
#define MCXN_ELS_DICE_IAK_KEYSLOT           MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT

#endif

#if defined(WOLFCRYPT_TZ_PSA) && !defined(WOLFBOOT_DICE_HW)
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>

#ifdef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
static NOINLINEFUNCTION void hal_uds_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0U)
        *p++ = 0U;
}
#endif

/* Derive UDS from device UUID for software DICE testing.
 * NOT secure — UUID is publicly observable. Only enabled with
 * WOLFBOOT_UDS_UID_FALLBACK_FORTEST. */
int hal_uds_derive_key(uint8_t *out, size_t out_len)
{
    volatile const uint32_t *uuid_addr =
        (volatile const uint32_t *)MCXN_UUID_ADDR;
    uint8_t uuid_be[16];
    uint32_t word;
    int i;
#if defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384 hash;
    uint8_t digest[SHA384_DIGEST_SIZE];
    size_t copy_len = sizeof(digest);
#elif defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256 hash;
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t copy_len = sizeof(digest);
#else
    (void)out; (void)out_len;
    return -1;
#endif

    if (out == NULL || out_len == 0)
        return -1;

#ifndef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
    (void)uuid_addr; (void)uuid_be; (void)word; (void)i;
#if defined(WOLFBOOT_HASH_SHA384) || defined(WOLFBOOT_HASH_SHA256)
    (void)hash; (void)digest; (void)copy_len;
#endif
    return -1;
#else
    for (i = 0; i < 4; i++) {
        word = uuid_addr[i];
        uuid_be[i * 4 + 0] = (uint8_t)(word >> 24);
        uuid_be[i * 4 + 1] = (uint8_t)(word >> 16);
        uuid_be[i * 4 + 2] = (uint8_t)(word >> 8);
        uuid_be[i * 4 + 3] = (uint8_t)(word);
    }

#if defined(WOLFBOOT_HASH_SHA384)
    {
        int ret = wc_InitSha384(&hash);
        if (ret == 0) {
            ret = wc_Sha384Update(&hash, uuid_be, sizeof(uuid_be));
            if (ret == 0)
                ret = wc_Sha384Final(&hash, digest);
            wc_Sha384Free(&hash);
        }
        if (ret != 0) {
            hal_uds_zeroize(uuid_be, sizeof(uuid_be));
            hal_uds_zeroize(digest, sizeof(digest));
            return -1;
        }
    }
#elif defined(WOLFBOOT_HASH_SHA256)
    {
        int ret = wc_InitSha256(&hash);
        if (ret == 0) {
            ret = wc_Sha256Update(&hash, uuid_be, sizeof(uuid_be));
            if (ret == 0)
                ret = wc_Sha256Final(&hash, digest);
            wc_Sha256Free(&hash);
        }
        if (ret != 0) {
            hal_uds_zeroize(uuid_be, sizeof(uuid_be));
            hal_uds_zeroize(digest, sizeof(digest));
            return -1;
        }
    }
#endif

    if (copy_len > out_len)
        copy_len = out_len;
    XMEMCPY(out, digest, copy_len);
    hal_uds_zeroize(digest, sizeof(digest));
    hal_uds_zeroize(uuid_be, sizeof(uuid_be));
    return 0;
#endif /* WOLFBOOT_UDS_UID_FALLBACK_FORTEST */
}
#endif /* WOLFCRYPT_TZ_PSA && !WOLFBOOT_DICE_HW */

#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void);
int hal_trng_get_entropy(unsigned char *out, unsigned int len);
#endif

static flash_config_t pflash;
static uint32_t pflash_sector_size = WOLFBOOT_SECTOR_SIZE;
uint32_t SystemCoreClock;

#ifdef TZEN
static void hal_sau_init(void)
{
    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area (boot partition) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1,
            0);

    /* Non-secure RAM */
    sau_init_region(2, 0x20020000, 0x20025FFF, 0);

    /* Peripherals */
    sau_init_region(3, 0x40000000, 0x4005FFFF, 0);
    sau_init_region(4, 0x40080000, 0x400DFFFF, 0);
    sau_init_region(5, 0x40100000, 0x4013FFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

static void periph_unsecure(void)
{
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port1);

    GPIO_EnablePinControlNonSecure(GPIO0, (1UL << 10) | (1UL << 27));
    GPIO_EnablePinControlNonSecure(GPIO1, (1UL << 2) | (1UL << 8) | (1UL << 9));
}
#endif

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* Single-byte RAM writes unpredictably fail when ECC is enabled */
    SYSCON->ECC_ENABLE_CTRL = 0;
    BOARD_InitBootClocks();
#ifdef DEBUG_UART
    uart_init();
#endif
#endif

#if defined(__WOLFBOOT) || !defined(TZEN)
    memset(&pflash, 0, sizeof(pflash));
    FLASH_Init(&pflash);
    FLASH_GetProperty(&pflash, kFLASH_PropertyPflashSectorSize,
            &pflash_sector_size);
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
    hal_sau_init();
#endif

}

#ifdef __WOLFBOOT
/* Assert hook needed by SDK assert() macro. */
void __assert_func(const char *a, int b, const char *c, const char *d)
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
#ifdef TZEN
    periph_unsecure();
#endif
}
#endif

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    const uint32_t word_size = 4U;
    int written = 0;

    while (len > 0) {
        if ((address & (word_size - 1U)) || (len < (int)word_size)) {
            uint32_t aligned = address & ~(word_size - 1U);
            uint32_t word;
            uint32_t offset = address - aligned;
            uint32_t copy = word_size - offset;

            if (copy > (uint32_t)len) {
                copy = (uint32_t)len;
            }

            memcpy(&word, (void *)aligned, word_size);
            memcpy(((uint8_t *)&word) + offset, data + written, copy);
            if (FLASH_Program(&pflash, aligned, (uint8_t *)&word, word_size) !=
                kStatus_FLASH_Success) {
                return -1;
            }

            address += copy;
            len -= (int)copy;
            written += (int)copy;
        }
        else {
            uint32_t chunk = (uint32_t)len & ~(word_size - 1U);

            if (FLASH_Program(&pflash, address, (uint8_t *)data + written,
                              chunk) != kStatus_FLASH_Success) {
                return -1;
            }

            address += chunk;
            len -= (int)chunk;
            written += (int)chunk;
        }
    }

    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t sector_size = pflash_sector_size;

    if (sector_size == 0U) {
        sector_size = WOLFBOOT_SECTOR_SIZE;
    }

    if ((address % sector_size) != 0U) {
        address -= address % sector_size;
    }

    while (len > 0) {
        if (FLASH_Erase(&pflash, address, sector_size,
                        kFLASH_ApiEraseKey) != kStatus_FLASH_Success) {
            return -1;
        }
        if (FLASH_VerifyErase(&pflash, address, sector_size) !=
            kStatus_FLASH_Success) {
            return -1;
        }
        address += sector_size;
        len -= (int)sector_size;
    }

    return 0;
}

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
#define ELS_CMD_RND_REQ 24U

void hal_trng_init(void)
{
    /* Enable ELS and wait for it to be ready */
    ELS->ELS_CTRL = S50_ELS_CTRL_ELS_EN(1);
    while (ELS->ELS_STATUS & S50_ELS_STATUS_ELS_BUSY_MASK)
        ;
}

void hal_trng_fini(void)
{
    /* Don't disable ELS, it might be used by other actors */
}

static int els_rnd_req(void *out, uint32_t len)
{
    while (ELS->ELS_STATUS & S50_ELS_STATUS_ELS_BUSY_MASK)
        ;
    ELS->ELS_DMA_RES0 = (uint32_t)(uintptr_t)out;
    ELS->ELS_DMA_RES0_LEN = len;
    ELS->ELS_CMDCFG0 = 0;
    ELS->ELS_CTRL = S50_ELS_CTRL_ELS_EN(1)
                   | S50_ELS_CTRL_ELS_START(1)
                   | S50_ELS_CTRL_ELS_CMD(ELS_CMD_RND_REQ);
    while (ELS->ELS_STATUS & S50_ELS_STATUS_ELS_BUSY_MASK)
        ;
    return (ELS->ELS_STATUS & S50_ELS_STATUS_ELS_ERR_MASK) ? -1 : 0;
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    uint32_t tmp;

    /* Handle unaligned head (up to 3 bytes) via temporary word */
    if ((uintptr_t)out & 3U) {
        uint32_t head = 4U - ((uintptr_t)out & 3U);
        if (head > len)
            head = len;
        if (els_rnd_req(&tmp, 4) != 0)
            return -1;
        memcpy(out, &tmp, head);
        out += head;
        len -= head;
    }

    /* Bulk aligned portion in one request */
    if (len >= 4) {
        uint32_t aligned_len = len & ~3U;
        if (els_rnd_req(out, aligned_len) != 0)
            return -1;
        out += aligned_len;
        len -= aligned_len;
    }

    /* Handle remaining tail bytes (1-3) via temporary word */
    if (len > 0) {
        if (els_rnd_req(&tmp, 4) != 0)
            return -1;
        memcpy(out, &tmp, len);
    }

    return 0;
}
#endif

void uart_init(void)
{
    lpuart_config_t config;
    const port_pin_config_t uart_rx = {
        .pullSelect = kPORT_PullUp,
#if defined(FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE) && FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE
        .pullValueSelect = kPORT_LowPullResistor,
#endif
#if defined(FSL_FEATURE_PORT_HAS_SLEW_RATE) && FSL_FEATURE_PORT_HAS_SLEW_RATE
        .slewRate = kPORT_FastSlewRate,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PASSIVE_FILTER) && FSL_FEATURE_PORT_HAS_PASSIVE_FILTER
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_OPEN_DRAIN) && FSL_FEATURE_PORT_HAS_OPEN_DRAIN
        .openDrainEnable = kPORT_OpenDrainDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH
        .driveStrength = kPORT_LowDriveStrength,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1
        .driveStrength1 = kPORT_NormalDriveStrength,
#endif
        .mux = kPORT_MuxAlt2,
#if defined(FSL_FEATURE_PORT_HAS_INPUT_BUFFER) && FSL_FEATURE_PORT_HAS_INPUT_BUFFER
        .inputBuffer = kPORT_InputBufferEnable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_INVERT_INPUT) && FSL_FEATURE_PORT_HAS_INVERT_INPUT
        .invertInput = kPORT_InputNormal,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK) && FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK
        .lockRegister = kPORT_UnlockRegister
#endif
    };
    const port_pin_config_t uart_tx = {
        .pullSelect = kPORT_PullDisable,
#if defined(FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE) && FSL_FEATURE_PORT_PCR_HAS_PULL_VALUE
        .pullValueSelect = kPORT_LowPullResistor,
#endif
#if defined(FSL_FEATURE_PORT_HAS_SLEW_RATE) && FSL_FEATURE_PORT_HAS_SLEW_RATE
        .slewRate = kPORT_FastSlewRate,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PASSIVE_FILTER) && FSL_FEATURE_PORT_HAS_PASSIVE_FILTER
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_OPEN_DRAIN) && FSL_FEATURE_PORT_HAS_OPEN_DRAIN
        .openDrainEnable = kPORT_OpenDrainDisable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH
        .driveStrength = kPORT_LowDriveStrength,
#endif
#if defined(FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1) && FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH1
        .driveStrength1 = kPORT_NormalDriveStrength,
#endif
        .mux = kPORT_MuxAlt2,
#if defined(FSL_FEATURE_PORT_HAS_INPUT_BUFFER) && FSL_FEATURE_PORT_HAS_INPUT_BUFFER
        .inputBuffer = kPORT_InputBufferEnable,
#endif
#if defined(FSL_FEATURE_PORT_HAS_INVERT_INPUT) && FSL_FEATURE_PORT_HAS_INVERT_INPUT
        .invertInput = kPORT_InputNormal,
#endif
#if defined(FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK) && FSL_FEATURE_PORT_HAS_PIN_CONTROL_LOCK
        .lockRegister = kPORT_UnlockRegister
#endif
    };

    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1U);
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM4);
    CLOCK_EnableClock(kCLOCK_LPFlexComm4);
    RESET_ClearPeripheralReset(kFC4_RST_SHIFT_RSTn);
    CLOCK_EnableClock(kCLOCK_Port1);

    PORT_SetPinConfig(PORT1, 8U, &uart_rx);
    PORT_SetPinConfig(PORT1, 9U, &uart_tx);

    (void)LP_FLEXCOMM_Init(4U, LP_FLEXCOMM_PERIPH_LPUART);
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200U;
    config.enableTx = true;
    config.enableRx = true;
    (void)LPUART_Init(LPUART4, &config, 12000000U);
}

void uart_write(const char *buf, unsigned int sz)
{
    const char *line;
    unsigned int line_sz;

    while (sz > 0) {
        line = memchr(buf, '\n', sz);
        if (line == NULL) {
            (void)LPUART_WriteBlocking(LPUART4, (const uint8_t *)buf, sz);
            break;
        }
        line_sz = (unsigned int)(line - buf);
        if (line_sz > sz - 1U) {
            line_sz = sz - 1U;
        }
        (void)LPUART_WriteBlocking(LPUART4, (const uint8_t *)buf, line_sz);
        (void)LPUART_WriteBlocking(LPUART4, (const uint8_t *)"\r\n", 2U);
        buf = line + 1;
        sz -= line_sz + 1U;
    }
}

#if defined(WOLFCRYPT_TZ_PSA) && defined(WOLFBOOT_DICE_HW) && defined(__WOLFBOOT)

/* Holds the raw 64-byte P-256 public key (X||Y) written by hal_dice_create_attest_key().
 * Consumed and zeroized by hal_dice_get_attest_pubkey(). */
static uint8_t s_dice_attest_pubkey[64];
static int     s_dice_attest_pubkey_valid = 0;

static NOINLINEFUNCTION void hal_dice_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0U) {
        *p++ = 0U;
    }
}

/* Derive 33-byte UEID from device UUID at IPC1 (MCXN_UUID_ADDR).
 * UUID (16 bytes, big-endian) is SHA-256 hashed to produce a 32-byte
 * opaque identifier. This matches WOLFBOOT_DICE_UEID_LEN = 33.
 * We can't use UDS because NXP_DIE_DICE_UDS_MK_SK is not accessible,
 * so we derive UEID from the UUID instead.  */
int hal_attestation_get_ueid(uint8_t *buf, size_t *len)
{
    volatile const uint32_t *uuid_addr =
        (volatile const uint32_t *)MCXN_UUID_ADDR;
    uint8_t uuid_be[16];
    uint32_t word;
    wc_Sha256 sha;
    int i, ret = 0;

    if (buf == NULL || len == NULL || *len < 33)
        ret = -1;

    if (ret == 0) {
        /* Read 4 words (16 bytes) and convert each to big-endian */
        for (i = 0; i < 4; i++) {
            word = uuid_addr[i];
            uuid_be[i * 4 + 0] = (uint8_t)(word >> 24);
            uuid_be[i * 4 + 1] = (uint8_t)(word >> 16);
            uuid_be[i * 4 + 2] = (uint8_t)(word >> 8);
            uuid_be[i * 4 + 3] = (uint8_t)(word);
        }

#ifdef DEBUG
        wolfBoot_printf("[DICE] UUID:");
        for (i = 0; i < (int)sizeof(uuid_be); i++)
            wolfBoot_printf(" %02x", uuid_be[i]);
        wolfBoot_printf("\r\n");
#endif

        /* SHA-256(UUID) -> 32-byte UEID payload */
        ret = wc_InitSha256(&sha);
        if (ret == 0) {
            ret = wc_Sha256Update(&sha, uuid_be, sizeof(uuid_be));
            if (ret == 0)
                ret = wc_Sha256Final(&sha, &buf[1]);
            wc_Sha256Free(&sha);
        }

        if (ret == 0) {
            /* UEID Type RANDOM per EAT spec */
            buf[0] = 0x01;
            *len = 33; /* WOLFBOOT_DICE_UEID_LEN */

#ifdef DEBUG
            wolfBoot_printf("[DICE] UEID:");
            for (i = 0; i < 33; i++)
                wolfBoot_printf(" %02x", buf[i]);
            wolfBoot_printf("\r\n");
#endif
        }
    }

    XMEMSET(uuid_be, 0, sizeof(uuid_be));
    return ret;
}

int hal_attestation_get_lifecycle(uint32_t *lifecycle)
{
    if (lifecycle == NULL)
        return -1;
    *lifecycle = 0x3000u; /* PSA_LIFECYCLE_SECURED (default) */
    return 0;
}

/* Counts actual hardware CDI derivations performed (wolfBoot is always skipped).
 * Shared with hal_dice_create_attest_key so it can reset after each complete sequence. */
static int cdi_derivation_count = 0;

/* Derive new CDI from measurement and previous CDI */
int hal_dice_update_cdi(const uint8_t *measurement, size_t meas_len,
                        const char *measurement_desc, size_t measurement_desc_len)
{
    uint8_t deriv[MCUXCLELS_HKDF_RFC5869_DERIVATIONDATA_SIZE];
    _Static_assert(MCUXCLELS_HKDF_RFC5869_DERIVATIONDATA_SIZE >= SHA256_DIGEST_SIZE,
        "MCUXCLELS_HKDF_RFC5869_DERIVATIONDATA_SIZE must be at least SHA256_DIGEST_SIZE");
    mcuxClEls_HkdfOption_t opts = {0};
    mcuxClEls_KeyProp_t props = {0};
    int ret = 0;

#ifdef DEBUG
    wolfBoot_printf("[DICE] update_cdi: derivation_count=%d meas_len=%u\r\n",
                    cdi_derivation_count, (unsigned)meas_len);
#endif
    XMEMSET(deriv, 0, sizeof(deriv));

    /* ROM DICE already incorporated wolfBoot as HKDF(UDS, wolfBoot_hash) -> initial_CDI.
     * Skip re-applying it — doing so would produce the wrong CDI chain. */
    if (measurement_desc != NULL &&
        measurement_desc_len == (sizeof(WOLFBOOT_DICE_COMPONENT_WOLFBOOT) - 1) &&
        XMEMCMP(measurement_desc, WOLFBOOT_DICE_COMPONENT_WOLFBOOT, measurement_desc_len) == 0) {
#ifdef DEBUG
        wolfBoot_printf("[DICE] update_cdi: skipping wolfboot component (ROM already applied)\r\n");
#endif
        return 0;
    }

    if (cdi_derivation_count > 0) {
        /* Key-slot constraint: only 1 derived CDI slot available.
         * Raise this limit only after adding extra slots. */
#ifdef DEBUG
        wolfBoot_printf("[DICE] update_cdi: cdi_derivation_count=%d > 0, too many components\r\n",
                        cdi_derivation_count);
#endif
        /* Do not return here: the shared epilogue resets cdi_derivation_count on error
         * (same as other failure paths). This branch issues no ELS commands; any key
         * material in the derived CDI slot is unchanged from the prior successful HKDF.
         * After the epilogue, the next successful call KDELETEs that slot then HKDFs
         * from the ROM initial CDI slot (count is zero again — fresh chain). */
        ret = -1;
    }

    if (ret == 0 && (measurement == NULL || meas_len == 0)) {
#ifdef DEBUG
        wolfBoot_printf("[DICE] update_cdi: invalid measurement (NULL or zero len)\r\n");
#endif
        ret = -1;
    }

    if (ret == 0) {
        if (meas_len > SHA256_DIGEST_SIZE) {
            /* Pre-hash to SHA-256 digest */
            wc_Sha256 sha;
            ret = wc_InitSha256(&sha);
            if (ret == 0) {
                ret = wc_Sha256Update(&sha, measurement, (word32)meas_len);
                if (ret == 0) {
                    ret = wc_Sha256Final(&sha, deriv);
                }
                wc_Sha256Free(&sha);
            }
#ifdef DEBUG
            if (ret != 0)
                wolfBoot_printf("[DICE] update_cdi: wc_Sha256 failed %d\r\n", ret);
#endif
        }
        else {
            XMEMCPY(deriv, measurement, meas_len);
        }
    }

    if (ret == 0) {
        /* Trigger the KDELETE command to free the key slot.
         * Note that the slot may be empty on the first update, but that's not an error
         * because ELS ignores the KDELETE command if the slot is empty.
         * We just give the names of token and return value
         * since it's declared within the macro */
        MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_kdel, tok_kdel,
            mcuxClEls_KeyDelete_Async(MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT));

        if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_KeyDelete_Async) != tok_kdel) ||
            (MCUXCLELS_STATUS_OK_WAIT != res_kdel)) {
#ifdef DEBUG
            wolfBoot_printf("[DICE] update_cdi: KeyDelete_Async failed"
                            " res=0x%x tok=0x%x\r\n",
                            (unsigned)res_kdel, (unsigned)tok_kdel);
#endif
            ret = -1;
        }

        MCUX_CSSL_FP_FUNCTION_CALL_END();

        /* Wait for hardware to finish */
        if (ret == 0) {
            MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_w, tok_w,
                mcuxClEls_WaitForOperation(MCUXCLELS_ERROR_FLAGS_CLEAR));

            if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_WaitForOperation) != tok_w) ||
                (MCUXCLELS_STATUS_OK != res_w)) {
#ifdef DEBUG
                wolfBoot_printf("[DICE] update_cdi: WaitForOperation(KDELETE) failed"
                                " res=0x%x tok=0x%x\r\n",
                                (unsigned)res_w, (unsigned)tok_w);
#endif
                ret = -1;
            }

            MCUX_CSSL_FP_FUNCTION_CALL_END();
        }
    }

    if (ret == 0) {
        /* first derivation: start from ROM-loaded initial CDI; subsequent: chain from derived CDI */
        mcuxClEls_KeyIndex_t hkdf_src_slot = (cdi_derivation_count == 0)
            ? MCXN_ELS_DICE_CDI_INITIAL_KEYSLOT
            : MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT;

        /* Set HKDF options */
        opts.bits.hkdf_algo = MCUXCLELS_HKDF_ALGO_RFC5869;

        /* Set key properties */
        props.bits.upprot_priv = MCUXCLELS_KEYPROPERTY_PRIVILEGED_TRUE;
        props.bits.upprot_sec = MCUXCLELS_KEYPROPERTY_SECURE_TRUE;
        props.bits.ukgsrc = MCUXCLELS_KEYPROPERTY_INPUT_FOR_ECC_TRUE;
        props.bits.uhkdf = MCUXCLELS_KEYPROPERTY_HKDF_TRUE;
        props.bits.fgp = MCUXCLELS_KEYPROPERTY_GENERAL_PURPOSE_SLOT_TRUE;
        props.bits.kbase = MCUXCLELS_KEYPROPERTY_BASE_SLOT;
        props.bits.kactv = MCUXCLELS_KEYPROPERTY_ACTIVE_TRUE;
        props.bits.ksize = MCUXCLELS_KEYPROPERTY_KEY_SIZE_256;

        /* Trigger the HKDF command.
         * We just give the names of token and return value
         * since it's declared within the macro */
        MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_hkdf, tok_hkdf,
            mcuxClEls_Hkdf_Rfc5869_Async(opts,
                                         hkdf_src_slot,
                                         MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT,
                                         props, deriv));

        if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_Hkdf_Rfc5869_Async) != tok_hkdf) ||
            (MCUXCLELS_STATUS_OK_WAIT != res_hkdf)) {
#ifdef DEBUG
            wolfBoot_printf("[DICE] update_cdi: Hkdf_Rfc5869_Async failed"
                            " res=0x%x tok=0x%x\r\n",
                            (unsigned)res_hkdf, (unsigned)tok_hkdf);
#endif
            ret = -1;
        }

        MCUX_CSSL_FP_FUNCTION_CALL_END();

        /* Wait for hardware to finish */
        if (ret == 0) {
            MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_w, tok_w,
                mcuxClEls_WaitForOperation(MCUXCLELS_ERROR_FLAGS_CLEAR));

            if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_WaitForOperation) != tok_w) ||
                (MCUXCLELS_STATUS_OK != res_w)) {
#ifdef DEBUG
                wolfBoot_printf("[DICE] update_cdi: WaitForOperation(HKDF) failed"
                                " res=0x%x tok=0x%x\r\n",
                                (unsigned)res_w, (unsigned)tok_w);
#endif
                ret = -1;
            }

            MCUX_CSSL_FP_FUNCTION_CALL_END();
        }
    }

    if (ret == 0)
        cdi_derivation_count++;
    else
        cdi_derivation_count = 0;   /* reset on error — create_attest_key won't be called */

    XMEMSET(deriv, 0, sizeof(deriv));
#ifdef DEBUG
    wolfBoot_printf("[DICE] update_cdi: ret=%d\r\n", ret);
#endif
    return ret;
}

/* Generate P-256 IAK from derived CDI using ELS KEYGEN.
 * Private key stays in ELS keystore. Public key written to system memory. */
int hal_dice_create_attest_key(void)
{
    uint8_t pub_key[64] __attribute__((aligned(4)));
    mcuxClEls_EccKeyGenOption_t opts = {0};
    mcuxClEls_KeyProp_t props = {0};
    int ret = 0;

    s_dice_attest_pubkey_valid = 0;
    XMEMSET(s_dice_attest_pubkey, 0, sizeof(s_dice_attest_pubkey));
#ifdef DEBUG
    wolfBoot_printf("[DICE] create_attest_key: start\r\n");
#endif

    /* No KeyDelete here: DETERMINISTIC EccKeyGen reads the CDI from
     * IAK_KEYSLOT (= CDI_DERIVED_KEYSLOT) as its seed input.
     * Deleting that slot first would destroy the source material. */

    /* Set KeyGen options */
    opts.bits.kgsrc    = MCUXCLELS_ECC_OUTPUTKEY_DETERMINISTIC;
    opts.bits.kgtypedh = MCUXCLELS_ECC_OUTPUTKEY_SIGN;

    /* Set key properties */
    props.bits.upprot_priv = MCUXCLELS_KEYPROPERTY_PRIVILEGED_TRUE;
    props.bits.upprot_sec = MCUXCLELS_KEYPROPERTY_SECURE_TRUE;
    props.bits.uecsg = MCUXCLELS_KEYPROPERTY_ECC_TRUE;
    props.bits.uksk = MCUXCLELS_KEYPROPERTY_KSK_TRUE;
    props.bits.fgp = MCUXCLELS_KEYPROPERTY_GENERAL_PURPOSE_SLOT_TRUE;
    props.bits.kbase = MCUXCLELS_KEYPROPERTY_BASE_SLOT;
    props.bits.kactv = MCUXCLELS_KEYPROPERTY_ACTIVE_TRUE;
    props.bits.ksize = MCUXCLELS_KEYPROPERTY_KEY_SIZE_256;

#ifdef DEBUG
    wolfBoot_printf("[DICE] create_attest_key: EccKeyGen"
                    " CDI_DERIVED_SLOT=%d IAK_SLOT=%d\r\n",
                    MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT,
                    MCXN_ELS_DICE_IAK_KEYSLOT);
#endif

    /* Trigger the ECC KeyGen command.
     * We just give the names of token and return value
     * since it's declared within the macro */
    MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_kg, tok_kg,
        mcuxClEls_EccKeyGen_Async(opts,
            MCXN_ELS_DICE_CDI_DERIVED_KEYSLOT,
            MCXN_ELS_DICE_IAK_KEYSLOT,
            props, NULL, pub_key));

    if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_EccKeyGen_Async) != tok_kg) ||
        (MCUXCLELS_STATUS_OK_WAIT != res_kg)) {
#ifdef DEBUG
            wolfBoot_printf("[DICE] create_attest_key: EccKeyGen_Async failed"
                            " res=0x%x tok=0x%x\r\n",
                            (unsigned)res_kg, (unsigned)tok_kg);
#endif
            ret = -1;
    }

    MCUX_CSSL_FP_FUNCTION_CALL_END();

    /* Wait for hardware to finish */
    if (ret == 0) {
        MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_w, tok_w,
            mcuxClEls_WaitForOperation(MCUXCLELS_ERROR_FLAGS_CLEAR));

        if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_WaitForOperation) != tok_w) ||
            (MCUXCLELS_STATUS_OK != res_w)) {
#ifdef DEBUG
            wolfBoot_printf("[DICE] create_attest_key: WaitForOperation(KEYGEN) failed"
                            " res=0x%x tok=0x%x\r\n",
                            (unsigned)res_w, (unsigned)tok_w);
#endif
            ret = -1;
        }

        MCUX_CSSL_FP_FUNCTION_CALL_END();
    }

    if (ret == 0) {
        XMEMCPY(s_dice_attest_pubkey, pub_key, sizeof(s_dice_attest_pubkey));
        s_dice_attest_pubkey_valid = 1;
    }

    cdi_derivation_count = 0;   /* reset for the next token build */
    XMEMSET(pub_key, 0, sizeof(pub_key));
#ifdef DEBUG
    wolfBoot_printf("[DICE] create_attest_key: ret=%d\r\n", ret);
#endif
    return ret;
}

int hal_dice_get_attest_pubkey(uint8_t *buf, size_t *len)
{
    if (buf == NULL || len == NULL || *len < 65)
        return -1;
    if (!s_dice_attest_pubkey_valid)
        return -1;

    buf[0] = 0x04; /* X9.63 uncompressed prefix */
    XMEMCPY(buf + 1, s_dice_attest_pubkey, sizeof(s_dice_attest_pubkey));
    *len = 65;

    /* Zeroize cached public key after copying out (read-once). */
    hal_dice_zeroize(s_dice_attest_pubkey, sizeof(s_dice_attest_pubkey));
    s_dice_attest_pubkey_valid = 0;

    return 0;
}

int hal_dice_sign_hash(const uint8_t *hash, size_t hash_len,
                       uint8_t *sig, size_t *sig_len)
{
    mcuxClEls_EccSignOption_t opts = {0};
    uint8_t hash_buf[SHA256_DIGEST_SIZE] __attribute__((aligned(4)));
    uint8_t sig_buf[MCUXCLELS_ECC_SIGNATURE_SIZE] __attribute__((aligned(4)));
    int ret = 0;
    _Static_assert(MCUXCLELS_ECC_SIGNATURE_SIZE == 64,
        "MCUXCLELS_ECC_SIGNATURE_SIZE must equal WOLFBOOT_DICE_SIG_LEN (64)");

#ifdef DEBUG
    wolfBoot_printf("[DICE] sign_hash: hash_len=%u\r\n", (unsigned)hash_len);
#endif

    if (hash == NULL || sig == NULL || sig_len == NULL || hash_len != SHA256_DIGEST_SIZE ||
        *sig_len < MCUXCLELS_ECC_SIGNATURE_SIZE) {
#ifdef DEBUG
        wolfBoot_printf("[DICE] sign_hash: invalid args"
                        " hash=%p sig=%p sig_len=%p hash_len=%u\r\n",
                        hash, sig, sig_len, (unsigned)hash_len);
#endif
        ret = -1;
    }

    if (ret == 0) {
        XMEMCPY(hash_buf, hash, SHA256_DIGEST_SIZE);

        /* Set options */
        opts.bits.echashchl = MCUXCLELS_ECC_HASHED;

        /* Trigger the ECC Sign command.
         * We just give the names of token and return value
         * since it's declared within the macro */
        MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_sign, tok_sign,
            mcuxClEls_EccSign_Async(opts,
                MCXN_ELS_DICE_IAK_KEYSLOT,
                hash_buf, NULL, 0, sig_buf));

        if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_EccSign_Async) != tok_sign) ||
            (MCUXCLELS_STATUS_OK_WAIT != res_sign)) {
#ifdef DEBUG
            wolfBoot_printf("[DICE] sign_hash: EccSign_Async failed"
                            " res=0x%x tok=0x%x\r\n",
                            (unsigned)res_sign, (unsigned)tok_sign);
#endif
            ret = -1;
        }

        MCUX_CSSL_FP_FUNCTION_CALL_END();

        /* Wait for hardware to finish */
        if (ret == 0) {
            MCUX_CSSL_FP_FUNCTION_CALL_BEGIN(res_w, tok_w,
                mcuxClEls_WaitForOperation(MCUXCLELS_ERROR_FLAGS_CLEAR));

            if ((MCUX_CSSL_FP_FUNCTION_CALLED(mcuxClEls_WaitForOperation) != tok_w) ||
                (MCUXCLELS_STATUS_OK != res_w)) {
#ifdef DEBUG
                wolfBoot_printf("[DICE] sign_hash: WaitForOperation(SIGN) failed"
                                " res=0x%x tok=0x%x\r\n",
                                (unsigned)res_w, (unsigned)tok_w);
#endif
                ret = -1;
            }

            MCUX_CSSL_FP_FUNCTION_CALL_END();
        }
    }

    if (ret == 0) {
        XMEMCPY(sig, sig_buf, MCUXCLELS_ECC_SIGNATURE_SIZE);
        *sig_len = MCUXCLELS_ECC_SIGNATURE_SIZE;
    }

    hal_dice_zeroize(hash_buf, sizeof(hash_buf));
    hal_dice_zeroize(sig_buf, sizeof(sig_buf));
#ifdef DEBUG
    wolfBoot_printf("[DICE] sign_hash: ret=%d\r\n", ret);
#endif
    return ret;
}

#endif /* WOLFCRYPT_TZ_PSA && WOLFBOOT_DICE_HW && __WOLFBOOT */
