/* mcxa.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include "fsl_spc.h"

/* Flash driver */
#include "fsl_romapi.h"

#include "hal/armv8m_tz.h"

#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && !defined(NONSECURE_APP))
#   define TZ_SECURE() (1)
#else
#   define TZ_SECURE() (0)
#endif

/*!< Core clock frequency: 96000000Hz */
#define BOARD_BOOTCLOCKFRO96M_CORE_CLOCK 96000000UL

static flash_config_t pflash;
static int flash_init = 0;

uint32_t SystemCoreClock;

extern void BOARD_BootClockFRO96M(void);

#ifdef __WOLFBOOT
/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

void hal_init(void)
{
    /* Clock setting  */
    BOARD_BootClockFRO96M();

    /* Clear the FLASH configuration structure */
    memset(&pflash, 0, sizeof(pflash));
    /* FLASH driver init */
    FLASH_Init(&pflash);
}

#ifdef TZEN
static void mcxa_configure_sau(void)
{
    /* Disable SAU */
    SAU_CTRL = 0;
    
    /* Configure SAU regions - adjust addresses based on MCXA memory map */
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
    mcxa_configure_sau();
#endif
}

#endif /* __WOLFBOOT */

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret;
    int w = 0;
    const uint8_t empty_qword[16] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

#if TZ_SECURE()
    /* Add TrustZone-specific handling if needed */
#endif

    while (len > 0) {
        if ((len < 16) || address & 0x0F) {
            uint8_t aligned_qword[16];
            uint32_t address_align = address - (address & 0x0F);
            uint32_t start_off = address - address_align;
            int i;

            memcpy(aligned_qword, (void*)address_align, 16);
            for (i = start_off; ((i < 16) && (i < len + (int)start_off)); i++) {
                aligned_qword[i] = data[w++];
            }
            if (memcmp(aligned_qword, empty_qword, 16) != 0) {
                ret = FLASH_ProgramPhrase(&pflash, address_align, aligned_qword, 16);
                if (ret != kStatus_Success)
                    return -1;
            }
            address += i;
            len -= i;
        }
        else {
            uint32_t len_align = len - (len & 0x0F);
            ret = FLASH_ProgramPhrase(&pflash, address, (uint8_t*)data + w, len_align);
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
#if TZ_SECURE()
    /* Add TrustZone-specific handling if needed */
#endif

    while ((address % 4) != 0)
        address --;
    if (FLASH_EraseSector(&pflash, address, len, kFLASH_ApiEraseKey) != kStatus_Success)
        return -1;
    return 0;
}
