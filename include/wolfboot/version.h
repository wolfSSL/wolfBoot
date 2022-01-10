/* version.h
 *
 * The wolfBoot library version
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

#ifndef WOLFBOOT_VERSION_H
#define WOLFBOOT_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif


#define LIBWOLFBOOT_VERSION_STRING "1.10.0"
#define LIBWOLFBOOT_VERSION_HEX 0x010a0000

#ifndef WOLFBOOT_VERSION
    #define WOLFBOOT_VERSION LIBWOLFBOOT_VERSION_HEX
#endif

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_VERSION_H */
