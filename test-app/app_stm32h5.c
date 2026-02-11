/* app_stm32h5.c
 *
 * Test bare-metal application.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "system.h"
#include "hal.h"
#include "hal/stm32h5.h"
#include "uart_drv.h"
#include "wolfboot/wolfboot.h"
#ifndef WOLFBOOT_NO_SIGN
#include "keystore.h"
#endif
#include "target.h"

#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif

#ifdef WOLFBOOT_TZ_PKCS11
#include "wcs/user_settings.h"
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/wc_pkcs11.h"
#include "wolfssl/wolfcrypt/random.h"
extern const char pkcs11_library_name[];
extern const CK_FUNCTION_LIST wolfpkcs11nsFunctionList;
#endif

#ifdef WOLFCRYPT_SECURE_MODE
int benchmark_test(void *args);
int wolfcrypt_test(void *args);
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/random.h"
#endif

#ifdef WOLFCRYPT_TZ_PSA
#include "psa/crypto.h"
#include "psa/error.h"
#include "psa/initial_attestation.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/sha256.h"
#include "wolfssl/wolfcrypt/sha512.h"
#include "wolfssl/wolfcrypt/sha3.h"
#endif

volatile unsigned int jiffies = 0;

/* Usart irq-based read function */
static uint8_t uart_buf_rx[1024];
static uint32_t uart_rx_bytes = 0;
static uint32_t uart_processed = 0;
static int uart_rx_isr(unsigned char *c, int len);
static int uart_poll(void);

#define LED_BOOT_PIN  (4) /* PG4 - Nucleo - Red Led */
#define LED_USR_PIN   (0) /* PB0 - Nucleo - Green Led */
#define LED_EXTRA_PIN (4) /* PF4 - Nucleo - Orange Led */
#define BOOT_TIME_PIN (13) /* PA13 - scope trigger */

#ifdef WOLFBOOT_TEST_FILLER
#define FILLER_SIZE (64 * 1024)
static volatile uint8_t filler_data[FILLER_SIZE] = { 0x01, 0x02, 0x03 };
#endif

#define NVIC_USART3_IRQN (60)

#ifndef GPIOA_MODER
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#endif

/* SysTick */
static uint32_t cpu_freq = 250000000;


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

    RCC_AHB2ENR_CLOCK_ER |= GPIOG_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR_CLOCK_ER;

    reg = GPIOG_MODER & ~(0x03 << (pin * 2));
    GPIOG_MODER = reg | (1 << (pin * 2));
    GPIOG_PUPDR &= ~(0x03 << (pin * 2));
    GPIOG_BSRR |= (1 << (pin));
}

void boot_time_pin_on_early(void)
{
    uint32_t reg;
    uint32_t pin = BOOT_TIME_PIN;

    RCC_AHB2ENR_CLOCK_ER |= GPIOA_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR_CLOCK_ER;

    reg = GPIOA_MODER & ~(0x03 << (pin * 2));
    GPIOA_MODER = reg | (1 << (pin * 2));
    GPIOA_PUPDR &= ~(0x03 << (pin * 2));
    GPIOA_BSRR = (1 << (pin));
}

static void boot_led_off(void)
{
    GPIOG_BSRR |= (1 << (LED_BOOT_PIN + 16));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;

    RCC_AHB2ENR_CLOCK_ER |= GPIOB_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR_CLOCK_ER;

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

    RCC_AHB2ENR_CLOCK_ER|= GPIOF_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR_CLOCK_ER;

    reg = GPIOF_MODER & ~(0x03 << (pin * 2));
    GPIOF_MODER = reg | (1 << (pin * 2));
    GPIOF_PUPDR &= ~(0x03 << (pin * 2));
    GPIOF_BSRR |= (1 << (pin));
}

void extra_led_off(void)
{
    GPIOF_BSRR |= (1 << (LED_EXTRA_PIN + 16));
}

extern int ecdsa_sign_verify(int devId);

/* Command line commands */
static int cmd_help(const char *args);
static int cmd_info(const char *args);
static int cmd_success(const char *args);
#ifdef WOLFBOOT_TZ_PKCS11
static int cmd_login_pkcs11(const char *args);
#endif
static int cmd_random(const char *args);
static int cmd_benchmark(const char *args);
static int cmd_test(const char *args);
static int cmd_timestamp(const char *args);
static int cmd_update(const char *args);
static int cmd_update_xmodem(const char *args);
static int cmd_reboot(const char *args);
#ifdef WOLFBOOT_TPM
static int cmd_tpm_info(const char *args);
#ifdef WOLFTPM_MFG_IDENTITY
static int cmd_tpm_idevid(const char *args);
static int cmd_tpm_iak(const char *args);
static int cmd_tpm_signed_timestamp(const char *args);
static int cmd_tpm_quote(const char *args);
#endif
#endif


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
    {cmd_help, "help", "shows this help message"},
    {cmd_info, "info", "display information about the system and partitions"},
    {cmd_success, "success", "confirm a successful update"},
