/* user_settings/hash_dispatch.h
 *
 * Dispatches to a HASH-family fragment based on which WOLFBOOT_HASH_*
 * flag is set. SHA-256 is the foundation default and needs no fragment.
 *
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
#ifndef _WOLFBOOT_USER_SETTINGS_HASH_DISPATCH_H_
#define _WOLFBOOT_USER_SETTINGS_HASH_DISPATCH_H_

#ifdef WOLFBOOT_HASH_SHA3_384
#  include "hash_sha3.h"
#endif

#ifdef WOLFBOOT_HASH_SHA384
#  include "hash_sha384.h"
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_HASH_DISPATCH_H_ */
