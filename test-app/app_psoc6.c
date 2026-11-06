/* app_psoc6.c
 *
 * Test bare-metal application for the Infineon PSoC 6 (CM0+ or CM4)
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

#include "cy_device_headers.h"
#include "cy_device.h"
#include "cy_flash.h"
#include "cy_gpio.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "hal/psoc6.h"

#ifdef PSOC6_CM4_APP
#define APP_CORE "CM4"
#else
#define APP_CORE "CM0+"
#endif

void main(void)
{
    uint32_t boot_ver;

    Cy_PDL_Init(CY_DEVICE_CFG);
    Cy_Flash_Init();
    Cy_GPIO_Pin_FastInit(PSOC6_LED_PORT, PSOC6_LED_PIN,
        CY_GPIO_DM_STRONG_IN_OFF, PSOC6_LED_OFF, HSIOM_SEL_GPIO);

    boot_ver = wolfBoot_current_firmware_version();
    wolfBoot_printf("Hello from PSoC6 " APP_CORE " firmware version %d\n",
        boot_ver);

    if (boot_ver != 1U) {
        wolfBoot_success();
        wolfBoot_printf("Update successful, firmware version %d confirmed\n",
            boot_ver);
        Cy_GPIO_Write(PSOC6_LED_PORT, PSOC6_LED_PIN, PSOC6_LED_ON);
    }

    while (1) {
        __WFI();
    }
}