#ifdef WOLFBOOT_TZ_PKCS11
    {cmd_login_pkcs11, "pkcs11", "enable and test crypto calls with PKCS11 in secure mode" },
#endif
    {cmd_random, "random", "generate a random number"},
    {cmd_timestamp, "timestamp", "print the current systick/timestamp"},
    {cmd_benchmark, "benchmark", "run the wolfCrypt benchmark"},
    {cmd_test, "test", "run the wolfCrypt test"},
    {cmd_update_xmodem, "update", "update the firmware via XMODEM"},
    {cmd_reboot, "reboot", "reboot the system"},
#ifdef WOLFBOOT_TPM
    {cmd_tpm_info, "tpm", "get TPM capabilities"},
#ifdef WOLFTPM_MFG_IDENTITY
    {cmd_tpm_idevid, "idevid", "show Initial Device Identification (IDevID) certificate"},
    {cmd_tpm_iak, "iak", "show Initial Attestation Identification (IAK) certificate"},
    {cmd_tpm_signed_timestamp, "signed_time", "TPM IAK signed timestamp attestation report"},
    {cmd_tpm_quote, "quote", "TPM IAK signed PCR(s) attestation report"},
#endif
#endif
    {NULL, "", ""}
};

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#   define AIRCR_SYSRESETREQ (1 << 2)

int cmd_reboot(const char *args)
{
    (void)args;
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        asm volatile("wfi");
    return 0; /* Never happens */
}

#define XSOH 0x01
#define XEOT 0x04
#define XACK 0x06
#define XNAK 0x15
#define XCAN 0x18


static uint8_t crc8(uint8_t *data, size_t len)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

#define XMODEM_PAYLOAD_SIZE 128
#define XMODEM_PACKET_SIZE (3 + XMODEM_PAYLOAD_SIZE + 1)
#define XMODEM_TIMEOUT   1000 /* milliseconds */

static void xcancel(void)
{
    uint32_t i;
    for (i = 0; i < 10; i++)
        uart_tx(XCAN);
}

static uint8_t xpkt_payload[XMODEM_PAYLOAD_SIZE];

static int cmd_update_xmodem(const char *args)
{
    int ret = -1;
    uint8_t xpkt[XMODEM_PACKET_SIZE];
    uint32_t dst_offset = 0;
    uint8_t pkt_num = 0, pkt_num_expected=0xFF;
    uint32_t pkt_size = XMODEM_PACKET_SIZE;
    uint32_t t_size = 0;
    uint32_t update_ver = 0;
    uint32_t now = jiffies;
    uint32_t i = 0;
    uint8_t pkt_num_inv;
    uint8_t crc, calc_crc;
    int transfer_started = 0;
    int eot_expected = 0;


    printf("Erasing update partition...");
    fflush(stdout);
#ifdef WOLFCRYPT_SECURE_MODE
    wolfBoot_nsc_erase_update(dst_offset, WOLFBOOT_PARTITION_SIZE);
#else
    hal_flash_unlock();
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS + dst_offset, WOLFBOOT_PARTITION_SIZE);
#endif
    printf("Done.\r\n");

    printf("Waiting for XMODEM transfer...\r\n");

    while (1) {
        now = jiffies;
        i = 0;

        while(i < XMODEM_PACKET_SIZE) {
            ret = uart_rx_isr(&xpkt[i], XMODEM_PACKET_SIZE - i);
            if (ret == 0) {
                if(jiffies > (now + XMODEM_TIMEOUT)) {
                    now = jiffies;
                    if (i == 0)
                        uart_tx(XNAK);
                    i = 0;
                } else {
                    asm volatile("wfi");
                }
            } else {
                now = jiffies;
                if (i == 0 && xpkt[0] == XEOT)
                    break;
                i += ret;
            }
        }

        if (xpkt[0] == XEOT) {
            ret = 0;
            uart_tx(XACK);
            extra_led_on();
            break;
        }
        else if (eot_expected) {
            ret = 1;
            uart_tx(XNAK);
            break;
        }

        if (xpkt[0] != XSOH) {
            continue;
        }
        pkt_num = xpkt[1];
        pkt_num_inv = ~xpkt[2];
        if (pkt_num == pkt_num_inv) {
            if (!transfer_started) /* sync */ {
                (pkt_num_expected = pkt_num);
                transfer_started = 1;
            } else if (pkt_num_expected != pkt_num) {
                uart_tx(XNAK);
                continue;
            }
            if ((pkt_num / 0x10) & 0x01)
                extra_led_on();
            else
                extra_led_off();

            /* Packet is valid */
            crc = xpkt[XMODEM_PACKET_SIZE - 1];
            calc_crc = crc8(xpkt, XMODEM_PACKET_SIZE - 1);
            if (crc == calc_crc) {
                /* CRC is valid */
                memcpy(xpkt_payload, xpkt + 3, XMODEM_PAYLOAD_SIZE);
#ifdef WOLFCRYPT_SECURE_MODE
                ret = wolfBoot_nsc_write_update(dst_offset, xpkt_payload, XMODEM_PAYLOAD_SIZE);
#else
                ret = hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + dst_offset, xpkt_payload, XMODEM_PAYLOAD_SIZE);
#endif
                if (ret != 0) {
                    xcancel();
                    printf("Error writing to flash\r\n");
                    break;
                }
                uart_tx(XACK);
                pkt_num++;
                pkt_num_expected++;
                dst_offset += XMODEM_PAYLOAD_SIZE;
                if (t_size == 0) {
                    /* At first packet, save expected partition size */
                    t_size = *(uint32_t *)(xpkt_payload + 4);
                    t_size += IMAGE_HEADER_SIZE;
                }
                if (dst_offset >= t_size) {
                    eot_expected = 1;
                }
            } else {
                uart_tx(XNAK);
            }
        } else {
            uart_tx(XNAK); /* invalid packet number received */
        }
    }
    for (i = 0; i < 10; i++)
        uart_tx('\r');

    printf("End of transfer. ret: %d\r\n", ret);
    if (ret != 0) {
        printf("Transfer failed\r\n");
    }
    else {
        printf("Transfer succeeded\r\n");
#ifdef WOLFCRYPT_SECURE_MODE
        update_ver = wolfBoot_nsc_update_firmware_version();
#else
        update_ver = wolfBoot_update_firmware_version();
#endif
        if (update_ver != 0) {
            printf("New firmware version: 0x%lx\r\n", update_ver);
            printf("Triggering update...\r\n");
#ifdef WOLFCRYPT_SECURE_MODE
            wolfBoot_nsc_update_trigger();
#else
            wolfBoot_update_trigger();
#endif
            printf("Update written successfully. Reboot to apply.\r\n");
        } else {
            printf("No valid image in update partition\r\n");
        }
    }

