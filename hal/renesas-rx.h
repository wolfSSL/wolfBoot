/* renesas-rx.h
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#ifndef _WOLFBOOT_RENESAS_RX_H_
#define _WOLFBOOT_RENESAS_RX_H_

#ifdef BIG_ENDIAN_ORDER
    #define ENDIAN_BIT8( n, bits)      (1 << (8- (n)))
    #define ENDIAN_BIT16(n, bits)      (1 << (16-(n)))
    #define ENDIAN_BIT32(n, bits)      (1 << (32-(n)))
    #define ENDIAN_VAL8( val, n, bits) ((val) << (8- (n)))
    #define ENDIAN_VAL16(val, n, bits) ((val) << (16-(n)))
    #define ENDIAN_VAL32(val, n, bits) ((val) << (32-(n)))
#else
    #define ENDIAN_BIT8( n)      (1 << (n))
    #define ENDIAN_BIT16(n)      (1 << (n))
    #define ENDIAN_BIT32(n)      (1 << (n))
    #define ENDIAN_VAL8( val, n) ((val) << (n))
    #define ENDIAN_VAL16(val, n) ((val) << (n))
    #define ENDIAN_VAL32(val, n) ((val) << (n))
#endif

#if defined(__CCRX__)
    #define RX_NOP() nop()
#elif defined(__GNUC__)
    #define RX_NOP() __asm("nop")
#elif defined(__ICCRX__)
    #define RX_NOP() __no_operation()
#endif


#endif /* !_WOLFBOOT_RENESAS_RX_H_ */
