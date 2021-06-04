/* unit-parser.c
 *
 * Unit test for parser functions in libwolfboot.c
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#include <stdio.h>
#include "src/libwolfboot.c"
static int locked = 0;

/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    return 0;
}
void hal_flash_unlock(void)
{
    if (!locked)
        printf("Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    if (locked)
        printf("Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}

void spi_flash_sector_erase(uint32_t address)
{

}
int spi_flash_read(uint32_t address, void *data, int len)
{
    return 0;
}
int spi_flash_write(uint32_t address, const void *data, int len)
{
    return 0;
}
/* End Mocks */

#define Min(A,B) ((A<B)?A:B)

int main(void)
{
    printf("WOLFBOOT_PARTITION_SIZE             : %u\n", WOLFBOOT_PARTITION_SIZE);
    printf("WOLFBOOT_SECTOR_SIZE                : %u\n", WOLFBOOT_SECTOR_SIZE);
    printf("Sectors per partition               : %u\n", (WOLFBOOT_PARTITION_SIZE / WOLFBOOT_SECTOR_SIZE));
    printf("ENCRYPT_TMP_SECRET_OFFSET           : %u\n", ENCRYPT_TMP_SECRET_OFFSET);
    printf("TRAILER_SKIP                        : %u\n", TRAILER_SKIP);
#if defined(EXT_ENCRYPTED)
    printf("TRAILER_OVERHEAD                    : %u\n", TRAILER_OVERHEAD);
#endif
    printf("WOLFBOOT_PARTITION_BOOT_ADDRESS     : %08X\n", WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printf("WOLFBOOT_PARTITION_UPDATE_ADDRESS   : %08X\n", WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printf("PART_BOOT_ENDFLAGS                  : %08X\n", PART_BOOT_ENDFLAGS);
    printf("PART_UPDATE_ENDFLAGS                : %08X\n", PART_UPDATE_ENDFLAGS);
#if !defined(EXT_FLASH) || defined(FLAGS_HOME)
    printf("Max firmware size                   : %u\n",  (Min(PART_BOOT_ENDFLAGS, PART_UPDATE_ENDFLAGS) - WOLFBOOT_PARTITION_BOOT_ADDRESS)
#if defined(EXT_ENCRYPTED)
           - TRAILER_OVERHEAD
#endif
           );
#else
    printf("Max firmware size                   : %u\n",
           (PART_BOOT_ENDFLAGS - WOLFBOOT_PARTITION_BOOT_ADDRESS)
#if defined(EXT_ENCRYPTED)
           - TRAILER_OVERHEAD
#endif
           );
#endif
    return 0;

}
