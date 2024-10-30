/* raspi3.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"
#ifndef ARCH_AARCH64
#   error "wolfBoot raspi3 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

#define TEST_ENCRYPT

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif


#define CORTEXA53_0_CPU_CLK_FREQ_HZ 1099989014
#define CORTEXA53_0_TIMESTAMP_CLK_FREQ 99998999

#define MMIO_BASE       0x3F000000

#define GPIO_BASE       MMIO_BASE + 0x200000

#define GPFSEL1         ((volatile unsigned int*)(GPIO_BASE+0x04))
#define GPPUD           ((volatile unsigned int*)(GPIO_BASE+0x94))
#define GPPUDCLK0       ((volatile unsigned int*)(GPIO_BASE+0x98))

/* PL011 UART registers */
#define UART0_BASE      GPIO_BASE + 0x1000
#define UART0_DR        ((volatile unsigned int*)(UART0_BASE+0x00))
#define UART0_FR        ((volatile unsigned int*)(UART0_BASE+0x18))
#define UART0_IBRD      ((volatile unsigned int*)(UART0_BASE+0x24))
#define UART0_FBRD      ((volatile unsigned int*)(UART0_BASE+0x28))
#define UART0_LCRH      ((volatile unsigned int*)(UART0_BASE+0x2C))
#define UART0_CR        ((volatile unsigned int*)(UART0_BASE+0x30))
#define UART0_IMSC      ((volatile unsigned int*)(UART0_BASE+0x38))
#define UART0_ICR       ((volatile unsigned int*)(UART0_BASE+0x44))

/* mail box message buffer */
volatile unsigned int  __attribute__((aligned(16))) mbox[36];

#define VIDEOCORE_MBOX  (MMIO_BASE+0x0000B880)
#define MBOX_READ       ((volatile unsigned int*)(VIDEOCORE_MBOX+0x0))
#define MBOX_POLL       ((volatile unsigned int*)(VIDEOCORE_MBOX+0x10))
#define MBOX_SENDER     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x14))
#define MBOX_STATUS     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x18))
#define MBOX_CONFIG     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x1C))
#define MBOX_WRITE      ((volatile unsigned int*)(VIDEOCORE_MBOX+0x20))
#define MBOX_RESPONSE   0x80000000
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000

#define MBOX_REQUEST    0

/* channels */
#define MBOX_CH_POWER   0
#define MBOX_CH_FB      1
#define MBOX_CH_VUART   2
#define MBOX_CH_VCHIQ   3
#define MBOX_CH_LEDS    4
#define MBOX_CH_BTNS    5
#define MBOX_CH_TOUCH   6
#define MBOX_CH_COUNT   7
#define MBOX_CH_PROP    8

/* tags */
#define MBOX_TAG_GETBRDVERSION  0x10002
#define MBOX_TAG_GETSERIAL      0x10004
#define MBOX_TAG_GET_CLOCK_RATE 0x30002
#define MBOX_TAG_SETCLKRATE     0x38002
#define MBOX_TAG_LAST           0

/* Fixed addresses */
extern void *kernel_addr, *update_addr, *dts_addr;
/* Loop <delay> times  */
static inline void delay(int32_t count)
{
    register unsigned int c;
    c = count;
    while (c--) {
        asm volatile("nop");
    }
}
/**
 * write message to mailbox
 */
static void mailbox_write(uint8_t chan)
{
    uint32_t ch = (((uint32_t)((unsigned long)&mbox) & ~0xf)
                | (chan & 0xf));

    /* wait until mail box becomes ready to write */
    while ((*MBOX_STATUS & MBOX_FULL) != 0) { }

    /* write the address of the message to mail-box channel */
    *MBOX_WRITE = ch;
}
/**
 * read message from mailbox
 */
static int mailbox_read(uint8_t chan)
{
    uint32_t ch = (((uint32_t)((unsigned long)&mbox) & ~0xf)
                | (chan & 0xf));
    /* now wait for the response */
    while(1) {
        while ((*MBOX_STATUS & MBOX_EMPTY) != 0) { }
        if(ch == *MBOX_READ)
            /* is it a valid successful response */
            return mbox[1] == MBOX_RESPONSE;
    }
    return 0;
}

/* UART functions for Raspberry Pi 3 UART */
void uart_tx(char c)
{
    /* wait until uart channel is ready to send */
    do{
        asm volatile("nop");
    } while(*UART0_FR & 0x20);
    *UART0_DR = c;
}

char uart_read(void)
{
    char c;
    /* wait until data is comming */
    do{
        asm volatile("nop");
    } while(*UART0_FR & 0x10);
    /* read it and return */
    c = (char)(*UART0_DR);
    return c;
}

/**
 * Send string to UART
 */
void uart_write(const char* buf, uint32_t sz) {
    uint32_t len = sz;

    while (len > 0 && *buf) {
        uart_tx(*buf++);
        len--;
    }
}

