/* printf.h (host test stub)
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* boot_state.c logs its decisions with wolfBoot_printf; the host test asserts
 * on return values, so the log is dropped to keep PASS/FAIL output clean. */
#ifndef UNIT_BOOT_STATE_PRINTF_H
#define UNIT_BOOT_STATE_PRINTF_H
#define wolfBoot_printf(...) do {} while (0)
#endif
