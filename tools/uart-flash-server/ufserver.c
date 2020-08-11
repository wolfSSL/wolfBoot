/* ufserver.c
 *
 * UART flash server
 *
 * Run on HOST machine to export an emulated external, non-volatile memory.
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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>

#ifndef __MACH__
#include <termio.h>
#include <linux/serial.h>
#endif


#define CMD_HDR_WOLF  'W'
#define CMD_HDR_VER   'V'
#define CMD_HDR_WRITE 0x01
#define CMD_HDR_READ  0x02
#define CMD_HDR_ERASE 0x03
#define CMD_ACK       0x06

#define FIRMWARE_PARTITION_SIZE 0x20000
#define SWAP_SIZE 0x1000
#define UART_BITRATE 460800

const char msgSha[]         = "Verifying SHA digest...";
const char msgReadUpdate[]  = "Fetching update blocks ";
const char msgReadSwap[]    = "Reading SWAP blocks    ";
const char msgWriteUpdate[] = "Writing backup blocks  ";
const char msgWriteSwap[]   = "Writing SWAP blocks    ";
const char msgEraseUpdate[] = "Erase update blocks    ";
const char msgEraseSwap[]   = "Erase swap blocks      ";

extern uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr);

const char blinker[]="-\\|/";
static int valid_update = 1;

void printmsg(const char *msg)
{
    static const char *cur_msg = NULL;
    static int b_idx = 0;
    if (cur_msg != msg) {
        cur_msg = msg;
    }
    b_idx++;
    if (b_idx > 3)
        b_idx = 0;
    printf("\r[%c] %s\t\t\t", blinker[b_idx], msg);
    fflush(stdout);
}


int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}
void hal_flash_unlock(void)
{
}
void hal_flash_lock(void)
{
}

#include <fcntl.h>
#include <err.h>

static int rate_to_constant(int baudrate) {
#ifdef __MACH__
#define B(x) case x: return x
#else
#define B(x) case x: return B##x
#endif
    switch(baudrate) {
        B(50);     B(75);     B(110);    B(134);    B(150);
        B(200);    B(300);    B(600);    B(1200);   B(1800);
        B(2400);   B(4800);   B(9600);   B(19200);  B(38400);
        B(57600);  B(115200); B(230400); B(460800); B(500000);
        B(576000); B(921600); B(1000000);B(1152000);B(1500000);
default: return 0;
}
#undef B
}

/* Open serial port in raw mode, with custom baudrate if necessary */
static int serial_open(const char *device, int rate)
{
	struct termios options;
	int fd;
	int speed = 0;

	/* Open and configure serial port */
	if ((fd = open(device,O_RDWR|O_NOCTTY)) == -1)
		return -1;

	speed = rate_to_constant(rate);

#ifndef __MACH__
	if (speed == 0) {
		/* Custom divisor */
        struct serial_struct serinfo;
		serinfo.reserved_char[0] = 0;
		if (ioctl(fd, TIOCGSERIAL, &serinfo) < 0)
			return -1;
		serinfo.flags &= ~ASYNC_SPD_MASK;
		serinfo.flags |= ASYNC_SPD_CUST;
		serinfo.custom_divisor = (serinfo.baud_base + (rate / 2)) / rate;
		if (serinfo.custom_divisor < 1)
			serinfo.custom_divisor = 1;
		if (ioctl(fd, TIOCSSERIAL, &serinfo) < 0)
			return -1;
		if (ioctl(fd, TIOCGSERIAL, &serinfo) < 0)
			return -1;
		if (serinfo.custom_divisor * rate != serinfo.baud_base) {
			warnx("actual baudrate is %d / %d = %f",
			      serinfo.baud_base, serinfo.custom_divisor,
			      (float)serinfo.baud_base / serinfo.custom_divisor);
		}
	}
#endif

	fcntl(fd, F_SETFL, 0);
	tcgetattr(fd, &options);
	cfsetispeed(&options, speed ?: 460800);
	cfsetospeed(&options, speed ?: 460800);
	cfmakeraw(&options);
	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~CRTSCTS;
	if (tcsetattr(fd, TCSANOW, &options) != 0)
		return -1;

	return fd;
}


