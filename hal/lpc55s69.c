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

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/hmac.h>

#ifdef WOLFSSL_HWPUF
#include <wolfssl/wolfcrypt/hwpuf.h>
#endif

#if defined(WOLFCRYPT_TZ_PSA)
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
#endif

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


#if defined(__WOLFBOOT) && defined(WOLFSSL_HWPUF)
#define HWPUF_PROV_MAGIC            0x564f5250ul    /* "PROV" */
#define HWPUF_PROV_FLASH_BASEADR    0x80000ul
#define HWPUF_PROV_FLASH_LEN        0x1000ul
#define HWPUF_PROV_UDS_KEY_INDEX    14              /* NOT the one in PFR */
#define HWPUF_PROV_UDS_KEY_SIZE     32

typedef struct hwpuf_prov {
    word32 magic;
    byte ac[HWPUF_ACTIVATION_CODE_SIZE];
    byte uds_kc[PUF_GET_KEY_CODE_SIZE_FOR_KEY_SIZE(HWPUF_PROV_UDS_KEY_SIZE)];
} hwpuf_prov;

static hwpuf_prov prov;

static int hwpuf_provision_get(void)
{
    if (sizeof(prov) > HWPUF_PROV_FLASH_LEN)
        return -1;

    flashInit();

    /* verify none of sectors in erased state */
    {
        uint32_t addr;
        uint32_t start = HWPUF_PROV_FLASH_BASEADR;
        uint32_t end = HWPUF_PROV_FLASH_BASEADR + HWPUF_PROV_FLASH_LEN;

        for (addr = start; addr < end; addr += pflash_page_size) {
            if (FLASH_VerifyErase(&pflash, addr, pflash_page_size)
                    == kStatus_FLASH_Success) {
                return -1;
            }
        }
    }

    XMEMCPY(&prov, (byte *)HWPUF_PROV_FLASH_BASEADR, sizeof(prov));

    if (prov.magic != HWPUF_PROV_MAGIC)
        return -1;

    return 0;
}

