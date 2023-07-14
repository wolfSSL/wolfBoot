/* stm32l5.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system.h"
#include "hal.h"
#include "uart_drv.h"
#include "wolfboot/wolfboot.h"
#include "wolfboot/wc_secure.h"

#ifdef SECURE_PKCS11
#include "wcs/user_settings.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_pkcs11.h>
#include <wolfssl/wolfcrypt/random.h>
extern const char pkcs11_library_name[];
extern const CK_FUNCTION_LIST wolfpkcs11nsFunctionList;
#endif

#define LED_BOOT_PIN (9)  /* PA9 - Nucleo - Red Led */
#define LED_USR_PIN (7)   /* PB7  - Nucleo  - Green Led */
#define LED_EXTRA_PIN (7) /* PC7  - Nucleo  - Blue Led */

/*Non-Secure */
#define RCC_BASE            (0x40021000)
#define PWR_BASE            (0x40007000)
#define GPIOA_BASE          0x42020000
#define GPIOB_BASE          0x42020400


#define GPIOA_MODER  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR  (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

#define GPIOB_MODER  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPDR  (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))

#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x4C ))
#define GPIOA_AHB2_CLOCK_ER (1 << 0)
#define GPIOB_AHB2_CLOCK_ER (1 << 2)

#define PWR_CR2              (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CR2_IOSV         (1 << 9)

static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;

    RCC_AHB2_CLOCK_ER|= GPIOA_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;
    PWR_CR2 |= PWR_CR2_IOSV;

    reg = GPIOA_MODER & ~(0x03 << (pin * 2));
    GPIOA_MODER = reg | (1 << (pin * 2));
    GPIOA_PUPDR &= ~(0x03 << (pin * 2));
    GPIOA_BSRR |= (1 << (pin + 16));
}

static void boot_led_off(void)
{
    GPIOA_BSRR |= (1 << (LED_BOOT_PIN));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;

    RCC_AHB2_CLOCK_ER|= GPIOB_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;

    reg = GPIOB_MODER & ~(0x03 << (pin * 2));
    GPIOB_MODER = reg | (1 << (pin * 2));
    GPIOB_PUPDR &= ~(0x03 << (pin * 2));
    GPIOB_BSRR |= (1 << (pin + 16));
}

void usr_led_off(void)
{
    GPIOB_BSRR |= (1 << (LED_USR_PIN));
}

static char CaBuf[2048];
static uint8_t my_pubkey[200];

extern int ecdsa_sign_verify(int devId);


void main(void)
{
    int ret;
    uint32_t rand;
    uint32_t i;
    uint32_t klen = 200;
    int otherkey_slot;
    unsigned int devId = 0;
    WC_RNG rng;
    Pkcs11Token token;
    Pkcs11Dev PKCS11_d;

    wcs_get_random((void*)&rand, 4);
    for (i = 0; i < (rand / 100000000); i++)
        ;
    hal_init();
    uart_init(115200, 8, 'N', 1);
    boot_led_on();
    usr_led_on();
    boot_led_off();
    if (wolfBoot_current_firmware_version() > 1)
        boot_led_on();

    wolfCrypt_Init();
    PKCS11_d.heap = NULL,
    PKCS11_d.func = &wolfpkcs11nsFunctionList;

    ret = wc_Pkcs11Token_Init(&token, &PKCS11_d, 0, NULL,
            NULL, 0);
    if (ret != 0) {
        while(1)
            ;
    }
    if (ret == 0) {
        ret = wc_CryptoDev_RegisterDevice(devId, wc_Pkcs11_CryptoDevCb,
                &token);
        if (ret != 0) {
            while(1)
                ;
        }
        if (ret == 0) {

#ifdef HAVE_ECC
            ret = ecdsa_sign_verify(devId);
            if (ret != 0)
                ret = 1;
#endif
        }
        wc_Pkcs11Token_Final(&token);
    }
    while(1)
        ;

    wolfCrypt_Cleanup();
    /* Never reached */
}