void uart_init()
{
    register unsigned int c;

    /* initialize UART. turn off UART 0*/
    *UART0_CR = 0;

    /* set up clock for consistent divisor values */
    mbox[0] = 9 * 4;
    mbox[1] = MBOX_REQUEST;
    /* tag for setting clock rate */
    mbox[2] = MBOX_TAG_SETCLKRATE;
    mbox[3] = 12;
    mbox[4] = 8;
    /* specify UART clock channel */
    mbox[5] = 2;
    /* UART clock rate in hz(4Mhz)*/
    mbox[6] = 4000000;
    /* not use turbo */
    mbox[7] = 0;
    mbox[8] = MBOX_TAG_LAST;

    mailbox_write(MBOX_CH_PROP);
    mailbox_read(MBOX_CH_PROP);

    /* disable pull up/down for all GPIO pins */
    *GPPUD = 0;
    delay(150);
    /* Disable pull up/down for pin 14 and 15 */
    *GPPUDCLK0 = (1<<14)|(1<<15);
    delay(150);
    /* flush GPIO setting to make it take effect */
    *GPPUDCLK0 = 0;

    /* clear pending interrupts */
    *UART0_ICR = 0x7FF;
    /* select 115200 baud rate */
    /* divider = 4000000 / (16 * 115200) = 2.17 = ~2 */
    *UART0_IBRD = 2;
    /* Fractional part register = (.17013 * 64) + 0.5 = 11.38 = ~11 */
    *UART0_FBRD = 0xB;
    /* Enable fifo, 8bit data transmission (1 stop bit, no parity) */
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
    /* enable UART0 transfer & receive*/
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

void* hal_get_primary_address(void)
{
    return (void*)&kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
  return (void*)&dts_addr;
}

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    XMEMCPY((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

#endif


void* hal_get_dts_update_address(void)
{
  return NULL; /* Not yet supported */
}

#if defined(DISPLAY_CLOCKS)
static uint32_t getclocks(uint8_t cid)
{
    /* Retrive clock rate */
    /* length of the message */
    mbox[0] = 8 * 4;
    mbox[1] = MBOX_REQUEST;
    /* tag for get board version */
    mbox[2] = MBOX_TAG_GET_CLOCK_RATE;
    /* buffer size */
    mbox[3] = 8;
    mbox[4] = 8;
    /* clock id CORE*/
    mbox[5] = cid;
    /* clock frequency */
    mbox[6] = 0;
    mbox[7] = MBOX_TAG_LAST;
    mailbox_write(MBOX_CH_PROP);

    if (mailbox_read(MBOX_CH_PROP)) {
        return mbox[6];
    }
    else {
        return 0;
    }
}
#endif /* DISPLAY clocks */

/* public HAL functions */
void hal_init(void)
{
    #if defined(TEST_ENCRYPT) && defined (EXT_ENCRYPTED)
    char enc_key[] = "0123456789abcdef0123456789abcdef"
        "0123456789abcdef";
    wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
    #endif

    uart_init();

    /* length of the message */
    mbox[0] = 7 * 4;
    mbox[1] = MBOX_REQUEST;
    /* tag for get board version */
    mbox[2] = MBOX_TAG_GETBRDVERSION;
    /* buffer size */
    mbox[3] = 4;
    mbox[4] = 0;
    mbox[5] = 0;
    mbox[6] = MBOX_TAG_LAST;

    /* send the message to the GPU  */
    mailbox_write(MBOX_CH_PROP);

    if (mailbox_read(MBOX_CH_PROP)) {
        wolfBoot_printf("My board version is: 0x%08x", mbox[5]);
        wolfBoot_printf("\n");
    } else {
        wolfBoot_printf("Unable to query board version!\n");
    }

#if defined(DISPLAY_CLOCKS) && defined(PRINTF_ENABLED)
    /* Get clocks */
    wolfBoot_printf("\n EMMC clock : %d Hz", getclocks(1));
    wolfBoot_printf("\n UART clock : %d Hz", getclocks(2));
    wolfBoot_printf("\n ARM  clock : %d Hz", getclocks(3));
    wolfBoot_printf("\n CORE clock : %d Hz", getclocks(4));
    wolfBoot_printf("\n V3D  clock : %d Hz", getclocks(5));
    wolfBoot_printf("\n H264 clock : %d Hz", getclocks(6));
    wolfBoot_printf("\n ISP  clock : %d Hz", getclocks(7));
    wolfBoot_printf("\n SDRAM clock : %d Hz", getclocks(8));
    wolfBoot_printf("\n PIXEL clock : %d Hz", getclocks(9));
    wolfBoot_printf("\n PWM  clock : %d Hz", getclocks(10));
    wolfBoot_printf("\n HEVC clock : %d Hz", getclocks(11));
    wolfBoot_printf("\n EMMC2 clock : %d Hz", getclocks(12));
    wolfBoot_printf("\n M2MC clock : %d Hz", getclocks(13));
    wolfBoot_printf("\n PIXEL_BVB clock : %d Hz\n", getclocks(14));
#endif

}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    return 0;
}
