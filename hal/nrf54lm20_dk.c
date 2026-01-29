/* nrf54lm20_dk.c
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

#ifdef TARGET_nrf54lm20

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hal.h"
#include "image.h"
#include "nrf54lm20.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

#define FLASH_TEST_SECTOR       WOLFBOOT_PARTITION_SWAP_ADDRESS
#define FLASH_TEST_SECTOR_SIZE  FLASH_PAGE_SIZE

#if USE_PMIC_LED
static bool pmic_led_power_ready;

static void led_power_gpio_init(void)
{
    const uint32_t mask = (1U << LED_PWR_CTRL_PIN);

    GPIO_PIN_CNF(LED_PWR_CTRL_PORT, LED_PWR_CTRL_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0);
    GPIO_DIRSET(LED_PWR_CTRL_PORT) = mask;
    GPIO_OUTCLR(LED_PWR_CTRL_PORT) = mask;
}

static void led_power_gpio_set(bool enable)
{
    const uint32_t mask = (1U << LED_PWR_CTRL_PIN);
    if (enable)
        GPIO_OUTSET(LED_PWR_CTRL_PORT) = mask;
    else
        GPIO_OUTCLR(LED_PWR_CTRL_PORT) = mask;
}

static void pmic_twi_init(void)
{
    static int initialized;
    if (initialized)
        return;

    const uintptr_t twim = PMIC_TWIM_BASE;
    const uint32_t scl_mask = (1U << PMIC_TWIM_SCL_PIN);
    const uint32_t sda_mask = (1U << PMIC_TWIM_SDA_PIN);
    const uint32_t line_mask = scl_mask | sda_mask;

    GPIO_PIN_CNF(PMIC_TWIM_PORT, PMIC_TWIM_SCL_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_PULL_UP);
    GPIO_PIN_CNF(PMIC_TWIM_PORT, PMIC_TWIM_SDA_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_PULL_UP);
    GPIO_DIRSET(PMIC_TWIM_PORT) = line_mask;
    GPIO_OUTSET(PMIC_TWIM_PORT) = line_mask;

    TWIM_ENABLE_REG(twim) = TWIM_ENABLE_ENABLE_Disabled;
    TWIM_PSEL_SCL_REG(twim) = (PSEL_PORT(PMIC_TWIM_PORT) | PMIC_TWIM_SCL_PIN);
    TWIM_PSEL_SDA_REG(twim) = (PSEL_PORT(PMIC_TWIM_PORT) | PMIC_TWIM_SDA_PIN);
    TWIM_FREQUENCY_REG(twim) = TWIM_FREQUENCY_FREQUENCY_K100;
    TWIM_ADDRESS_REG(twim) = PMIC_I2C_ADDRESS;
    TWIM_DMA_RX_TERMINATE(twim) = TWIM_DMA_RX_TERMINATEONBUSERROR_ENABLE_Enabled;
    TWIM_DMA_TX_TERMINATE(twim) = TWIM_DMA_TX_TERMINATEONBUSERROR_ENABLE_Enabled;
    TWIM_ENABLE_REG(twim) = TWIM_ENABLE_ENABLE_Enabled;

    initialized = 1;
}

static int pmic_twi_wait_stopped(void)
{
    const uintptr_t twim = PMIC_TWIM_BASE;

    for (uint32_t guard = 0; guard < PMIC_TWIM_TIMEOUT; guard++) {
        if (TWIM_EVENTS_STOPPED(twim) != 0U) {
            TWIM_EVENTS_STOPPED(twim) = 0;
            TWIM_EVENTS_ERROR(twim) = 0;
            return 0;
        }
        if (TWIM_EVENTS_ERROR(twim) != 0U) {
            uint32_t err = TWIM_ERRORSRC_REG(twim);
            TWIM_ERRORSRC_REG(twim) = err;
            TWIM_EVENTS_ERROR(twim) = 0;
            TWIM_TASKS_STOP(twim) = TWIM_TASKS_STOP_TASKS_STOP_Trigger;
            return -1;
        }
    }

    TWIM_TASKS_STOP(twim) = TWIM_TASKS_STOP_TASKS_STOP_Trigger;
    return -1;
}

static int pmic_twi_xfer(const uint8_t* tx, size_t tx_len,
                         uint8_t* rx, size_t rx_len)
{
    const uintptr_t twim = PMIC_TWIM_BASE;

    if (((tx_len > 0U) && (tx == NULL)) ||
        ((rx_len > 0U) && (rx == NULL)) ||
        ((tx_len == 0U) && (rx_len == 0U)))
        return -1;

    TWIM_EVENTS_STOPPED(twim) = 0;
    TWIM_EVENTS_ERROR(twim) = 0;
    TWIM_EVENTS_LASTTX(twim) = 0;
    TWIM_EVENTS_LASTRX(twim) = 0;
    TWIM_EVENTS_DMA_TX_END(twim) = 0;
    TWIM_EVENTS_DMA_RX_END(twim) = 0;
    TWIM_SHORTS_REG(twim) = 0;

    if (tx_len > 0U) {
        TWIM_DMA_TX_PTR(twim) = (uint32_t)tx;
        TWIM_DMA_TX_MAXCNT(twim) = (uint32_t)tx_len;
    }
    if (rx_len > 0U) {
        TWIM_DMA_RX_PTR(twim) = (uint32_t)rx;
        TWIM_DMA_RX_MAXCNT(twim) = (uint32_t)rx_len;
    }

    if ((tx_len > 0U) && (rx_len > 0U)) {
        TWIM_SHORTS_REG(twim) = (TWIM_SHORTS_LASTTX_DMA_RX_START_Msk |
                                 TWIM_SHORTS_LASTRX_STOP_Msk);
        TWIM_TASKS_DMA_TX_START(twim) = TWIM_TASKS_DMA_TX_START_START_Trigger;
    }
    else if (rx_len > 0U) {
        TWIM_SHORTS_REG(twim) = TWIM_SHORTS_LASTRX_STOP_Msk;
        TWIM_TASKS_DMA_RX_START(twim) = TWIM_TASKS_DMA_RX_START_START_Trigger;
    }
    else {
        TWIM_SHORTS_REG(twim) = TWIM_SHORTS_LASTTX_STOP_Msk;
        TWIM_TASKS_DMA_TX_START(twim) = TWIM_TASKS_DMA_TX_START_START_Trigger;
    }

    if (pmic_twi_wait_stopped() != 0) {
        TWIM_SHORTS_REG(twim) = 0;
        return -1;
    }

    TWIM_SHORTS_REG(twim) = 0;
    return 0;
}

static int npm1300_reg_write(uint16_t reg, const uint8_t* data, size_t len)
{
    uint8_t frame[2U + PMIC_REG_PAYLOAD_MAX];

    if ((data == NULL) || (len == 0U) || (len > PMIC_REG_PAYLOAD_MAX))
        return -1;

    frame[0] = (uint8_t)((reg >> 8) & 0xFFU);
    frame[1] = (uint8_t)(reg & 0xFFU);
    memcpy(&frame[2], data, len);
    return pmic_twi_xfer(frame, len + 2U, NULL, 0);
}

static int npm1300_reg_write_u8(uint16_t reg, uint8_t value)
{
    return npm1300_reg_write(reg, &value, 1U);
}

void pmic_led_power_control(bool enable)
{
    if (!pmic_led_power_ready)
        return;

    led_power_gpio_set(enable);
    if (enable)
        (void)npm1300_reg_write_u8(NPM1300_REG_TASK_LDSW2_SET, 0x01);
    else
        (void)npm1300_reg_write_u8(NPM1300_REG_TASK_LDSW2_CLR, 0x01);
}

int npm1300_configure_led_power(void)
{
    int ret;

    pmic_led_power_ready = false;
    pmic_twi_init();
    led_power_gpio_init();

    ret = npm1300_reg_write_u8(NPM1300_REG_LDSW2LDOSEL, 0x00);
    if (ret != 0)
        return ret;

    /* Soft-start load switch 2 at 50mA to drive the LED rail reliably */
    ret = npm1300_reg_write_u8(NPM1300_REG_LDSWCONFIG, (uint8_t)(3U << 4));
    if (ret != 0)
        return ret;

    ret = npm1300_reg_write_u8(NPM1300_REG_GPIOMODE(1), 0x00);
    if (ret != 0)
        return ret;

    ret = npm1300_reg_write_u8(NPM1300_REG_GPIOPUEN(1), 0x00);
    if (ret != 0)
        return ret;

    ret = npm1300_reg_write_u8(NPM1300_REG_GPIOPDEN(1), 0x00);
    if (ret != 0)
        return ret;

    ret = npm1300_reg_write_u8(NPM1300_REG_LDSW2_GPISEL, 0x02);
    if (ret != 0)
        return ret;

    /* Ensure the LED rail starts in the OFF state. */
    (void)npm1300_reg_write_u8(NPM1300_REG_TASK_LDSW2_CLR, 0x01);

    pmic_led_power_ready = true;
    return 0;
}

