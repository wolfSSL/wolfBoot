/* test-update-server.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *=============================================================================
 *
 * OTA Upgrade mechanism implemented using UART
 *
 */

#define _XOPEN_SOURCE 600

#include <stdio.h>                  /* standard in/out procedures */
#include <stdlib.h>                 /* defines system calls */
#include <string.h>                 /* necessary for memset */
#include <netdb.h>
#include <sys/socket.h>             /* used for all socket calls */
#include <netinet/in.h>             /* used for sockaddr_in6 */
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#define MSGLEN      (4 + 4 + 8)
#ifndef UART_DEV
    #ifdef __MACH__
        #define UART_DEV "/dev/cu.usbmodem1411"
    #else
        #define UART_DEV "/dev/ttyACM0"
    #endif
#endif
#ifndef B115200
#define B115200 115200
#endif

static volatile int cleanup;                 /* To handle shutdown */
union usb_ack {
    uint32_t offset;
    uint8_t u8[4];
};


static uint8_t      pktbuf[MSGLEN];
static unsigned int pktbuf_size = 0;
static int serialfd = -1;
static uint32_t high_ack;


void alarm_handler(int signo)
{
    if (serialfd >= 0 && pktbuf_size > 0) {
        write(serialfd, pktbuf, pktbuf_size);
        printf("retransmitting...\n");
        alarm(2);
    }
}

static int recv_ack(union usb_ack *ack)
{
    unsigned char c;
    int res;
    int err = 0;
    while (1) {
        res = read(serialfd, &c, 1);
        if (res <= 0) {
            continue;
        }
        if (c == '#') {
            int i = 0;
            err = 0;
            ack->offset = 0;
            while (i < 4) {
                res = read(serialfd, &c, 1);
                if (res < 1) {
                    usleep(10000);
                    continue;
                }
                ack->u8[i++] = c;
            }
            return 0;
        }
        if (c == '!') {
            if (++err > 3) {
                return -1;
            }
        }
    }
}

static void check(uint8_t *pkt, int size)
{
    uint16_t *c = (uint16_t *)(pkt + 2);
    int i;
    uint16_t *p = (uint16_t *)(pkt + 4);
    *c = 0;
    pkt[0] = 0xA5;
    pkt[1] = 0x5A;
    for (i = 0; i < ((size - 4) >> 1); i++)
        *c += p[i];
}


int main(int argc, char** argv)
{
    /* Variables for awaiting datagram */
    int           res = 1;
    uint32_t      len, tot_len;
    int           ffd; /* Firmware file descriptor */
    struct stat   st;
    union usb_ack ack;
    struct termios tty;
    sigset(SIGALRM, alarm_handler);

    if (argc != 2) {
        printf("Usage: %s firmware_filename\n", argv[0]);
        exit(1);
    }

    /* open file and get size */
    ffd = open(argv[1], O_RDONLY);
    if (ffd < 0) {
        perror("opening file");
        exit(2);
    }
    res = fstat(ffd, &st);
    if (res != 0) {
        perror("fstat file");
        exit(2);
    }
    tot_len = st.st_size;

    /* open UART */
    printf("Opening %s UART\n", UART_DEV);
    serialfd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (serialfd < 0) {
        fprintf(stderr, "failed opening serial %s\n", UART_DEV);
        exit(2);
    }
    tcgetattr(serialfd, &tty);
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | (CS8);
    tty.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY| INLCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~(ONLCR|OCRNL);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~ISTRIP;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    tcsetattr(serialfd, TCSANOW, &tty);

    /* Wait for start hash (asterisk) */
    while (1) {
        char c;

        res = read(serialfd, &c, 1);
        if (res <= 0) {
            usleep(10000);
            continue;
        }
        if (c == '*') {
            break;
        }
        else {
            printf("%c",c);
            fflush(stdout);
        }

    }
    printf("Target connected.\n");
    usleep(500000);
    printf("Starting update.\n");

    do {
        uint8_t hdr[2] = { 0xA5, 0x5A};
        len = 0;
        lseek(ffd, 0, SEEK_SET);
        write(serialfd, &hdr, 2);
        write(serialfd, &tot_len, sizeof(uint32_t));
        printf("Sent image file size (%d)\n", tot_len);
        while (len < tot_len) {
            res = recv_ack(&ack);
            if (res == 0) {
                if (ack.offset > tot_len) {
                    printf("Ignore bogus ack...\n");
                    continue;
                }
                if (ack.offset < high_ack) {
                    printf("Ignore low ack...\n");
                    continue;
                }
                high_ack = ack.offset;
                pktbuf_size = 0;
                if (ack.offset != len) {
                    printf("buf rewind %u\n", ack.offset);
                    lseek(ffd, ack.offset, SEEK_SET);
                    len = ack.offset;
                }
                memcpy(pktbuf + 4, &len, sizeof(len));
                res = read(ffd, pktbuf + 4 + sizeof(uint32_t), MSGLEN - (4 + sizeof(uint32_t)));
                if (res < 0) {
                    printf("EOF\r\n");
                    cleanup = 1;
                    break;
                }
                pktbuf_size = res + 4 + sizeof(uint32_t);
                check(pktbuf, pktbuf_size);
                write(serialfd, pktbuf, pktbuf_size);
                len += res;

                printf("Sent bytes: %d/%d  %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x                \r",
                    len, tot_len, pktbuf[0], pktbuf[1], pktbuf[2], pktbuf[3], pktbuf[4], pktbuf[5], pktbuf[6], pktbuf[7]);

                fflush(stdout);
                alarm(2);
            }
        }
        printf("\n\n");
    } while (0);

    printf("waiting for last ack...\n");
    while(!cleanup) {
        res = recv_ack(&ack);
        if ((res == 0 ) && (ack.offset == tot_len)) {
            printf("Transfer complete.\n");
            break;
        }
    }
    printf("All done.\n");
    close(serialfd);

    return 0;
}
