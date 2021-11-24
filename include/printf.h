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

#ifndef WOLFBOOT_PRINTF_INCLUDED
#define WOLFBOOT_PRINTF_INCLUDED

#if defined(DEBUG_ZYNQ) && !defined(PRINTF_ENABLED)
#    define PRINTF_ENABLED
#endif

#if defined(WOLFBOOT_DEBUG_EFI) && !defined(PRINTF_ENABLED)
#    define PRINTF_ENABLED
#endif

#ifdef PRINTF_ENABLED
#   include <stdio.h>
#   if defined(DEBUG_ZYNQ) && !defined(USE_QNX)
#       include "xil_printf.h"
#       define wolfBoot_printf(_f_, ...) xil_printf(_f_, ##__VA_ARGS__)
#   elif defined(WOLFBOOT_DEBUG_EFI)
        /* NOTE: %s arguments will not work as EFI uses widechar string */
#       define wolfBoot_printf(_f_, ...) Print(L##_f_, ##__VA_ARGS__)
#   else
#       define wolfBoot_printf(_f_, ...) printf(_f_, ##__VA_ARGS__)
#   endif
#else
#   define wolfBoot_printf(_f_, ...) do{}while(0)
#endif

#endif /* !WOLFBOOT_PRINTF_INCLUDED */