static void get_led_port_pin(int lednum, int *port, int *pin)
{
    switch(lednum)
    {
        case 0: *port = 1; *pin = 22; break;
        case 1: *port = 1; *pin = 25; break;
        case 2: *port = 1; *pin = 27; break;
        case 3: *port = 1; *pin = 28; break;
        default: *port = 1; *pin = 22; break;
    }
}

void board_status_led_blink(int numLoops)
{
    int port, pin;

    const uint32_t toggle_delay_ms = 500U;
    const int led_flash_loops = numLoops;

    // setup GPIOs
    for(int i=0; i<4; i++)
    {
        get_led_port_pin(i, &port, &pin);
        GPIO_PIN_CNF(port, pin) = (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_STD_DRIVE_1);
        GPIO_DIRSET(port) = 1U << pin;
        GPIO_OUTCLR(port) = 1U << pin;
    }

    for(int i=0; i<led_flash_loops; i++)
    {
        monitor_write("\nLED Loop #");
        monitor_write_uint((unsigned int)i+1);

        // flash each of the 4 LEDs in succession
        for (int j = 0; j < 4; j++)
        {
            monitor_write("\n  LED #");
            monitor_write_uint((unsigned int) j);
            get_led_port_pin(j, &port, &pin);

            GPIO_OUTSET(port) = 1U << pin;
            sleep_ms(toggle_delay_ms);

            GPIO_OUTCLR(port) = 1U << pin;
            sleep_ms(toggle_delay_ms);
        }
    }
}
#endif /* USE_PMIC_LED */

