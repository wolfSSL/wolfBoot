/* stm32f7.c
 *
 * Test bare-metal application with UART update
 *
 * Copyright (C) 2018 wolfSSL Inc.
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


/* UART module */
#define UART1_PIN_AF 7
#define UART1_RX_PIN 10
#define UART1_TX_PIN 9
#define UART1 (0x40011000)
#define UART1_CR1      (*(volatile uint32_t *)(UART1 + 0x00))
#define UART1_CR2      (*(volatile uint32_t *)(UART1 + 0x04))
#define UART1_BRR      (*(volatile uint32_t *)(UART1 + 0x0C))
#define UART1_ISR      (*(volatile uint32_t *)(UART1 + 0x1C))
#define UART1_RDR      (*(volatile uint32_t *)(UART1 + 0x24))
#define UART1_TDR      (*(volatile uint32_t *)(UART1 + 0x28))
#define UART_CR1_UART_ENABLE    (1 << 0)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR1_SYMBOL_LEN     (1 << 28)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_ISR_TX_EMPTY       (1 << 7)
#define UART_ISR_RX_NOTEMPTY    (1 << 5)

#define CLOCK_SPEED (216000000)

#define APB2_CLOCK_ER           (*(volatile uint32_t *)(0x40023844))
#define UART1_APB2_CLOCK_ER (1 << 4)
#define AHB1_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOA_AHB1_CLOCK_ER (1 << 0)
#define GPIOD_AHB1_CLOCK_ER (1 << 3)
#define GPIOA_BASE 0x40020000
#define GPIOD_BASE 0x40020c00
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_PUPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x0c))
#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OTYPE (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_OSPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_PUPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x0c))
#define GPIOD_ODR   (*(volatile uint32_t *)(GPIOD_BASE + 0x14))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))
#define GPIOD_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))
void hal_erase_bank2(void);

