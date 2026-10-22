/* app_lpc55s69.c
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
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iocon.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "hal/lpc55s69.h"

/* wolfCrypt test/benchmark support */
#ifdef WOLFCRYPT_TEST
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif

#ifdef WOLFCRYPT_BENCHMARK
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfboot/wc_secure.h"
#endif

#ifdef WOLFCRYPT_TZ_PSA
#include "psa/crypto.h"
#include "psa/error.h"
#include "psa/initial_attestation.h"
#endif

#ifdef WOLFBOOT_PKCS11_TESTAPP
#include "test_pkcs11.h"
#endif

#define RED_LED     LPC55S69_LED_RED_PIN
#define GREEN_LED   LPC55S69_LED_GREEN_PIN
#define BLUE_LED    LPC55S69_LED_BLUE_PIN

extern void hal_init(void);


volatile uint64_t SysTick_time_ms;

void SysTick_Handler(void)
{
    SysTick_time_ms++;
}

#ifdef WOLFCRYPT_BENCHMARK
static void systick_init(void)
{
    SysTick_Config(CLOCK_GetCoreSysClkFreq() / 1000U); /* 1 ms period */
}
#endif

static void leds_init(void)
{
    const uint32_t led_mux = IOCON_FUNC0 | IOCON_MODE_PULLUP | IOCON_DIGITAL_EN;

#ifndef TZEN
    /* With TZEN the secure side owns the pin mux and sets it before boot */
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, RED_LED, led_mux);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, GREEN_LED, led_mux);
    IOCON_PinMuxSet(IOCON, LPC55S69_LED_PORT, BLUE_LED, led_mux);
#else
    (void)led_mux;
#endif

    /* All three off (active low), then outputs */
    GPIO->SET[LPC55S69_LED_PORT] = (1UL << RED_LED) | (1UL << GREEN_LED) |
        (1UL << BLUE_LED);
    GPIO->DIR[LPC55S69_LED_PORT] |= (1UL << RED_LED) | (1UL << GREEN_LED) |
        (1UL << BLUE_LED);
}

#ifdef WOLFCRYPT_SECURE_MODE
static void print_random_number(void)
{
    uint8_t rnd;
    int ret;

    ret = wcs_get_random(&rnd, sizeof(rnd));
    if (ret != 0) {
        wolfBoot_printf("Random number: generate failed (%d)\n", ret);
    }
    else {
        wolfBoot_printf("Today's lucky number: 0x%02x\n", rnd);
    }
}
#endif

static void check_parts(
    uint32_t *pboot_ver, uint32_t *pupdate_ver,
    uint8_t *pboot_state, uint8_t *pupdate_state
)
{
#ifdef TZEN
    *pboot_ver = wolfBoot_nsc_current_firmware_version();
    *pupdate_ver = wolfBoot_nsc_update_firmware_version();
    if (wolfBoot_nsc_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_nsc_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#else
    *pboot_ver = wolfBoot_current_firmware_version();
    *pupdate_ver = wolfBoot_update_firmware_version();
    if (wolfBoot_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#endif

    wolfBoot_printf("    boot:   ver=0x%lx state=0x%02x\n", *pboot_ver, *pboot_state);
    wolfBoot_printf("    update: ver=0x%lx state=0x%02x\n", *pupdate_ver, *pupdate_state);
}

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)

#define LINE_LEN 16
static void print_hex(const uint8_t *buffer, uint32_t length, int dumpChars)
{
    uint32_t i, sz;

    if (!buffer) {
        wolfBoot_printf("\tNULL\n");
        return;
    }

    while (length > 0) {
        sz = length;
        if (sz > LINE_LEN)
            sz = LINE_LEN;

        wolfBoot_printf("\t");
        for (i = 0; i < LINE_LEN; i++) {
            if (i < sz)
                wolfBoot_printf("%02x ", buffer[i]);
            else
                wolfBoot_printf("   ");
        }
        if (dumpChars) {
            wolfBoot_printf("| ");
            for (i = 0; i < sz; i++) {
                if (buffer[i] > 31 && buffer[i] < 127)
                    wolfBoot_printf("%c", buffer[i]);
                else
                    wolfBoot_printf(".");
            }
        }
        wolfBoot_printf("\n");

        buffer += sz;
        length -= sz;
    }
}

static int run_attestation_test(void)
{
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64];
    uint8_t token[1024];
    size_t token_size = 0;
    psa_status_t status;
    size_t i;

    for (i = 0; i < sizeof(challenge); i++) {
        challenge[i] = (uint8_t)i;
    }

    status = psa_initial_attest_get_token(challenge, sizeof(challenge),
                                          token, sizeof(token), &token_size);
    if (status != PSA_SUCCESS) {
        wolfBoot_printf("attest: get token failed (%d)\n", status);
        return -1;
    }
    wolfBoot_printf("attest: token size %lu bytes\n", (unsigned long)token_size);
    print_hex(token, (uint32_t)token_size, 1);
    return 0;
}
#endif /* WOLFBOOT_ATTESTATION_TEST && WOLFCRYPT_TZ_PSA */