#if USE_MONITOR

#define MAX_CLI_PARAMS          10

extern int uart_read(int device, uint8_t* buf, unsigned int sz);

const char hexascii[] = "0123456789ABCDEF";

void bits_to_hexascii(int bits, uint32_t value, char *output)
{
    int nybble = 0;

    while(bits > 0)
    {
        if(bits >= 4)
            bits -= 4;
        output[nybble++] = hexascii[((value >> bits) & 0x0F)];
    }
    output[nybble] = 0;
}

int scan_decimal(char *str)
{
    int value = 0;

    while(*str != 0)
    {
        char ch = *str++;
        value *= 10;
        if(ch >= '0' && ch <= '9')
            value += (uint32_t)(ch - '0');
    }

    return value;
}

uint32_t scan_hexadecimal(char *str)
{
    uint32_t value = 0;

    while(*str != 0)
    {
        char ch = *str++;
        value *= 16;
        if(ch >= '0' && ch <= '9')
            value += (uint32_t)(ch - '0');
        else if(ch >= 'a' && ch <= 'f')
            value += (uint32_t)(ch - 'a' + 10);
        else if(ch >= 'A' && ch <= 'F')
            value += (uint32_t)(ch - 'A' + 10);
    }

    return value;
}

void flash_dump(uint32_t address, int length)
{
    uint32_t addr = address;
    uint8_t byte = 0;
    char buffer[10];
    char text[16+1];

    monitor_write("\n");
    length = length % 16 == 0 ? length/16 : length/16+1;
    length = length == 0 ? 16 : length;
    for(int i=0; i<length; i++)
    {
        bits_to_hexascii(32, addr, buffer);
        monitor_write(buffer);
        monitor_write(" : ");
        for(int j=0; j<16; j++)
        {
            byte = *((uint32_t *) addr);
            if(byte >= 32 && byte < 127)
                text[j] = byte;
            else
                text[j] = '.';
            bits_to_hexascii(8, byte, buffer);
            monitor_write(buffer);
            monitor_write(" ");
            ++addr;
        }
        text[16] = 0;
        monitor_write(" : ");
        monitor_write(text);
        monitor_write("\n");
    }
}

static const char test_data[] = "This is some test data. Can you read it?";

void flash_test(void)
{
    uint32_t address = FLASH_TEST_SECTOR;

    int rc = 0;
    rc = hal_flash_erase(address, FLASH_TEST_SECTOR_SIZE);
    rc = hal_flash_write(address, (const uint8_t *)test_data, sizeof(test_data));
    (void)rc;
}

void flash_erase(void)
{
    uint32_t address = FLASH_TEST_SECTOR;

    int rc = hal_flash_erase(address, FLASH_TEST_SECTOR_SIZE);
    (void)rc;
}

void flash_show(void)
{
    uint32_t address = FLASH_TEST_SECTOR;
    int length = 256;

    flash_dump(address, length);
}
            
