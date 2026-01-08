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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "../hal/s32k1xx.h"
#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "target.h"
#include "image.h"

#ifdef TARGET_s32k1xx

/* ============== SysTick Timer ============== */

static volatile uint32_t systick_count = 0;

/* SysTick interrupt handler - called isr_systick to match startup_arm.c */
void isr_systick(void)
{
    systick_count++;
}

static uint32_t get_time_ms(void)
{
    return systick_count;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = systick_count;
    while ((systick_count - start) < ms) {
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

static void led_red_on(void)
{
    GPIOD_PCOR = (1UL << LED_PIN_RED);  /* Active low */
}

static void led_red_off(void)
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
extern void uart_init(void);
extern void uart_write(const char* buf, unsigned int sz);
extern int uart_read(char* c);

/* Simple printf-like function using wolfBoot_printf format */
#include <stdarg.h>

static void uart_putc(char c)
{
    uart_write(&c, 1);
}

static void uart_puts(const char* s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void print_hex_byte(uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(b >> 4) & 0x0F]);
    uart_putc(hex[b & 0x0F]);
}

static void print_hex32(uint32_t val)
{
    uart_puts("0x");
    print_hex_byte((val >> 24) & 0xFF);
    print_hex_byte((val >> 16) & 0xFF);
    print_hex_byte((val >> 8) & 0xFF);
    print_hex_byte(val & 0xFF);
}

static void print_dec(uint32_t val)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

/* Non-blocking character read */
static int uart_getc(char* c)
{
    return uart_read(c);
}

/* Blocking character read */
static char uart_getc_blocking(void)
{
    char c;
    while (uart_read(&c) <= 0) {
        __asm__ volatile ("wfi");
    }
    return c;
}
#endif /* DEBUG_UART */

/* ============== Partition State Names ============== */

static const char* part_state_name(uint8_t state, int valid)
{
    if (!valid) {
        return "(no trailer)";
    }
    switch (state) {
        case IMG_STATE_NEW:         return "NEW";
        case IMG_STATE_UPDATING:    return "UPDATING";
        case IMG_STATE_TESTING:     return "TESTING";
        case IMG_STATE_SUCCESS:     return "SUCCESS";
        default:                    return "UNKNOWN";
    }
}

/* ============== Key Type Names ============== */

static const char* key_type_name(uint32_t type)
{
    switch (type) {
        case AUTH_KEY_ED25519: return "Ed25519";
        case AUTH_KEY_ECC256:  return "ECDSA P-256 (secp256r1)";
        case AUTH_KEY_RSA2048: return "RSA-2048";
        case AUTH_KEY_RSA4096: return "RSA-4096";
        case AUTH_KEY_ED448:   return "Ed448";
        case AUTH_KEY_ECC384:  return "ECDSA P-384 (secp384r1)";
        case AUTH_KEY_ECC521:  return "ECDSA P-521 (secp521r1)";
        case AUTH_KEY_RSA3072: return "RSA-3072";
        case AUTH_KEY_LMS:     return "LMS";
        case AUTH_KEY_XMSS:    return "XMSS";
        default:               return "Unknown";
    }
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

    boot_state_valid = (wolfBoot_get_partition_state(PART_BOOT, &boot_state) == 0);
    update_state_valid = (wolfBoot_get_partition_state(PART_UPDATE, &update_state) == 0);

    uart_puts("\n=== Partition Information ===\n");

    uart_puts("Boot Partition:\n");
    uart_puts("  Address: ");
    print_hex32(WOLFBOOT_PARTITION_BOOT_ADDRESS);
    uart_puts("\n");
    uart_puts("  Version: ");
    print_dec(boot_ver);
    uart_puts("\n");
    uart_puts("  State:   ");
    uart_puts(part_state_name(boot_state, boot_state_valid));
    uart_puts("\n");

    uart_puts("Update Partition:\n");
    uart_puts("  Address: ");
    print_hex32(WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    uart_puts("\n");
    uart_puts("  Version: ");
    if (update_ver == 0) {
        uart_puts("(empty)");
    } else {
        print_dec(update_ver);
    }
    uart_puts("\n");
    uart_puts("  State:   ");
    uart_puts(part_state_name(update_state, update_state_valid));
    uart_puts("\n");

    uart_puts("Swap Partition:\n");
    uart_puts("  Address: ");
    print_hex32(WOLFBOOT_PARTITION_SWAP_ADDRESS);
    uart_puts("\n");
    uart_puts("  Size:    ");
    print_dec(WOLFBOOT_SECTOR_SIZE);
    uart_puts(" bytes\n");
}

static void print_keystore_info(void)
{
    uint32_t n_keys;
    int i;

    uart_puts("\n=== Keystore Information ===\n");

    /* Show configured signature and hash algorithms */
#ifdef WOLFBOOT_SIGN_ECC256
    uart_puts("Signature: ECDSA P-256 (secp256r1)\n");
#elif defined(WOLFBOOT_SIGN_ECC384)
    uart_puts("Signature: ECDSA P-384 (secp384r1)\n");
#elif defined(WOLFBOOT_SIGN_ECC521)
    uart_puts("Signature: ECDSA P-521 (secp521r1)\n");
#elif defined(WOLFBOOT_SIGN_ED25519)
    uart_puts("Signature: Ed25519\n");
#elif defined(WOLFBOOT_SIGN_ED448)
    uart_puts("Signature: Ed448\n");
#elif defined(WOLFBOOT_SIGN_RSA2048)
    uart_puts("Signature: RSA-2048\n");
#elif defined(WOLFBOOT_SIGN_RSA3072)
    uart_puts("Signature: RSA-3072\n");
#elif defined(WOLFBOOT_SIGN_RSA4096)
    uart_puts("Signature: RSA-4096\n");
#elif defined(WOLFBOOT_SIGN_LMS)
    uart_puts("Signature: LMS\n");
#elif defined(WOLFBOOT_SIGN_XMSS)
    uart_puts("Signature: XMSS\n");
#else
    uart_puts("Signature: Unknown\n");
#endif

#ifdef WOLFBOOT_HASH_SHA256
    uart_puts("Hash:      SHA-256\n");
#elif defined(WOLFBOOT_HASH_SHA384)
    uart_puts("Hash:      SHA-384\n");
#elif defined(WOLFBOOT_HASH_SHA512)
    uart_puts("Hash:      SHA-512\n");
#elif defined(WOLFBOOT_HASH_SHA3_384)
    uart_puts("Hash:      SHA3-384\n");
#else
    uart_puts("Hash:      Unknown\n");
#endif

    n_keys = keystore_num_pubkeys();
    uart_puts("Number of public keys: ");
    print_dec(n_keys);
    uart_puts("\n");

    for (i = 0; i < (int)n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint8_t* keybuf = keystore_get_buffer(i);
        int j;

        uart_puts("\nKey #");
        print_dec(i);
        uart_puts(":\n");
        uart_puts("  Algorithm: ");
        uart_puts(key_type_name(type));
        uart_puts("\n");
        uart_puts("  Size:      ");
        print_dec(size);
        uart_puts(" bytes\n");
        uart_puts("  Data:      ");

        /* Print first 16 bytes of key */
        for (j = 0; j < 16 && j < (int)size; j++) {
            print_hex_byte(keybuf[j]);
            uart_putc(' ');
        }
        if (size > 16) {
            uart_puts("...");
        }
        uart_puts("\n");
    }
}

/* ============== XMODEM Transfer ============== */

#define XSOH    0x01
#define XEOT    0x04
#define XACK    0x06
#define XNAK    0x15
#define XCAN    0x18

#define XMODEM_PAYLOAD_SIZE     128
#define XMODEM_PACKET_SIZE      (3 + XMODEM_PAYLOAD_SIZE + 1)
#define XMODEM_TIMEOUT_MS       1000

static uint8_t crc8_checksum(uint8_t* data, int len)
{
    uint8_t sum = 0;
    int i;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static void xmodem_cancel(void)
{
    int i;
    for (i = 0; i < 10; i++) {
        uart_putc(XCAN);
    }
}

static int cmd_update_xmodem(void)
{
    int ret = -1;
    uint8_t xpkt[XMODEM_PACKET_SIZE];
    uint8_t payload[XMODEM_PAYLOAD_SIZE];
    uint32_t dst_offset = 0;
    uint8_t pkt_num = 0, pkt_num_expected = 0xFF;
    uint32_t t_size = 0;
    uint32_t now;
    uint32_t i = 0;
    int transfer_started = 0;
    int eot_expected = 0;
    uint32_t erase_addr;
    int erase_ret;

    uart_puts("Erasing update partition...\n");
#ifdef DEBUG_FLASH
    uart_puts("  Address: ");
    print_hex32(WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    uart_puts("\n  Size:    ");
    print_hex32(WOLFBOOT_PARTITION_SIZE);
    uart_puts(" (");
    print_dec(WOLFBOOT_PARTITION_SIZE);
    uart_puts(" bytes)\n");
#endif

    hal_flash_unlock();

#ifdef DEBUG_FLASH
    /* Erase sector by sector with debug output */
    erase_addr = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    while (erase_addr < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE) {
        uart_puts("  Erasing sector at ");
        print_hex32(erase_addr);
        uart_puts("...");

        erase_ret = hal_flash_erase(erase_addr, WOLFBOOT_SECTOR_SIZE);
        if (erase_ret != 0) {
            uart_puts(" FAILED (");
            print_dec(erase_ret);
            uart_puts(")\n");
            hal_flash_lock();
            return -1;
        }
        uart_puts(" OK\n");
        erase_addr += WOLFBOOT_SECTOR_SIZE;
    }
#else
    (void)erase_addr;
    (void)erase_ret;
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
#endif

    uart_puts("Done.\n");

    uart_puts("Waiting for XMODEM transfer...\n");
    uart_puts("(Send file now using XMODEM protocol)\n");

    while (1) {
        now = get_time_ms();
        i = 0;

        /* Receive packet */
        while (i < XMODEM_PACKET_SIZE) {
            char c;
            int r = uart_getc(&c);
            if (r > 0) {
                xpkt[i++] = (uint8_t)c;
                now = get_time_ms();
                if (i == 1 && xpkt[0] == XEOT) {
                    break;  /* End of transmission */
                }
            } else {
                if (get_time_ms() > (now + XMODEM_TIMEOUT_MS)) {
                    now = get_time_ms();
                    if (i == 0) {
                        uart_putc(XNAK);  /* Request retransmit */
                    }
                    i = 0;
                } else {
                    __asm__ volatile ("wfi");
                }
            }
        }

        /* Check for EOT */
        if (xpkt[0] == XEOT) {
            uart_putc(XACK);
            led_red_on();  /* Indicate transfer complete */
            ret = 0;
            break;
        } else if (eot_expected) {
            uart_putc(XNAK);
            ret = -1;
            break;
        }

        /* Validate SOH */
        if (xpkt[0] != XSOH) {
            continue;
        }

        /* Validate packet number */
        pkt_num = xpkt[1];
        if ((uint8_t)(~xpkt[2]) == pkt_num) {
            if (!transfer_started) {
                pkt_num_expected = pkt_num;
                transfer_started = 1;
            } else if (pkt_num_expected != pkt_num) {
                uart_putc(XNAK);
                continue;
            }

            /* Toggle LED to show activity */
            if ((pkt_num & 0x0F) == 0) {
                led_red_on();
            } else if ((pkt_num & 0x0F) == 8) {
                led_red_off();
            }

            /* Validate checksum */
            uint8_t crc = xpkt[XMODEM_PACKET_SIZE - 1];
            uint8_t calc_crc = crc8_checksum(xpkt, XMODEM_PACKET_SIZE - 1);

            if (crc == calc_crc) {
                /* Write to flash */
                memcpy(payload, xpkt + 3, XMODEM_PAYLOAD_SIZE);
                ret = hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + dst_offset,
                                      payload, XMODEM_PAYLOAD_SIZE);
                if (ret != 0) {
                    xmodem_cancel();
                    uart_puts("Error: Flash write failed\n");
                    break;
                }

                uart_putc(XACK);
                pkt_num_expected++;
                dst_offset += XMODEM_PAYLOAD_SIZE;

                /* Get expected size from header */
                if (t_size == 0 && dst_offset >= 8) {
                    t_size = *(uint32_t*)(payload + 4) + IMAGE_HEADER_SIZE;
                }

                if (t_size > 0 && dst_offset >= t_size) {
                    eot_expected = 1;
                }
            } else {
                uart_putc(XNAK);
            }
        } else {
            uart_putc(XNAK);
        }
    }

    hal_flash_lock();

    uart_puts("\nTransfer ");
    if (ret == 0) {
        uart_puts("complete!\n");

        uint32_t update_ver = wolfBoot_update_firmware_version();
        if (update_ver != 0) {
            uart_puts("New firmware version: ");
            print_dec(update_ver);
            uart_puts("\n");
            uart_puts("Triggering update...\n");
            wolfBoot_update_trigger();
            uart_puts("Reboot to apply update.\n");
        } else {
            uart_puts("Warning: No valid image detected\n");
        }
    } else {
        uart_puts("failed.\n");
    }

    led_red_off();
    return ret;
}

/* ============== Console Commands ============== */

static void cmd_help(void);
static void cmd_info(void);
static void cmd_success(void);
static void cmd_reboot(void);
static void cmd_update(void);

typedef struct {
    const char* name;
    const char* help;
    void (*fn)(void);
} console_cmd_t;

static const console_cmd_t commands[] = {
    {"help",    "Show this help message",           cmd_help},
    {"info",    "Display partition and key info",   cmd_info},
    {"success", "Mark firmware as successful",      cmd_success},
    {"update",  "Update firmware via XMODEM",       cmd_update},
    {"reboot",  "Reboot the system",                cmd_reboot},
    {NULL, NULL, NULL}
};

static void cmd_help(void)
{
    int i;
    uart_puts("\nAvailable commands:\n");
    for (i = 0; commands[i].name != NULL; i++) {
        uart_puts("  ");
        uart_puts(commands[i].name);
        uart_puts(" - ");
        uart_puts(commands[i].help);
        uart_puts("\n");
    }
}

static void cmd_info(void)
{
    print_partition_info();
    print_keystore_info();
}

static void cmd_success(void)
{
    wolfBoot_success();
    uart_puts("Firmware marked as successful.\n");
}

static void cmd_reboot(void)
{
    uart_puts("Rebooting...\n");
    delay_ms(100);  /* Allow UART to flush */
    arch_reboot();
}

static void cmd_update(void)
{
    cmd_update_xmodem();
}

static void parse_command(const char* cmd)
{
    int i;
    for (i = 0; commands[i].name != NULL; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            commands[i].fn();
            return;
        }
    }
    uart_puts("Unknown command: ");
    uart_puts(cmd);
    uart_puts("\nType 'help' for available commands.\n");
}

#define CMD_BUF_SIZE 64

static void console_loop(uint32_t version)
{
    char cmd[CMD_BUF_SIZE];
    int idx;
    char c;

    (void)version;  /* LED is set once at startup, no toggling */

    while (1) {
        uart_puts("\ncmd> ");
        idx = 0;

        while (idx < CMD_BUF_SIZE - 1) {
            int ret = uart_getc(&c);
            if (ret > 0) {
                if (c == '\r' || c == '\n') {
                    uart_puts("\n");
                    break;
                } else if (c == 0x08 || c == 0x7F) {  /* Backspace */
                    if (idx > 0) {
                        uart_puts("\b \b");
                        idx--;
                    }
                } else if (c >= 32 && c < 127) {
                    uart_putc(c);
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

    /* Simple test message */
    uart_puts("Test App Started!\n");
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
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("S32K1xx wolfBoot Test Application\n");
    uart_puts("Copyright 2025 wolfSSL Inc.\n");
    uart_puts("========================================\n");
    uart_puts("Firmware Version: ");
    print_dec(version);
    uart_puts("\n");

    /* Auto-mark success for testing if version > 1 */
    if (version > 1) {
        uint8_t state = 0;
        wolfBoot_get_partition_state(PART_BOOT, &state);
        if (state == IMG_STATE_TESTING) {
            uart_puts("Testing state detected, marking success...\n");
            wolfBoot_success();
        }
    }

    /* Show initial info */
    print_partition_info();

    uart_puts("\nType 'help' for available commands.\n");

    /* Enter interactive console */
    console_loop(version);
#else
    /* No UART - just blink LED */
    while (1) {
        led_toggle_version(version);
        delay_ms(500);
    }
#endif
}

#endif /* TARGET_s32k1xx */
