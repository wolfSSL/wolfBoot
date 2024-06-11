/* app_stm32h5.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <sys/stat.h>
#include "system.h"
#include "hal.h"
#include "uart_drv.h"
#include "wolfboot/wolfboot.h"
#include "wolfcrypt/benchmark/benchmark.h"
#include "wolfcrypt/test/test.h"
#include "keystore.h"

#ifdef SECURE_PKCS11
#include "wcs/user_settings.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_pkcs11.h>
#include <wolfssl/wolfcrypt/random.h>
extern const char pkcs11_library_name[];
extern const CK_FUNCTION_LIST wolfpkcs11nsFunctionList;
#endif

volatile unsigned int jiffies = 0;

#define LED_BOOT_PIN (4) /* PG4 - Nucleo - Red Led */
#define LED_USR_PIN  (0) /* PB0 - Nucleo - Green Led */
#define LED_EXTRA_PIN  (4) /* PF4 - Nucleo - Orange Led */

/*Non-Secure */
#define RCC_BASE            (0x44020C00)   /* RM0481 - Table 3 */
#define GPIOG_BASE 0x42021800
#define GPIOB_BASE 0x42020400
#define GPIOF_BASE 0x42021400

#define GPIOG_MODER (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_PUPDR (*(volatile uint32_t *)(GPIOG_BASE + 0x0C))
#define GPIOG_BSRR  (*(volatile uint32_t *)(GPIOG_BASE + 0x18))

#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPDR (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))

#define GPIOF_MODER (*(volatile uint32_t *)(GPIOF_BASE + 0x00))
#define GPIOF_PUPDR (*(volatile uint32_t *)(GPIOF_BASE + 0x0C))
#define GPIOF_BSRR  (*(volatile uint32_t *)(GPIOF_BASE + 0x18))

#define RCC_AHB2ENR1_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))
#define GPIOG_AHB2ENR1_CLOCK_ER (1 << 6)
#define GPIOF_AHB2ENR1_CLOCK_ER (1 << 5)
#define GPIOB_AHB2ENR1_CLOCK_ER (1 << 1)
#define GPIOD_AHB2ENR1_CLOCK_ER (1 << 3)

/* SysTick */
static uint32_t cpu_freq = 250000000;

#define SYSTICK_BASE (0xE000E010)
#define SYSTICK_CSR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_RVR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_CVR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))
#define SYSTICK_CALIB   (*(volatile uint32_t *)(SYSTICK_BASE + 0x0C))

int clock_gettime (clockid_t clock_id, struct timespec *tp)
{
    (void)clock_id;
    tp->tv_sec = jiffies / 1000;
    tp->tv_nsec = (jiffies % 1000) * 1000000;
    return 0;
}

static void systick_enable(void)
{
    SYSTICK_RVR = ((cpu_freq / 1000) - 1);
    SYSTICK_CVR = 0;
    SYSTICK_CSR |= 0x07;
}

void isr_systick(void)
{
    jiffies++;
}

static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOG_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOG_MODER & ~(0x03 << (pin * 2));
    GPIOG_MODER = reg | (1 << (pin * 2));
    GPIOG_PUPDR &= ~(0x03 << (pin * 2));
    GPIOG_BSRR |= (1 << (pin));
}

static void boot_led_off(void)
{
    GPIOG_BSRR |= (1 << (LED_BOOT_PIN + 16));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOB_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOB_MODER & ~(0x03 << (pin * 2));
    GPIOB_MODER = reg | (1 << (pin * 2));
    GPIOB_PUPDR &= ~(0x03 << (pin * 2));
    GPIOB_BSRR |= (1 << (pin));
}

void usr_led_off(void)
{
    GPIOB_BSRR |= (1 << (LED_USR_PIN + 16));
}

void extra_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_EXTRA_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOF_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOF_MODER & ~(0x03 << (pin * 2));
    GPIOF_MODER = reg | (1 << (pin * 2));
    GPIOF_PUPDR &= ~(0x03 << (pin * 2));
    GPIOF_BSRR |= (1 << (pin));
}

void extra_led_off(void)
{
    GPIOF_BSRR |= (1 << (LED_EXTRA_PIN + 16));
}

static char CaBuf[2048];
static uint8_t my_pubkey[200];

extern int ecdsa_sign_verify(int devId);

/* Command line commands */
static int cmd_help(const char *args);
static int cmd_info(const char *args);
static int cmd_success(const char *args);
static int cmd_login_pkcs11(const char *args);
static int cmd_random(const char *args);
static int cmd_benchmark(const char *args);
static int cmd_test(const char *args);
static int cmd_timestamp(const char *args);



#define CMD_BUFFER_SIZE 256
#define CMD_NAME_MAX 64



/* Command parser */
struct console_command {
    int (*fn)(const char *args);
    const char name[CMD_NAME_MAX];
    const char help[CMD_BUFFER_SIZE];
};

