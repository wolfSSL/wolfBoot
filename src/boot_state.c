/* boot_state.c
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

#include "user_settings.h"
#include "boot_state.h"
#include "printf.h"
#include "hal.h"
#include <string.h>

#ifdef WOLFBOOT_ANTI_ROLLBACK

#define CRC32_POLY_REVERSED 0xEDB88320UL
#define CRC32_INIT          0xFFFFFFFFUL
#define CRC32_BITS_PER_BYTE 8

/* Bitwise CRC-32 (IEEE 802.3). Table-free: the record is 40 bytes and this
 * runs once or twice per boot, so the table is not worth the footprint. */
static uint32_t boot_state_crc32(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = CRC32_INIT;
    uint32_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i];
        for (bit = 0; bit < CRC32_BITS_PER_BYTE; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ CRC32_POLY_REVERSED;
            }
            else {
                crc >>= 1;
            }
        }
    }
    return crc ^ CRC32_INIT;
}

/* The CRC covers every byte of the record except the trailing crc field. */
static uint32_t boot_state_compute_crc(const struct boot_state *st)
{
    return boot_state_crc32((const uint8_t *)st,
                            (uint32_t)(sizeof(struct boot_state)
                                       - sizeof(st->crc)));
}

static void boot_state_log(const char *msg)
{
    wolfBoot_printf("%s\r\n", msg);
}

static void boot_state_log_val(const char *msg, uint32_t value)
{
    wolfBoot_printf("%s%u\r\n", msg, value);
}

int boot_state_load(struct boot_state *st)
{
    int ret;

    if (st == NULL) {
        return BOOT_STATE_UNAVAILABLE;
    }

    memset(st, 0, sizeof(struct boot_state));

    ret = boot_state_backend_read((uint8_t *)st, sizeof(struct boot_state));
    if (ret == BOOT_STATE_UNPROVISIONED) {
        return BOOT_STATE_UNPROVISIONED;
    }
    /* A present but malformed record (e.g. a short read from a wrong-sized NV
     * index) is corrupt, not unavailable: the reference exists and cannot be
     * trusted, which must halt the boot rather than be waved through as a
     * merely-unreachable backend. */
    if (ret == BOOT_STATE_CORRUPT) {
        return BOOT_STATE_CORRUPT;
    }
    if (ret != 0) {
        return BOOT_STATE_UNAVAILABLE;
    }

    /* Storage exists. From here on anything unexpected means the reference
     * cannot be trusted, which is the one condition that must stop the boot. */
    if (st->magic != BOOT_STATE_MAGIC) {
        return BOOT_STATE_CORRUPT;
    }
    if (st->struct_version != BOOT_STATE_VERSION) {
        return BOOT_STATE_CORRUPT;
    }
    if (st->crc != boot_state_compute_crc(st)) {
        return BOOT_STATE_CORRUPT;
    }
    return BOOT_STATE_OK;
}

/* Persist, refreshing the CRC first. Failures are logged but not fatal: losing
 * an audit write must not turn a good image into an unbootable unit. */
static void boot_state_save(struct boot_state *st)
{
    st->magic = BOOT_STATE_MAGIC;
    st->struct_version = BOOT_STATE_VERSION;
    st->reserved = 0;
    st->crc = boot_state_compute_crc(st);

    if (boot_state_backend_write((const uint8_t *)st,
                                 sizeof(struct boot_state)) != 0) {
#if defined(WOLFBOOT_ANTI_ROLLBACK_STRICT)
        boot_state_log("could not persist anti-rollback state, refusing to boot");
        /* Never returns. */
        hal_hold_in_reset();
#else
        boot_state_log("WARNING: could not persist boot state");
#endif
    }
}

/* Load for update. A corrupt reference halts here, which is the whole point
 * of the check. Unprovisioned is a clean slate, not a fault. */
