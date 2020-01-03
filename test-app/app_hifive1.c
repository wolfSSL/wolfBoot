/* hifive1.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"

/* UART API's in hal/hifive1.c */
extern void uart_write(char c);
extern char uart_read(void);

#define MSGSIZE 16
#define PAGESIZE (0x1000) /* Flash sector: 4K */
static const char ERR='!';
static const char START='*';
static const char UPDATE='U';
static const char ACK='#';
static uint8_t msg[MSGSIZE];

uint8_t flash_page[PAGESIZE];
extern void write_page(uint32_t dst);
static void ack(uint32_t _off)
{
    uint32_t offset = _off;
    uint8_t *off = (uint8_t *)(&offset);
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

void main(void) {
    uint32_t tlen = 0;
    volatile uint32_t recv_seq;
    uint32_t r_total = 0;
    uint32_t tot_len = 0;
    uint32_t next_seq = 0;
    uint32_t version = 0;
    uint8_t *v_array = (uint8_t *)&version;
    int i;

    hal_init(); /* defaults: CPU = 320MHz, Baud = 115200 */

    memset(flash_page, 0xFF, PAGESIZE);

    version = wolfBoot_current_firmware_version();
    if ((version & 0x01) == 0)
        wolfBoot_success();
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
            if (tlen > WOLFBOOT_PARTITION_SIZE - 8) {
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
            int flash_page_idx = recv_seq % PAGESIZE;
            memcpy(&(flash_page[flash_page_idx]), msg + 8, psize);
            flash_page_idx += psize;
            if ((flash_page_idx == PAGESIZE) || (next_seq + psize >= tot_len)) {
                uint32_t dst = (WOLFBOOT_PARTITION_UPDATE_ADDRESS - 0x20000000) + recv_seq + psize - flash_page_idx;
                /* long jump */
                asm volatile("mv    a0, %0;" \
                             "la    a2, write_page;" \
                             "jalr  a2;" :: "r" (dst) : "a0","a2","a4", "memory");
                asm volatile ("fence.i; fence r,r");
                memset(flash_page, 0xFF, PAGESIZE);
            }
            next_seq += psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            /* Update complete */
            /* long jump */
            asm volatile( "la    a4, wolfBoot_update_trigger;" \
                    "jalr  a4;" ::: "a4", "memory");
            asm volatile ("fence.i; fence r,r");
            break;
        }
    }
    /* Wait for reboot */
    while(1)
        ;
}