struct console_command COMMANDS[] =
{
     { cmd_help, "help", "shows this help message"},
     { cmd_info, "info", "display information about the system and partitions"},
     { cmd_success, "success", "confirm a successful update"},
     { cmd_login_pkcs11, "pkcs11", "enable and test crypto calls with PKCS11 in secure mode" },
     { cmd_random, "random", "generate a random number"},
     { cmd_timestamp, "timestamp", "print the current timestamp"},
     { cmd_benchmark, "benchmark", "run the wolfCrypt benchmark"},
     { cmd_test, "test", "run the wolfCrypt test"},
     { NULL, "", ""}
};

static int cmd_help(const char *args)
{
    int i;
    for (i = 0;; i++) {
        if(COMMANDS[i].fn == NULL)
            break;
        printf("%s : %s\r\n", COMMANDS[i].name, COMMANDS[i].help);
    }
    return 0;
}

static int cmd_info(const char *args)
{
    int i, j;
    uint32_t cur_fw_version, update_fw_version;
    uint32_t n_keys;
    uint16_t hdrSz;

    cur_fw_version = wolfBoot_current_firmware_version();
    update_fw_version = wolfBoot_update_firmware_version();

    printf("\r\n");
    printf("System information\r\n");
    printf("====================================\r\n");
    printf("Firmware version : 0x%lx\r\n", wolfBoot_current_firmware_version());
    if (update_fw_version != 0) {
        printf("Candidate firmware version : 0x%lx\r\n", update_fw_version);
    } else {
        printf("No image in update partition.\r\n");
    }

    printf("\r\n");
    printf("Bootloader OTP keystore information\r\n");
    printf("====================================\r\n");
    n_keys = keystore_num_pubkeys();
    printf("Number of public keys: %lu\r\n", n_keys);
    for (i = 0; i < n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint32_t mask = keystore_get_mask(i);
        uint8_t *keybuf = keystore_get_buffer(i);

        printf("\r\n");
        printf("  Public Key #%d: size %lu, type %lx, mask %08lx\r\n", i,
                size, type, mask);
        printf("  ====================================\r\n  ");
        for (j = 0; j < size; j++) {
            printf("%02X ", keybuf[j]);
            if (j % 16 == 15) {
                printf("\r\n  ");
            }
        }
        printf("\r\n");
    }
    return 0;
}

static int cmd_success(const char *args)
{
    wolfBoot_success();
    printf("update success confirmed.\r\n");
    return 0;
}

static int cmd_random(const char *args)
{
    WC_RNG rng;
    int ret;
    uint32_t rand;

    ret = wc_InitRng(&rng);
    if (ret != 0) {
        printf("Failed to initialize RNG\r\n");
        return -1;
    }
    ret = wc_RNG_GenerateBlock(&rng, (byte *)&rand, sizeof(rand));
    if (ret != 0) {
        printf("Failed to generate random number\r\n");
        wc_FreeRng(&rng);
        return -1;
    }
    printf("Today's lucky number: 0x%08lX\r\n", rand);
    printf("Brought to you by wolfCrypt's DRBG fed by HW TRNG in Secure world\r\n");
    wc_FreeRng(&rng);
    return 0;
}

static int cmd_timestamp(const char *args)
{
    struct timespec tp = {};
    clock_gettime(0, &tp);
    printf("Current timestamp: %llu.%03lu\r\n", tp.tv_sec, tp.tv_nsec/1000000);
    printf("Current systick: %u\r\n", jiffies);
    printf("VTOR: %08lx\r\n", (*(volatile uint32_t *)(0xE000ED08)));
    return 0;
}

