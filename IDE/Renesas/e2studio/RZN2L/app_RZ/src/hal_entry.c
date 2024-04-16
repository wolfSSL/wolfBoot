/* hal_entry.c
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
 *
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include "hal_data.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event)
BSP_PLACE_IN_SECTION(".warm_start");
FSP_CPP_FOOTER

bsp_io_level_t pin_level = BSP_IO_LEVEL_LOW;
uint16_t count = 0;
uint32_t firmware_version;
uint8_t succeed_issued = 0;
const uint8_t MAX_LED = 10;

extern bsp_leds_t g_bsp_leds;
/* Set the blink frequency (must be <= bsp_delay_units */
uint32_t freq_in_hz = 5;

void hal_init(void);

static inline void panic_stop(void)
{
    while(1)
        ;
}

/*******************************************************************************
 * @brief  Blinky example application
 *
 * Blinks all leds at a rate of 1 second using the software delay function
 * provided by the BSP.
 ******************************************************************************/
void hal_entry (void)
{
    int active;
    hal_init();

    /* Define the units to be used with the software delay function */
    const bsp_delay_units_t bsp_delay_units = BSP_DELAY_UNITS_MILLISECONDS;

    /* Calculate the delay in terms of bsp_delay_units */
    const uint32_t delay = bsp_delay_units / freq_in_hz;

    __asm volatile ("cpsie i");

    //printf("Hello World\n");
    /* If this board has no LEDs then trap here */
    if (0 == g_bsp_leds.led_count)
    {
        panic_stop();
    }

    /* get firmware version */
    /* The same as: wolfBoot_get_image_version(PART_BOOT); */
    active = wolfBoot_dualboot_candidate();
    if (active < 0) { /* panic if no images available */
        wolfBoot_printf("No valid image found!\n");
        panic_stop();
    }
    firmware_version = wolfBoot_get_image_version((uint8_t)active);

    /* GIC settings for CPUINT0. */
    R_BSP_IrqCfgEnable(VECTOR_NUMBER_INTCPU0, 1, NULL);


    /* Enable interrupt. */
    __asm volatile ("cpsie i");

    while (1)
    {
        /* Generate INTCPU0. */
        R_ICU_NS->NS_SWINT = 0x00000001U;

        /* Delay */
        R_BSP_SoftwareDelay(delay, bsp_delay_units);
    }
}

/******************************************************************************
 * This function is called at various points during the startup process.
 * This implementation uses the event
 * that is called right before main() to set up the pins.
 * @param[in]  event  Where at in the start up process the code is currently at
 ******************************************************************************/
void R_BSP_WarmStart(bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
        /* Pre clock initialization */
    }

    if (BSP_WARM_START_POST_C == event)
    {
        /* C runtime environment and system clocks are setup. */

        /* Configure pins. */
        R_IOPORT_Open (&g_ioport_ctrl, &g_bsp_pin_cfg);
    }
}

#define LED_ALL_OFF 255

static void LED_ON(uint16_t led)
{
    bsp_leds_t leds = g_bsp_leds;
    uint32_t pin = leds.p_leds[led];

    /* This code uses BSP IO functions to show how it is used.*/
    R_BSP_PinAccessEnable();

    switch(led) {
        case BSP_LED_RLED0:
        case BSP_LED_RLED1:
            R_BSP_PinWrite((bsp_io_port_pin_t) pin, pin_level);
            break;
        case BSP_LED_RLED2:
        case BSP_LED_RLED3:
            /* Update board LEDs */
            for (uint32_t i = 2; i < leds.led_count; i++)
            {
                /* Get pin to toggle */
                pin = leds.p_leds[i];

                /* Write to this pin */
                R_BSP_PinWrite((bsp_io_port_pin_t) pin, pin_level);
            }
         break;
        case LED_ALL_OFF:
            for (uint32_t i = 0; i < leds.led_count; i++) {
                /* Get pin to toggle */
                pin = leds.p_leds[i];

                /* Write to this pin */
                R_BSP_PinWrite((bsp_io_port_pin_t) pin, BSP_IO_LEVEL_HIGH);
            }
            break;
        default:break;
    }

    /* Protect PFS registers */
    R_BSP_PinAccessDisable();
}

void intcpu0_handler(void)
{
    if (firmware_version == 2) {
        /* YELLO LED */
        LED_ON(BSP_LED_RLED0);
        freq_in_hz = 10;
    } else {
        /* RED LED */
        LED_ON(BSP_LED_RLED2);
        freq_in_hz = 1;
    }
    /* Toggle level for next write */
    if (BSP_IO_LEVEL_LOW == pin_level)
    {
        pin_level = BSP_IO_LEVEL_HIGH;
    }
    else
    {
        pin_level = BSP_IO_LEVEL_LOW;
    }

    if (count > MAX_LED && !succeed_issued) {
        if (firmware_version >= 1 && !succeed_issued) {
            wolfBoot_success();
            succeed_issued = 1;
        } else {
            /* unknown version      */
            /* fastest LED blinking  */
            freq_in_hz = 1;
            count = 0;
        }
    }

    count++;
}
