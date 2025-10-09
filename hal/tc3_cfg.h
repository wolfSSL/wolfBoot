/* tc3_cfg.h
 *
 * Copyright (C) 2014-2025 wolfSSL Inc.
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
 * along with wolfBoot.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TC3_CFG_H
#define TC3_CFG_H

#if defined(WOLFBOOT_AURIX_TC3XX_HSM)
#define TC3_CFG_HAVE_ARM
#else
#define TC3_CFG_HAVE_TRICORE
#endif

#define TC3_CFG_HAVE_BOARD
#define TC3_BOARD_TC375LITEKIT 1
#define TC3_CFG_HAVE_WOLFBOOT

#endif /* TC3_CFG_H */