#ifndef WOLFCRYPT_SECURE_MODE
    hal_flash_lock();
#endif

    return ret;
}

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

const char part_state_names[6][16] = {
    "NEW",
    "UPDATING",
    "FFLAGS",
    "TESTING",
    "CONFIRMED",
    "[Invalid state]"
};

static const char *part_state_name(uint8_t state)
{
    switch(state) {
        case IMG_STATE_NEW:
            return part_state_names[0];
        case IMG_STATE_UPDATING:
            return part_state_names[1];
        case IMG_STATE_FINAL_FLAGS:
            return part_state_names[2];
        case IMG_STATE_TESTING:
            return part_state_names[3];
        case IMG_STATE_SUCCESS:
            return part_state_names[4];
        default:
            return part_state_names[5];
    }
}

#define LINE_LEN 16
void print_hex(const uint8_t* buffer, uint32_t length, int dumpChars)
{
    uint32_t i, sz;

    if (!buffer) {
        printf("\tNULL\n");
        return;
    }

    while (length > 0) {
        sz = length;
        if (sz > LINE_LEN)
            sz = LINE_LEN;

        printf("\t");
        for (i = 0; i < LINE_LEN; i++) {
            if (i < length)
                printf("%02x ", buffer[i]);
            else
                printf("   ");
        }
        if (dumpChars) {
            printf("| ");
            for (i = 0; i < sz; i++) {
                if (buffer[i] > 31 && buffer[i] < 127)
                    printf("%c", buffer[i]);
                else
                    printf(".");
            }
        }
        printf("\r\n");

        buffer += sz;
        length -= sz;
    }
}

static int cmd_info(const char *args)
{
    int i;
    uint32_t cur_fw_version, update_fw_version;
    uint32_t n_keys;
    uint16_t hdrSz;
    uint8_t boot_part_state = IMG_STATE_NEW, update_part_state = IMG_STATE_NEW;

#ifdef WOLFCRYPT_SECURE_MODE
    cur_fw_version = wolfBoot_nsc_current_firmware_version();
    update_fw_version = wolfBoot_nsc_update_firmware_version();

    wolfBoot_nsc_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_nsc_get_partition_state(PART_UPDATE, &update_part_state);
#else
    cur_fw_version = wolfBoot_current_firmware_version();
    update_fw_version = wolfBoot_update_firmware_version();

    wolfBoot_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_get_partition_state(PART_UPDATE, &update_part_state);
#endif

    printf("\r\n");
    printf("System information\r\n");
    printf("====================================\r\n");
    printf("Flash banks are %sswapped.\r\n", ((FLASH_OPTSR_CUR & (FLASH_OPTSR_SWAP_BANK)) == 0)?"not ":"");
    printf("Firmware version : 0x%lx\r\n", cur_fw_version);
    printf("Current firmware state: %s\r\n", part_state_name(boot_part_state));
    if (update_fw_version != 0) {
        if (update_part_state == IMG_STATE_UPDATING)
            printf("Candidate firmware version : 0x%lx\r\n", update_fw_version);
        else
            printf("Backup firmware version : 0x%lx\r\n", update_fw_version);
        printf("Update state: %s\r\n", part_state_name(update_part_state));
        if (update_fw_version > cur_fw_version) {
            printf("'reboot' to initiate update.\r\n");
        } else {
            printf("Update image older than current.\r\n");
        }
    } else {
        printf("No image in update partition.\r\n");
    }

#ifndef WOLFBOOT_NO_SIGN
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
        print_hex(keybuf, size, 0);
    }
