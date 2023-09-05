/* lms_common.h
 *
 * Common callback functions used by LMS to write/read the private
 * key file.
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef LMS_COMMON_H
#define LMS_COMMON_H
static int lms_write_key(const byte * priv, word32 privSz, void * context)
{
    FILE *       file = NULL;
    const char * filename = NULL;
    int          n_cmp = 0;
    size_t       n_read = 0;
    size_t       n_write = 0;
    byte         buff[HSS_MAX_PRIVATE_KEY_LEN];
    int          err = 0;

    if (priv == NULL || context == NULL || privSz == 0) {
        fprintf(stderr, "error: invalid write args\n");
        return WC_LMS_RC_BAD_ARG;
    }

    filename = context;

    /* Open file for read and write. */
    file = fopen(filename, "r+");
    if (!file) {
        /* Create the file if it didn't exist. */
        file = fopen(filename, "w+");
        if (!file) {
            fprintf(stderr, "error: fopen(%s, \"w+\") failed: %d\n", filename,
                    ferror(file));
            return WC_LMS_RC_WRITE_FAIL;
        }
    }

    n_write = fwrite(priv, 1, privSz, file);

    if (n_write != privSz) {
        fprintf(stderr, "error: wrote %zu, expected %d: %d\n", n_write, privSz,
                ferror(file));
        return WC_LMS_RC_WRITE_FAIL;
    }

    err = fclose(file);
    if (err) {
        fprintf(stderr, "error: fclose returned %d\n", err);
        return WC_LMS_RC_WRITE_FAIL;
    }

    /* Verify private key data has actually been written to persistent
     * storage correctly. */
    file = fopen(filename, "r+");
    if (!file) {
        fprintf(stderr, "error: fopen(%s, \"r+\") failed: %d\n", filename,
                ferror(file));
        return WC_LMS_RC_WRITE_FAIL;
    }

    XMEMSET(buff, 0, n_write);

    n_read = fread(buff, 1, n_write, file);

    if (n_read != n_write) {
        fprintf(stderr, "error: read %zu, expected %zu: %d\n", n_read, n_write,
                ferror(file));
        return WC_LMS_RC_WRITE_FAIL;
    }

    n_cmp = XMEMCMP(buff, priv, n_write);
    if (n_cmp != 0) {
        fprintf(stderr, "error: write data was corrupted: %d\n", n_cmp);
        return WC_LMS_RC_WRITE_FAIL;
    }

    err = fclose(file);
    if (err) {
        fprintf(stderr, "error: fclose returned %d\n", err);
        return WC_LMS_RC_WRITE_FAIL;
    }

    return WC_LMS_RC_SAVED_TO_NV_MEMORY;
}

static int lms_read_key(byte * priv, word32 privSz, void * context)
{
    FILE *       file = NULL;
    const char * filename = NULL;
    size_t       n_read = 0;

    if (priv == NULL || context == NULL || privSz == 0) {
        fprintf(stderr, "error: invalid read args\n");
        return WC_LMS_RC_BAD_ARG;
    }

    filename = context;

    file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "error: fopen(%s, \"rb\") failed\n", filename);
        return WC_LMS_RC_READ_FAIL;
    }

    n_read = fread(priv, 1, privSz, file);

    if (n_read != privSz) {
        fprintf(stderr, "error: read %zu, expected %d: %d\n", n_read, privSz,
                ferror(file));
        return WC_LMS_RC_READ_FAIL;
    }

    fclose(file);

    return WC_LMS_RC_READ_TO_MEMORY;
}
#endif /* LMS_COMMON_H */
