/* app_mcxn.c
 *
 * Minimal test application scaffold for NXP MCXN targets.
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
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

extern void hal_init(void);

static void gpio_init_output(GPIO_Type *gpio, PORT_Type *port,
                             clock_ip_name_t gpio_clock,
                             clock_ip_name_t port_clock, uint32_t pin,
                             uint8_t initial_level)
{
    const port_pin_config_t pin_config = {
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
        .driveStrength1 = kPORT_LowDriveStrength,
#endif
        .mux = kPORT_MuxAlt0,
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
    const gpio_pin_config_t gpio_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = initial_level,
    };

    CLOCK_EnableClock(gpio_clock);
    CLOCK_EnableClock(port_clock);
    GPIO_PinInit(gpio, pin, &gpio_config);
    PORT_SetPinConfig(port, pin, &pin_config);
}

void main(void)
{
    uint32_t boot_ver;

    hal_init();

#ifdef WOLFCRYPT_SECURE_MODE
    boot_ver = wolfBoot_nsc_current_firmware_version();
#else
    boot_ver = wolfBoot_current_firmware_version();
#endif

    wolfBoot_printf("Hello from firmware version %d\n", boot_ver);

    if (boot_ver == 1) {
        /* Red off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 10U, 1U);
        /* Green off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 27U, 1U);
        /* Blue on */
        gpio_init_output(GPIO1, PORT1, kCLOCK_Gpio1, kCLOCK_Port1, 2U, 0U);
    }
    else {
        /* Red off */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 10U, 1U);
        /* Green on */
        gpio_init_output(GPIO0, PORT0, kCLOCK_Gpio0, kCLOCK_Port0, 27U, 0U);
        /* Blue off */
        gpio_init_output(GPIO1, PORT1, kCLOCK_Gpio1, kCLOCK_Port1, 2U, 1U);

#ifdef WOLFCRYPT_SECURE_MODE
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
    }

    while (1) {
        __asm__ volatile ("wfi");
    }
}
