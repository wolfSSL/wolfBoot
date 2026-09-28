/* mpfs250_cal.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Microchip's CAL library headers for the Athena F5200, kept apart from the
 * HAL because they need two defines set and one type renamed to coexist with
 * wolfSSL.  CAL is referenced, never vendored: it carries a Mercury Systems
 * notice.  MPFS_ATHENA_CAL_DIR puts them on the include path. */

#ifndef MPFS250_CAL_H
#define MPFS250_CAL_H

/* Guarded because the HSS copy of calpolicy.h already defines both. */
#ifndef CALCONFIGH
#define CALCONFIGH "config_user.h"
#endif
#ifndef INC_STDINT_H
#define INC_STDINT_H            /* make caltypes.h use <stdint.h> */
#endif

/* caltypes.h and wolfSSL's types.h both define a type named uint128_t, a
 * union of words there and __uint128_t here; nothing below needs CAL's. */
#define uint128_t cal_uint128_t
#include "calini.h"
#include "hash.h"
#include "sym.h"
#undef uint128_t

#endif /* MPFS250_CAL_H */
