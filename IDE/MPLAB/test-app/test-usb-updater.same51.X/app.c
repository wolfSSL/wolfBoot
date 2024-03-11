/* app.c
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
/**
 * @file app.c
 * @brief test update application for microchip targets, over SERCOM5
 * verification.
 */
#include "config/default/definitions.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wolfboot/wolfboot.h>
#include "hal.h"
#define HAVE_LIBWOLFBOOT 1

#define USART_BUFFER_SZ (16)

static const char UPDATE_ERR = '!';
static const char UPDATE_START = '*';
static const char UPDATE_ACK = '#';

void APP_Initialize(void) {
    int i;
    uint32_t ver;
    uint8_t *v_array = (uint8_t *) & ver;
    unsigned char cmd;
    SERCOM5_USART_Enable();
#ifdef HAVE_LIBWOLFBOOT
    wolfBoot_success();
    ver = wolfBoot_current_firmware_version();
#else
    #define WOLFBOOT_SECTOR_SIZE (0x2000)
    #define WOLFBOOT_PARTITION_SIZE (0x20000)
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS (0x088000)
    ver = 1U;
#endif
    /* Send command to start the update */
    cmd = UPDATE_START;
    SERCOM5_USART_Write(&cmd, 1);
    
    /* Send current version */
    for (i = 3; i >= 0; i--) {
        SERCOM5_USART_Write(&v_array[i], 1);
    }

    /* Ready to receive the update now */

}

static void ack(uint32_t _off)
{
    uint8_t *off = (uint8_t *)(&_off);
    unsigned char cmd = UPDATE_ACK;
    SERCOM5_USART_Write(&cmd, 1);
    SERCOM5_USART_Write(off, 4);
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

static uint8_t rxbuf[USART_BUFFER_SZ];
#define PAGESIZE 256
static uint8_t page[PAGESIZE];

void APP_Tasks(void) {

    uint32_t r_total = 0;
    uint32_t tot_len = 0, tlen = 0;
    uint32_t next_seq = 0, recv_seq = 0;
    hal_flash_unlock();
    while (1) {
        int r;
        r_total = 0;
        do {
            /* Serial update packages start with "0xA5 0x5A" 
             * 
             * Loop until synchronized
             */
            while (r_total < 2) {
                r = SERCOM5_USART_Read(&rxbuf[r_total], 1);
                if (r == 1)
                    r_total++;

                if ((r_total == 2) && ((rxbuf[0] != 0xA5) || rxbuf[1] != 0x5A)) {
                    r_total = 0;
                    continue;
                }
            }
            r = SERCOM5_USART_Read(&rxbuf[r_total], 1);
            if (r > 0)
                r_total += r;

            /* Break if tot_len is received from the host */
            if ((tot_len == 0) && r_total == 2 + sizeof (uint32_t))
                break;
            /* Break if data received is bigger than the total len */
            if ((r_total > 8) && (tot_len <= ((r_total - 8) + next_seq)))
                break;
        } while (r_total < USART_BUFFER_SZ);

        /* Set total length based on the first packet received */
        if (tot_len == 0) {
            tlen = rxbuf[2] + (rxbuf[3] << 8) + (rxbuf[4] << 16) + (rxbuf[5] << 24);
            if (tlen > WOLFBOOT_PARTITION_SIZE - 8) {
                /* Invalid total length: abort transfer + restart */
                unsigned char cmd = UPDATE_ERR;
                SERCOM5_USART_Write(&cmd, 1);
                SERCOM5_USART_Write(&cmd, 1);
                SERCOM5_USART_Write(&cmd, 1);
                cmd = UPDATE_START;
                SERCOM5_USART_Write(&cmd, 1);
                recv_seq = 0;
                tot_len = 0;
                continue;
            }
            tot_len = tlen;
            /* (acknowledging '0' starts the actual transfer) */
            ack(0);
            continue;
        }

        if (check(rxbuf, r_total) < 0) {
            ack(next_seq);
            continue;
        }
        recv_seq = rxbuf[4] + (rxbuf[5] << 8) + (rxbuf[6] << 16) + (rxbuf[7] << 24);
        if (recv_seq == next_seq) {
            int psize = r_total - 8;
            int page_idx = recv_seq % PAGESIZE;
            memcpy(&page[recv_seq % PAGESIZE], rxbuf + 8, psize);
            page_idx += psize;
            if ((page_idx == PAGESIZE) || (next_seq + psize >= tot_len)) {
                uint32_t dst = (WOLFBOOT_PARTITION_UPDATE_ADDRESS + recv_seq + psize) - page_idx;
                if ((dst % WOLFBOOT_SECTOR_SIZE) == 0) {
                    hal_flash_erase(dst, WOLFBOOT_SECTOR_SIZE);
                }
                hal_flash_write(dst, page, PAGESIZE);
                memset(page, 0xFF, PAGESIZE);
            }
            next_seq += psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            /* Update complete */
            wolfBoot_update_trigger();
            hal_flash_lock();
            break;
        }
    }
    /* Wait for reboot */
    while (1)
        ;
}