static int boot_state_load_for_update(struct boot_state *st)
{
    int status = boot_state_load(st);

    if (status == BOOT_STATE_CORRUPT) {
        boot_state_log("Version reference is corrupt, refusing to boot");
        /* Never returns. */
        hal_hold_in_reset();
    }
    if (status == BOOT_STATE_UNAVAILABLE) {
#if defined(WOLFBOOT_ANTI_ROLLBACK_STRICT)
        boot_state_log("anti-rollback state unavailable, refusing to boot");
        /* Never returns. */
        hal_hold_in_reset();
#else
        boot_state_log("WARNING: boot state backend unavailable");
#endif
    }
    return status;
}

static int boot_state_update(uint32_t booted_version)
{
    struct boot_state st;
    struct boot_state before;
    int status;

    status = boot_state_load_for_update(&st);
    if (status == BOOT_STATE_UNAVAILABLE) {
        return status;
    }

    if (status == BOOT_STATE_UNPROVISIONED) {
        /* Trust on first use: this image defines the floor. */
        memset(&st, 0, sizeof(st));
        st.anchor = booted_version;
        st.record = booted_version;
        boot_state_log_val("Anti-rollback anchor initialised at version ",
                           booted_version);
        boot_state_save(&st);
        return status;
    }

    /* Anything other than a trustworthy record must not be persisted over.
     * A corrupt reference already halts in boot_state_load_for_update(); this
     * guard keeps a freshly-zeroed anchor from ever replacing it should that
     * halt path change. */
    if (status != BOOT_STATE_OK) {
        return status;
    }

    /* Snapshot so the write can be skipped when nothing actually changes. */
    memcpy(&before, &st, sizeof(before));

    if (booted_version < st.anchor) {
        /* Explicitly allowed: an older but validly signed image boots. The
         * audit event is the deliverable, not a veto. */
        st.rollback_count++;
        st.last_rollback_from = st.anchor;
        st.last_rollback_to = booted_version;
        boot_state_log_val("ROLLBACK: booting version ", booted_version);
        boot_state_log_val("ROLLBACK: highest version previously booted was ",
                           st.anchor);
        boot_state_log_val("ROLLBACK: total rollbacks recorded ",
                           st.rollback_count);
    }
    else if (booted_version > st.anchor) {
        st.anchor = booted_version;
    }

    st.record = booted_version;

    /* A unit rebooting the same image changes nothing, so skip the write and
     * spend no NV write endurance on it. Compare everything except the CRC,
     * which is recomputed on save. */
    if (memcmp(&before, &st, sizeof(st) - sizeof(st.crc)) == 0) {
        return status;
    }

    boot_state_save(&st);
    return status;
}

void boot_state_on_boot(uint32_t booted_version)
{
    if (boot_state_update(booted_version) == BOOT_STATE_UNAVAILABLE) {
        return;
    }
    /* Lock on every boot, including those that wrote nothing, so the OS can
     * never move the floor. */
    if (boot_state_backend_lock() != 0) {
#if defined(WOLFBOOT_ANTI_ROLLBACK_STRICT)
        boot_state_log("could not lock anti-rollback state, refusing to boot");
        /* Never returns. */
        hal_hold_in_reset();
#else
        boot_state_log("WARNING: could not lock boot state");
#endif
    }
}

void boot_state_on_failure(uint32_t fail_code)
{
    struct boot_state st;
    int status;

    status = boot_state_load(&st);
    if (status == BOOT_STATE_UNAVAILABLE) {
        return;
    }
    if (status == BOOT_STATE_CORRUPT) {
        /* Deliberately do not persist here. Writing a fresh record over a
         * corrupt reference would replace the corruption with a valid, zeroed
         * anchor - turning the one condition that must stop the boot into
         * something the next boot silently recovers from, with the rollback
         * floor reset to zero. Report it and leave the stored state alone. */
        boot_state_log("Boot failure with a corrupt version reference; "
                       "not persisting over it");
        return;
    }
    if (status == BOOT_STATE_UNPROVISIONED) {
        /* Nothing stored yet: start a record so the failure is still captured. */
        memset(&st, 0, sizeof(st));
    }

    st.boot_fail_count++;
    st.last_fail_code = fail_code;
    boot_state_log_val("Recording boot failure, code ", fail_code);
    boot_state_save(&st);
}

#endif /* WOLFBOOT_ANTI_ROLLBACK */
