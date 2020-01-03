/* kinetis.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fsl_common.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"
#include "wolfboot/wolfboot.h"

/* FRDM-K64 board */
#if defined(CPU_MK64FN1M0VLL12)
#define BOARD_LED_GPIO GPIOB
#define BOARD_LED_GPIO_PORT PORTB
#define BOARD_LED_GPIO_CLOCK kCLOCK_PortB
#define BOARD_LED_GPIO_PIN 23U
/* FRDM-K82 board */
#elif defined (CPU_MK82FN256VLL15)
#define BOARD_LED_GPIO_PORT PORTC
#define BOARD_LED_GPIO_CLOCK kCLOCK_PortC
#define BOARD_LED_GPIO GPIOC
#define BOARD_LED_GPIO_PIN 8U
#endif

#ifdef TEST_APP_STANDALONE
/* This are the registers for the NV flash configuration area.
 * Access these field by setting the relative flags in NV_Flash_Config.
 */
#define NVTYPE_LEN (16)

const uint8_t __attribute__((section(".flash_config"))) NV_Flash_Config[NVTYPE_LEN] = {
    /* Backdoor comparison key (2 words) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* P-Flash protection 1 */
    0xFF, 0xFF,
    /* P-Flash protection 2 */
    0xFF, 0xFF,

    /* Flash security register */
    ((0xFE)),
    /* Flash option register */
    0xFF,
    /* EERAM protection register */
    0xFF,
    /* D-Flash protection register */
    0xFF
};

#if defined(CPU_MK82FN256VLL15)
struct stage1_config
{
    uint32_t tag;
    uint32_t crcStartAddress;
    uint32_t crcByteCount;
    uint32_t crcExpectedValue;
    uint8_t  enabledPeripherals;
    uint8_t  i2cSlaveAddress;
    uint16_t peripheralDetectionTimeoutMs;
    uint16_t usbVid;
    uint16_t usbPid;
    uint32_t usbStringsPointer;
    uint8_t  clockFlags;
    uint8_t  clockDivider;
    uint8_t  bootFlags;
    uint8_t  RESERVED1;
    uint32_t mmcauConfigPointer;
    uint32_t keyBlobPointer;
    uint8_t  RESERVED2[8];
    uint32_t qspiConfigBlockPtr;
    uint8_t  RESERVED3[12];
};

const struct stage1_config __attribute__((section(".stage1_config")))
 NV_Stage1_Config = {
    .tag = 0x6766636BU,                      /* Magic Number */
    .crcStartAddress = 0xFFFFFFFFU,          /* Disable CRC check */
    .crcByteCount = 0xFFFFFFFFU,             /* Disable CRC check */
    .crcExpectedValue = 0xFFFFFFFFU,         /* Disable CRC check */
    .enabledPeripherals = 0x17,              /* Enable all peripherals */
    .i2cSlaveAddress = 0xFF,                 /* Use default I2C address */
    .peripheralDetectionTimeoutMs = 0x01F4U, /* Use default timeout */
    .usbVid = 0xFFFFU,                       /* Use default USB Vendor ID */
    .usbPid = 0xFFFFU,                       /* Use default USB Product ID */
    .usbStringsPointer = 0xFFFFFFFFU,        /* Use default USB Strings */
    .clockFlags = 0x01,                      /* Enable High speed mode */
    .clockDivider = 0xFF,                    /* Use clock divider 1 */
    .bootFlags = 0x01,                       /* Enable communication with host */
    .mmcauConfigPointer = 0xFFFFFFFFU,       /* No MMCAU configuration */
    .keyBlobPointer = 0x000001000,           /* keyblob data is at 0x1000 */
    .qspiConfigBlockPtr = 0xFFFFFFFFU        /* No QSPI configuration */
};
#endif
#endif



void main(void) {
    int i = 0;
#ifdef CPU_MK64FN1M0VLL12
    /* Immediately disable Watchdog after boot */
    /*  Write Keys to unlock register */
    *((volatile unsigned short *)0x4005200E) = 0xC520;
    *((volatile unsigned short *)0x4005200E) = 0xD928;
    /* disable watchdog via STCTRLH register */
    *((volatile unsigned short *)0x40052000) = 0x01D2u;
#endif
    gpio_pin_config_t led_config = {
        kGPIO_DigitalOutput, 0,
    };

    CLOCK_EnableClock(BOARD_LED_GPIO_CLOCK);
    PORT_SetPinMux(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, kPORT_MuxAsGpio);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, led_config.outputLogic);
    BOARD_LED_GPIO->PDDR |= (1U << BOARD_LED_GPIO_PIN);
    GPIO_PortClear(BOARD_LED_GPIO, 1u << BOARD_LED_GPIO_PIN);

#if 0
    while(1) {
        for(i = 0; i < 7200000; i++) {

        }

        GPIO_PortToggle(BOARD_LED_GPIO, 1 << BOARD_LED_GPIO_PIN);
    }
#endif

    while(1)
        __WFI();
}
