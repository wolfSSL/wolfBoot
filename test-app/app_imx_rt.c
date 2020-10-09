/* wolfBoot test application for iMX-RT1060-EVK
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

/* Get debug console frequency. */
static uint32_t rt1060_debug_console_get_freq(void)
{
    uint32_t freq;
    /* To make it simple, we assume default PLL and divider settings, and the only variable
       from application is use PLL3 source or OSC source */
    if (CLOCK_GetMux(kCLOCK_UartMux) == 0) /* PLL3 div6 80M */
    {
        freq = (CLOCK_GetPllFreq(kCLOCK_PllUsb1) / 6U) / (CLOCK_GetDiv(kCLOCK_UartDiv) + 1U);
    }
    else
    {
        freq = CLOCK_GetOscFreq() / (CLOCK_GetDiv(kCLOCK_UartDiv) + 1U);
    }

    return freq;
}
/* Initialize debug console. */
#define UART_TYPE kSerialPort_Uart
#define UART_BASEADDR (uint32_t) LPUART1
#define UART_INSTANCE 1U
#define UART_BAUDRATE (115200U)
void rt1060_init_debug_console(void)
{
    uint32_t uartClkSrcFreq = rt1060_debug_console_get_freq();
    DbgConsole_Init(UART_INSTANCE, UART_BAUDRATE, UART_TYPE, uartClkSrcFreq);
}

/* Pin settings */
void rt1060_init_pins(void) {
  CLOCK_EnableClock(kCLOCK_Iomuxc);           /* iomuxc clock (iomuxc_clk_enable): 0x03U */

  gpio_pin_config_t USER_LED_config = {
      .direction = kGPIO_DigitalOutput,
      .outputLogic = 0U,
      .interruptMode = kGPIO_NoIntmode
  };
  GPIO_PinInit(GPIO1, 9U, &USER_LED_config);

  IOMUXC_SetPinMux(
      IOMUXC_GPIO_AD_B0_10_ARM_TRACE_SWO,     /* GPIO_AD_B0_10 is configured as ARM_TRACE_SWO */
      0U);
  IOMUXC_SetPinMux(
      IOMUXC_GPIO_AD_B0_12_LPUART1_TX,        /* GPIO_AD_B0_12 is configured as LPUART1_TX */
      0U);
  IOMUXC_SetPinMux(
      IOMUXC_GPIO_AD_B0_13_LPUART1_RX,        /* GPIO_AD_B0_13 is configured as LPUART1_RX */
      0U);
  IOMUXC_SetPinConfig(
      IOMUXC_GPIO_AD_B0_10_ARM_TRACE_SWO,     /* GPIO_AD_B0_10 PAD functional properties : */
      0x90B1U);
  IOMUXC_SetPinConfig(
      IOMUXC_GPIO_AD_B0_12_LPUART1_TX,        /* GPIO_AD_B0_12 PAD functional properties : */
      0x10B0U);
  IOMUXC_SetPinConfig(
      IOMUXC_GPIO_AD_B0_13_LPUART1_RX,        /* GPIO_AD_B0_13 PAD functional properties : */
      0x10B0U);
}


void rt1060_init_boot_clock(void);

void main()
{
    rt1060_init_pins();
    rt1060_init_boot_clock();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    rt1060_init_debug_console();
    PRINTF("wolfBoot Test app, version = %d\n", wolfBoot_current_firmware_version());
    while(1) {
        SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
        if (g_pinSet)
        {
            GPIO_PinWrite(GPIO1, 9U, 0U);
            g_pinSet = false;
        }
        else
        {
            GPIO_PinWrite(GPIO1, 9U, 1U);
            g_pinSet = true;
        }
    }
}

