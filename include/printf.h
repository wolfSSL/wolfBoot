/* printf.h
 *
 * The HAL API definitions.
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

#ifndef WOLFBOOT_PRINTF_INCLUDED
#define WOLFBOOT_PRINTF_INCLUDED

#if defined(DEBUG_ZYNQ) && !defined(PRINTF_ENABLED)
#   define PRINTF_ENABLED
#endif
#if defined(WOLFBOOT_DEBUG_EFI) && !defined(PRINTF_ENABLED)
#   define PRINTF_ENABLED
#endif
#if defined(ARCH_SIM) && !defined(PRINTF_ENABLED)
#   define PRINTF_ENABLED
#endif

#if defined(DEBUG_UART)
    #if !defined(UART_FLASH) && !defined(WOLFBOOT_FSP)
        void uart_init(void);
    #endif
    void uart_write(const char* buf, unsigned int sz);

    /* turn on small printf support in string.c */
    #if !defined(PRINTF_ENABLED) && !defined(NO_PRINTF_UART)
        #define PRINTF_ENABLED
    #endif
#endif

/* support for wolfBoot_printf logging */
#if defined(PRINTF_ENABLED) && !defined(WOLFBOOT_NO_PRINTF)
#   include <stdio.h>
#   if defined(DEBUG_ZYNQ) && !defined(USE_QNX) && !defined(DEBUG_UART)
#       include "xil_printf.h"
#       define wolfBoot_printf(_f_, ...) xil_printf(_f_, ##__VA_ARGS__)
#   elif defined(WOLFBOOT_DEBUG_EFI)
#       include "efi/efi.h"
#       include "efi/efilib.h"
        /* NOTE: %s arguments will not work as EFI uses widechar string */
#       define wolfBoot_printf(_f_, ...) Print(L##_f_, ##__VA_ARGS__)
#   elif defined(DEBUG_UART)
        /* use minimal printf support in string.h */
        void uart_printf(const char* fmt, ...);
#       define wolfBoot_printf(_f_, ...) uart_printf(_f_, ##__VA_ARGS__)
#   elif defined(__CCRX__)
#       define wolfBoot_printf printf
#   elif defined(WOLFBOOT_LOG_PRINTF)
        /* allow output to stdout */
#       define wolfBoot_printf(_f_, ...) printf(_f_, ##__VA_ARGS__)
#   else
        /* use stderr by default */
#       define wolfBoot_printf(_f_, ...) fprintf(stderr, _f_, ##__VA_ARGS__)
#   endif
#else
#   define wolfBoot_printf(_f_, ...) do{}while(0)
#endif

#endif /* !WOLFBOOT_PRINTF_INCLUDED */
