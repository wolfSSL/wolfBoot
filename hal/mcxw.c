/* mcxw.c
 *
 * Stubs for custom HAL implementation. Defines the 
 * functions used by wolfboot for a specific target.
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

#include <stdint.h>
#include <target.h>
#include "image.h"
/* FSL includes */
#include "fsl_common.h"

/* Clock + RAM voltage settings */
#include "fsl_clock.h"
//#include "fsl_spc.h"

/* Flash driver */
#include "fsl_device_registers.h"
#include "fsl_lpspi_flash.h"
#include "fsl_flash_api.h"
#include "fsl_ccm32k.h"

#define FLASH FMU0

/*!< Core clock frequency: 48000000Hz */
#define BOARD_BOOTCLOCKRUN_CORE_CLOCK              48000000U
static flash_config_t pflash;
static uint32_t pflash_sector_size = WOLFBOOT_SECTOR_SIZE;

uint32_t SystemCoreClock;


#ifdef __WOLFBOOT

extern void BOARD_BootClockRUN(void);



/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}


void hal_prepare_boot(void)
{

}

#endif

void hal_init(void)
{
#ifdef __WOLFBOOT
    /* Clock setting  */
    BOARD_BootClockRUN();
#endif
    /* Clear the FLASH configuration structure */
    memset(&pflash, 0, sizeof(pflash));
    /* FLASH driver init */
    FLASH_Init(&pflash);
    FLASH_GetProperty(&pflash, kFLASH_PropertyPflash0SectorSize,
            &pflash_sector_size);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret;
    int w = 0;
    const uint32_t empty_qword[4] = {
        0xFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
    };

    while (len > 0) {
        if ((len < 16) || (address & 0x0F)) {
            uint32_t aligned_qword[4];
            uint32_t address_align = address - (address & 0x0F);
            uint32_t start_off = address - address_align;
            int i;

            memcpy(aligned_qword, (void*)address_align, 16);
            for (i = start_off; ((i < 16) && (i < len + (int)start_off)); i++) {
                ((uint8_t *)aligned_qword)[i] = data[w++];
            }
            if (memcmp(aligned_qword, empty_qword, 16) != 0) {
                ret = FLASH_Program(&pflash, FLASH, address_align, aligned_qword,
                        16);
                if (ret != kStatus_Success)
                    return -1;
            }
            address += i;
            len -= i;
        }
        else {
            uint32_t len_align = len - (len & 0x0F);
            if (((uint32_t)data + w) & 0x0F) {
                uint32_t __attribute__((aligned(16))) aligned_data[4];
                memcpy(aligned_data, (void*)((uint32_t)data + w), len_align);
                ret = FLASH_Program(&pflash, FLASH, address, (uint32_t*)data + w,
                        len_align);
            }
            else
            {
                ret = FLASH_Program(&pflash, FLASH, address, (uint32_t*)data + w,
                        len_align);
            }
            if (ret != kStatus_Success)
                return -1;
            len -= len_align;
            address += len_align;
        }
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    status_t result;
    if (address % pflash_sector_size)
        address -= address % pflash_sector_size;
    while (len > 0) {
        result = FLASH_Erase(&pflash, FLASH, address, pflash_sector_size,
                kFLASH_ApiEraseKey);
        if (kStatus_FLASH_Success != result)
            return -1;

        /* Verify sector if it's been erased. */
        result = FLASH_VerifyEraseSector(&pflash, FLASH, address,
                pflash_sector_size);
        if (kStatus_FLASH_Success != result)
            return -1;
        len -= pflash_sector_size;
    }
    return 0;
}

