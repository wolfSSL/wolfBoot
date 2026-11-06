/* init.c
 *
 * Minimal /init for the x86_64_lilo QEMU test: prints the kernel command
 * line on the console and powers the machine off.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* Built with -nostdlib -static: x86_64 Linux system calls only. */

static long sys(long n, long a, long b, long c, long d, long e)
{
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;

    __asm__ volatile ("syscall" : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

#define SYS_read    0
#define SYS_write   1
#define SYS_open    2
#define SYS_mkdir   83
#define SYS_mount   165
#define SYS_reboot  169

static unsigned long len(const char *s)
{
    unsigned long n = 0;

    while (s[n])
        n++;
    return n;
}

static void out(int fd, const char *s)
{
    sys(SYS_write, fd, (long)s, (long)len(s), 0, 0);
}

void _start(void)
{
    static char cmdline[2048];
    long n;
    int fd, con;

    con = (int)sys(SYS_open, (long)"/dev/console", 1 /* O_WRONLY */, 0, 0, 0);
    if (con < 0)
        con = 1;
    sys(SYS_mkdir, (long)"/proc", 0555, 0, 0, 0);
    sys(SYS_mount, (long)"proc", (long)"/proc", (long)"proc", 0, 0);
    fd = (int)sys(SYS_open, (long)"/proc/cmdline", 0, 0, 0, 0);
    n = (fd >= 0) ? sys(SYS_read, fd, (long)cmdline, sizeof(cmdline) - 1, 0, 0)
                  : -1;
    if (n < 0)
        n = 0;
    cmdline[n] = '\0';
    out(con, "\nWOLFBOOT-LILO-TEST: initrd running, cmdline: ");
    out(con, cmdline);
    out(con, "\n");
    /* LINUX_REBOOT_MAGIC1, MAGIC2, LINUX_REBOOT_CMD_POWER_OFF */
    sys(SYS_reboot, 0xfee1dead, 672274793, 0x4321fedc, 0, 0);
    for (;;)
        ;
}