void main(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state, update_state;
#ifdef WOLFBOOT_PKCS11_TESTAPP
    int ret;
#endif

    hal_init();
    leds_init();
#ifdef WOLFCRYPT_BENCHMARK
    systick_init();
    __enable_irq();
#endif

    wolfBoot_printf("\n==================================\n");
    wolfBoot_printf("LPC55S69 wolfBoot demo Application\n");
    wolfBoot_printf("Copyright 2026 wolfSSL Inc\n");
    wolfBoot_printf("==================================\n");

#ifdef WOLFCRYPT_SECURE_MODE
    print_random_number();
#endif
    check_parts(&boot_ver, &update_ver, &boot_state, &update_state);

    if (
        boot_ver != 0 &&
        (boot_state == IMG_STATE_TESTING || boot_state == IMG_STATE_NEW)
    )
    {
#ifdef TZEN
        wolfBoot_printf("Calling wolfBoot_nsc_success()\n");
        wolfBoot_nsc_success();
#else
        wolfBoot_printf("Calling wolfBoot_success()\n");
        wolfBoot_success();
#endif
        check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
    }

    if (boot_ver == 1)
    {
        /* blue on */
        GPIO_PinWrite(GPIO, LPC55S69_LED_PORT, BLUE_LED, 0);

        if (update_ver != 0) {
            wolfBoot_printf("Update detected, version: 0x%lx\n", update_ver);
            wolfBoot_printf("Triggering update...\n");
#ifdef TZEN
            wolfBoot_nsc_update_trigger();
#else
            wolfBoot_update_trigger();
#endif
            check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
            wolfBoot_printf("...done. Reboot to apply.\n");
        }
    }
    else {
        /* green on */
        GPIO_PinWrite(GPIO, LPC55S69_LED_PORT, GREEN_LED, 0);
    }

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)
    if (run_attestation_test() == 0) {
        __asm__ volatile ("bkpt #0x7f");
    }
    else {
        wolfBoot_printf("attest: failed (with HWPUF=1, the PUF must have been "
                        "provisioned: WOLFBOOT_HWPUF_PROVISION=1)\n");
        __asm__ volatile ("bkpt #0x7e");
    }
#endif

#ifdef WOLFBOOT_PKCS11_TESTAPP
    ret = test_pkcs11_start();
    if (ret == PKCS11_TEST_FIRST_BOOT_OK) {
        __asm__ volatile ("bkpt #0x7d");
    }
    else if (ret == PKCS11_TEST_SECOND_BOOT_OK) {
        __asm__ volatile ("bkpt #0x7f");
    }
    else {
        __asm__ volatile ("bkpt #0x7e");
    }
#endif

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    wolfCrypt_Init();

# ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\nRunning wolfCrypt tests...\n");
    wolfcrypt_test(NULL);
    wolfBoot_printf("Tests complete.\n\n");
# endif

# ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("\nRunning wolfCrypt benchmarks...\n");
    benchmark_test(NULL);
    wolfBoot_printf("Benchmarks complete.\n\n");
# endif

    wolfCrypt_Cleanup();
#endif

    while (1) {
        __asm__ volatile ("wfi");
    }
}

#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef EBADF
#define EBADF  9
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
int errno;

#include "sys/stat.h"
int WEAKFUNCTION _getpid(void)
{
    return 1;
}

int WEAKFUNCTION _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

void WEAKFUNCTION _exit(int status)
{
    _kill(status, -1);
    while (1) {}
}

int WEAKFUNCTION _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return -1;
}

int WEAKFUNCTION _write(int file, char *ptr, int len)
{
    (void)file;
#ifdef DEBUG_UART
    uart_write(ptr, (unsigned int)len);
#else
    (void)ptr;
#endif
    return len;
}

int WEAKFUNCTION _close(int file)
{
    (void)file;
    return -1;
}

int WEAKFUNCTION _isatty(int file)
{
    (void)file;
    return 1;
}

int WEAKFUNCTION _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int WEAKFUNCTION _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/* Heap management */
extern char _end; /* Defined by linker */
extern char _Min_Heap_Size; /* Linker symbol: address is the value */

void WEAKFUNCTION *_sbrk(int incr)
{
    static char *heap_end = 0;
    char *prev_heap_end;
    char *heap_limit;

    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;

    heap_limit = &_end + (uintptr_t)&_Min_Heap_Size;

    if (heap_end + incr > heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end += incr;
    return prev_heap_end;
}
