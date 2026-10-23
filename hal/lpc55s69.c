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

#include <wolfssl/wolfcrypt/types.h>

#ifdef TZEN
#include "hal/armv8m_tz.h"
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
static const uint32_t pflash_page_size = 512U;
uint32_t SystemCoreClock; /* set in clock_config.c */

static void flashInit(void)
{
    static int initialized = 0;

    if (!initialized) {
        XMEMSET(&pflash, 0, sizeof(pflash));
        FLASH_Init(&pflash);
        FFR_Init(&pflash);
        initialized = 1;
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

typedef struct hwpuf_prov {
    byte ac[PUF_ACTIVATION_CODE_SIZE];
    byte uds_kc[PUF_GET_KEY_CODE_SIZE_FOR_KEY_SIZE(HWPUF_PROV_UDS_KEY_SIZE)];
} hwpuf_prov;

/* The payload must end before the commit page */
typedef char hwpuf_prov_size_check[
    (sizeof(hwpuf_prov) <= HWPUF_PROV_FLASH_LEN - HWPUF_PROV_PAGE_SIZE) ?
    1 : -1];

/* prov holds no secure information, so does not need aggressive zeroing */
static hwpuf_prov prov;

#define HWPUF_PROV_BLANK    1

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
            *(volatile uint32_t *)HWPUF_PROV_COMMIT_ADDRESS == 0xFFFFFFFFul)
        return HWPUF_PROV_BLANK;

    commit_page[0] = HWPUF_PROV_MAGIC;
    if (FLASH_VerifyProgram(&pflash, HWPUF_PROV_COMMIT_ADDRESS,
            HWPUF_PROV_PAGE_SIZE, (const uint8_t *)commit_page,
            &failed_addr, &failed_data) != kStatus_FLASH_Success)
        return -1;

    if (*(volatile uint32_t *)HWPUF_PROV_COMMIT_ADDRESS != HWPUF_PROV_MAGIC)
        return -1;

    for (addr = WOLFBOOT_HWPUF_PROV_ADDRESS; addr < end;
            addr += HWPUF_PROV_PAGE_SIZE) {
        if (FLASH_VerifyErase(&pflash, addr, HWPUF_PROV_PAGE_SIZE)
                == kStatus_FLASH_Success)
            return -1;
    }

    XMEMCPY(rec, (const void *)WOLFBOOT_HWPUF_PROV_ADDRESS, sizeof(*rec));
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
    if (XMEMCMP((const void *)WOLFBOOT_HWPUF_PROV_ADDRESS, &prov,
               sizeof(prov)) != 0)
        goto unlock_out;
    if (hal_flash_write(HWPUF_PROV_COMMIT_ADDRESS,
                        (const uint8_t *)&magic, sizeof(magic)) != 0)
        goto unlock_out;
    if (*(volatile uint32_t *)HWPUF_PROV_COMMIT_ADDRESS != HWPUF_PROV_MAGIC)
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

#ifdef TZEN
/* AHB_SECURE_CTRL (AHBSC) gates SRAM at the bus level on this part: the SAU
 * alone does not make an SRAM block non-secure.  The boot ROM leaves every
 * 4 KB block secure+privileged; hand SRAM2 (0x20020000..0x2002FFFF, the NS
 * app RAM) to the NS world as non-secure+privileged before the NS jump.
 * Unprivileged NS code is intentionally kept out of SRAM2: the NS test-app
 * runs privileged. */
#define AHBSC_SRAM2_RULE0   (*(volatile uint32_t *)(AHB_SECURE_CTRL_BASE + 0xA0u))
#define AHBSC_SRAM2_RULE1   (*(volatile uint32_t *)(AHB_SECURE_CTRL_BASE + 0xA4u))

static void hal_ahbsc_init(void)
{
    AHBSC_SRAM2_RULE0 = 0x11111111u; /* blocks 0-7: NS + privileged */
    AHBSC_SRAM2_RULE1 = 0x11111111u; /* blocks 8-15: NS + privileged */
}

static void hal_sau_init(void)
{
    hal_ahbsc_init();

    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area (boot+update partition) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + (WOLFBOOT_PARTITION_SIZE * 2) - 1,
            0);

    /* Non-secure RAM */
    sau_init_region(2, 0x20020000, 0x2002FFFF, 0);

    /* Peripherals */
    sau_init_region(3, 0x40086000, 0x40086FFF, 0);  /* uart on flexcomm0 */
    sau_init_region(4, 0x4008c000, 0x4008FFFF, 0);  /* gpio for leds */

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

static void periph_unsecure(void)
{
}
#endif /* TZEN */

static void hal_flash_fix_ecc(void)
{
    uint8_t page_buf[512];
    uint32_t addr;
    uint32_t start = ARCH_FLASH_OFFSET;
    uint32_t end   = WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE;

    XMEMSET(page_buf, 0xFF, sizeof(page_buf));

    for (addr = start; addr < end; addr += pflash_page_size) {
        if (FLASH_VerifyErase(&pflash, addr, pflash_page_size)
                == kStatus_FLASH_Success) {
            FLASH_Program(&pflash, addr, page_buf, pflash_page_size);
        }
    }
}

#ifdef __WOLFBOOT
#define IOCON_PIO_FUNC0 0x00u         /*!<@brief Selects pin function 0 */
#define IOCON_PIO_MODE_PULLUP 0x20u   /*!<@brief Selects pull-up function */
#define IOCON_PIO_SLEW_STANDARD 0x00u /*!<@brief Standard mode, output slew rate control is enabled */
#define IOCON_PIO_INV_DI 0x00u        /*!<@brief Input function is not inverted */
#define IOCON_PIO_DIGITAL_EN 0x0100u  /*!<@brief Enables digital function */
#define IOCON_PIO_OPENDRAIN_DI 0x00u  /*!<@brief Open drain is disabled */

#define RED_LED     6
#define GREEN_LED   7
#define BLUE_LED    4

static void leds_pin_init(void)
{
    /* red output off */
    GPIO->SET[1] = 1UL << RED_LED;
    GPIO->DIR[1] |= 1UL << RED_LED;
    /* green output off */
    GPIO->SET[1] = 1UL << GREEN_LED;
    GPIO->DIR[1] |= 1UL << GREEN_LED;
    /* blue output off */
    GPIO->SET[1] = 1UL << BLUE_LED;
    GPIO->DIR[1] |= 1UL << BLUE_LED;

    const uint32_t LED_PINMUX_CONFIG = (/* Pin is configured as PIO */
                               IOCON_PIO_FUNC0 |
                               /* Selects pull-up function */
                               IOCON_PIO_MODE_PULLUP |
                               /* Standard mode, output slew rate control is enabled */
                               IOCON_PIO_SLEW_STANDARD |
                               /* Input function is not inverted */
                               IOCON_PIO_INV_DI |
                               /* Enables digital function */
                               IOCON_PIO_DIGITAL_EN |
                               /* Open drain is disabled */
                               IOCON_PIO_OPENDRAIN_DI);
    IOCON_PinMuxSet(IOCON, 1, RED_LED, LED_PINMUX_CONFIG);
    IOCON_PinMuxSet(IOCON, 1, GREEN_LED, LED_PINMUX_CONFIG);
    IOCON_PinMuxSet(IOCON, 1, BLUE_LED, LED_PINMUX_CONFIG);
}
#endif /* __WOLFBOOT */

extern int wc_hashcrypt_init(void);
extern int wc_casper_init(void);

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* lpc55s69 must run < 100 MHz for flash write/erase to work */
    BOARD_BootClockFROHF96M();

    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);

