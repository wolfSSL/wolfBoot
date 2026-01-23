/* app_s32k1xx.c
 *
 * Test bare-metal application for NXP S32K1xx
 * Features:
 * - LED indicator based on firmware version (Green=v1, Blue=v>1)
 * - Interactive console with commands
 * - XMODEM firmware update support
 * - Partition and keystore information display
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
#include "hal.h"
#include "../hal/s32k1xx.h"
#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "target.h"
#include "image.h"

#ifdef TARGET_s32k1xx

/* RAMFUNCTION for test-app: code that runs during flash operations must be in RAM */
#ifdef RAM_CODE
    #define APP_RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#else
    #define APP_RAMFUNCTION
#endif

/* ============== SysTick Timer ============== */

static volatile uint32_t jiffies = 0;

/* SysTick interrupt handler - called isr_systick to match startup_arm.c */
void isr_systick(void)
{
    jiffies++;
}

static uint32_t get_time_ms(void)
{
    return jiffies;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = jiffies;
    while ((jiffies - start) < ms) {
        __asm__ volatile ("wfi");
    }
}

static void systick_init(void)
{
    /* Configure SysTick for 1ms tick */
    SYST_RVR = (CLOCK_SPEED / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;
}

/* ============== LED Functions ============== */

static void led_init(void)
{
    /* Enable clock to PORTD */
    PCC_PORTD |= PCC_CGC;

    /* Configure LED pins as GPIO */
    PORTD_PCR0 = PORT_PCR_MUX_GPIO;   /* Blue LED */
    PORTD_PCR15 = PORT_PCR_MUX_GPIO;  /* Red LED */
    PORTD_PCR16 = PORT_PCR_MUX_GPIO;  /* Green LED */

    /* Set as outputs */
    GPIOD_PDDR |= (1UL << LED_PIN_BLUE) | (1UL << LED_PIN_RED) | (1UL << LED_PIN_GREEN);

    /* All LEDs off initially (active low) */
    GPIOD_PSOR = (1UL << LED_PIN_BLUE) | (1UL << LED_PIN_RED) | (1UL << LED_PIN_GREEN);
}

static void led_green_on(void)
{
    GPIOD_PCOR = (1UL << LED_PIN_GREEN);  /* Active low */
}

static void led_green_off(void)
{
    GPIOD_PSOR = (1UL << LED_PIN_GREEN);
}

static void led_blue_on(void)
{
    GPIOD_PCOR = (1UL << LED_PIN_BLUE);  /* Active low */
}

static void led_blue_off(void)
{
    GPIOD_PSOR = (1UL << LED_PIN_BLUE);
}

static void APP_RAMFUNCTION led_red_on(void)
{
    GPIOD_PCOR = (1UL << LED_PIN_RED);  /* Active low */
}

static void APP_RAMFUNCTION led_red_off(void)
{
    GPIOD_PSOR = (1UL << LED_PIN_RED);
}

static void led_toggle_version(uint32_t version)
{
    if (version == 1) {
        GPIOD_PTOR = (1UL << LED_PIN_GREEN);
    } else {
        GPIOD_PTOR = (1UL << LED_PIN_BLUE);
    }
}

/* Set LED based on version: Green for v1, Blue for v>1 */
static void led_set_version(uint32_t version)
{
    /* Turn off both first */
    led_green_off();
    led_blue_off();

    if (version == 1) {
        led_green_on();
    } else if (version > 1) {
        led_blue_on();
    }
}

/* ============== System Control ============== */

void arch_reboot(void)
{
    SCB_AIRCR = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
    while (1) {
        __asm__ volatile ("wfi");
    }
}

/* ============== UART / Printf Support ============== */

#ifdef DEBUG_UART
/* UART functions are declared in s32k1xx.h and implemented in hal/s32k1xx.c */

/* Flag to block text output during XMODEM transfer */
static volatile int xmodem_active = 0;

/* ============== UART RX Interrupt Buffering ============== */
#define UART_RX_BUF_SIZE    512
static volatile uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint32_t uart_rx_head = 0;  /* Write index (ISR writes here) */
static volatile uint32_t uart_rx_tail = 0;  /* Read index (app reads from here) */

/* LPUART1 RX Interrupt Handler */
void isr_lpuart1(void)
{
    uint32_t stat = LPUART1_STAT;

    /* Clear only the error flags (write 1 to clear) - do NOT write other bits */
    uint32_t errors = stat & (LPUART_STAT_OR | LPUART_STAT_NF | LPUART_STAT_FE | LPUART_STAT_PF);
    if (errors) {
        LPUART1_STAT = errors;  /* Write 1 to clear only error flags */
    }

    /* Read all available bytes from FIFO */
    while (LPUART1_STAT & LPUART_STAT_RDRF) {
        uint8_t c = (uint8_t)(LPUART1_DATA & 0xFF);
        uint32_t next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;

        /* Store byte if buffer not full */
        if (next_head != uart_rx_tail) {
            uart_rx_buf[uart_rx_head] = c;
            uart_rx_head = next_head;
        }
        /* else: buffer full, discard byte */
    }
}

/* Read from RX buffer (for XMODEM) - returns number of bytes read
 * Must be RAMFUNCTION since it's called during flash operations */
static int APP_RAMFUNCTION uart_rx_isr(uint8_t *buf, int max_len)
{
    int count = 0;

    while (count < max_len && uart_rx_tail != uart_rx_head) {
        buf[count++] = uart_rx_buf[uart_rx_tail];
        uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
    }

    return count;
}

/* Check if RX data available */
static int uart_rx_available(void)
{
    return (uart_rx_head != uart_rx_tail) ? 1 : 0;
}

/* Read single character from RX buffer (for console) - returns 1 if char read, 0 if none */
static int uart_getc(char *c)
{
    if (uart_rx_tail != uart_rx_head) {
        *c = uart_rx_buf[uart_rx_tail];
        uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
        return 1;
    }
    return 0;
}

/* Enable LPUART RX interrupt */
static void uart_rx_irq_enable(void)
{
    /* Set interrupt priority lower than SysTick (higher number = lower priority)
     * SysTick defaults to priority 0, so set LPUART to 2 to ensure
     * jiffies keeps incrementing even during heavy UART traffic. */
    NVIC_SetPriority(LPUART1_IRQn, 2);

    /* Enable LPUART1 interrupt in NVIC */
    NVIC_EnableIRQ(LPUART1_IRQn);

    /* Enable Receiver Interrupt in LPUART */
    LPUART1_CTRL |= LPUART_CTRL_RIE;
}

/* Print hex buffer (similar to stm32h5 style) */
#define LINE_LEN 16
static void print_hex(const uint8_t* buffer, uint32_t length, int dumpChars)
{
    uint32_t i, sz;

    if (!buffer) {
        printf("\tNULL\r\n");
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
#endif /* DEBUG_UART */

/* ============== Partition State Names ============== */

static const char* part_state_name(uint8_t state, int state_retval)
{
    if (state_retval == 0) {
        return "(no trailer)";
    }
    switch (state) {
        case IMG_STATE_NEW:      return "NEW";
        case IMG_STATE_UPDATING: return "UPDATING";
        case IMG_STATE_TESTING:  return "TESTING";
        case IMG_STATE_SUCCESS:  return "SUCCESS";
        default:                 return "UNKNOWN";
    }
}

/* ============== Key Type Names ============== */

static const char* key_type_name(uint32_t type)
{
    switch (type) {
        case AUTH_KEY_ECC256:  return "ECDSA P-256 (secp256r1)";
        case AUTH_KEY_ECC384:  return "ECDSA P-384 (secp384r1)";
        case AUTH_KEY_ECC521:  return "ECDSA P-521 (secp521r1)";
        case AUTH_KEY_RSA2048: return "RSA-2048";
        case AUTH_KEY_RSA3072: return "RSA-3072";
        case AUTH_KEY_RSA4096: return "RSA-4096";
        case AUTH_KEY_ED25519: return "Ed25519";
        case AUTH_KEY_ED448:   return "Ed448";
        case AUTH_KEY_LMS:     return "LMS";
        case AUTH_KEY_XMSS:    return "XMSS";
        case AUTH_KEY_ML_DSA:  return "ML-DSA";
        default:               return "Unknown";
    }
}

static const char* hash_type_name(void)
{
#ifdef WOLFBOOT_HASH_SHA256
    return "SHA-256";
#elif defined(WOLFBOOT_HASH_SHA384)
    return "SHA-384";
#elif defined(WOLFBOOT_HASH_SHA512)
    return "SHA-512";
#elif defined(WOLFBOOT_HASH_SHA3_384)
    return "SHA3-384";
#endif
    return "Unknown";
}

/* ============== Information Display ============== */

#ifdef DEBUG_UART
static void print_partition_info(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state = 0, update_state = 0;
    int boot_state_valid, update_state_valid;

    boot_ver = wolfBoot_current_firmware_version();
    update_ver = wolfBoot_update_firmware_version();

    boot_state_valid = wolfBoot_get_partition_state(PART_BOOT, &boot_state);
    update_state_valid = wolfBoot_get_partition_state(PART_UPDATE, &update_state);

    printf("\r\n=== Partition Information ===\r\n");

    printf("Boot Partition:\r\n");
    printf("  Address: 0x%08lX\r\n", (unsigned long)WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printf("  Version: %lu\r\n", (unsigned long)boot_ver);
    printf("  State:   %s\r\n", part_state_name(boot_state, boot_state_valid));

    printf("Update Partition:\r\n");
    printf("  Address: 0x%08lX\r\n", (unsigned long)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printf("  Version: %lu\r\n", (unsigned long)update_ver);
    printf("  State:   %s\r\n", part_state_name(update_state, update_state_valid));

    printf("Swap Partition:\r\n");
    printf("  Address: 0x%08lX\r\n", (unsigned long)WOLFBOOT_PARTITION_SWAP_ADDRESS);
    printf("  Size:    %lu bytes\r\n", (unsigned long)WOLFBOOT_SECTOR_SIZE);
}

static void print_keystore_info(void)
{
#ifndef WOLFBOOT_NO_SIGN
    uint32_t n_keys;
    int i;

    printf("\r\n=== Keystore Information ===\r\n");

    n_keys = keystore_num_pubkeys();
    printf("Number of public keys: %lu\r\n", (unsigned long)n_keys);
    printf("Hash: %s\r\n", hash_type_name());

    for (i = 0; i < (int)n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint8_t* keybuf = keystore_get_buffer(i);

        printf("\r\nKey #%d:\r\n", i);
        printf("  Algorithm: %s\r\n", key_type_name(type));
        printf("  Size:      %lu bytes\r\n", (unsigned long)size);
        printf("  Data:\r\n");
        print_hex(keybuf, size, 0);
    }
#else
    printf("\r\n=== Keystore Information ===\r\n");
    printf("Signing disabled (SIGN=NONE)\r\n");
#endif /* !WOLFBOOT_NO_SIGN */
}

/* ============== XMODEM Transfer ============== */

#define XSOH    0x01
#define XEOT    0x04
#define XACK    0x06
#define XNAK    0x15
#define XCAN    0x18
#define XCRC    'C'     /* Request CRC mode */

#define XMODEM_PAYLOAD_SIZE     128
#define XMODEM_PACKET_SIZE_CRC  (3 + XMODEM_PAYLOAD_SIZE + 2)  /* SOH + blk + ~blk + data + CRC16 */
#define XMODEM_TIMEOUT_MS       1000

/* CRC-16-CCITT for XMODEM-CRC mode */
static uint16_t APP_RAMFUNCTION crc16_ccitt(const uint8_t* data, int len)
{
    uint16_t crc = 0;
    int i, j;
    for (i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Raw byte transmit for XMODEM (declared in hal, runs from RAM) */
extern void uart_tx(uint8_t byte);

/* RAM-based memory copy for use during flash operations */
static void APP_RAMFUNCTION ram_memcpy(void *dst, const void *src, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (len--) {
        *d++ = *s++;
    }
}

static void APP_RAMFUNCTION xmodem_cancel(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        uart_tx(XCAN);
    }
}

/* XMODEM receive state - passed to RAM function */
typedef struct {
    uint32_t dst_offset;
    int result;
    /* Debug counters */
    uint32_t pkts_received;
    uint32_t pkts_crc_fail;
    uint32_t pkts_num_fail;
    uint32_t pkts_soh_fail;
    uint32_t timeouts;
} xmodem_state_t;

/* Core XMODEM-CRC receive loop - runs entirely from RAM during flash operations
 * Uses XMODEM-CRC mode (133-byte packets with 16-bit CRC)
 * Returns: 0 on success, -1 on error
 */
static int APP_RAMFUNCTION xmodem_receive_ram(xmodem_state_t *state)
{
    uint8_t xpkt[XMODEM_PACKET_SIZE_CRC];
    uint8_t payload[XMODEM_PAYLOAD_SIZE];
    uint8_t pkt_num = 0, pkt_num_expected = 0xFF;
    uint32_t t_size = 0;
    uint32_t now;
    uint32_t i = 0;
    int transfer_started = 0;
    int eot_expected = 0;
    int ret = -1;

    state->dst_offset = 0;
    state->result = -1;
    state->pkts_received = 0;
    state->pkts_crc_fail = 0;
    state->pkts_num_fail = 0;
    state->pkts_soh_fail = 0;
    state->timeouts = 0;

    /* Send 'C' to request CRC mode (XMODEM-CRC) */
    uart_tx(XCRC);

    while (1) {
        now = jiffies;  /* Direct access to volatile - faster than function call */
        i = 0;

        /* Receive packet - uses interrupt-buffered RX to avoid FIFO overflow */
        while (i < XMODEM_PACKET_SIZE_CRC) {
            int r = uart_rx_isr(&xpkt[i], XMODEM_PACKET_SIZE_CRC - i);
            if (r > 0) {
                i += r;
                now = jiffies;
                if (i >= 1 && xpkt[0] == XEOT) {
                    break;  /* End of transmission */
                }
            } else if (jiffies > (now + XMODEM_TIMEOUT_MS)) {
                now = jiffies;
                state->timeouts++;
                if (i == 0) {
                    uart_tx(XCRC);  /* Request CRC mode again */
                }
                i = 0;
            }
        }

        /* Check for EOT */
        if (xpkt[0] == XEOT) {
            uart_tx(XACK);
            led_red_on();  /* Indicate transfer complete */
            ret = 0;
            break;
        } else if (eot_expected) {
            uart_tx(XNAK);
            ret = -1;
            break;
        }

        /* Validate SOH */
        if (xpkt[0] != XSOH) {
            state->pkts_soh_fail++;
            continue;
        }
        state->pkts_received++;

        /* Validate packet number */
        pkt_num = xpkt[1];
        if ((uint8_t)(~xpkt[2]) == pkt_num) {
            uint16_t recv_crc, calc_crc;

            if (!transfer_started) {
                pkt_num_expected = pkt_num;
                transfer_started = 1;
            } else if (pkt_num_expected != pkt_num) {
                uart_tx(XNAK);
                continue;
            }

            /* Toggle LED to show activity */
            if ((pkt_num & 0x0F) == 0) {
                led_red_on();
            } else if ((pkt_num & 0x0F) == 8) {
                led_red_off();
            }

            /* Validate CRC-16 - XMODEM-CRC uses CRC over DATA bytes only */
            recv_crc = ((uint16_t)xpkt[XMODEM_PACKET_SIZE_CRC - 2] << 8) |
                       xpkt[XMODEM_PACKET_SIZE_CRC - 1];
            calc_crc = crc16_ccitt(xpkt + 3, XMODEM_PAYLOAD_SIZE);

            if (recv_crc == calc_crc) {
                /* Copy payload using RAM-based memcpy */
                ram_memcpy(payload, xpkt + 3, XMODEM_PAYLOAD_SIZE);

                /* Send ACK first, then write to flash.
                 * This allows sender to prepare next packet while we write.
                 * Risk: if write fails, we've already ACKed - but that's rare.
                 */
                uart_tx(XACK);

                /* Write to flash */
                ret = hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + state->dst_offset,
                                      payload, XMODEM_PAYLOAD_SIZE);
                if (ret != 0) {
                    xmodem_cancel();
                    /* No printf here - we're in RAM */
                    break;
                }
                pkt_num_expected++;
                state->dst_offset += XMODEM_PAYLOAD_SIZE;

                /* Get expected size from header (offset 4 = image size) */
                if (t_size == 0 && state->dst_offset >= 8) {
                    t_size = *(uint32_t*)(payload + 4) + IMAGE_HEADER_SIZE;
                }

                if (t_size > 0 && state->dst_offset >= t_size) {
                    eot_expected = 1;
                }
            } else {
                state->pkts_crc_fail++;
                uart_tx(XNAK);
            }
        } else {
            state->pkts_num_fail++;
            uart_tx(XNAK);
        }
    }

    state->result = ret;
    return ret;
}

static int cmd_update_xmodem(void)
{
    xmodem_state_t state;
    int ret;
    uint32_t erase_addr;
    int erase_ret;

    printf("Erasing update partition...\r\n");
#ifdef DEBUG_FLASH
    printf("  Address: 0x%08lX\r\n", (unsigned long)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printf("  Size:    0x%08lX (%lu bytes)\r\n",
           (unsigned long)WOLFBOOT_PARTITION_SIZE,
           (unsigned long)WOLFBOOT_PARTITION_SIZE);
#endif

    hal_flash_unlock();

#ifdef DEBUG_FLASH
    /* Erase sector by sector with debug output */
    erase_addr = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    while (erase_addr < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE) {
        printf("  Erasing sector at 0x%08lX...", (unsigned long)erase_addr);
        fflush(stdout);

        erase_ret = hal_flash_erase(erase_addr, WOLFBOOT_SECTOR_SIZE);
        if (erase_ret != 0) {
            printf(" FAILED (%d)\r\n", erase_ret);
            hal_flash_lock();
            return -1;
        }
        printf(" OK\r\n");
        erase_addr += WOLFBOOT_SECTOR_SIZE;
    }
#else
    (void)erase_addr;
    (void)erase_ret;
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
#endif

    printf("Done.\r\n");
    printf("Waiting for XMODEM transfer...\r\n");
    printf("(Send file now using XMODEM-CRC protocol)\r\n");

    /* Flush all printf output before starting XMODEM */
    fflush(stdout);
    /* Wait for UART TX to complete */
    while (!(LPUART1_STAT & LPUART_STAT_TC)) {}

    /* Drain any pending RX data before starting XMODEM */
    {
        char c;
        while (uart_getc(&c) > 0) {}  /* Use ISR buffer, not hardware */
    }

    /* Small delay to ensure clean start */
    delay_ms(100);

    /* Block all printf output during XMODEM */
    xmodem_active = 1;

    /* Run the receive loop from RAM */
    ret = xmodem_receive_ram(&state);

    /* Re-enable printf output */
    xmodem_active = 0;

    hal_flash_lock();

    /* Wait for sender to finish and drain any pending RX data.
     * This prevents printf output from mixing with XMODEM retransmits. */
    {
        char c;
        delay_ms(3000);  /* Wait for sender to give up */
        while (uart_read(&c) > 0) {}  /* Drain RX buffer */
    }

    printf("\r\nTransfer %s\r\n", (ret == 0) ? "complete!" : "failed.");
    printf("XMODEM stats: recv=%lu, crc_fail=%lu, num_fail=%lu, soh_fail=%lu, timeouts=%lu\r\n",
           (unsigned long)state.pkts_received, (unsigned long)state.pkts_crc_fail,
           (unsigned long)state.pkts_num_fail, (unsigned long)state.pkts_soh_fail,
           (unsigned long)state.timeouts);

    if (ret == 0) {
        uint32_t update_ver = wolfBoot_update_firmware_version();
        if (update_ver != 0) {
            printf("New firmware version: %lu\r\n", (unsigned long)update_ver);
            printf("Triggering update...\r\n");
            wolfBoot_update_trigger();
            printf("Reboot to apply update.\r\n");
        } else {
            printf("Warning: No valid image detected\r\n");
        }
    }

    led_red_off();
    return ret;
}

/* ============== Console Commands ============== */

static int cmd_help(const char *args);
static int cmd_info(const char *args);
static int cmd_success(const char *args);
static int cmd_reboot(const char *args);
static int cmd_update(const char *args);
static int cmd_timestamp(const char *args);
static int cmd_trigger(const char *args);
static int cmd_status(const char *args);

typedef struct {
    int (*fn)(const char *args);
    const char* name;
    const char* help;
} console_cmd_t;

static const console_cmd_t commands[] = {
    {cmd_help,      "help",     "Show this help message"},
    {cmd_info,      "info",     "Display partition and key info"},
    {cmd_status,    "status",   "Show partition versions and states"},
    {cmd_success,   "success",  "Mark firmware as successful"},
    {cmd_trigger,   "trigger",  "Trigger update (if update image in flash)"},
    {cmd_update,    "update",   "Update firmware via XMODEM"},
    {cmd_timestamp, "timestamp", "Show current system time"},
    {cmd_reboot,    "reboot",   "Reboot the system"},
    {NULL, NULL, NULL}
};

static int cmd_help(const char *args)
{
    int i;
    (void)args;
    printf("\r\nAvailable commands:\r\n");
    for (i = 0; commands[i].name != NULL; i++) {
        printf("  %s - %s\r\n", commands[i].name, commands[i].help);
    }
    return 0;
}

static int cmd_info(const char *args)
{
    (void)args;
    print_partition_info();
    print_keystore_info();
    return 0;
}

static int cmd_success(const char *args)
{
    (void)args;
    wolfBoot_success();
    printf("Firmware marked as successful.\r\n");
    return 0;
}

static int cmd_timestamp(const char *args)
{
    (void)args;
    printf("Current systick: %lu ms\r\n", (unsigned long)jiffies);
    return 0;
}

static int cmd_status(const char *args)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state, update_state;
    int ret;
    (void)args;

    boot_ver = wolfBoot_current_firmware_version();
    update_ver = wolfBoot_update_firmware_version();

    printf("\r\n=== Partition Status ===\r\n");
    printf("Boot Partition:   v%lu @ 0x%lX\r\n",
           (unsigned long)boot_ver, (unsigned long)WOLFBOOT_PARTITION_BOOT_ADDRESS);

    ret = wolfBoot_get_partition_state(PART_BOOT, &boot_state);
    if (ret == 0) {
        printf("  State: %s (0x%02X)\r\n",
               (boot_state == IMG_STATE_SUCCESS) ? "SUCCESS" :
               (boot_state == IMG_STATE_TESTING) ? "TESTING" :
               (boot_state == IMG_STATE_UPDATING) ? "UPDATING" : "NEW",
               boot_state);
    } else {
        printf("  State: (no trailer)\r\n");
    }

    printf("Update Partition: v%lu @ 0x%lX\r\n",
           (unsigned long)update_ver, (unsigned long)WOLFBOOT_PARTITION_UPDATE_ADDRESS);

    ret = wolfBoot_get_partition_state(PART_UPDATE, &update_state);
    if (ret == 0) {
        printf("  State: %s (0x%02X)\r\n",
               (update_state == IMG_STATE_SUCCESS) ? "SUCCESS" :
               (update_state == IMG_STATE_TESTING) ? "TESTING" :
               (update_state == IMG_STATE_UPDATING) ? "UPDATING" : "NEW",
               update_state);
    } else {
        printf("  State: (no trailer)\r\n");
    }

    if (update_ver > 0 && update_ver > boot_ver) {
        printf("\r\nUpdate available! Use 'trigger' command to start update.\r\n");
    }

    return 0;
}

static int cmd_trigger(const char *args)
{
    uint32_t update_ver;
    (void)args;

    update_ver = wolfBoot_update_firmware_version();
    if (update_ver == 0) {
        printf("No update image found in update partition.\r\n");
        return -1;
    }

    printf("Update image version: %lu\r\n", (unsigned long)update_ver);
    printf("Triggering update...\r\n");
    wolfBoot_update_trigger();
    printf("Update triggered. Use 'reboot' to start update.\r\n");
    return 0;
}

static int cmd_reboot(const char *args)
{
    (void)args;
    printf("Rebooting...\r\n");
    fflush(stdout);
    delay_ms(100);  /* Allow UART to flush */
    arch_reboot();
    return 0;
}

static int cmd_update(const char *args)
{
    (void)args;
    return cmd_update_xmodem();
}

static int parse_command(const char* cmd)
{
    int i;
    for (i = 0; commands[i].name != NULL; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            return commands[i].fn(NULL);
        }
    }
    printf("Unknown command: %s\r\n", cmd);
    printf("Type 'help' for available commands.\r\n");
    return -1;
}

#define CMD_BUF_SIZE 64

static void console_loop(void)
{
    char cmd[CMD_BUF_SIZE];
    int idx;
    char c;

    while (1) {
        printf("\r\ncmd> ");
        fflush(stdout);
        idx = 0;

        while (idx < CMD_BUF_SIZE - 1) {
            int ret = uart_getc(&c);
            if (ret > 0) {
                if (c == '\r' || c == '\n') {
                    printf("\r\n");
                    break;
                } else if (c == 0x08 || c == 0x7F) {  /* Backspace */
                    if (idx > 0) {
                        printf("\b \b");
                        fflush(stdout);
                        idx--;
                    }
                } else if (c >= 32 && c < 127) {
                    printf("%c", c);
                    fflush(stdout);
                    cmd[idx++] = c;
                }
            }
            /* No delay - tight polling loop for responsive input */
        }

        cmd[idx] = '\0';
        if (idx > 0) {
            parse_command(cmd);
        }
    }
}
#endif /* DEBUG_UART */

/* ============== Clock Functions ============== */

/* Ensure FIRC (48 MHz) is enabled for UART */
static void clock_ensure_firc(void)
{
    /* Check if FIRC is valid */
    if (!(SCG_FIRCCSR & SCG_FIRCCSR_FIRCVLD)) {
        /* Enable FIRC if not already enabled */
        SCG_FIRCDIV = (1UL << 8) | (1UL << 0);  /* FIRCDIV1=/1, FIRCDIV2=/1 */
        SCG_FIRCCFG = 0;  /* Range 0: 48 MHz */
        SCG_FIRCCSR = SCG_FIRCCSR_FIRCEN;

        /* Wait for FIRC valid */
        while (!(SCG_FIRCCSR & SCG_FIRCCSR_FIRCVLD)) {}
    }

    /* Ensure system is running from FIRC */
    if ((SCG_CSR & SCG_CSR_SCS_MASK) != SCG_CSR_SCS_FIRC) {
        SCG_RCCR = SCG_xCCR_SCS_FIRC |
                   (0UL << SCG_xCCR_DIVCORE_SHIFT) |
                   (0UL << SCG_xCCR_DIVBUS_SHIFT) |
                   (1UL << SCG_xCCR_DIVSLOW_SHIFT);

        /* Wait for clock switch */
        while ((SCG_CSR & SCG_CSR_SCS_MASK) != SCG_CSR_SCS_FIRC) {}
    }
}

/* ============== Main Entry Point ============== */

void main(void)
{
    uint32_t version;

    /* Disable watchdog - bootloader may have enabled it */
    WDOG_CNT = WDOG_CNT_UNLOCK;
    while (!(WDOG_CS & WDOG_CS_ULK)) {}
    WDOG_TOVAL = 0xFFFF;
    WDOG_CS = WDOG_CS_UPDATE | WDOG_CS_CMD32EN | WDOG_CS_CLK_LPO;  /* Disabled, but updatable */
    while (!(WDOG_CS & WDOG_CS_RCS)) {}

    /* Ensure FIRC clock is running at 48 MHz for UART */
    clock_ensure_firc();

#ifdef DEBUG_UART
    /* Reinitialize UART - bootloader may have changed settings in hal_prepare_boot */
    uart_init();
    /* Enable interrupt-based RX buffering for reliable XMODEM transfers */
    uart_rx_irq_enable();
    /* Disable stdout buffering to prevent delayed output during XMODEM */
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    /* Initialize test-app hardware */
    systick_init();
    led_init();

    /* Enable interrupts */
    __asm__ volatile ("cpsie i");

    /* Get current firmware version */
    version = wolfBoot_current_firmware_version();

    /* Set LED based on version: Green for v1, Blue for v>1 */
    led_set_version(version);

#ifdef DEBUG_UART
    printf("\r\n");
    printf("========================================\r\n");
    printf("S32K1xx wolfBoot Test Application\r\n");
    printf("Copyright 2025 wolfSSL Inc.\r\n");
    printf("========================================\r\n");
    printf("Firmware Version: %lu\r\n", (unsigned long)version);

    /* Auto-mark success for testing if version > 1 */
    if (version > 1) {
        uint8_t state = 0;
        wolfBoot_get_partition_state(PART_BOOT, &state);
        if (state == IMG_STATE_TESTING) {
            printf("Testing state detected, marking success...\r\n");
            wolfBoot_success();
        }
    }

    /* Show initial info */
    print_partition_info();

    printf("\r\nType 'help' for available commands.\r\n");

    /* Enter interactive console */
    console_loop();
#else
    /* No UART - just blink LED */
    while (1) {
        led_toggle_version(version);
        delay_ms(500);
    }
#endif
}

/* ============== Syscalls for printf support ============== */

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

void _exit(int status)
{
    _kill(status, -1);
    while (1) {}
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return -1;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
#ifdef DEBUG_UART
    /* Block text output during XMODEM to prevent protocol interference */
    if (!xmodem_active) {
        uart_write(ptr, len);
    }
#else
    (void)ptr;
#endif
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

/* Back-end for malloc, used for printf */
extern unsigned int _end;  /* From linker script: end of BSS */
extern unsigned int _end_stack;  /* From linker script: end of RAM */

void *_sbrk(int incr)
{
    static unsigned char *heap = NULL;
    unsigned char *prev_heap;

    if (heap == NULL) {
        heap = (unsigned char *)&_end;
    }

    prev_heap = heap;

    /* Align increment to 4 bytes */
    if (((incr >> 2) << 2) != incr)
        incr = ((incr >> 2) + 1) << 2;

    /* Check we don't overflow into the stack */
    if ((heap + incr) > (unsigned char *)&_end_stack) {
        return (void *)-1;
    }

    heap += incr;
    return prev_heap;
}

#endif /* TARGET_s32k1xx */
