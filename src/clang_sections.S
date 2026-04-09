
/* clang_sections.S
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

/* Workaround for GNU ld mishandling AT() on output sections when no input
 * object contributes a matching section.  Clang does not emit empty .data/.bss
 * sections in object files (GCC does), so GNU ld fails to assign the correct
 * LMA for the .data output section, producing a huge sparse binary.
 * Including this object ensures .data and .bss exist as inputs.
 */
.section .data
.section .bss