#else
    printf("\r\n");
    printf("Signing disabled (SIGN=NONE)\r\n");
#endif
    return 0;
}

static int cmd_success(const char *args)
{
#ifdef WOLFCRYPT_SECURE_MODE
    wolfBoot_nsc_success();
#else
    wolfBoot_success();
#endif
    printf("update success confirmed.\r\n");
    return 0;
}

static int cmd_random(const char *args)
{
#ifdef WOLFCRYPT_TZ_PSA
    uint32_t rand = 0;
    psa_status_t status = psa_generate_random((uint8_t *)&rand, sizeof(rand));
    if (status != PSA_SUCCESS) {
        printf("Failed to generate PSA random number (%ld)\r\n",
               (long)status);
        return -1;
    }
    printf("Today's lucky number: 0x%08lX\r\n", rand);
    printf("Brought to you by PSA crypto + HW TRNG in Secure world\r\n");
#elif defined(WOLFCRYPT_SECURE_MODE)
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
#else
    printf("Feature only supported with WOLFCRYPT_TZ=1\n");
#endif
    return 0;
}

static int cmd_timestamp(const char *args)
{
    struct timespec tp = {};
    clock_gettime(0, &tp);
    printf("Current timestamp: %lu.%03lu\r\n",
        (long unsigned int)tp.tv_sec, tp.tv_nsec/1000000);
    printf("Current systick: %u\r\n", jiffies);
    printf("VTOR: 0x%08lx\r\n", (*(volatile uint32_t *)(0xE000ED08)));
    return 0;
}

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)
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
        printf("attest: get token failed (%d)\r\n", status);
        return -1;
    }
    printf("attest: token size %lu bytes\r\n", (unsigned long)token_size);
    print_hex(token, (uint32_t)token_size, 1);
    return 0;
}
#endif

#ifdef WOLFCRYPT_TZ_PSA
/* Hash helpers for app-side measurement printing. */
#if defined(WOLFBOOT_HASH_SHA256)
#define APP_HASH_HDR HDR_SHA256
#define APP_HASH_SIZE (32u)
typedef wc_Sha256 app_hash_t;
#define app_hash_init(h) wc_InitSha256((h))
#define app_hash_update(h, data, len) \
    wc_Sha256Update((h), (const byte *)(data), (word32)(len))
#define app_hash_final(h, out) wc_Sha256Final((h), (byte *)(out))
#elif defined(WOLFBOOT_HASH_SHA384)
#define APP_HASH_HDR HDR_SHA384
#define APP_HASH_SIZE (48u)
typedef wc_Sha384 app_hash_t;
#define app_hash_init(h) wc_InitSha384((h))
#define app_hash_update(h, data, len) \
    wc_Sha384Update((h), (const byte *)(data), (word32)(len))
#define app_hash_final(h, out) wc_Sha384Final((h), (byte *)(out))
#elif defined(WOLFBOOT_HASH_SHA3_384)
#define APP_HASH_HDR HDR_SHA3_384
#define APP_HASH_SIZE (48u)
typedef wc_Sha3 app_hash_t;
#define app_hash_init(h) wc_InitSha3_384((h), NULL, INVALID_DEVID)
#define app_hash_update(h, data, len) \
    wc_Sha3_384_Update((h), (const byte *)(data), (word32)(len))
#define app_hash_final(h, out) wc_Sha3_384_Final((h), (byte *)(out))
#else
#define APP_HASH_HDR 0
#define APP_HASH_SIZE (0u)
typedef int app_hash_t;
#define app_hash_init(h) (void)(h)
#define app_hash_update(h, data, len) (void)(h), (void)(data), (void)(len)
#define app_hash_final(h, out) (void)(h), (void)(out)
#endif

static int hash_region(uintptr_t address, uint32_t size, uint8_t *out)
{
    app_hash_t hash;
    const uint8_t *ptr = (const uint8_t *)address;
    uint32_t pos = 0;

    if (out == NULL || size == 0 || APP_HASH_SIZE == 0u) {
        return -1;
    }

    app_hash_init(&hash);

    while (pos < size) {
        uint32_t chunk = size - pos;
        if (chunk > 256) {
            chunk = 256;
        }
        app_hash_update(&hash, ptr + pos, chunk);
        pos += chunk;
    }

    app_hash_final(&hash, out);
    return 0;
}

static int run_psa_boot_attestation(void)
{
    psa_status_t status;
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64];
    uint8_t token[1024];
#if (APP_HASH_SIZE > 0u)
    uint8_t hash_buf[APP_HASH_SIZE];
#endif
    size_t token_size = 0;
    int ret = 0;
    size_t i;

    printf("PSA boot attestation: start\r\n");

    printf("  step 1: TODO verify boot image post-boot\r\n");
    printf("  step 2: TODO read boot image measurement (HDR_HASH)\r\n");

    printf("  step 3: compute wolfBoot measurement\r\n");
