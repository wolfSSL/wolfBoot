/* user_settings.h (host test stub)
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

/* Shadows the build's user_settings.h so boot_state.c can be compiled on the
 * host without a target configuration. The anti-rollback logic needs nothing
 * from it; the feature is selected with -DWOLFBOOT_ANTI_ROLLBACK. */
#ifndef UNIT_BOOT_STATE_USER_SETTINGS_H
#define UNIT_BOOT_STATE_USER_SETTINGS_H
#endif
