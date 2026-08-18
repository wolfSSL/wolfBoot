/* c2000_stdint.h
 *
 * Force-included (--preinclude) for the TI C2000 C28x (ARCH=C2000) build.
 *
 * The C28x is word-addressed with CHAR_BIT==16 and has NO 8-bit integer type,
 * so ISO <stdint.h> correctly does not define int8_t/uint8_t on this target.
 * wolfBoot (and TI's own driverlib hw_types.h) represents an octet in a 16-bit
 * cell, so provide the exact-width 8-bit aliases as 16-bit types.  When a
 * translation unit also pulls in driverlib's hw_types.h, its identical
 * typedefs produce diagnostic #303, suppressed for this arch in arch.mk.
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

#ifndef WOLFBOOT_C2000_STDINT_H
#define WOLFBOOT_C2000_STDINT_H

#include <stdint.h>

/* Guarded to the C28x: this header is --preinclude'd only for ARCH=C2000, but
 * the guard keeps the 8-bit aliases from ever redefining a real int8_t/uint8_t
 * should it be pulled into a normal-byte translation unit. */
#if defined(__TMS320C28XX__)
typedef uint16_t uint8_t;
typedef int16_t  int8_t;
#endif

#endif /* WOLFBOOT_C2000_STDINT_H */