#if defined(WOLFBOOT_PARTITION_BOOT_ADDRESS) && defined(ARCH_FLASH_OFFSET)
#if (APP_HASH_SIZE > 0u)
    if (ret == 0) {
        uintptr_t start = (uintptr_t)ARCH_FLASH_OFFSET;
        uintptr_t end = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        if (end <= start) {
            printf("  step 3: invalid wolfBoot region\r\n");
            ret = -1;
        } else if (hash_region(start, (uint32_t)(end - start), hash_buf) != 0) {
            printf("  step 3: wolfBoot hash failed\r\n");
            ret = -1;
        } else {
            printf("  step 3: wolfBoot hash (%u bytes)\r\n",
                   (unsigned int)APP_HASH_SIZE);
            print_hex(hash_buf, APP_HASH_SIZE, 0);
        }
    }
#else
    printf("  step 3: hash algorithm not enabled\r\n");
#endif
#else
    printf("  step 3: wolfBoot region unavailable for hashing\r\n");
#endif

    printf("  step 4: generate attestation challenge\r\n");
    status = psa_generate_random(challenge, sizeof(challenge));
    if (status != PSA_SUCCESS) {
        printf("  step 4: PSA RNG failed (%ld), using deterministic nonce\r\n",
               (long)status);
        for (i = 0; i < sizeof(challenge); i++) {
            challenge[i] = (uint8_t)i;
        }
    } else {
        printf("  step 4: challenge ready (%u bytes)\r\n",
               (unsigned int)sizeof(challenge));
    }

    printf("  step 5: request IAT token size\r\n");
    status = psa_initial_attest_get_token_size(sizeof(challenge), &token_size);
    if (status != PSA_SUCCESS) {
        printf("  step 5: token size failed (%ld)\r\n", (long)status);
        ret = -1;
    } else {
        printf("  step 5: token size %lu bytes\r\n",
               (unsigned long)token_size);
    }

    printf("  step 6: request IAT token\r\n");
    if (ret == 0 && token_size <= sizeof(token)) {
        status = psa_initial_attest_get_token(challenge, sizeof(challenge),
                                              token, sizeof(token), &token_size);
        if (status != PSA_SUCCESS) {
            printf("  step 6: token failed (%ld)\r\n", (long)status);
            ret = -1;
        } else {
            printf("  step 6: token received (%lu bytes)\r\n",
                   (unsigned long)token_size);
            print_hex(token, (uint32_t)token_size, 1);
        }
    } else if (ret == 0) {
        printf("  step 6: token buffer too small (%lu > %lu)\r\n",
               (unsigned long)token_size, (unsigned long)sizeof(token));
        ret = -1;
    }

    printf("PSA boot attestation: %s\r\n", ret == 0 ? "success" : "failed");

    if (ret == 0)
        asm volatile ("bkpt #0x7f");
    else 
        asm volatile ("bkpt #0x7e");


    return ret;
}
#endif

#ifdef WOLFBOOT_TZ_PKCS11
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
        printf("Initializing token...");
        fflush(stdout);
        ret = wolfpkcs11nsFunctionList.C_InitToken(1,
                (byte *)TokenPin, strlen(TokenPin), (byte *)SoPinName);
    }
    if (ret == 0) {
        printf("Done.\r\n");
        printf("Opening session...");
        fflush(stdout);
        ret = wolfpkcs11nsFunctionList.C_OpenSession(1,
                CKF_SERIAL_SESSION | CKF_RW_SESSION,
                NULL, NULL, &session);
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
    if (ret == 0) {
        printf("PKCS11 initialization completed successfully.\r\n");
        pkcs11_initialized = 1;
    }
    return ret;
}
#endif /* WOLFBOOT_TZ_PKCS11 */

static int cmd_benchmark(const char *args)
{
#ifdef WOLFCRYPT_SECURE_MODE
    benchmark_test(NULL);
#endif
    return 0;
}

/* Test command */
static int cmd_test(const char *args)
{
#ifdef WOLFCRYPT_SECURE_MODE
    wolfcrypt_test(NULL);
#endif
    return 0;
}

#ifdef WOLFBOOT_TPM
#include <wolftpm/tpm2.h>
#include <wolftpm/tpm2_wrap.h>

static int TPM2_PCRs_Print(void)
{
    int rc;
    int pcrCount, pcrIndex;
    GetCapability_In  capIn;
    GetCapability_Out capOut;
    TPML_PCR_SELECTION* pcrSel;
    char algName[24];

    /* List available PCR's */
    XMEMSET(&capIn, 0, sizeof(capIn));
    capIn.capability = TPM_CAP_PCRS;
    capIn.property = 0;
    capIn.propertyCount = 1;
    rc = wolfBoot_tpm2_get_capability(&capIn, &capOut);
    if (rc == TPM_RC_SUCCESS) {
        pcrSel = &capOut.capabilityData.data.assignedPCR;
        printf("Assigned PCR's:\r\n");
        for (pcrCount=0; pcrCount < (int)pcrSel->count; pcrCount++) {

            printf("\t%s: ", wolfBoot_tpm2_get_alg_name(
                pcrSel->pcrSelections[pcrCount].hash, algName, sizeof(algName)));
            for (pcrIndex=0;
                pcrIndex<pcrSel->pcrSelections[pcrCount].sizeofSelect*8;
                pcrIndex++) {
                if ((pcrSel->pcrSelections[pcrCount].pcrSelect[pcrIndex/8] &
                        ((1 << (pcrIndex % 8)))) != 0) {
                    printf(" %d", pcrIndex);
                }
            }
            printf("\r\n");
        }
    }
    return rc;
}

