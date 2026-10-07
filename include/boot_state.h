/* boot_state.h
 *
 * Persistent anti-rollback reference and boot audit counters.
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

/*
 * Anti-rollback keeps two version numbers plus a small set of audit counters.
 *
 *   anchor  the highest image version ever successfully booted. Monotonically
 *           non-decreasing. Booting anything below it is a rollback.
 *   record  the version booted most recently. Follows the image actually
 *           booted, so unlike the anchor it can go down.
 *
 * The requirement is that a validly signed but older image still boots, and
 * only leaves a persistent audit event behind; the single condition that
 * halts the boot is a version reference that cannot be trusted. So a corrupt
 * anchor is fatal, while a rollback is merely recorded.
 *
 * The anchor is initialised trust-on-first-use: the first image that boots
 * successfully on an unprovisioned unit defines the floor. That keeps units
 * deployable with no provisioning step, at the cost that whatever runs first
 * sets the baseline -- if a unit is powered up with a downgraded image before
 * it is trusted, that lower version becomes the anchor. Provision before
 * first boot where that matters.
 *
 * Storage is deliberately compact: one record of counters rather than an
 * event log, because the v1 backend is TPM NV, which is small and has limited
 * write endurance. Full detail still goes to the boot console.
 *
 * The backend is reached only through boot_state_backend_{read,write}(), so
 * moving the reference off the TPM later -- for instance into the anti-rollback
 * registers of a root-of-trust device -- is a backend swap and not a change to
 * any of the policy above.
 */

#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stdint.h>

#define BOOT_STATE_MAGIC   0x42535431UL /* "BST1" */
#define BOOT_STATE_VERSION 1

/* Result of loading the persisted state. */
#define BOOT_STATE_OK            0  /* loaded and trustworthy */
#define BOOT_STATE_UNPROVISIONED 1  /* nothing stored yet; not an error */
#define BOOT_STATE_CORRUPT     (-1) /* stored but unusable -- fatal */
#define BOOT_STATE_UNAVAILABLE (-2) /* backend could not be reached */

/* Failure codes recorded in last_fail_code. */
#define BOOT_FAIL_NONE            0
#define BOOT_FAIL_NO_IMAGE        1
#define BOOT_FAIL_VERIFY_PRIMARY  2
#define BOOT_FAIL_VERIFY_BOTH     3
#define BOOT_FAIL_ROLLBACK        4

/* On-storage layout. Packed and explicitly sized so the representation does
 * not shift between the bootloader and any provisioning tool. */
struct boot_state {
    uint32_t magic;
    uint32_t anchor;
    uint32_t record;
    uint32_t rollback_count;
    uint32_t last_rollback_from;
    uint32_t last_rollback_to;
    uint32_t boot_fail_count;
    uint32_t last_fail_code;
    uint16_t struct_version;
    uint16_t reserved;
    uint32_t crc;
} __attribute__((packed));

/* Backend, implemented per platform. v1 uses TPM NV (src/boot_state_tpm.c). */
int boot_state_backend_read(uint8_t *buf, uint32_t len);
int boot_state_backend_write(const uint8_t *buf, uint32_t len);
/* Write-locks the record until the next TPM reset, so the booted OS cannot
 * rewrite it. */
int boot_state_backend_lock(void);

/* Load the persisted state. Returns one of the BOOT_STATE_* codes above. */
int boot_state_load(struct boot_state *st);

/* Called once an image has verified, immediately before handing off.
 * Applies the rollback check, updates anchor and record, and persists. */
void boot_state_on_boot(uint32_t booted_version);

/* Called when the boot policy has decided no image may be booted. Records the
 * failure so the evidence survives the power cycle. */
void boot_state_on_failure(uint32_t fail_code);

#endif /* BOOT_STATE_H */