# ifdef DEBUG_UART
    uart_init();
    uart_write("lpc55s69 init\n", 14);
# endif

# ifdef WOLFSSL_NXP_LPC55S6X
    CLOCK_EnableClock(kCLOCK_HashCrypt);
    wc_hashcrypt_init();
    CLOCK_EnableClock(kCLOCK_Casper);
    wc_casper_init();
# endif

    CLOCK_EnableClock(kCLOCK_Rng);
    RESET_PeripheralReset(kRNG_RST_SHIFT_RSTn);
    RNG_Init(RNG);

    leds_pin_init();
#endif /* __WOLFBOOT */

#if defined(WOLFSSL_NXP_HWPUF) && (defined(__WOLFBOOT) || !defined(TZEN))
    /* FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL: PUF_Init() won't do this */
    CLOCK_EnableClock(kCLOCK_Puf);
#endif

#if defined(__WOLFBOOT) || !defined(TZEN)
    flashInit();
    hal_flash_fix_ecc();
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
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
#endif /* __WOLFBOOT */

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t page_buf[512];
    uint32_t page_addr;
    uint32_t offset;
    uint32_t chunk;

    while (len > 0) {
        page_addr = address & ~(pflash_page_size - 1);
        offset = address - page_addr;
        chunk = pflash_page_size - offset;
        if ((uint32_t)len < chunk)
            chunk = (uint32_t)len;

        if (FLASH_VerifyErase(&pflash, page_addr, pflash_page_size)
                == kStatus_FLASH_Success) {
            XMEMSET(page_buf, 0xFF, pflash_page_size);
        } else {
            XMEMCPY(page_buf, (void *)page_addr, pflash_page_size);

            if (FLASH_Erase(&pflash, page_addr, pflash_page_size,
                    kFLASH_ApiEraseKey) != kStatus_FLASH_Success)
                return -1;
        }

        XMEMCPY(page_buf + offset, data, chunk);

        if (FLASH_Program(&pflash, page_addr, page_buf, pflash_page_size)
                != kStatus_FLASH_Success)
            return -1;

        address += chunk;
        data += chunk;
        len -= (int)chunk;
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
    uint8_t page_buf[512];
    uint32_t pos;

    if (address % pflash_page_size != 0 || len % pflash_page_size != 0)
        return -1;

    XMEMSET(page_buf, 0xFF, sizeof(page_buf));

    for (pos = address; pos < address + (uint32_t)len; pos += pflash_page_size) {
        if (FLASH_Erase(&pflash, pos, pflash_page_size, kFLASH_ApiEraseKey)
                != kStatus_FLASH_Success)
            return -1;

        if (FLASH_Program(&pflash, pos, page_buf, pflash_page_size)
                != kStatus_FLASH_Success)
            return -1;
    }

    return 0;
}