void uart_write(const char c)
{
    uint32_t reg;
    do {
        reg = UART1_ISR;
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART1_TDR = c;
}

void uart_print(const char *s)
{
    int i = 0;
    while(s[i])
        uart_write(s[i++]);
}

static void uart_pins_setup(void)
{
    uint32_t reg;
    AHB1_CLOCK_ER |= GPIOA_AHB1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOA_MODE & ~ (0x03 << (UART1_RX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOA_MODE & ~ (0x03 << (UART1_TX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART1_TX_PIN * 2));

    /* Alternate function: use hi pins (9 and 10) */
    reg = GPIOA_AFH & ~(0xf << ((UART1_TX_PIN - 8) * 4));
    GPIOA_AFH = reg | (UART1_PIN_AF << ((UART1_TX_PIN - 8) * 4));
    reg = GPIOA_AFH & ~(0xf << ((UART1_RX_PIN - 8) * 4));
    GPIOA_AFH = reg | (UART1_PIN_AF << ((UART1_RX_PIN - 8) * 4));
}

int uart_setup(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF7 */
    uart_pins_setup();
    /* Turn on the device */
    APB2_CLOCK_ER |= UART1_APB2_CLOCK_ER;

    /* Configure for TX + RX */
    UART1_CR1 |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE);

    /* Configure clock */
    UART1_BRR =  CLOCK_SPEED / bitrate;

    /* Configure data bits */
    if (data == 8)
        UART1_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART1_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART1_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
        case 'E':
            UART1_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART1_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits  (not supported)*/
    (void)stop;

    /* Turn on uart */
    UART1_CR1 |= UART_CR1_UART_ENABLE;

    return 0;
}

char uart_read(void)
{
    char c;
    volatile uint32_t reg;
    do {
        reg = UART1_ISR;
    } while ((reg & UART_ISR_RX_NOTEMPTY) == 0);
    c = (char)(UART1_RDR & 0xff);
    return c;
}





#define MSGSIZE 16
#define PAGESIZE (256)
static uint8_t page[PAGESIZE];
static const char ERR='!';
static const char START='*';
static const char UPDATE='U';
static const char ACK='#';
static uint8_t msg[MSGSIZE];

static void ack(uint32_t _off)
{
    uint8_t *off = (uint8_t *)(&_off);
    int i;
    uart_write(ACK);
    for (i = 0; i < 4; i++) {
        uart_write(off[i]);
    }
}

static int check(uint8_t *pkt, int size)
{
    int i;
    uint16_t c = 0;
    uint16_t c_rx = *((uint16_t *)(pkt + 2));
    uint16_t *p = (uint16_t *)(pkt + 4);
    for (i = 0; i < ((size - 4) >> 1); i++)
        c += p[i];
    if (c == c_rx)
        return 0;
    return -1;
}


void uart_update_mgr(void)
{
    uint32_t tlen = 0;
    volatile uint32_t recv_seq;
    uint32_t r_total = 0;
    uint32_t tot_len = 0;
    uint32_t next_seq = 0;
    uint32_t version = 0;
    uint8_t *v_array = (uint8_t *)&version;
    int i;
    memset(page, 0xFF, PAGESIZE);
    hal_flash_unlock();
//    version = wolfBoot_current_firmware_version();
//    if ((version & 0x01) == 0)
//        wolfBoot_success();
    uart_write(START);
    for (i = 3; i >= 0; i--) {
        uart_write(v_array[i]);
    }
    while (1) {
        r_total = 0;
        do {
            while(r_total < 2) {
                msg[r_total++] = uart_read();
                if ((r_total == 2) && ((msg[0] != 0xA5) || msg[1] != 0x5A)) {
                    r_total = 0;
                    continue;
                }
            }
            msg[r_total++] = uart_read();
            if ((tot_len == 0) && r_total == 2 + sizeof(uint32_t))
                break;
            if ((r_total > 8)  && (tot_len <= ((r_total - 8) + next_seq)))
                break;
        } while (r_total < MSGSIZE);
        if (tot_len == 0)  {
            tlen = msg[2] + (msg[3] << 8) + (msg[4] << 16) + (msg[5] << 24);
            if (tlen > 2048 * 1024) {
                uart_write(ERR);
                uart_write(ERR);
                uart_write(ERR);
                uart_write(ERR);
                uart_write(START);
                recv_seq = 0;
                tot_len = 0;
                continue;
            }
            tot_len = tlen;
            hal_erase_bank2();
            ack(0);
            continue;
        }
        if (check(msg, r_total) < 0) {
            ack(next_seq);
            continue;
        }
        recv_seq = msg[4] + (msg[5] << 8) + (msg[6] << 16) + (msg[7] << 24);
        if (recv_seq == next_seq)
        {
            int psize = r_total - 8;
            int page_idx = recv_seq % PAGESIZE;
            memcpy(&page[recv_seq % PAGESIZE], msg + 8, psize);
            page_idx += psize;
            if ((page_idx == PAGESIZE) || (next_seq + psize >= tot_len)) {
                uint32_t dst = (0x08120000, recv_seq + psize) - page_idx;
                hal_flash_write(dst, page, PAGESIZE);
                memset(page, 0xFF, PAGESIZE);
            }
            next_seq += psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            /* Update complete */
            //wolfBoot_update_trigger();
            hal_flash_lock();
            break;
        }
    }
    /* Wait for reboot */
    while(1)
        ;
}

#define LED_BOOT_PIN (4)
#define LED_USR_PIN (12)
static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    AHB1_CLOCK_ER |= GPIOD_AHB1_CLOCK_ER;
    reg = GPIOD_MODE & ~(0x03 << (pin * 2));
    GPIOD_MODE = reg | (1 << (pin * 2));
    reg = GPIOD_PUPD & ~(0x03 << (pin * 2));
    GPIOD_PUPD = reg | (1 << (pin * 2));
    GPIOD_BSRR |= (1 << pin);
}

static void boot_led_off(void)
{
    GPIOD_BSRR |= (1 << (LED_BOOT_PIN + 16));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;
    AHB1_CLOCK_ER |= GPIOA_AHB1_CLOCK_ER;
    reg = GPIOA_MODE & ~(0x03 << (pin * 2));
    GPIOA_MODE = reg | (1 << (pin * 2));
    reg = GPIOA_PUPD & ~(0x03 << (pin * 2));
    GPIOA_PUPD = reg | (1 << (pin * 2));
    GPIOA_BSRR |= (1 << pin);
}

void usr_led_off(void)
{
    GPIOA_BSRR |= (1 << (LED_USR_PIN + 16));
}

void main(void)
{
    hal_init();

    boot_led_on();
    usr_led_on();
    boot_led_off();
//    usr_led_off();
    uart_setup(115200, 8, 'N', 1);
    uart_update_mgr();
    while(1)
        ;
}
