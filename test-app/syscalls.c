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
#include <stdarg.h>
#include <sys/stat.h>

/* Provide our own errno for bare-metal.
 * Using the libc errno via <errno.h> can conflict with TLS-based errno
 * on cross-toolchains (e.g. powerpc-linux-gnu glibc). */
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef EBADF
#define EBADF  9
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
int errno;

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

/* Forward declarations of UART functions from wolfBoot string.c */
extern void uart_write(const char *buf, unsigned int sz);
extern void uart_vprintf(const char* fmt, va_list argp);

int _write(int file, char *ptr, int len)
{
    /* Write to UART for stdout/stderr */
    if (file == 1 || file == 2) {
        uart_write(ptr, len);
        return len;
    }

    errno = EBADF;
    return -1;
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

/* ========== Standard I/O functions for bare-metal ==========
 * These override glibc's implementations which require TLS and
 * other OS facilities that don't exist in bare-metal.
 * wolfCrypt test/benchmark code calls standard printf, not wolfBoot_printf.
 */
int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list argp);

/* Route all printf-family functions through our vsnprintf (in this file)
 * rather than uart_vprintf (in string.c). This ensures float formatting
 * via UART_PRINTF_FLOAT works, since string.c is compiled by the parent
 * Makefile without that flag. */
int vprintf(const char *fmt, va_list args)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0)
        uart_write(buf, (len < (int)sizeof(buf)) ? len : (int)sizeof(buf) - 1);
    return len;
}

int printf(const char *fmt, ...)
{
    va_list args;
    int len;
    va_start(args, fmt);
    len = vprintf(fmt, args);
    va_end(args);
    return len;
}

/* fprintf - ignore FILE* stream, all output goes to UART */
int fprintf(void *stream, const char *fmt, ...)
{
    va_list args;
    int len;
    (void)stream;
    va_start(args, fmt);
    len = vprintf(fmt, args);
    va_end(args);
    return len;
}

int vfprintf(void *stream, const char *fmt, va_list args)
{
    (void)stream;
    return vprintf(fmt, args);
}

/* ========== Buffer-based formatting (snprintf) ========== */

static int buf_num(char *buf, int pos, int size, unsigned int num,
                   int base, int is_signed, int zeropad, int width,
                   int is_upper)
{
    char tmp[12];
    int i = 0, neg = 0, total;

    if (is_signed && (int)num < 0) {
        neg = 1;
        num = (unsigned int)(-(int)num);
    }

    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num > 0 && i < (int)sizeof(tmp)) {
            int d = num % base;
            tmp[i++] = (d < 10) ? ('0' + d) :
                       ((is_upper ? 'A' : 'a') + d - 10);
            num /= base;
        }
    }

    total = i + neg;
    while (total < width && pos < size - 1) {
        buf[pos++] = zeropad ? '0' : ' ';
        total++;
    }
    if (neg && pos < size - 1)
        buf[pos++] = '-';
    while (i > 0 && pos < size - 1)
        buf[pos++] = tmp[--i];

    return pos;
}

int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list argp)
{
    int pos = 0;
    const char *fmtp = fmt;
    int zeropad, maxdigits, precision, leftjust;

    if (size == 0) return 0;

    while (fmtp && *fmtp != '\0' && pos < (int)size - 1) {
        if (*fmtp != '%') {
            buf[pos++] = *fmtp++;
            continue;
        }
        fmtp++; /* skip % */

        zeropad = maxdigits = leftjust = 0;
        precision = -1;
        if (*fmtp == '-') { leftjust = 1; fmtp++; }
        while (*fmtp != '\0') {
            if (*fmtp == '*') {
                maxdigits = va_arg(argp, int);
                fmtp++;
            } else if (*fmtp >= '0' && *fmtp <= '9') {
                if (*fmtp == '0' && maxdigits == 0)
                    zeropad = 1;
                maxdigits = maxdigits * 10 + (*fmtp - '0');
                fmtp++;
            } else if (*fmtp == '.') {
                fmtp++;
                if (*fmtp == '*') {
                    precision = va_arg(argp, int);
                    fmtp++;
                } else {
                    precision = 0;
                    while (*fmtp >= '0' && *fmtp <= '9') {
                        precision = precision * 10 + (*fmtp - '0');
                        fmtp++;
                    }
                }
            } else if (*fmtp == 'l' || *fmtp == 'z') {
                fmtp++;
            } else {
                break;
            }
        }

        switch (*fmtp) {
            case '%':
                if (pos < (int)size - 1) buf[pos++] = '%';
                break;
            case 'd': case 'i':
                pos = buf_num(buf, pos, size,
                    (unsigned int)va_arg(argp, int), 10, 1,
                    zeropad, maxdigits, 0);
                break;
            case 'u':
                pos = buf_num(buf, pos, size,
                    va_arg(argp, unsigned int), 10, 0,
                    zeropad, maxdigits, 0);
                break;
            case 'x':
                pos = buf_num(buf, pos, size,
                    va_arg(argp, unsigned int), 16, 0,
                    zeropad, maxdigits, 0);
                break;
            case 'X':
                pos = buf_num(buf, pos, size,
                    va_arg(argp, unsigned int), 16, 0,
                    zeropad, maxdigits, 1);
                break;
            case 'p':
                if (pos < (int)size - 1) buf[pos++] = '0';
                if (pos < (int)size - 1) buf[pos++] = 'x';
                pos = buf_num(buf, pos, size,
                    (unsigned int)(uintptr_t)va_arg(argp, void*), 16, 0,
                    1, 8, 0);
                break;
            case 's':
            {
                const char *str = va_arg(argp, const char*);
                int slen;
                const char *sp;
                if (!str) str = "(null)";
                sp = str;
                slen = 0;
                while (*sp++) slen++;
                if (leftjust) {
                    sp = str;
                    while (*sp && pos < (int)size - 1)
                        buf[pos++] = *sp++;
                    while (slen < maxdigits && pos < (int)size - 1) {
                        buf[pos++] = ' ';
                        slen++;
                    }
                } else {
                    while (slen < maxdigits && pos < (int)size - 1) {
                        buf[pos++] = ' ';
                        slen++;
                    }
                    sp = str;
                    while (*sp && pos < (int)size - 1)
                        buf[pos++] = *sp++;
                }
                break;
            }
            case 'c':
                if (pos < (int)size - 1)
                    buf[pos++] = (char)va_arg(argp, int);
                break;
#ifdef UART_PRINTF_FLOAT
            case 'f': case 'e': case 'g':
            {
                double val = va_arg(argp, double);
                int prec = (precision >= 0) ? precision : 3;
                unsigned int ipart;
                double frac;
                int digit, k;

                if (val < 0.0) {
                    if (pos < (int)size - 1) buf[pos++] = '-';
                    val = -val;
                }
                ipart = (unsigned int)val;
                pos = buf_num(buf, pos, size, ipart, 10, 0, 0, 0, 0);
                if (prec > 0) {
                    frac = val - (double)ipart;
                    if (pos < (int)size - 1) buf[pos++] = '.';
                    for (k = 0; k < prec && pos < (int)size - 1; k++) {
                        frac *= 10.0;
                        digit = (int)frac;
                        if (digit > 9) digit = 9;
                        buf[pos++] = '0' + digit;
                        frac -= (double)digit;
                    }
                }
                break;
            }
#endif /* UART_PRINTF_FLOAT */
            default:
                break;
        }
        fmtp++;
    }

    buf[pos] = '\0';
    return pos;
}

