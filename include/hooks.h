/* hooks.h
 *
 * wolfBoot hooks API definitions.
 *
 * Hooks allow users to inject custom logic at well-defined points in the
 * wolfBoot boot process. Each hook is independently enabled via its own
 * build macro.
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

#ifndef WOLFBOOT_HOOKS_H
#define WOLFBOOT_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

struct wolfBoot_image;

#ifdef WOLFBOOT_HOOK_LOADER_PREINIT
void wolfBoot_hook_preinit(void);
#endif

#ifdef WOLFBOOT_HOOK_LOADER_POSTINIT
void wolfBoot_hook_postinit(void);
#endif

#ifdef WOLFBOOT_HOOK_BOOT
void wolfBoot_hook_boot(struct wolfBoot_image *boot_img);
#endif

#ifdef WOLFBOOT_HOOK_PANIC
void wolfBoot_hook_panic(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_HOOKS_H */
