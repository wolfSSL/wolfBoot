/* filesystem.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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


#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

#ifndef WOLFBOOT_PARTITION_FILENAME
#error "WOLFBOOT_PARTITION_FILENAME needs to be defined for filesystem HAL"
#endif

#ifndef MIN
   #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/* HAL Stubs */
void hal_init(void)
{
    return;
}
int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}
int hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}
void hal_flash_unlock(void)
{
    return;
}
void hal_flash_lock(void)
{
    return;
}
void hal_prepare_boot(void)
{
    return;
}
void do_boot(const uint32_t *app_offset)
{
    (void)app_offset;
}

/* filesystem access */
static XFILE fp = XBADFILE;
static byte fp_write = 0;
static long fp_size = 0;

static int setup_file(byte read_only)
{
    if (fp != XBADFILE) {
        if (!read_only && !fp_write) {
            /* Need to reopen to allow writing */
            XFCLOSE(fp);
            fp = XBADFILE;
        }
    }
    if (fp == XBADFILE) {
        fp = XFOPEN(WOLFBOOT_PARTITION_FILENAME, read_only ? "rb" : "r+b");
        if (fp != XBADFILE) {
            fp_write = !read_only;
            if (XFSEEK(fp, 0, XSEEK_END) < 0 || (fp_size = XFTELL(fp)) < 0 ||
                    XFSEEK(fp, 0, XSEEK_SET) < 0) {
                /* Failed to get the file size */
                XFCLOSE(fp);
                fp = XBADFILE;
            }
        }
    }
    return fp != XBADFILE ? 0 : -1;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    if (setup_file(0) != 0)
        return -1;
    if (address + len > (uintptr_t)fp_size)
        return -1; /* Don't allow writing past the file size */
    if (XFSEEK(fp, address, XSEEK_SET) < 0)
        return -1;
    if ((int)XFWRITE(data, 1, len, fp) != len)
        return -1;
    if (XFFLUSH(fp) != 0)
        return -1;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (setup_file(1) != 0)
        return -1;
    if (XFSEEK(fp, address, XSEEK_SET) < 0)
        return -1;
    return (int)XFREAD(data, 1, len, fp);
}
int ext_flash_erase(uintptr_t address, int len)
{
    byte zeros[256];
    XMEMSET(zeros, 0, sizeof(zeros));
    for (; len > 0; len -= sizeof(zeros), address += sizeof(zeros)) {
        if (ext_flash_write(address, zeros, (int)MIN((int)sizeof(zeros), len)) != 0)
            return -1;
    }
    return 0;
}
void ext_flash_lock(void)
{
    return;
}
void ext_flash_unlock(void)
{
    return;
}
