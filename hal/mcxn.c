/* mcxn.c
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

#ifdef WOLFCRYPT_SECURE_MODE
/* These functions are stubs for now, because the MCUXpresso SDK doesn't
 * implement drivers for the MCXN's TRNG. */
void hal_trng_init(void)
{
}

void hal_trng_fini(void)
{
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    (void)out;
    (void)len;
    return -1;
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
