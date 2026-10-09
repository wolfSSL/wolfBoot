/* mcxw.c
 *
 * HAL for the NXP MCX W71 (MCXW716C) Cortex-M33.
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
#include <target.h>
#include "image.h"
#include "fsl_common.h"
#include "fsl_clock.h"

#ifdef __WOLFBOOT
#include "fsl_port.h"
#endif

#ifdef DEBUG_UART
#include "fsl_lpuart.h"
#endif

#if defined(BOOT_BENCHMARK) && defined(__WOLFBOOT)
#include "fsl_tstmr.h"
#endif

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
#include "fsl_elemu.h"
#include "fsl_sscp_mu.h"
#include "fsl_sss_sscp.h"
#endif

#include "mcxw.h"

#if (WOLFBOOT_SECTOR_SIZE % MCXW_FLASH_SECTOR_SIZE) != 0
#error WOLFBOOT_SECTOR_SIZE must be a multiple of the 8KB flash sector
#endif

uint32_t SystemCoreClock;

#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void);
int hal_trng_get_entropy(unsigned char *out, unsigned int len);
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
static void hal_sau_init(void)
{
    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area (boot partition) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1, 0);

    /* Non-secure RAM, above the STCM used by wolfBoot */
    sau_init_region(2, MCXW_NS_RAM_START, MCXW_NS_RAM_END, 0);

    /* FMU, SMSCM, ELEMU, MRCC, PORT and DMA stay secure */
    sau_init_region(3, MCXW_UART_NS_START, MCXW_UART_NS_END, 0);
    sau_init_region(4, MCXW_GPIOA_NS_START, MCXW_GPIOA_NS_END, 0);

    NVIC_SetTargetState(LPUART1_IRQn);

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}
#endif

#ifdef DEBUG_UART
#ifdef __WOLFBOOT
void uart_init(void)
{
    lpuart_config_t config;

    CLOCK_EnableClock(kCLOCK_PortC);
    PORT_SetPinMux(PORTC, MCXW_UART_RX_PIN, kPORT_MuxAlt3);
    PORT_SetPinMux(PORTC, MCXW_UART_TX_PIN, kPORT_MuxAlt3);
    CLOCK_SetIpSrc(kCLOCK_Lpuart1, kCLOCK_IpSrcFro192M);

    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = MCXW_UART_BAUD;
    config.enableTx = true;
    config.enableRx = true;
    (void)LPUART_Init(MCXW_UART, &config, CLOCK_GetIpFreq(kCLOCK_Lpuart1));
}
#endif

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            (void)LPUART_WriteBlocking(MCXW_UART, (const uint8_t *)"\r", 1U);
        }
        (void)LPUART_WriteBlocking(MCXW_UART, (const uint8_t *)&buf[i], 1U);
    }
}
#endif /* DEBUG_UART */

#if defined(BOOT_BENCHMARK) && defined(__WOLFBOOT)
/* TSTMR0 counts microseconds and, unlike DWT CYCCNT, runs without a debugger */
uint64_t hal_get_timer_us(void)
{
    static int started;

    if (!started) {
        CLOCK_EnableClock(kCLOCK_Tstmr0);
        started = 1;
    }
    return TSTMR_ReadTimeStamp(TSTMR0);
}
#endif

#ifdef __WOLFBOOT
extern void BOARD_BootClockRUN(void);

void __assert_func(const char *a, int b, const char *c, const char *d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    while (1) {
    }
}

#ifdef TZEN
/* The non-secure application drives the RGB LED but not its clock or mux */
static void periph_unsecure(void)
{
    CLOCK_EnableClock(kCLOCK_GpioA);
    CLOCK_EnableClock(kCLOCK_PortA);
    PORT_SetPinMux(PORTA, MCXW_LED_GREEN_PIN, kPORT_MuxAsGpio);
    PORT_SetPinMux(PORTA, MCXW_LED_BLUE_PIN, kPORT_MuxAsGpio);
    PORT_SetPinMux(PORTA, MCXW_LED_RED_PIN, kPORT_MuxAsGpio);
    GPIOA->PCNS |= MCXW_LED_PINS_MASK;
}
#endif