uint8_t *mmap_firmware(const char *fname)
{
    uint8_t *base_fw;
    struct stat st;
    int fd;
    uint32_t signature_word;
    if (stat(fname, &st) != 0) {
        perror ("stat");
        return (void *)-1;
    }
    fd = open(fname, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return (void *)-1;
    }
    if (read(fd, &signature_word, sizeof(uint32_t)) != (sizeof(uint32_t))) {
        perror("read");
        return (void *)-1;
    }
    if ((st.st_size <= FIRMWARE_PARTITION_SIZE)) {
        uint8_t pad = 0xFF;
        int i;
        int fsize = st.st_size;
        lseek(fd, fsize, SEEK_SET);
        for (i = 0; i < (FIRMWARE_PARTITION_SIZE - (fsize)); i++)
            write(fd, &pad, 1);
        lseek(fd, FIRMWARE_PARTITION_SIZE, SEEK_SET);
        for (i = 0; i < SWAP_SIZE; i++)
            write(fd, &pad, 1);
    }
    if (strncmp((char *)&signature_word, "WOLF", 4) != 0) {
        fprintf(stderr, "Warning: the binary file provided does not appear to contain a valid firmware partition file. (If the update is encrypted, this is OK)\n");
        valid_update = 0;
    } else {
        int i;
        const char update_flags[] = "pBOOT";
        lseek(fd, FIRMWARE_PARTITION_SIZE - 5, SEEK_SET);
        write(fd, update_flags, 5);
        for (i = 0; i < SWAP_SIZE; i++)
            write(fd, update_flags, 5);
    }
    base_fw = mmap(NULL, FIRMWARE_PARTITION_SIZE + SWAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base_fw == (void *)(-1)) {
        perror("mmap");
        return (void *)-1;
    }
    return base_fw;
}


uint32_t fw_version(uint8_t *fw)
{
    uint16_t l;
    uint8_t *p;
    uint32_t ver;
    l = wolfBoot_find_header(fw + 8, HDR_VERSION, &p);
    if (l != 4)
        return (uint32_t)-1;
    ver = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    return ver;
}

int open_uart(const char *uart_dev)
{
    return serial_open(uart_dev, UART_BITRATE);
}

static void send_ack(int ud)
{
    uint8_t ack = CMD_ACK;
    write(ud, &ack, 1);
    fsync(ud);
}

static int read_word(int ud, uint32_t *w)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)w;
    int ret;
    for (i = 0; i < 4; i++) {
        ret = read(ud, &b[i], 1);
        if (ret != 1)
            return 0;
        send_ack(ud);
    }
    return 4;
}

static void uart_flash_erase(uint8_t *base, int ud)
{
    uint32_t address;
    uint32_t len;
    uint32_t i;
    if (read_word(ud,&address) != 4)
        return;
    if (read_word(ud,&len) != 4)
        return;
    if (address + len > (FIRMWARE_PARTITION_SIZE + SWAP_SIZE))
        return;
    if (address < FIRMWARE_PARTITION_SIZE) {
        printmsg(msgEraseUpdate);
    } else {
        printmsg(msgEraseSwap);
    }
    for (i = 0; i < len; i++) {
        base[address + i] = 0xFF;
    }
    /* Send additional ack at the end of erase */
    send_ack(ud);
    msync(base, FIRMWARE_PARTITION_SIZE + SWAP_SIZE, MS_SYNC);
}


static void uart_flash_read(uint8_t *base, int ud)
{
    uint32_t address;
    uint32_t len;
    uint32_t i;
    uint8_t ack;
    if (read_word(ud,&address) != 4)
        return;
    if (read_word(ud,&len) != 4)
        return;
    if (len == 16) {
        printmsg(msgSha);
    } else if (address < FIRMWARE_PARTITION_SIZE) {
        printmsg(msgReadUpdate);
    } else {
        printmsg(msgReadSwap);
    }
    if (address + len > (FIRMWARE_PARTITION_SIZE + SWAP_SIZE))
        return;
    for (i = 0; i < len; i++) {
        write(ud, base + address + i, 1);
        read(ud, &ack, 1);
    }
}

