/* mpfs250_ubl.h
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

/* PolarFire SoC boot mode 2 User Boot Loader (UBL) stub.
 *
 * The System Controller releases all five harts at the stub entry.  The
 * E51 verifies the eNVM wolfBoot manifest; the U54s park in
 * src/boot_riscv_start.S on a DTIM mailbox until the E51 publishes the
 * verified entry.  The mailbox sits at the top of the E51 DTIM, above
 * everything the eNVM wolfBoot uses (hal/mpfs250.h).  No UL suffixes:
 * also used from assembly. */

#ifndef MPFS250_UBL_H
#define MPFS250_UBL_H

/* DTIM content is undefined at power-on and the U54s may read the mailbox
 * before the E51 has cleared it, so the entry is valid only with its
 * complement and the magic beside it; the E51 writes the magic last. */
#define MPFS_UBL_MAGIC_ADDR     0x01001F00  /* u32 MPFS_UBL_MAGIC */
#define MPFS_UBL_ENTRY_ADDR     0x01001F08  /* u64 entry */
#define MPFS_UBL_ENTRY_INV_ADDR 0x01001F10  /* u64 ~entry */
#define MPFS_UBL_MAGIC          0x55424C21  /* "UBL!" */
/* One 32-bit acknowledgement word per hart, written before it jumps. */
#define MPFS_UBL_ACK_BASE       0x01001F20

#endif /* MPFS250_UBL_H */