void hal_prepare_boot(void)
{
#ifdef TZEN
    periph_unsecure();
#endif
}
#endif /* __WOLFBOOT */

void hal_init(void)
{
#ifdef __WOLFBOOT
    BOARD_BootClockRUN();
#endif
#if defined(__WOLFBOOT) && defined(DEBUG_UART)
    uart_init();
#endif
#if defined(TZEN) && !defined(NONSECURE_APP)
    hal_sau_init();
#endif
}

static void RAMFUNCTION mcxw_flash_cache_flush(void)
{
    uint32_t ocmdr = SMSCM->OCMDR0;

    /* Clear the flash cache and drop the speculation buffer, then restore */
    SMSCM->OCMDR0 = (ocmdr & ~(SMSCM_OCMDR0_OCMCF1_MASK |
                               SMSCM_OCMDR0_OCMCF2_MASK)) |
                    SMSCM_OCMDR0_OCMCF1(MCXW_OCMCF1_SPECULATION_OFF) |
                    SMSCM_OCMDR0_OCMCF2(MCXW_OCMCF2_CACHE_CLEAR);
    __DSB();
    SMSCM->OCMDR0 = ocmdr;
    MCM->CPCR2 |= MCM_CPCR2_CCBC_MASK;
    __DSB();
    __ISB();
}

static uint32_t RAMFUNCTION mcxw_fmu_wait(uint32_t mask)
{
    uint32_t fstat;

    do {
        fstat = FMU0->FSTAT;
    } while (((fstat & mask) == 0U) && ((fstat & MCXW_FSTAT_ERR_MASK) == 0U));

    return fstat;
}

/* Runs one phrase-sized FMU command; words == NULL writes zeros (erase).
 * The secure alias keeps the stores secure where the SAU marks BOOT
 * non-secure. */
static int RAMFUNCTION mcxw_fmu_cmd(uint32_t cmd, uint32_t address,
    const uint32_t *words)
{
    volatile uint32_t *dst;
    uint32_t primask;
    uint32_t fstat;
    uint32_t i;

    dst = MCXW_FLASH_PTR(address | MCXW_FLASH_SECURE_ALIAS);
    primask = __get_PRIMASK();
    __disable_irq();

    while ((FMU0->FSTAT & FMU_FSTAT_CCIF_MASK) == 0U) {
    }
    FMU0->FSTAT = MCXW_FSTAT_CLEAR_MASK;
    FMU0->FCCOB[0] = cmd;
    FMU0->FSTAT = FMU_FSTAT_CCIF_MASK;

    fstat = mcxw_fmu_wait(FMU_FSTAT_PEWEN_MASK);
    if ((fstat & MCXW_FSTAT_ERR_MASK) == 0U) {
        for (i = 0; i < MCXW_FLASH_PHRASE_WORDS; i++) {
            dst[i] = (words != NULL) ? words[i] : 0U;
        }
        fstat = mcxw_fmu_wait(FMU_FSTAT_PERDY_MASK);
    }
    if ((fstat & MCXW_FSTAT_ERR_MASK) == 0U) {
        FMU0->FSTAT = FMU_FSTAT_PERDY_MASK;
    }
    do {
        fstat = FMU0->FSTAT;
    } while ((fstat & FMU_FSTAT_CCIF_MASK) == 0U);

    mcxw_flash_cache_flush();
    __set_PRIMASK(primask);

    return ((fstat & MCXW_FSTAT_ERR_MASK) != 0U) ? -1 : 0;
}