#ifdef WOLFBOOT_HWPUF_PROVISION
static int hwpuf_provision_set(int force)
{
    int ret = -1;
    wc_HWPUF hwpuf;

    if (sizeof(prov) > HWPUF_PROV_FLASH_LEN)
        return -1;

    if (!force) {
        if (hwpuf_provision_get() == 0)
            return -1;
    }

    XMEMSET(&prov, 0, sizeof(prov));

    if (wc_HWPUF_Register(&hwpuf, NULL, INVALID_DEVID) != 0)
        return -1;

    if (wc_HWPUF_Init(&hwpuf) != 0)
        goto error_out;

    if (wc_HWPUF_Enroll(&hwpuf, prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    (void)wc_HWPUF_Deinit(&hwpuf);
    (void)wc_HWPUF_Init(&hwpuf);

    if (wc_HWPUF_Start(&hwpuf, prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    if (wc_HWPUF_GenerateKey(&hwpuf,
                             HWPUF_PROV_UDS_KEY_INDEX, HWPUF_PROV_UDS_KEY_SIZE,
                             prov.uds_kc, sizeof(prov.uds_kc)) != 0)
        goto error_out;

    prov.magic = HWPUF_PROV_MAGIC;

    flashInit();

    hal_flash_unlock();
    ret = hal_flash_erase(HWPUF_PROV_FLASH_BASEADR, HWPUF_PROV_FLASH_LEN);
    if (ret != 0) {
        hal_flash_lock();
        goto error_out;
    }
    ret = hal_flash_write(HWPUF_PROV_FLASH_BASEADR,
                          (const uint8_t *)&prov, sizeof(prov));
    hal_flash_lock();
    if (ret != 0)
        goto error_out;

    ret = 0;

error_out:
    hal_zeroize(&prov, sizeof(prov));
    (void)wc_HWPUF_Zeroize(&hwpuf);
    (void)wc_HWPUF_Deinit(&hwpuf);
    (void)wc_HWPUF_Unregister(&hwpuf);
    return ret;
}
#endif /* WOLFBOOT_HWPUF_PROVISION */

#if defined(WOLFCRYPT_TZ_PSA)
static int uds_from_hwpuf(uint8_t *out, size_t out_len)
{
    int ret = -1;
    wc_HWPUF hwpuf;
    byte uds[HWPUF_PROV_UDS_KEY_SIZE];

    if (hwpuf_provision_get() != 0)
        return -1;

    if (wc_HWPUF_Register(&hwpuf, NULL, INVALID_DEVID) != 0)
        return -1;

    if (wc_HWPUF_Init(&hwpuf) != 0)
        goto error_out;

    if (wc_HWPUF_Start(&hwpuf, prov.ac, sizeof(prov.ac)) != 0)
        goto error_out;

    ret = wc_HWPUF_GetKey(&hwpuf, prov.uds_kc, sizeof(prov.uds_kc),
                          uds, sizeof(uds));
    if (ret != 0)
        goto error_out;

    ret = wc_HKDF_ex((int)HAL_KDF_HASH_TYPE, uds, (word32)sizeof(uds),
                     (const byte *)"WOLFBOOT-UDS", (word32)12,
                     (const byte *)"WOLFBOOT-UDS", (word32)12,
                     (byte *)out, (word32)out_len,
                     NULL, INVALID_DEVID);
    if (ret != 0)
        goto error_out;

error_out:
    hal_zeroize(uds, sizeof(uds));
    hal_zeroize(&prov, sizeof(prov));
    (void)wc_HWPUF_Zeroize(&hwpuf);
    (void)wc_HWPUF_Deinit(&hwpuf);
    (void)wc_HWPUF_Unregister(&hwpuf);
    return ret == 0 ? 0 : -1;
}
#endif /* WOLFCRYPT_TZ_PSA */
#endif /* __WOLFBOOT && WOLFSSL_HWPUF */

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

#if defined(__WOLFBOOT) && defined(WOLFCRYPT_TZ_PSA) && !defined(WOLFBOOT_DICE_HW)
#ifdef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
static int uds_from_uid(uint8_t *out, size_t out_len)
{
    uint8_t uid[16];
#if defined(WOLFBOOT_HASH_SHA256)
    uint8_t digest[SHA256_DIGEST_SIZE];
#elif defined(WOLFBOOT_HASH_SHA384)
    uint8_t digest[SHA384_DIGEST_SIZE];
#elif defined(WOLFBOOT_HASH_SHA3_384)
    uint8_t digest[SHA3_384_DIGEST_SIZE];
#endif
    size_t copy_len = sizeof(digest);

    flashInit();

    if (FFR_GetUUID(&pflash, uid) != kStatus_Success)
        return -1;
    reverse_array(uid, sizeof(uid));

#if defined(WOLFBOOT_HASH_SHA256)
    {
        wc_Sha256 hash;
        wc_InitSha256(&hash);
        wc_Sha256Update(&hash, uid, sizeof(uid));
        wc_Sha256Final(&hash, digest);
    }
#elif defined(WOLFBOOT_HASH_SHA384)
    {
        wc_Sha384 hash;
        wc_InitSha384(&hash);
        wc_Sha384Update(&hash, uid, sizeof(uid));
        wc_Sha384Final(&hash, digest);
    }
#elif defined(WOLFBOOT_HASH_SHA3_384)
    {
        wc_Sha3 hash;
        wc_InitSha3_384(&hash, NULL, INVALID_DEVID);
        wc_Sha3_384_Update(&hash, uid, sizeof(uid));
        wc_Sha3_384_Final(&hash, digest);
    }
#endif

    if (copy_len > out_len) {
        copy_len = out_len;
    }
    memcpy(out, digest, copy_len);
#ifdef DEBUG
    {
        int idx;
        wolfBoot_printf("[ATTEST] UDS FORTEST:");
        for (idx = 0; idx < (int)out_len; ++idx)
            wolfBoot_printf(" %02x", out[idx]);
        wolfBoot_printf("\n");
    }
#endif
    return 0;
}
#endif /* WOLFBOOT_UDS_UID_FALLBACK_FORTEST */

/* Derive UDS from hw puf, fall back to derive from uid */
int hal_uds_derive_key(uint8_t *out, size_t out_len)
{
    if (out == NULL || out_len == 0)
        return -1;

#ifdef WOLFBOOT_UDS_UID_FALLBACK_FORTEST
    return uds_from_uid(out, out_len);
#elif defined(WOLFSSL_HWPUF)
    return uds_from_hwpuf(out, out_len);
#else
    return -1;
#endif
}
#endif /* __WOLFBOOT && WOLFCRYPT_TZ_PSA && !WOLFBOOT_DICE_HW */

#ifdef TZEN
static void hal_sau_init(void)
{
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
    sau_init_region(3, 0x40000000, 0x4003FFFF, 0);
    sau_init_region(4, 0x40080000, 0x400AFFFF, 0);
    sau_init_region(5, 0x40100000, 0x4010FFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

static void periph_unsecure(void)
{
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);
}
#endif

static void hal_flash_fix_ecc(void)
{
    uint8_t page_buf[512];
    uint32_t addr;
    uint32_t start = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t end   = WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE;

    memset(page_buf, 0xFF, sizeof(page_buf));

    for (addr = start; addr < end; addr += pflash_page_size) {
        if (FLASH_VerifyErase(&pflash, addr, pflash_page_size)
                == kStatus_FLASH_Success) {
            FLASH_Program(&pflash, addr, page_buf, pflash_page_size);
        }
    }
}


extern int wc_hashcrypt_init(void);
extern int wc_casper_init(void);

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* lpc55s69 must run < 100 MHz for flash write/erase to work */
    BOARD_BootClockFROHF96M();

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
#endif /* __WOLFBOOT */

#if defined(__WOLFBOOT) || !defined(TZEN)
    flashInit();
    hal_flash_fix_ecc();
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
    hal_sau_init();
#endif

#ifdef __WOLFBOOT
    wolfCrypt_Init();
#endif

#if defined(__WOLFBOOT) && defined(WOLFBOOT_HWPUF_PROVISION)
    if (hwpuf_provision_set(0) != 0)
        uart_write("hwpuf provision failure (already provisioned?)\n", 47);
    else
        uart_write("hwpuf provision success\n", 24);
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
            memset(page_buf, 0xFF, pflash_page_size);
        } else {
            memcpy(page_buf, (void *)page_addr, pflash_page_size);

            if (FLASH_Erase(&pflash, page_addr, pflash_page_size,
                    kFLASH_ApiEraseKey) != kStatus_FLASH_Success)
                return -1;
        }

        memcpy(page_buf + offset, data, chunk);

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

    memset(page_buf, 0xFF, sizeof(page_buf));

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
#endif


#define IOCON_PIO_DIGITAL_EN 0x0100u  /*!<@brief Enables digital function */
#define IOCON_PIO_FUNC1 0x01u         /*!<@brief Selects pin function 1 */
#define IOCON_PIO_INV_DI 0x00u        /*!<@brief Input function is not inverted */
#define IOCON_PIO_MODE_INACT 0x00u    /*!<@brief No addition pin function */
#define IOCON_PIO_OPENDRAIN_DI 0x00u  /*!<@brief Open drain is disabled */
#define IOCON_PIO_SLEW_STANDARD 0x00u /*!<@brief Standard mode, output slew rate control is enabled */

#ifdef DEBUG_UART
void uart_init(void)
{
    CLOCK_EnableClock(kCLOCK_Iocon);
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
#endif