static int cmd_tpm_info(const char *args)
{
    int rc;
    WOLFTPM2_CAPS caps;
    TPML_HANDLE handles;
#ifdef WOLFBOOT_MEASURED_PCR_A
    byte hashBuf[TPM_MAX_DIGEST_SIZE];
    int hashSz;
#endif

    printf("Get TPM 2.0 module information\r\n");

    rc = wolfBoot_tpm2_caps(&caps);
    if (rc == 0) {
        printf("Mfg %s (%d), Vendor %s, Fw %u.%u (0x%x), "
            "FIPS 140-2 %d, CC-EAL4 %d\r\n",
            caps.mfgStr, caps.mfg, caps.vendorStr, caps.fwVerMajor,
            caps.fwVerMinor, caps.fwVerVendor, caps.fips140_2, caps.cc_eal4);
    }

    /* List the active persistent handles */
    rc = wolfBoot_tpm2_get_handles(PERSISTENT_FIRST, &handles);
    if (rc >= 0) {
        int i;
        printf("Found %d persistent handles\r\n", rc);
        for (i=0; i<(int)handles.count; i++) {
            printf("\tHandle 0x%x\r\n", (unsigned int)handles.handle[i]);
        }
        rc = 0;
    }

    /* Print the available PCR's */
    if (rc == 0) {
        rc = TPM2_PCRs_Print();
    }

#ifdef WOLFBOOT_MEASURED_PCR_A
    /* Read measured boot PCR */
    if (rc == 0) {
        char algName[24];
        printf("Measured boot: PCR %d - %s\r\n", WOLFBOOT_MEASURED_PCR_A,
            wolfBoot_tpm2_get_alg_name(WOLFBOOT_TPM_PCR_ALG, algName, sizeof(algName)));
        hashSz = 0;
        rc = wolfBoot_tpm2_read_pcr(WOLFBOOT_MEASURED_PCR_A, hashBuf, &hashSz);
        if (rc == 0) {
            int i;
            printf("PCR (%d bytes): ", hashSz);
            for (i = 0; i < hashSz; i++) {
                printf("%02x", hashBuf[i]);
            }
            printf("\r\n");
        }
    }
#endif

    if (rc != 0) {
        char error[100];
        printf("TPM error 0x%x: %s\r\n",
            rc, wolfBoot_tpm2_get_rc_string(rc, error, sizeof(error)));
    }

    return rc;
}

#ifdef WOLFTPM_MFG_IDENTITY

/* Forward declarations */
static void print_signature(const TPMT_SIGNATURE* sig);

static int cmd_tpm_idevid(const char *args)
{
    int rc;
    uint8_t cert[1024];
    uint32_t certSz = (uint32_t)sizeof(cert);
    uint32_t handle = TPM2_IDEVID_CERT_HANDLE;

    rc = wolfBoot_tpm2_read_cert(handle, cert, &certSz);
    if (rc == 0) {
        printf("IDevID Handle 0x%x\r\n", (unsigned int)handle);
        print_hex(cert, certSz, 1);
    }
    else {
        char error[100];
        printf("TPM error 0x%x: %s\r\n",
            rc, wolfBoot_tpm2_get_rc_string(rc, error, sizeof(error)));
    }
    return rc;
}

static int cmd_tpm_iak(const char *args)
{
    int rc;
    uint8_t cert[1024];
    uint32_t certSz = (uint32_t)sizeof(cert);
    uint32_t handle = TPM2_IAK_CERT_HANDLE;

    rc = wolfBoot_tpm2_read_cert(handle, cert, &certSz);
    if (rc == 0) {
        printf("IAK Handle 0x%x\r\n", (unsigned int)handle);
        print_hex(cert, certSz, 1);
    }
    else {
        char error[100];
        printf("TPM error 0x%x: %s\r\n",
            rc, wolfBoot_tpm2_get_rc_string(rc, error, sizeof(error)));
    }
    return rc;
}

