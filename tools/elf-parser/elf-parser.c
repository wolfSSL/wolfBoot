/* elf-parser.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 *
 * elf parser tool
 */

#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "string.h"
#include "elf.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int ret = 0;
    uint8_t *image = NULL;
    size_t imageSz = 0;
    FILE *f = NULL;
    uintptr_t entry = 0;
    const char* filename = "wolfboot.elf";

    if (argc >= 2) {
        filename = argv[1];
    }

    printf("ELF Parser:\n");

    /* open and load key buffer */
    f = fopen(filename, "rb");
    if (f == NULL) {
        printf("Open file %s failed!\n", filename);
        ret = -1;
    }
    if (ret == 0) {
        fseek(f, 0, SEEK_END);
        imageSz = ftell(f);
        fseek(f, 0, SEEK_SET);
        image = malloc(imageSz);
        if (image == NULL) {
            printf("Allocate %lu failed!\n", imageSz);
            ret = -1;
        }
    }
    if (ret == 0) {
        size_t readSz = fread(image, 1, imageSz, f);
        if (readSz != imageSz) {
            printf("File read error! %lu\n", readSz);
            ret = -1;
        }
    }
    fclose(f);

    if (ret == 0) {
        ret = elf_load_image(image, &entry);
    }

    printf("Return %d, Load %p\n", ret, (void*)entry);

    free(image);

    return ret;
}
