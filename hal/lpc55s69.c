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
#include "fsl_iocon.h"
#include "fsl_reset.h"
#include "fsl_rng.h"
#include "fsl_usart.h"
#include "loader.h"

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

static flash_config_t pflash;
static const uint32_t pflash_page_size = 512U;
uint32_t SystemCoreClock; /* set in clock_config.c */

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
    sau_init_region(2, 0x20020000, 0x20027FFF, 0);

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

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* lpc55s69 must run < 100 MHz for flash write/erase to work */
    BOARD_BootClockFROHF96M();
//    BOARD_BootClockPLL150M();
#ifdef DEBUG_UART
    uart_init();
    uart_write("lpc55s69 init\n", 14);
#endif
#endif

#if defined(__WOLFBOOT) || !defined(TZEN)
    memset(&pflash, 0, sizeof(pflash));
    FLASH_Init(&pflash);
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
    if (
        address % pflash_page_size == 0 &&
        len % pflash_page_size == 0 &&
        FLASH_Program(&pflash, address, data, len) == kStatus_FLASH_Success
    )
    {
        return 0;
    }

    return -1;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    if (
        address % pflash_page_size == 0 &&
        len % pflash_page_size == 0 &&
        FLASH_Erase(&pflash, address, len, kFLASH_ApiEraseKey)
            == kStatus_FLASH_Success
    )
    {
        return 0;
    }

    return -1;
}

#ifdef NO_DIRECT_READ_OF_ERASED_SECTOR
int RAMFUNCTION hal_flash_is_erased_at(uint32_t address)
{
    address &= ~(WOLFBOOT_SECTOR_SIZE - 1);
    return FLASH_VerifyErase(&pflash, address, WOLFBOOT_SECTOR_SIZE) == kStatus_FLASH_Success;
}
#endif

#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void)
{
#ifdef __WOLFBOOT
    CLOCK_EnableClock(kCLOCK_Rng);
    RESET_PeripheralReset(kRNG_RST_SHIFT_RSTn);
#endif
    RNG_Init(RNG);
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