static void uart_flash_write(uint8_t *base, int ud)
{
    uint32_t address;
    uint32_t len;
    uint32_t i;
    if (read_word(ud,&address) != 4)
        return;
    if (read_word(ud,&len) != 4)
        return;
    if (address < FIRMWARE_PARTITION_SIZE) {
        printmsg(msgWriteUpdate);
    } else {
        printmsg(msgWriteSwap);
    }
    if (address + len > (FIRMWARE_PARTITION_SIZE + SWAP_SIZE))
        return;
    for (i = 0; i < len; i++) {
        read(ud, base + address + i, 1);
        send_ack(ud);
    }
    msync(base, FIRMWARE_PARTITION_SIZE + SWAP_SIZE, MS_SYNC);
}


static void serve_update(uint8_t *base, const char *uart_dev)
{
    int ret = 0;
    uint8_t buf[8];
    int ud = open_uart(uart_dev);
    if (ud < 0) {
        fprintf(stderr, "Cannot open serial port %s: %s.\n", uart_dev, strerror(errno));
        exit(3);
    }
    while (1) {
       /* read STX */
       ret = read(ud, buf, 1);
       if (ret < 0) {
           return;
       }
       if (ret == 0)
           continue;

       if ((buf[0] != CMD_HDR_WOLF) && (buf[0] != CMD_HDR_VER)) {
           printf("bad hdr: %02x\n", buf[0]);
           continue;
       }
       if (buf[0] == CMD_HDR_VER) {
            uint32_t v;
            int idx = 1;
            send_ack(ud);
            while (idx < 5) {
                ret = read(ud, buf + idx, 1);
                if (ret > 0) {
                    send_ack(ud);
                    idx++;
                }
                else {
                    printf("UART error while reading target version\n");
                    break;
                }
            }
            if (idx == 5) {
                printf("\r\n** TARGET REBOOT **\n");
                v = buf[1] + (buf[2] << 8) + (buf[3] << 16) + (buf[4] << 24);
                printf("Version running on target: %u\n", v);
            }
            continue;
       }
       /* send ack */
       send_ack(ud);
       ret = read(ud, buf, 1);
       if (ret < 0) {
           return;
       }
       if (ret == 0) {
          printf("Timeout!\n");
          continue;
       }
       /* Read command code */
       switch(buf[0]) {
           case CMD_HDR_ERASE:
               send_ack(ud);
               uart_flash_erase(base, ud);
               break;
           case CMD_HDR_READ:
               send_ack(ud);
               uart_flash_read(base, ud);
               break;
           case CMD_HDR_WRITE:
               send_ack(ud);
               uart_flash_write(base, ud);
               break;
           default:
               fprintf(stderr, "Unrecognized command: %02X\n", buf[0]);
               break;
       }
    }
}

void usage(char *pname)
{
    printf("Usage: %s binary_file serial_port\nExample:\n%s firmware_v3_signed.bin /dev/ttyUSB0\n", pname, pname);
    exit(1);
}

int main(int argc, char *argv[])
{
    uint8_t *base_fw;
    uint32_t base_fw_ver = 0;
    if (argc != 3) {
        usage(argv[0]);
    }
    base_fw = mmap_firmware(argv[1]);
    if (base_fw == (void *)(-1)) {
        fprintf(stderr, "Error opening binary file '%s'.\n", argv[1]);
        exit(2);
    }
    if (valid_update) {
        printf("%s has a wolfboot manifest header\n", basename(argv[1]));
        base_fw_ver = fw_version(base_fw);
        printf("%s contains version %u\n", basename(argv[1]), base_fw_ver);
    }
    serve_update(base_fw, argv[2]);
    return 0;
}
