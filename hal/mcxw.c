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

/* Flash driver */
#include "fsl_device_registers.h"
#include "fsl_lpspi_flash.h"
#include "fsl_flash_api.h"
#include "fsl_ccm32k.h"

#include "hal/armv8m_tz.h"

#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && !defined(NONSECURE_APP))
#   define TZ_SECURE() (1)
#else
#   define TZ_SECURE() (0)
#endif

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


#ifdef TZEN
static void mcxw_configure_sau(void)
{
    /* Disable SAU */
    SAU_CTRL = 0;
    
    /* Configure SAU regions - adjust addresses based on MCXW memory map */
    sau_init_region(0, 0x00000000, 0x0003FFFF, 1); /* Secure flash */
    sau_init_region(1, 0x00040000, 0x0007FFFF, 0); /* Non-secure flash */
    sau_init_region(2, 0x20000000, 0x2001FFFF, 1); /* Secure RAM */
    sau_init_region(3, 0x20020000, 0x2003FFFF, 0); /* Non-secure RAM */
    sau_init_region(4, 0x40000000, 0x5FFFFFFF, 0); /* Non-secure peripherals */
    
    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    
    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}
#endif

void hal_prepare_boot(void)
{
#ifdef TZEN
    mcxw_configure_sau();
#endif
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
    const uint32_t flash_word_size = 16;
    const uint32_t empty_qword[4] = {
        0xFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
    };

#if TZ_SECURE()
    /* Add TrustZone-specific handling if needed */
#endif

    while (len > 0) {
        if ((len < (int)flash_word_size) || (address & (flash_word_size - 1))) {
            uint32_t aligned_qword[4];
            uint32_t address_align = address - (address & (flash_word_size - 1));
            uint32_t start_off = address - address_align;
            uint32_t i;

            memcpy(aligned_qword, (void*)address_align, flash_word_size);
            for (i = start_off; ((i < flash_word_size) && (i < len + start_off)); i++) {
                ((uint8_t *)aligned_qword)[i] = data[w++];
            }
            if (memcmp(aligned_qword, empty_qword, flash_word_size) != 0) {
                ret = FLASH_Program(&pflash, FLASH, address_align, aligned_qword,
                        flash_word_size);
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
    
#if TZ_SECURE()
    /* Add TrustZone-specific handling if needed */
#endif

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

