/* syscalls.c
 *
 * Newlib syscall stubs for embedded systems without filesystem
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

#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

#undef errno
extern int errno;

/* Heap management */
extern char _end; /* Defined by linker */
extern char _Min_Heap_Size; /* Linker symbol: address is the value */

char *__env[1] = { 0 };
char **environ = __env;

int _close(int file)
{
    return -1;
}

int _fstat(int file, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    return 0;
}

int _read(int file, char *ptr, int len)
{
    return 0;
}

void *_sbrk(int incr)
{
    static char *heap_end = 0;
    char *prev_heap_end;
    char *heap_limit;

    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;

    /* Calculate heap limit: _Min_Heap_Size is a linker symbol whose
     * address represents the size value */
    heap_limit = &_end + (uintptr_t)&_Min_Heap_Size;

    if (heap_end + incr > heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end += incr;
    return prev_heap_end;
}

/* Forward declaration of UART write function */
extern void uart_write(const char *buf, unsigned int sz);

int _write(int file, char *ptr, int len)
{
    /* Write to UART for stdout/stderr */
    if (file == 1 || file == 2) {
        uart_write(ptr, len);
    }

    return len;
}

void _exit(int status)
{
    while (1) {
        /* Intentional infinite loop - bare-metal has nowhere to exit to */
    }
}

int _kill(int pid, int sig)
{
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}
