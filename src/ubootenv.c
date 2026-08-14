/* ubootenv.c
 *
 * U-Boot environment based A/B slot selection, RAUC-compatible.
 * See include/ubootenv.h for the format and the flow it replicates.
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

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ubootenv.h"

/* Standard (zlib) CRC32 - self-contained so this module has no dependencies and
 * is trivially unit-testable. Matches the CRC32 U-Boot uses for its env. */
static uint32_t ubootenv_crc32(const uint8_t *p, size_t len)
{
    uint32_t crc;
    size_t i;
    int b;

    crc = 0xFFFFFFFFUL;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (b = 0; b < 8; b++) {
            if ((crc & 1U) != 0U)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Parse a small non-negative decimal; -1 on a non-numeric value. */
static long env_atol(const char *s)
{
    long v;

    if (s == NULL)
        return -1;
    while (*s == ' ')
        s++;
    if (*s < '0' || *s > '9')
        return -1;
    v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (long)(*s - '0');
        s++;
    }
    return v;
}

/* Format a non-negative decimal into out (caller supplies >= 16 bytes). */
static void env_ltoa(long v, char *out)
{
    char tmp[16];
    int i;
    int j;

    i = 0;
    j = 0;
    if (v <= 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = '\0';
}

/* Build "BOOT_<name>_LEFT" into out. Returns 0 on success, -1 if it would not
 * fit - refusing rather than silently dropping the "_LEFT" suffix (which would
 * look up the wrong / a colliding counter key). The caller skips such a slot. */
static int env_leftkey(const char *name, char *out, size_t out_max)
{
    const char *pre = "BOOT_";
    const char *suf = "_LEFT";
    size_t prel = 5; /* strlen("BOOT_") */
    size_t sufl = 5; /* strlen("_LEFT") */
    size_t n = 0;

    if (prel + strlen(name) + sufl + 1 > out_max)
        return -1;
    while (*pre != '\0')
        out[n++] = *pre++;
    while (*name != '\0')
        out[n++] = *name++;
    while (*suf != '\0')
        out[n++] = *suf++;
    out[n] = '\0';
    return 0;
}

int uboot_env_verify(const uint8_t *env, size_t env_len)
{
    if (env == NULL || env_len < 5)
        return 0;
    return (rd_le32(env) == ubootenv_crc32(env + 4, env_len - 4)) ? 1 : 0;
}

/* U-Boot's *redundant* environment layout is [crc32:4][flags:1][data]; its
 * CRC covers the data starting at offset 5, not 4. Detect it so a valid
 * redundant env (a common RAUC choice) is refused rather than misread as
 * corruption and wiped. Returns 1 if the buffer validates as a redundant env. */
static int uboot_env_is_redundant(const uint8_t *env, size_t env_len)
{
    if (env == NULL || env_len < 6)
        return 0;
    return (rd_le32(env) == ubootenv_crc32(env + 5, env_len - 5)) ? 1 : 0;
}

void uboot_env_reseal(uint8_t *env, size_t env_len)
{
    if (env == NULL || env_len < 5)
        return;
    wr_le32(env, ubootenv_crc32(env + 4, env_len - 4));
}

int uboot_env_get(const uint8_t *env, size_t env_len, const char *key,
    char *val_out, size_t val_max)
{
    const char *data;
    const char *end;
    const char *p;
    size_t keylen;

    if (env == NULL || key == NULL || val_out == NULL || env_len < 5 ||
            val_max == 0)
        return -1;
    data = (const char *)(env + 4);
    end = data + (env_len - 4);
    keylen = strlen(key);
    p = data;
    while (p < end && *p != '\0') {
        const char *entry = p;
        size_t elen;
        while (p < end && *p != '\0')
            p++;
        elen = (size_t)(p - entry);
        if (p < end)
            p++; /* skip the entry NUL */
        if (elen > keylen && entry[keylen] == '=' &&
                memcmp(entry, key, keylen) == 0) {
            const char *v = entry + keylen + 1;
            size_t vlen = elen - keylen - 1;
            if (vlen >= val_max)
                vlen = val_max - 1;
            memcpy(val_out, v, vlen);
            val_out[vlen] = '\0';
            return (int)vlen;
        }
    }
    return -1;
}

int uboot_env_set(uint8_t *env, size_t env_len, const char *key,
    const char *val)
{
    uint8_t *data;
    uint8_t *end;
    uint8_t *p;
    uint8_t *list_end;
    size_t keylen;
    size_t vlen;
    size_t entrylen;
    size_t old_total;
    size_t avail;

    if (env == NULL || key == NULL || val == NULL || env_len < 5)
        return -1;
    data = env + 4;
    end = env + env_len;
    keylen = strlen(key);
    vlen = strlen(val);

    /* 1) Measure the space the existing entries for key occupy plus the free
     *    space, WITHOUT modifying anything, so a would-not-fit result below is
     *    non-destructive (never deletes the old value on failure). */
    old_total = 0;
    p = data;
    while (p < end && *p != '\0') {
        uint8_t *entry = p;
        size_t elen;
        while (p < end && *p != '\0')
            p++;
        elen = (size_t)(p - entry);
        if (p < end)
            p++; /* include the NUL */
        if (elen > keylen && entry[keylen] == '=' &&
                memcmp(entry, key, keylen) == 0)
            old_total += (size_t)(p - entry);
    }
    list_end = p; /* start of the terminating NUL run */
    entrylen = keylen + 1 + vlen + 1;
    avail = (size_t)(end - list_end) + old_total; /* freed by removal + tail */
    if (entrylen + 1 > avail)                     /* +1 keeps the terminator */
        return -1;

    /* 2) Remove any existing entry for key (shift the remainder down). */
    p = data;
    while (p < end && *p != '\0') {
        uint8_t *entry = p;
        size_t elen;
        while (p < end && *p != '\0')
            p++;
        elen = (size_t)(p - entry);
        if (p < end)
            p++; /* include the NUL */
        if (elen > keylen && entry[keylen] == '=' &&
                memcmp(entry, key, keylen) == 0) {
            size_t tail = (size_t)(end - p);
            memmove(entry, p, tail);
            memset(entry + tail, 0, (size_t)(end - (entry + tail)));
            p = entry; /* continue scanning from the compacted position */
        }
    }

    /* 3) Append "key=val\0"; the following byte stays NUL (list terminator). */
    list_end = data;
    while (list_end < end && *list_end != '\0') {
        uint8_t *e = list_end;
        while (e < end && *e != '\0')
            e++;
        if (e < end)
            e++;
        list_end = e;
    }
    memcpy(list_end, key, keylen);
    list_end[keylen] = '=';
    memcpy(list_end + keylen + 1, val, vlen);
    list_end[keylen + 1 + vlen] = '\0';
    return 0;
}

/* Copy the next space-separated token of BOOT_ORDER into name; advance *po.
 * Returns the token length (0 when the list is exhausted). */
static int env_next_name(const char **po, char *name, size_t name_max)
{
    const char *o = *po;
    int n = 0;

    while (*o == ' ')
        o++;
    while (*o != '\0' && *o != ' ' && n < (int)name_max - 1)
        name[n++] = *o++;
    name[n] = '\0';
    *po = o;
    return n;
}

int uboot_env_select_slot(uint8_t *env, size_t env_len, struct uboot_slot *out)
{
    char order[UBOOT_ENV_VAL_MAX];
    char leftkey[UBOOT_ENV_VAL_MAX];
    char leftval[16];
    char name[UBOOT_ENV_VAL_MAX];
    const char *o;
    long left;
    int n;

    if (env == NULL || out == NULL || env_len < 5)
        return -1;
    memset(out, 0, sizeof(*out));

    /* Corrupt / blank env: reinitialize with defaults - but never destroy a
     * valid-but-different layout. */
    if (!uboot_env_verify(env, env_len)) {
        /* A redundant-format env ([crc32][flags][data]) validates at offset 5.
         * Refuse rather than wipe: overwriting it would destroy live RAUC
         * state. The caller falls back to the static command line. */
        if (uboot_env_is_redundant(env, env_len))
            return -1;
        /* Genuinely invalid: reinitialize defaults IN MEMORY so this boot can
         * proceed, but flag it (out->reinitialized) so the caller does NOT
         * persist the reset over on-disk state it could not validate. */
        env_ltoa(UBOOT_ENV_DEFAULT_TRIES, leftval);
        memset(env, 0, env_len);
        (void)uboot_env_set(env, env_len, "BOOT_ORDER", "A B");
        (void)uboot_env_set(env, env_len, "BOOT_A_LEFT", leftval);
        (void)uboot_env_set(env, env_len, "BOOT_B_LEFT", leftval);
        uboot_env_reseal(env, env_len);
        out->reinitialized = 1;
    }

    if (uboot_env_get(env, env_len, "BOOT_ORDER", order, sizeof(order)) < 0) {
        memcpy(order, "A B", 4);
        (void)uboot_env_set(env, env_len, "BOOT_ORDER", order);
        uboot_env_reseal(env, env_len);
    }

    /* Malformed (empty / whitespace-only) BOOT_ORDER yields no tokens: report
     * failure per the header contract rather than "success, no slot". */
    o = order;
    if (env_next_name(&o, name, sizeof(name)) == 0)
        return -1;

    /* Pass 1: first slot with tries left -> select + decrement. */
    o = order;
    while ((n = env_next_name(&o, name, sizeof(name))) > 0) {
        /* Skip a name too long to form a valid "BOOT_<name>_LEFT" key rather
         * than looking up a truncated / colliding counter. */
        if (env_leftkey(name, leftkey, sizeof(leftkey)) != 0)
            continue;
        left = UBOOT_ENV_DEFAULT_TRIES;
        if (uboot_env_get(env, env_len, leftkey, leftval, sizeof(leftval)) >= 0)
            left = env_atol(leftval);
        if (left > 0) {
            env_ltoa(left - 1, leftval);
            /* If the decrement cannot be stored (env full), do NOT select this
             * slot: booting it with an un-decremented counter would retry the
             * same hung slot forever. Fail so the caller uses the static
             * command line. (uboot_env_set is non-destructive on failure.) */
            if (uboot_env_set(env, env_len, leftkey, leftval) != 0)
                return -1;
            uboot_env_reseal(env, env_len);
            memcpy(out->name, name, (size_t)(n + 1));
            out->selected = 1;
            return 0;
        }
    }

    /* Pass 2: none left -> re-arm every counter, ask the caller to reboot. */
    env_ltoa(UBOOT_ENV_DEFAULT_TRIES, leftval);
    o = order;
    while (env_next_name(&o, name, sizeof(name)) > 0) {
        if (env_leftkey(name, leftkey, sizeof(leftkey)) != 0)
            continue;
        (void)uboot_env_set(env, env_len, leftkey, leftval);
    }
    uboot_env_reseal(env, env_len);
    out->rearmed = 1;
    out->selected = 0;
    return 0;
}