static int cmd_login_pkcs11(const char *args)
{
    int ret = -1;
    unsigned int devId = 0;
    Pkcs11Token token;
    Pkcs11Dev PKCS11_d;
    unsigned long session;
    char TokenPin[] = "0123456789ABCDEF";
    char UserPin[] = "ABCDEF0123456789";
    char SoPinName[] = "SO-PIN";
    static int pkcs11_initialized = 0;

    if (pkcs11_initialized) {
        printf("PKCS11 already initialized.\r\n");
        return 0;
    }

#ifdef SECURE_PKCS11
    printf("PKCS11 Login\r\n");

    printf("Initializing wolfCrypt...");
    fflush(stdout);
    wolfCrypt_Init();
    printf("Done.\r\n");

    PKCS11_d.heap = NULL,
    PKCS11_d.func = (CK_FUNCTION_LIST *)&wolfpkcs11nsFunctionList;

    printf("Initializing EccKey token...");
    fflush(stdout);
    ret = wc_Pkcs11Token_Init(&token, &PKCS11_d, 1, "EccKey",
            (const byte*)TokenPin, strlen(TokenPin));

    if (ret == 0) {
        printf("Done.\r\n");
        printf("Retrieving crypto engine function list...");
        fflush(stdout);
        ret = wolfpkcs11nsFunctionList.C_OpenSession(1,
                CKF_SERIAL_SESSION | CKF_RW_SESSION,
                NULL, NULL, &session);
    }
    if (ret == 0) {
        printf("Done.\r\n");
        printf("Initializing token...");
        fflush(stdout);
        ret = wolfpkcs11nsFunctionList.C_InitToken(1,
                (byte *)TokenPin, strlen(TokenPin), (byte *)SoPinName);
    }

    if (ret == 0) {
        printf("Done.\r\n");
        printf("Logging in as SO...");
        ret = wolfpkcs11nsFunctionList.C_Login(session, CKU_SO,
                (byte *)TokenPin,
                strlen(TokenPin));
    }
    if (ret == 0) {
        extra_led_on();
        printf("Done.\r\n");
        printf("Setting PIN...");
        ret = wolfpkcs11nsFunctionList.C_InitPIN(session,
                (byte *)TokenPin,
                strlen(TokenPin));
    }
    if (ret == 0) {
        printf("Done.\r\n");
        printf("Logging out...");
        ret = wolfpkcs11nsFunctionList.C_Logout(session);
    }
    if (ret == 0) {
        printf("Done.\r\n");
        printf("Registering crypto calls with wolfCrypt...");
        ret = wc_CryptoDev_RegisterDevice(devId, wc_Pkcs11_CryptoDevCb,
                &token);
    }
    if (ret == 0) {
        printf("Done.\r\n");
#ifdef HAVE_ECC
        printf("Testing ECC...");
        ret = ecdsa_sign_verify(devId);
        if (ret != 0) {
            ret = -1;
            printf("Failed.\r\n");
        }
        else {
            usr_led_on();
            printf("Done.\r\n");
        }
#endif
    }

#endif /* SECURE_PKCS11 */
    if (ret == 0) {
        printf("PKCS11 initialization completed successfully.\r\n");
        pkcs11_initialized = 1;
    }
    return ret;
}

static int cmd_benchmark(const char *args)
{

    benchmark_test(NULL);
    return 0;
}

/* Test command */
static int cmd_test(const char *args)
{
    wolfcrypt_test(NULL);
    return 0;
}

static int parse_cmd(const char *cmd)
{
    int retval = -2;
    int i;
    for (i = 0;; i++) {
        if(COMMANDS[i].fn == NULL)
            break;
        if (strncmp(cmd, COMMANDS[i].name, strlen(COMMANDS[i].name)) == 0) {
            retval = COMMANDS[i].fn(cmd);
            break;
        }
    }
    return retval;
}


/* Main loop reading commands from UART */
static void console_loop(void)
{
    int ret;
    int idx = 0;
    char cmd[CMD_BUFFER_SIZE];
    char c;
    while (1) {
        printf("\r\n");
        printf("cmd> ");
        fflush(stdout);
        idx = 0;
        do {
            ret = uart_rx((uint8_t *)&c);
            if (ret > 0) {
                if (c == '\r')
                    break;
                cmd[idx++] = c;
            }
        } while (idx < (CMD_BUFFER_SIZE - 1));
        if (idx > 0) {
            cmd[idx] = 0;
            if (parse_cmd(cmd) == -2) {
                printf("Unknown command\r\n");
            }
        }
    }
}


void main(void)
{
    int ret;
    uint32_t rand;
    uint32_t i;
    uint32_t klen = 200;
    int otherkey_slot;
    uint32_t app_version;


    /* Turn on boot LED */
    boot_led_on();

    /* Enable SysTick */
    systick_enable();

    app_version = wolfBoot_current_firmware_version();

    uart_init(115200, 8, 'N', 1);
    printf("========================\r\n");
    printf("STM32H5 wolfBoot demo Application\r\n");
    printf("Copyright 2024 wolfSSL Inc\r\n");
    printf("GPL v3\r\n");
    printf("Version : 0x%lx\r\n", app_version);
    printf("========================\r\n");

    console_loop();


    while(1)
        ;

    /* Never reached */
}



/* Syscall helpers + UART interface for printf */


int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  return -1;
}

void _exit (int status)
{
  _kill(status, -1);
  while (1) {}    /* Make sure we hang here */
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
{
  (void)file;
  int DataIdx;
  int ret;

  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
      do {
          ret = uart_rx((uint8_t *)ptr);
          if (ret > 0)
              ptr++;
      } while (ret == 0);
      if (ret == 0)
          break;
  }
  return DataIdx;
}

int _write(int file, char *ptr, int len)
{
  (void)file;
  int DataIdx;

  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
      uart_tx(*ptr++);
  }
  return len;
}

int _close(int file)
{
  (void)file;
  return -1;
}


int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

