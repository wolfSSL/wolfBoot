/* string.h
 *
 * Implementations of standard library functions to eliminate external dependencies.
 *
 *
 * Copyright (C) 2021 wolfSSL Inc.
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


#include <stddef.h>
#include <string.h>

#ifdef DEBUG_UART
    #include "printf.h"
    #ifdef PRINTF_ENABLED
    #include <stdarg.h>
    #endif
#endif

int islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

int isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

int tolower(int c)
{
    return isupper(c) ? c - 'A' + 'a' : c;
}

int toupper(int c)
{
    return islower(c) ? c - 'a' + 'A' : c;
}

int isalpha(int c)
{
    return (isupper(c) || islower(c));
}

#if !defined(__IAR_SYSTEMS_ICC__) && !defined(PLATFORM_X86_64_EFI)
void *memset(void *s, int c, size_t n)
{
    unsigned char *d = (unsigned char *)s;

    while (n--) {
        *d++ = (unsigned char)c;
    }

    return s;
}
#endif /* IAR */

char *strcat(char *dest, const char *src)
{
    size_t i = 0;
    size_t j = strlen(dest);

    for (i = 0; i < strlen(src); i++) {
        dest[j++] = src[i];
    }
    dest[j] = '\0';

    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    int diff = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;
        s1++;
        s2++;
    }

    return diff;
}

int strcasecmp(const char *s1, const char *s2)
{
    int diff = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;

        if ((diff == 'A' - 'a') || (diff == 'a' - 'A'))
            diff = 0;

        s1++;
        s2++;
    }

    return diff;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    int diff = 0;
    size_t i = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;

        if ((diff == 'A' - 'a') || (diff == 'a' - 'A'))
            diff = 0;

        s1++;
        s2++;
        if (++i > n)
            break;
    }
    return diff;
}

size_t strlen(const char *s)
{
    size_t i = 0;

    while (s[i] != 0)
        i++;

    return i;
}

char *strncat(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    size_t j = strlen(dest);

    for (i = 0; i < strlen(src); i++) {
        if (j >= (n - 1)) {
            break;
        }
        dest[j++] = src[i];
    }
    dest[j] = '\0';

    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    int diff = 0;

    while (n > 0) {
        diff = (unsigned char)*s1 - (unsigned char)*s2;
        if (diff || !*s1)
            break;
        s1++;
        s2++;
        n--;
    }

    return diff;
}

#if  !defined(__IAR_SYSTEMS_ICC__) && !defined(PLATFORM_X86_64_EFI)
#include "image.h"
void RAMFUNCTION *memcpy(void *dst, const void *src, size_t n)
{
    size_t i;
    const char *s = (const char *)src;
    char *d = (char *)dst;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dst;
}
#endif /* IAR */

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0')
            break;
    }

    return dst;
}

char *strcpy(char *dst, const char *src)
{
   size_t i = 0;

    while(1) {
        dst[i] = src[i];
        if (src[i] == '\0')
            break;
        i++;
    }

    return dst;
}


int memcmp(const void *_s1, const void *_s2, size_t n)
{
    int diff = 0;
    const unsigned char *s1 = (const unsigned char *)_s1;
    const unsigned char *s2 = (const unsigned char *)_s2;

    while (!diff && n) {
        diff = (int)*s1 - (int)*s2;
        s1++;
        s2++;
        n--;
    }

    return diff;
}

#ifndef __IAR_SYSTEMS_ICC__
void *memmove(void *dst, const void *src, size_t n)
{
    int i;
    if (dst == src)
        return dst;
    if (src < dst)  {
        const char *s = (const char *)src;
        char *d = (char *)dst;
        for (i = n - 1; i >= 0; i--) {
            d[i] = s[i];
        }
        return dst;
    } else {
        return memcpy(dst, src, n);
    }
}
#endif

#if defined(PRINTF_ENABLED) && defined(DEBUG_UART)
void uart_writenum(int num, int base)
{
    int i = 0;
    char buf[sizeof(int)*2+1];
    const char* kDigitLut = "0123456789ABCDEF";
    unsigned int val = (unsigned int)num;
#ifdef BIG_ENDIAN_ORDER
    int sz = 0;
#endif
    if (base == 10 && num < 0) { /* handle negative */
        buf[i++] = '-';
        val = -num;
    }
    do {
    #ifdef BIG_ENDIAN_ORDER
        buf[sizeof(buf)-sz-1] = kDigitLut[(val % base)];
        sz++;
    #else
        buf[i++] = kDigitLut[(val % base)];
    #endif
        val /= base;
    } while (val > 0U);
#ifdef BIG_ENDIAN_ORDER
    memmove(&buf[i], &buf[sizeof(buf)-sz], sz);
    i+=sz;
#endif
    uart_write(buf, i);
}

void uart_vprintf(const char* fmt, va_list argp)
{
    char* fmtp = (char*)fmt;
    while (fmtp != NULL && *fmtp != '\0') {
        /* print non formatting characters */
        if (*fmtp != '%') {
            uart_write(fmtp++, 1);
            continue;
        }
        fmtp++; /* skip % */

        /* find formatters */
        while (*fmtp != '\0') {
            if (*fmtp >= '0' && *fmtp <= '9') {
                /* length formatter - skip */
                fmtp++;
            }
            else if (*fmtp == 'l') {
                /* long - skip */
                fmtp++;
            }
            else if (*fmtp == 'z') {
                /* auto type - skip */
                fmtp++;
            }
            else {
                break;
            }
        }

        switch (*fmtp) {
            case '%':
                uart_write(fmtp, 1);
                break;
            case 'u':
            case 'i':
            case 'd':
            {
                int n = (int)va_arg(argp, int);
                uart_writenum(n, 10);
                break;
            }
            case 'x':
            case 'p':
            {
                int n = (int)va_arg(argp, int);
                uart_writenum(n, 16);
                break;
            }
            case 's':
            {
                char* str = (char*)va_arg(argp, char*);
                uart_write(str, (uint32_t)strlen(str));
                break;
            }
            case 'c':
            {
                char c = (char)va_arg(argp, int);
                uart_write(&c, 1);
                break;
            }
            default:
                break;
        }
        fmtp++;
    };
}
void uart_printf(const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    uart_vprintf(fmt, argp);
    va_end(argp);
}
#endif /* PRINTF_ENABLED && DEBUG_UART */