static int parse_command_line( char *cmdline, int *argc, char *argv[] )
{
   char  *cp;
   int   cnt;

   cnt = 0;

   cp = strtok( cmdline, " \t" );

   do
   {
      if( cp )
         argv[cnt++] = cp;
      else
         break;
      cp = strtok( NULL, " \t" );

   } while( cnt < MAX_CLI_PARAMS );

   *argc = cnt;

   return cnt;
}

void monitor_write(const char* s)
{
    if (s != NULL)
        uart_write(s, (unsigned int)strlen(s));
}

void monitor_write_uint(uint32_t value)
{
    char tmp[12];
    int pos = (int)sizeof(tmp) - 1;
    tmp[pos--] = '\0';
    do {
        tmp[pos--] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && pos >= 0);
    monitor_write(&tmp[pos + 1]);
}

static int argc;
static char *argv[MAX_CLI_PARAMS];

static int monitor_handle_command(char* line)
{
    if ((line == NULL) || (*line == '\0'))
        return 0;

    parse_command_line( line, &argc, argv );
    char *cmd  = argv[0];
    char *arg1 = argv[1];
    char *arg2 = argv[2];

    if (strcmp(cmd, "help") == 0) {
        monitor_write("\nCommands:\n");
        monitor_write("  help        - show this message\n");
        monitor_write("  version     - print current firmware version\n");
#if USE_PMIC_LED
        monitor_write("  led [count] - flash LEDs\n");
#endif
        monitor_write("  dump <addr> [len] - dump flash\n");
        monitor_write("  flash <cmd> - flash commands:\n");
        monitor_write("     write    - write test block to flash\n");
        monitor_write("     erase    - erase test block in flash\n");
        monitor_write("     show     - show test block in flash\n");

        monitor_write("  reboot      - restart the system\n");
        monitor_write("  exit        - return to code that started the monitor\n");
    }
    else if (strcmp(cmd, "flash") == 0) {
        if(argc >= 2) {
            if(strcmp(arg1, "write")==0)
               flash_test();
            else if(strcmp(arg1, "erase")==0)
               flash_erase();
            else if(strcmp(arg1, "show")==0)
               flash_show();
        }
    }
    else if (strcmp(cmd, "dump") == 0) {
        uint32_t addr = 0;
        int len = 0;
        if(argc >= 2) {
            addr = scan_hexadecimal(arg1);
            if(argc == 3)
                len = scan_decimal(arg2);
            flash_dump(addr, len);
        }
    }
    else if (strcmp(cmd, "version") == 0) {
        monitor_write("\nFirmware version: ");
        monitor_write_uint(wolfBoot_current_firmware_version());
        monitor_write("\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        monitor_write("\nRebooting...\n");
        arch_reboot();
    }
#if USE_PMIC_LED
    else if (strcmp(cmd, "led") == 0) {
        int count = 1;
        if(argc == 2) {
            count = scan_decimal(arg1);
        }
        monitor_write("\nLED test...");
        board_status_led_blink(count);
        monitor_write("\n");
    }
#endif
    else if (strcmp(cmd, "exit") == 0) {
        monitor_write("\n");
        return 1;
    }
    else {
        monitor_write("\nUnknown command. Type 'help'.\n");
    }

    return 0;
}

void monitor_loop(void)
{
    static const char* prompt = "\nwolfBoot> ";
    uint8_t ch;
    uint8_t chBuf[2];
    char line[128];
    unsigned int idx = 0;

    chBuf[1] = 0;

    monitor_write("\nwolfBoot monitor ready. Type 'help' for commands.\n");

    for (;;) {
        monitor_write(prompt);
        idx = 0;
        memset(line, 0, sizeof(line));

        while (1) {
            int ret = uart_read(DEVICE_MONITOR, &ch, 1);
            if (ret <= 0)
                continue;

            // echo
            chBuf[0] = ch;
            monitor_write((const char *)chBuf);

            if ((ch == '\r') || (ch == '\n')) {
                if(monitor_handle_command(line))
                    return; // exit was requested
                break;
            }
            else if ((ch == 0x08 || ch == 0x7F) && idx > 0) {
                idx--;
                line[idx] = '\0';
                if(ch == 0x08) {
                    monitor_write((const char *)" ");
                    chBuf[0] = ch;
                    monitor_write((const char *)chBuf);
                }
            }
            else if ((ch >= 0x20) && (ch <= 0x7E)) {
                if (idx < (sizeof(line) - 1U)) {
                    line[idx++] = (char)ch;
                    line[idx] = '\0';
                }
            }
        }
    }
}
#endif /* USE_MONITOR */

#endif /* TARGET_nrf54lm20 */