static int cmd_tpm_signed_timestamp(const char *args)
{
    int rc;
    WOLFTPM2_KEY aik;
    GetTime_Out getTime;
    TPMS_ATTEST timeAttest;

    rc = wolfBoot_tpm2_get_aik(&aik, NULL, 0);
    if (rc == 0) {
        rc = wolfBoot_tpm2_get_timestamp(&aik, &getTime);
    }
    if (rc == 0) {
        rc = wolfBoot_tpm2_parse_attest(&getTime.timeInfo, &timeAttest);
    }
    if (rc == 0) {
        if (timeAttest.magic != TPM_GENERATED_VALUE) {
            printf("\tError, attested data not generated by the TPM = 0x%X\n",
                (unsigned int)timeAttest.magic);
        }

        printf("TPM with signature attests (type 0x%x):\n", timeAttest.type);
        /* time value in milliseconds that advances while the TPM is powered */
        printf("\tTPM uptime since last power-up (in ms): %lu\n",
            (unsigned long)timeAttest.attested.time.time.time);
        /* time value in milliseconds that advances while the TPM is powered */
        printf("\tTPM clock, total time the TPM has been on (in ms): %lu\n",
            (unsigned long)timeAttest.attested.time.time.clockInfo.clock);
        /* number of occurrences of TPM Reset since the last TPM2_Clear() */
        printf("\tReset Count: %u\n",
            (unsigned int)timeAttest.attested.time.time.clockInfo.resetCount);
        /* number of times that TPM2_Shutdown() or _TPM_Hash_Start have occurred since the last TPM Reset or TPM2_Clear(). */
        printf("\tRestart Count: %u\n",
            (unsigned int)timeAttest.attested.time.time.clockInfo.restartCount);
        /* This parameter is set to YES when the value reported in Clock is guaranteed to be unique for the current Owner */
        printf("\tClock Safe: %u\n",
            timeAttest.attested.time.time.clockInfo.safe);
        /* a TPM vendor-specific value indicating the version number of the firmware */
        printf("\tFirmware Version (vendor specific): 0x%lX\n",
            (unsigned long)timeAttest.attested.time.firmwareVersion);

        print_signature(&getTime.signature);
    }

    if (rc != 0) {
        char error[100];
        printf("TPM get timestamp error 0x%x: %s\r\n",
            rc, wolfBoot_tpm2_get_rc_string(rc, error, sizeof(error)));
    }

    return rc;
}

static void print_signature(const TPMT_SIGNATURE* sig)
{
    char algName[24];
    printf("\tTPM generated %s signature:\n",
        wolfBoot_tpm2_get_alg_name(sig->sigAlg, algName, sizeof(algName)));
    printf("\tHash algorithm: %s\n",
        wolfBoot_tpm2_get_alg_name(sig->signature.any.hashAlg, algName, sizeof(algName)));
    switch (sig->sigAlg) {
        case TPM_ALG_ECDSA:
        case TPM_ALG_ECDAA:
            printf("\tR size: %d\n", sig->signature.ecdsa.signatureR.size);
            print_hex(sig->signature.ecdsa.signatureR.buffer, sig->signature.ecdsa.signatureR.size, 0);
            printf("\tS size: %d\n", sig->signature.ecdsa.signatureS.size);
            print_hex(sig->signature.ecdsa.signatureS.buffer, sig->signature.ecdsa.signatureS.size, 0);
            break;
        case TPM_ALG_RSASSA:
        case TPM_ALG_RSAPSS:
            printf("\tSignature size: %d\n", sig->signature.rsassa.sig.size);
            print_hex(sig->signature.rsassa.sig.buffer, sig->signature.rsassa.sig.size, 0);
            break;
    };
}

static int cmd_tpm_quote(const char *args)
{
    int rc;
    WOLFTPM2_KEY aik;
    Quote_Out quoteResult;
    TPMS_ATTEST quoteAttest;
    uint8_t  pcrArray[1];
    uint32_t pcrArraySz = 0;

#ifdef WOLFBOOT_MEASURED_PCR_A
    pcrArray[0] = WOLFBOOT_MEASURED_PCR_A;
    pcrArraySz++;
#else
    pcrArray[0] = 16; /* test PCR */
    pcrArraySz++;
#endif

    rc = wolfBoot_tpm2_get_aik(&aik, NULL, 0);
    if (rc == 0) {
        rc = wolfBoot_tpm2_quote(&aik, pcrArray, pcrArraySz, &quoteResult);
    }
    if (rc == 0) {
        rc = wolfBoot_tpm2_parse_attest(&quoteResult.quoted, &quoteAttest);
    }
    if (rc == 0) {
        printf("TPM with signature attests (type 0x%x):\n", quoteAttest.type);
        printf("\tTPM signed %lu PCRs\n",
            (unsigned long)quoteAttest.attested.quote.pcrSelect.count);

        printf("\tPCR digest:\n");
        print_hex(quoteAttest.attested.quote.pcrDigest.buffer,
                  quoteAttest.attested.quote.pcrDigest.size, 0);

        print_signature(&quoteResult.signature);
    }
    else {
        char error[100];
        printf("TPM quote error 0x%x: %s\r\n", rc,
            wolfBoot_tpm2_get_rc_string(rc, error, sizeof(error)));
    }
    return rc;
}
#endif /* WOLFTPM_MFG_IDENTITY */
#endif /* WOLFBOOT_TPM */


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
    unsigned char c;
    while (1) {
        printf("\r\n");
        printf("cmd> ");
        fflush(stdout);
        idx = 0;
        do {
            ret = uart_rx_isr((uint8_t *)&c, 1);
            if (ret > 0) {
                if ((c >= 32) && (c < 127)) {
                    printf("%c", c);
                    fflush(stdout);
                    cmd[idx++] = (char)c;
                } else if (c == '\r') {
                    printf("\r\n");
                    fflush(stdout);
                    break; /* End of command. Parse it. */
                } else if (c == 0x08) { /* Backspace */
                    if (idx > 0) {
                        printf("%c", 0x08);
                        printf(" ");
                        printf("%c", 0x08);
                        fflush(stdout);
                        idx--;
                    }
                }
            }
        } while (idx < (CMD_BUFFER_SIZE - 1));
        if (idx > 0) {
            cmd[idx] = 0;
            if (parse_cmd(cmd) == -2) {
                printf("Unknown command: %s\r\n", cmd);
            }
        }
    }
}

