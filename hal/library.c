/* library.c
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if 1 /* for desktop testing */
    #define HAVE_UNISTD_H
    #define PRINTF_ENABLED
#else /* restricted build */
    #define NO_FILESYSTEM
#endif

#ifdef HAVE_UNISTD_H
    #include <unistd.h>
    #define exit _exit
#else
    #define exit(x) while(1);
#endif

#include "image.h"
#include "printf.h"

/* force off NO_FILESYSTEM coming from include/user_settings.h */
#ifdef PRINTF_ENABLED
    #undef NO_FILESYSTEM
#endif

/* HAL Stubs */
void hal_init(void)
{
    return;
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
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

int do_boot(uint32_t* v)
{
    wolfBoot_printf("booting %p"
#ifdef HAVE_UNISTD_H
         "(actually exiting)"
#else
         "(actually spin loop)"
#endif
         "\n", v);
    exit(0);
}

static uintptr_t gImage;
#ifdef NO_FILESYSTEM
static const uint8_t test_img[] = {
    0x57, 0x4F, 0x4C, 0x46, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x01,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x08, 0x00, 0x1E, 0xBC,
    0x0E, 0x62, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x20, 0x00, 0x08, 0xF9, 0x46, 0x2E,
    0x0F, 0x70, 0x33, 0x38, 0xAC, 0x19, 0xFF, 0x82, 0xC8, 0xAC, 0xD6, 0x9A, 0xF9,
    0xB2, 0x1F, 0xED, 0x60, 0x3F, 0x68, 0x7B, 0x85, 0xDB, 0x46, 0x8B, 0x3A, 0x7E,
    0x65, 0xE0, 0x10, 0x00, 0x20, 0x00, 0x02, 0x45, 0x14, 0xB0, 0x5A, 0x37, 0x95,
    0x3E, 0x17, 0x49, 0xAD, 0x75, 0xE7, 0x71, 0xD5, 0x65, 0xBB, 0x78, 0x7F, 0xFA,
    0xF6, 0x31, 0x4F, 0x63, 0xF9, 0x20, 0x3D, 0xA1, 0x56, 0xB2, 0x71, 0x7C, 0x20,
    0x00, 0x40, 0x00, 0xC6, 0x7A, 0xEB, 0x04, 0xB1, 0xB8, 0x82, 0xE7, 0x97, 0xD8,
    0x00, 0x80, 0x1D, 0x93, 0xA9, 0x80, 0x37, 0xE0, 0x63, 0x7F, 0x78, 0x15, 0xD8,
    0xD1, 0x22, 0xD6, 0x75, 0x0B, 0x04, 0xE9, 0x71, 0x12, 0xB7, 0x09, 0x32, 0xBC,
    0xB7, 0xFC, 0xA1, 0x9D, 0x32, 0xC0, 0x7D, 0xDB, 0x63, 0xE2, 0x12, 0xF2, 0xE2,
    0x41, 0xF4, 0x15, 0x7A, 0x38, 0xB5, 0xCD, 0xAA, 0x01, 0xB3, 0x5E, 0xF2, 0xCC,
    0xD9, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
#endif

int wolfBoot_start(void)
{
    struct wolfBoot_image os_image;
    int ret = 0;
    memset(&os_image, 0, sizeof(os_image));

    os_image.hdr = (uint8_t*)gImage;

    if ((ret = wolfBoot_open_image_address(&os_image, (uint8_t*)gImage)) < 0) {
        wolfBoot_printf("Failed to open image address.\n");
        goto exit;
    }

    if ((ret = wolfBoot_verify_integrity(&os_image)) < 0) {
        wolfBoot_printf("Failed to verify integrity.\n");
        goto exit;
    }

    if ((ret = wolfBoot_verify_authenticity(&os_image)) < 0) {
        wolfBoot_printf("Failed to verify authenticity.\n");
        goto exit;
    }

    wolfBoot_printf("Firmware Valid\n");

    do_boot((uint32_t*)os_image.fw_base);

 exit:
    if (ret < 0) {
        wolfBoot_printf("Failure %d: Hdr %d, Hash %d, Sig %d\n", ret,
                        os_image.hdr_ok, os_image.sha_ok, os_image.signature_ok);
        return -1;
    }
    else {
        return 0;
    }

}


int main(int argc, const char* argv[])
{
    int ret = 0;

#ifdef NO_FILESYSTEM
    wolfBoot_printf("NO_FILESYSTEM is defined, looking at test_img");
    gImage = (uintptr_t)test_img;
#else
    if (argc == 2) {
        size_t sz = 0, bread = 0;
        FILE* img = fopen(argv[1], "rb");
        if (img == NULL) {
            wolfBoot_printf("Failed to open file: %s!\n\n", argv[1]);
            wolfBoot_printf("Usage: %s image_file.bin\n", argv[0]);
            return -3;
        }
        else {
            wolfBoot_printf("Looking at image file: %s\n", argv[1]);
            fseek(img, 0, SEEK_END);
            sz = ftell(img);
            fseek(img, 0, SEEK_SET);

            gImage = (uintptr_t)malloc(sz);
        }

        if (((void*)gImage) == NULL) {
            wolfBoot_printf("Failed to malloc %zu bytes for image.\n", sz);
            ret = -1;
        }
        else {
            /* check the image */
            bread = fread((void*)gImage, 1, sz, img);
        }

        if (bread == sz) {
            wolfBoot_printf("Confirmed expected size: %zu bytes.\n", bread);
        }
        else {
            ret = -2;
            wolfBoot_printf("Read %zu of %zu bytes from %s\n", bread, sz, argv[1]);
        }
        fclose(img);
    }
    else {
        wolfBoot_printf("usage: %s image_file.bin\n", argv[0]);
        ret = 255;
    }
#endif
    if (ret == 0) {
        wolfBoot_printf("Checking image... ");
        ret = wolfBoot_start();
    }
    if (ret == 0) {
        wolfBoot_printf("Success!\n");
    }
    else {
        if (ret != 255) {
            /* Only show error if we actually processed file, not missing params */
            wolfBoot_printf("Failed to verify with wolfBoot_start\n");
        }
    }

#ifndef NO_FILESYSTEM
    if ((void*)gImage != NULL)
        free((void*)gImage);
#endif
    return ret;
}