#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void)
{
    /* handled in hal_init() regardless */
}

void hal_trng_fini(void)
{
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    if (RNG_GetRandomData(RNG, out, len) == kStatus_Success)
        return 0;

    return -1;
}
#endif /* WOLFCRYPT_SECURE_MODE */


#define IOCON_PIO_DIGITAL_EN 0x0100u  /*!<@brief Enables digital function */
#define IOCON_PIO_FUNC1 0x01u         /*!<@brief Selects pin function 1 */
#define IOCON_PIO_INV_DI 0x00u        /*!<@brief Input function is not inverted */
#define IOCON_PIO_MODE_INACT 0x00u    /*!<@brief No addition pin function */
#define IOCON_PIO_OPENDRAIN_DI 0x00u  /*!<@brief Open drain is disabled */
#define IOCON_PIO_SLEW_STANDARD 0x00u /*!<@brief Standard mode, output slew rate control is enabled */

#ifdef DEBUG_UART
void uart_init(void)
{
    const uint32_t port0_pin29_config = (/* Pin is configured as FC0_RXD_SDA_MOSI_DATA */
                                         IOCON_PIO_FUNC1 |
                                         /* No addition pin function */
                                         IOCON_PIO_MODE_INACT |
                                         /* Standard mode, output slew rate control is enabled */
                                         IOCON_PIO_SLEW_STANDARD |
                                         /* Input function is not inverted */
                                         IOCON_PIO_INV_DI |
                                         /* Enables digital function */
                                         IOCON_PIO_DIGITAL_EN |
                                         /* Open drain is disabled */
                                         IOCON_PIO_OPENDRAIN_DI);
    /* PORT0 PIN29 (coords: 92) is configured as FC0_RXD_SDA_MOSI_DATA */
    IOCON_PinMuxSet(IOCON, 0U, 29U, port0_pin29_config);
    const uint32_t port0_pin30_config = (/* Pin is configured as FC0_TXD_SCL_MISO_WS */
                                         IOCON_PIO_FUNC1 |
                                         /* No addition pin function */
                                         IOCON_PIO_MODE_INACT |
                                         /* Standard mode, output slew rate control is enabled */
                                         IOCON_PIO_SLEW_STANDARD |
                                         /* Input function is not inverted */
                                         IOCON_PIO_INV_DI |
                                         /* Enables digital function */
                                         IOCON_PIO_DIGITAL_EN |
                                         /* Open drain is disabled */
                                         IOCON_PIO_OPENDRAIN_DI);
    /* PORT0 PIN30 (coords: 94) is configured as FC0_TXD_SCL_MISO_WS */
    IOCON_PinMuxSet(IOCON, 0U, 30U, port0_pin30_config);

    /* attach 12 MHz clock to FLEXCOMM0 (debug console) */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM0);
    CLOCK_EnableClock(kCLOCK_FlexComm0);
    RESET_ClearPeripheralReset(kFC0_RST_SHIFT_RSTn);

    usart_config_t config;
    USART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200U;
    config.enableTx = true;
    config.enableRx = true;
    (void)USART_Init(USART0, &config, 12000000U);
}

void uart_write(const char *buf, unsigned int sz)
{
    const char *line;
    unsigned int line_sz;

    while (sz > 0)
    {
        line = memchr(buf, '\n', sz);
        if (line == NULL) {
            (void)USART_WriteBlocking(USART0, (const uint8_t *)buf, sz);
            break;
        }
        line_sz = (unsigned int)(line - buf);
        if (line_sz > sz - 1U) {
            line_sz = sz - 1U;
        }
        (void)USART_WriteBlocking(USART0, (const uint8_t *)buf, line_sz);
        (void)USART_WriteBlocking(USART0, (const uint8_t *)"\r\n", 2U);
        buf = line + 1;
        sz -= line_sz + 1U;
    }
}
#endif /* DEBUG_UART */