void isr_usart3(void)
{
    volatile uint32_t reg;
    usr_led_on();
    reg = UART_ISR(UART3);
    if (reg & UART_ISR_RX_NOTEMPTY) {
        if (uart_rx_bytes >= 1023)
            reg = UART_RDR(UART3);
        else
            uart_buf_rx[uart_rx_bytes++] = (unsigned char)(UART_RDR(UART3) & 0xFF);
    }
}

static int uart_rx_isr(unsigned char *c, int len)
{
    uint32_t avail;
    UART_CR1(UART3) &= ~UART_ISR_RX_NOTEMPTY;
    if (len < 0) {
        len = 0;
    }
    avail = uart_rx_bytes - uart_processed;
    if ((uint32_t)len > avail) {
        len = (int)avail;
    }
    if (len > 0) {
        memcpy(c, uart_buf_rx + uart_processed, len);
        uart_processed += len;
        if (uart_processed >= uart_rx_bytes) {
            uart_processed = 0;
            uart_rx_bytes = 0;
            usr_led_off();
        }
    }
    UART_CR1(UART3) |= UART_ISR_RX_NOTEMPTY;
    return len;
}

static int uart_poll(void)
{
    return (uart_rx_bytes > uart_processed)?1:0;
}

void main(void)
{
    int ret;
    uint32_t app_version;

    /* Turn on boot LED */
    boot_led_on();

#ifdef WOLFBOOT_TEST_FILLER
    filler_data[FILLER_SIZE - 1] = 0xAA;
#endif

    /* Enable SysTick */
    systick_enable();

#ifdef WOLFCRYPT_SECURE_MODE
    app_version = wolfBoot_nsc_current_firmware_version();
#else
    app_version = wolfBoot_current_firmware_version();
#endif

    nvic_irq_setprio(NVIC_USART3_IRQN, 0);
    nvic_irq_enable(NVIC_USART3_IRQN);

    uart_init(115200, 8, 'N', 1);
    UART_CR1(UART3) |= UART_ISR_RX_NOTEMPTY;
    UART_CR3(UART3) |= UART_CR3_RXFTIE;

    printf("========================\r\n");
    printf("STM32H5 wolfBoot demo Application\r\n");
    printf("Copyright 2024 wolfSSL Inc\r\n");
    printf("GPL v3\r\n");
    printf("Version : 0x%lx\r\n", app_version);
    printf("========================\r\n");

#ifdef WOLFCRYPT_TZ_PSA
    ret = psa_crypto_init();
    if (ret == PSA_SUCCESS) {
        printf("PSA crypto init ok\r\n");
    } else {
        printf("PSA crypto init failed (%d)\r\n", ret);
    }
#endif

    cmd_info(NULL);
#ifdef WOLFBOOT_TPM
    cmd_tpm_info(NULL);
#endif

#if defined(WOLFBOOT_ATTESTATION_TEST) && defined(WOLFCRYPT_TZ_PSA)
    (void)run_attestation_test();
#endif

#ifdef WOLFCRYPT_TZ_PSA
    (void)run_psa_boot_attestation();
#endif

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

int WEAKFUNCTION _read(int file, char *ptr, int len)
{
  (void)file;
  int DataIdx;
  int ret;
  return -1;
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

#ifndef WOLFCRYPT_SECURE_MODE
/* Back-end for malloc, used for token handling */
extern unsigned int _start_heap; /* From linker script: heap memory */
extern unsigned int _heap_size;  /* From linker script: heap limit */

void * _sbrk(unsigned int incr)
{
    static unsigned char *heap = (unsigned char *)&_start_heap;
    static uint32_t heapsize = (uint32_t)(&_heap_size);
    void *old_heap = heap;
    if (((incr >> 2) << 2) != incr)
        incr = ((incr >> 2) + 1) << 2;

    if (heap == NULL)
        heap = (unsigned char *)&_start_heap;
    else
        heap += incr;
    if (((uint32_t)heap - (uint32_t)(&_start_heap)) > heapsize) {
        heap -= incr;
        return NULL;
    }
    return old_heap;
}
#endif
