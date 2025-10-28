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
#include "hal/stm32h5.h"
#include "uart_drv.h"
#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "target.h"
#include "image.h"

#ifdef SECURE_PKCS11
#include "wcs/user_settings.h"
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/wc_pkcs11.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfcrypt/benchmark/benchmark.h"
#include "wolfcrypt/test/test.h"
extern const char pkcs11_library_name[];
extern const CK_FUNCTION_LIST wolfpkcs11nsFunctionList;
#endif

volatile unsigned int jiffies = 0;

/* Usart irq-based read function */
static uint8_t uart_buf_rx[1024];
static uint32_t uart_rx_bytes = 0;
static uint32_t uart_processed = 0;
static int uart_rx_isr(unsigned char *c, int len);
static int uart_poll(void);

#define LED_BOOT_PIN (4) /* PG4 - Nucleo - Red Led */
#define LED_USR_PIN  (0) /* PB0 - Nucleo - Green Led */
#define LED_EXTRA_PIN  (4) /* PF4 - Nucleo - Orange Led */

#define NVIC_USART3_IRQN (60)

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
static int cmd_update(const char *args);
static int cmd_update_xmodem(const char *args);
static int cmd_reboot(const char *args);



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
     { cmd_update_xmodem, "update", "update the firmware via XMODEM"},
     { cmd_reboot, "reboot", "reboot the system"},
     { NULL, "", ""}
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
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

#define XMODEM_PAYLOAD_SIZE 128
#define XMODEM_PACKET_SIZE (3 + XMODEM_PAYLOAD_SIZE + 1)
#define XMODEM_TIMEOUT   1000 /* milliseconds */

static void xcancel(void)
{
    int i;
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
    wolfBoot_nsc_erase_update(dst_offset, WOLFBOOT_PARTITION_SIZE);
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
                ret = wolfBoot_nsc_write_update(dst_offset, xpkt_payload, XMODEM_PAYLOAD_SIZE);
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
                /*uart_tx(XACK);*/
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
        update_ver = wolfBoot_nsc_update_firmware_version();
        if (update_ver != 0) {
            printf("New firmware version: 0x%lx\r\n", update_ver);
            printf("Triggering update...\r\n");
            wolfBoot_nsc_update_trigger();
            printf("Update written successfully. Reboot to apply.\r\n");
        } else {
            printf("No valid image in update partition\r\n");
        }
    }

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

static int cmd_info(const char *args)
{
    int i, j;
    uint32_t cur_fw_version, update_fw_version;
    uint32_t n_keys;
    uint16_t hdrSz;
    uint8_t boot_part_state = IMG_STATE_NEW, update_part_state = IMG_STATE_NEW;

    cur_fw_version = wolfBoot_nsc_current_firmware_version();
    update_fw_version = wolfBoot_nsc_update_firmware_version();

    wolfBoot_nsc_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_nsc_get_partition_state(PART_UPDATE, &update_part_state);

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
    wolfBoot_nsc_success();
    printf("update success confirmed.\r\n");
    return 0;
}

static int cmd_random(const char *args)
{
#ifdef WOLFCRYPT_SECURE_MODE
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
    printf("Current timestamp: %llu.%03lu\r\n", tp.tv_sec, tp.tv_nsec/1000000);
    printf("Current systick: %u\r\n", jiffies);
    printf("VTOR: %08lx\r\n", (*(volatile uint32_t *)(0xE000ED08)));
    return 0;
}

static int cmd_login_pkcs11(const char *args)
{
    int ret = -1;
#ifdef SECURE_PKCS11
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
    if (ret == 0) {
        printf("PKCS11 initialization completed successfully.\r\n");
        pkcs11_initialized = 1;
    }
#else
    printf("Feature only supported with WOLFCRYPT_TZ=1\n");
#endif /* SECURE_PKCS11 */
    return ret;
}

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
    UART_CR1(UART3) &= ~UART_ISR_RX_NOTEMPTY;
    if (len > (uart_rx_bytes - uart_processed))
        len = (uart_rx_bytes - uart_processed);
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

    /* Enable SysTick */
    systick_enable();

    app_version = wolfBoot_nsc_current_firmware_version();

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
