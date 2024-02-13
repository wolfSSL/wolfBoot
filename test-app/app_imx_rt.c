/* wolfBoot test application for iMX-RT1060-EVK
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include "wolfboot/wolfboot.h"
#include <stdint.h>
#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"

static int g_pinSet = false;
extern void imx_rt_init_boot_clock(void);

#ifndef USER_LED_GPIO
#define USER_LED_GPIO GPIO1
#endif

#ifndef USER_LED_PIN
#define USER_LED_PIN  9U
#endif

/* Get debug console frequency. */
static uint32_t debug_console_get_freq(void)
{
    uint32_t freq;

    /* To make it simple, we assume default PLL and divider settings, and the
     * only variable from application is use PLL3 source or OSC source */
    if (CLOCK_GetMux(kCLOCK_UartMux) == 0) { /* PLL3 div6 80M */
        freq = (CLOCK_GetPllFreq(kCLOCK_PllUsb1) / 6U) /
               (CLOCK_GetDiv(kCLOCK_UartDiv) + 1U);
    }
    else {
        freq = CLOCK_GetOscFreq() / (CLOCK_GetDiv(kCLOCK_UartDiv) + 1U);
    }

    return freq;
}
/* Initialize debug console. */
#define UART_TYPE     kSerialPort_Uart
#define UART_BASEADDR LPUART1_BASE
#define UART_INSTANCE 1U
#define UART_BAUDRATE (115200U)
void init_debug_console(void)
{
    uint32_t uartClkSrcFreq = debug_console_get_freq();
    DbgConsole_Init(UART_INSTANCE, UART_BAUDRATE, UART_TYPE, uartClkSrcFreq);
}

#if defined(CPU_MIMXRT1062DVL6A) || defined(CPU_MIMXRT1064DVL6A)
/* Pin settings (same for both 1062 and 1064) */
void rt1060_init_pins(void)
{
    gpio_pin_config_t USER_LED_config = {
        .direction = kGPIO_DigitalOutput,
        .outputLogic = 0U,
        .interruptMode = kGPIO_NoIntmode
    };

    CLOCK_EnableClock(kCLOCK_Iomuxc); /* iomuxc clock (iomuxc_clk_enable): 0x03U */

    GPIO_PinInit(USER_LED_GPIO, USER_LED_PIN, &USER_LED_config);

    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);
    IOMUXC_SetPinMux( /* GPIO_AD_B0_10 is configured as ARM_TRACE_SWO */
        IOMUXC_GPIO_AD_B0_10_ARM_TRACE_SWO, 0U);
    IOMUXC_SetPinMux( /* GPIO_AD_B0_12 is configured as LPUART1_TX */
        IOMUXC_GPIO_AD_B0_12_LPUART1_TX,    0U);
    IOMUXC_SetPinMux( /* GPIO_AD_B0_13 is configured as LPUART1_RX */
        IOMUXC_GPIO_AD_B0_13_LPUART1_RX,    0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0x10B0U);
    IOMUXC_SetPinConfig( /* GPIO_AD_B0_10 PAD functional properties : */
        IOMUXC_GPIO_AD_B0_10_ARM_TRACE_SWO, 0x90B1U);
    IOMUXC_SetPinConfig( /* GPIO_AD_B0_12 PAD functional properties : */
        IOMUXC_GPIO_AD_B0_12_LPUART1_TX,    0x10B0U);
    IOMUXC_SetPinConfig( /* GPIO_AD_B0_13 PAD functional properties : */
        IOMUXC_GPIO_AD_B0_13_LPUART1_RX,    0x10B0U);
}
#endif

#ifdef CPU_MIMXRT1052DVJ6B
void rt1050_init_pins(void)
{
    gpio_pin_config_t USER_LED_config = {
        .direction = kGPIO_DigitalOutput,
        .outputLogic = 0U,
        .interruptMode = kGPIO_NoIntmode
    };

    CLOCK_EnableClock(kCLOCK_Iomuxc);

    GPIO_PinInit(USER_LED_GPIO, USER_LED_PIN, &USER_LED_config);

    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_12_LPUART1_TXD, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_13_LPUART1_RXD, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_CCM_CLKO1, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_05_CCM_CLKO2, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_12_LPUART1_TXD, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_13_LPUART1_RXD, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_CCM_CLKO1, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_05_CCM_CLKO2, 0x10B0U);
}
#endif

#ifdef CPU_MIMXRT1042XJM5B
void rt1040_init_pins(void)
{
    gpio_pin_config_t USER_LED_config = {
        .direction = kGPIO_DigitalOutput,
        .outputLogic = 0U,
        .interruptMode = kGPIO_NoIntmode
    };

    CLOCK_EnableClock(kCLOCK_Iomuxc);

    GPIO_PinInit(USER_LED_GPIO, USER_LED_PIN, &USER_LED_config);

    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_12_LPUART1_TX, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_13_LPUART1_RX, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_CCM_CLKO1, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_05_CCM_CLKO2, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_12_LPUART1_TX, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_13_LPUART1_RX, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_CCM_CLKO1, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_05_CCM_CLKO2, 0x10B0U);
}
#endif


void main(void)
{
    imx_rt_init_boot_clock();
#if defined(CPU_MIMXRT1062DVL6A) || defined(CPU_MIMXRT1064DVL6A)
    rt1060_init_pins();
#elif defined(CPU_MIMXRT1052DVJ6B)
    rt1050_init_pins();
#elif defined(CPU_MIMXRT1042XJM5B)
    rt1040_init_pins();
#endif
    SystemCoreClockUpdate();
    init_debug_console();

    PRINTF("wolfBoot Test app, version = %d\r\n",
        wolfBoot_current_firmware_version());

    if (wolfBoot_current_firmware_version() == 1) {
        wolfBoot_update_trigger();
    } else {
        wolfBoot_success();
    }

    while (1) {
        /* 100ms delay */
        SDK_DelayAtLeastUs(100 * 1000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

        /* toggle user LED */
        if (g_pinSet) {
            GPIO_PinWrite(USER_LED_GPIO, USER_LED_PIN, 0U);
            g_pinSet = false;
        }
        else {
            GPIO_PinWrite(USER_LED_GPIO, USER_LED_PIN, 1U);
            g_pinSet = true;
        }
    }
}