/* Accepts the secure (0x1xxxxxxx) alias; PROD_DATA is never written */
static int RAMFUNCTION mcxw_flash_range(uint32_t *address, uint32_t len)
{
    uint32_t addr = *address & ~MCXW_FLASH_SECURE_ALIAS;

    if ((addr >= MCXW_FLASH_PROD_DATA) ||
            (len > MCXW_FLASH_PROD_DATA - addr)) {
        return -1;
    }
    *address = addr;
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t phrase[MCXW_FLASH_PHRASE_WORDS];
    uint8_t *p = (uint8_t *)phrase;
    const volatile uint32_t *cur;
    uint32_t off;
    uint32_t chunk;
    uint32_t i;
    int erased;
    int changed;
    int ret = 0;

    if ((len < 0) || ((len > 0) && (data == NULL))) {
        return -1;
    }
    if (mcxw_flash_range(&address, (uint32_t)len) != 0) {
        return -1;
    }

    while ((len > 0) && (ret == 0)) {
        off = address & (MCXW_FLASH_PHRASE_SIZE - 1U);
        chunk = MCXW_FLASH_PHRASE_SIZE - off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }
        cur = MCXW_FLASH_PTR(address - off);

        erased = 1;
        for (i = 0; i < MCXW_FLASH_PHRASE_WORDS; i++) {
            phrase[i] = cur[i];
            if (phrase[i] != 0xFFFFFFFFU) {
                erased = 0;
            }
        }
        changed = 0;
        for (i = 0; i < chunk; i++) {
            if (p[off + i] != data[i]) {
                p[off + i] = data[i];
                changed = 1;
            }
        }

        /* ECC flash: a phrase is programmed once between erases */
        if (changed && !erased) {
            ret = -1;
        }
        else if (changed) {
            ret = mcxw_fmu_cmd(MCXW_FMU_CMD_PROGRAM_PHRASE, address - off,
                    phrase);
        }
        address += chunk;
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

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end;
    int ret = 0;

    if (len <= 0) {
        return -1;
    }
    if (mcxw_flash_range(&address, (uint32_t)len) != 0) {
        return -1;
    }

    end = address + (uint32_t)len;
    address -= address % MCXW_FLASH_SECTOR_SIZE;
    while ((address < end) && (ret == 0)) {
        ret = mcxw_fmu_cmd(MCXW_FMU_CMD_ERASE_SECTOR, address, NULL);
        address += MCXW_FLASH_SECTOR_SIZE;
    }

    return ret;
}

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
/* The MCX W71 has no TRNG of its own; entropy comes from the S200 enclave */
static sscp_context_t ele_sscp;
static sss_sscp_session_t ele_session;
static sss_sscp_rng_t ele_rng;
static int trng_ready;

void hal_trng_init(void)
{
    int session_open = 0;

    if (trng_ready) {
        return;
    }
    if ((ELEMU_mu_wait_for_ready(ELEMUA, MCXW_ELE_WAIT) == kStatus_Success) &&
            (sscp_mu_init(&ele_sscp, ELEMUA) == kStatus_SSCP_Success) &&
            (sss_sscp_open_session(&ele_session, 0U, kType_SSS_Ele200,
                &ele_sscp) == kStatus_SSS_Success)) {
        session_open = 1;
    }
    if (session_open &&
            (sss_sscp_rng_context_init(&ele_session, &ele_rng,
                MCXW_ELE_RNG_HIGH_QUALITY) == kStatus_SSS_Success)) {
        /* A zero-length request seeds the enclave TRNG */
        if (sss_sscp_rng_get_random(&ele_rng, NULL, 0U) ==
                kStatus_SSS_Success) {
            trng_ready = 1;
        }
        else {
            (void)sss_sscp_rng_free(&ele_rng);
        }
    }
    if (session_open && !trng_ready) {
        (void)sss_sscp_close_session(&ele_session);
    }
}

void hal_trng_fini(void)
{
    if (trng_ready) {
        (void)sss_sscp_rng_free(&ele_rng);
        (void)sss_sscp_close_session(&ele_session);
        trng_ready = 0;
    }
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    if (!trng_ready) {
        return -1;
    }
    return (sss_sscp_rng_get_random(&ele_rng, out, len) ==
            kStatus_SSS_Success) ? 0 : -1;
}
#endif
