/* ubootenv.h
 *
 * U-Boot environment based A/B slot selection, RAUC-compatible.
 *
 * wolfBoot replicates the slot arbitration a RAUC "bootloader=uboot" setup
 * normally performs in a U-Boot boot script: read BOOT_ORDER and the per-slot
 * BOOT_<name>_LEFT try counters from a shared U-Boot environment, pick the first
 * slot in BOOT_ORDER with tries remaining, decrement its counter, and hand the
 * kernel "root=<dev> rauc.slot=<name>". RAUC (userspace, via fw_setenv) writes
 * the same environment. The environment lives in a raw region wolfBoot can read
 * with disk_part_read()/disk_part_write() (wolfBoot has no filesystem support).
 *
 * Environment layout (U-Boot "simple", non-redundant):
 *   [ crc32 : 4 bytes little-endian ][ data : env_len - 4 bytes ]
 * data is a NUL-separated list of "key=value" entries terminated by an empty
 * key (an extra NUL); trailing space is zero padding, and the crc32 covers the
 * whole data region including the padding.
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
#ifndef UBOOTENV_H
#define UBOOTENV_H

#include <stdint.h>
#include <stddef.h>

/* Default environment size (matches GCX fw_env.config: 0x4000 = 16 KiB). */
#ifndef UBOOT_ENV_SIZE
#define UBOOT_ENV_SIZE 0x4000
#endif

/* Default number of boot attempts a freshly (re)armed slot gets. */
#ifndef UBOOT_ENV_DEFAULT_TRIES
#define UBOOT_ENV_DEFAULT_TRIES 3
#endif

/* Longest slot value we handle in bootargs building. */
#define UBOOT_ENV_VAL_MAX 64

/* Selected slot returned by uboot_env_select_slot(). */
struct uboot_slot {
    char name[UBOOT_ENV_VAL_MAX]; /* RAUC bootname, e.g. "A" or "B" */
    int  selected;                /* 1 if a slot with tries left was chosen */
    int  rearmed;                 /* 1 if all counters were exhausted+reset */
    int  reinitialized;           /* 1 if the env failed CRC and was reset to
                                   * defaults in memory: the caller must NOT
                                   * persist the buffer (it would destroy the
                                   * last-good on-disk RAUC state). */
};

/* Verify the leading CRC32 over the data region. Returns 1 if valid, else 0. */
int uboot_env_verify(const uint8_t *env, size_t env_len);

/* Recompute and store the leading CRC32 over the data region. */
void uboot_env_reseal(uint8_t *env, size_t env_len);

/* Copy the value of <key> into val_out (NUL-terminated, bounded by val_max).
 * Returns the value length on success, or -1 if the key is absent. */
int uboot_env_get(const uint8_t *env, size_t env_len,
    const char *key, char *val_out, size_t val_max);

/* Set (overwrite or append) <key>=<val> in the data region. Does NOT reseal.
 * Returns 0 on success, -1 if it would not fit. */
int uboot_env_set(uint8_t *env, size_t env_len, const char *key,
    const char *val);

/* Run the A/B boot-slot state machine on an in-memory environment image:
 *   - if the CRC32 is invalid, reinitialize defaults (BOOT_ORDER "A B",
 *     BOOT_<name>_LEFT = UBOOT_ENV_DEFAULT_TRIES);
 *   - pick the first name in BOOT_ORDER whose BOOT_<name>_LEFT > 0, decrement
 *     that counter, and report it in *out (out->selected = 1);
 *   - if none have tries left, re-arm every counter to the default, set
 *     out->rearmed = 1 and out->selected = 0 (the caller should reboot);
 *   - reseal the CRC32.
 * The env buffer is modified in place; the caller writes it back to storage.
 * Returns 0 on success, -1 on a malformed BOOT_ORDER / buffer error. */
int uboot_env_select_slot(uint8_t *env, size_t env_len, struct uboot_slot *out);

#endif /* UBOOTENV_H */