int snprintf(char *buf, unsigned int size, const char *fmt, ...)
{
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int puts(const char *s)
{
    const char *p = s;
    unsigned int len = 0;
    while (*p++) len++;
    uart_write(s, len);
    uart_write("\n", 1);
    return 0;
}

int putchar(int c)
{
    char ch = (char)c;
    uart_write(&ch, 1);
    return c;
}

int fputc(int c, void *stream)
{
    char ch = (char)c;
    (void)stream;
    uart_write(&ch, 1);
    return c;
}

int fputs(const char *s, void *stream)
{
    const char *p = s;
    unsigned int len = 0;
    (void)stream;
    while (*p++) len++;
    uart_write(s, len);
    return 0;
}

/* ========== Fortified (_chk) function overrides ==========
 * GCC with _FORTIFY_SOURCE converts printf/snprintf/memset calls to
 * __printf_chk/__snprintf_chk/__memset_chk at compile time.
 * These resolve to glibc which crashes bare-metal. Provide our own. */

int __printf_chk(int flag, const char *fmt, ...)
{
    va_list args;
    int len;
    (void)flag;
    va_start(args, fmt);
    len = vprintf(fmt, args);
    va_end(args);
    return len;
}

int __snprintf_chk(char *buf, unsigned int maxlen, int flag,
                   unsigned int buflen, const char *fmt, ...)
{
    va_list args;
    unsigned int size = (maxlen < buflen) ? maxlen : buflen;
    int ret;
    (void)flag;
    va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

extern void *memset(void *s, int c, unsigned int n);
extern void *memcpy(void *dst, const void *src, unsigned int n);

void *__memset_chk(void *s, int c, unsigned int len, unsigned int slen)
{
    (void)slen; /* skip bounds check in bare-metal */
    return memset(s, c, len);
}

void *__memcpy_chk(void *dst, const void *src, unsigned int len,
                   unsigned int dstlen)
{
    (void)dstlen;
    return memcpy(dst, src, len);
}

/* ========== Heap allocator (bare-metal) ==========
 * Simple allocator using _sbrk. Replaces glibc malloc/free/realloc
 * which require TLS and internal glibc state not available bare-metal. */

void *malloc(unsigned int size)
{
    void *p = _sbrk((int)size);
    if (p == (void *)-1)
        return (void *)0;
    return p;
}

void free(void *ptr)
{
    (void)ptr; /* no-op: bare-metal bump allocator doesn't reclaim */
}

void *realloc(void *ptr, unsigned int size)
{
    void *newp;
    if (!ptr)
        return malloc(size);
    newp = malloc(size);
    if (newp)
        memcpy(newp, ptr, size); /* may over-copy, but safe for bump alloc */
    return newp;
}

/* ========== Stdio stubs ==========
 * stdout and fflush referenced by wolfCrypt test/benchmark code. */

static int _stdout_fd = 1;
void *stdout = &_stdout_fd;
void *stderr = &_stdout_fd;

int fflush(void *stream)
{
    (void)stream;
    return 0; /* UART output is unbuffered */
}
