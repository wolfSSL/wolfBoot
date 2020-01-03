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
 * Update tool to verify update version
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
#define UART_DEV "/dev/ttyS0"
#endif
#ifndef B115200
#define B115200 115200
#endif

void alarm_handler(int signo)
{
    printf("0\n");
    exit(0);
}


int main(int argc, char** argv)
{
    struct termios tty;
    int res, serialfd;
    int star = 0;
    int i = 3;
    uint32_t ver = 0;
    sigset(SIGALRM, alarm_handler);


    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        exit(1);
    }

    serialfd = open(UART_DEV, O_RDWR | O_NOCTTY);
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

    alarm(60);

    while (i >= 0) {
        char c;
        res = read(serialfd, &c, 1);
        if (res <= 0) {
            usleep(10000);
            continue;
        }
        if (!star) {
            if (c == '*') {
                star = 1;
                i = 3;
            } else {
                star = 0;
            }
        } else {
            ver += (((uint32_t)c) << (i-- * 8));
        }
    }
    printf("%d\n", ver);
    close(serialfd);
    return 0;
}
