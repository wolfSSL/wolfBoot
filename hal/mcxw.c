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

#define FLASH FMU0

/*!< Core clock frequency: 48000000Hz */
#define BOARD_BOOTCLOCKRUN_CORE_CLOCK              48000000U
static flash_config_t pflash;
static uint32_t pflash_sector_size = WOLFBOOT_SECTOR_SIZE;

uint32_t SystemCoreClock;

#ifdef TZEN
static void hal_sau_init(void)
{
    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + 2 * WOLFBOOT_PARTITION_SIZE - 1,
            0);

    /* ROM bootloader API */
    sau_init_region(2, 0x14800000, 0x14817FFF, 0);

    /* Non-secure RAM */
    sau_init_region(3, 0x20010000, 0x20015FFF, 0);

    /* Peripherals */
    sau_init_region(4, 0x40000000, 0x4007FFFF, 0);
    sau_init_region(5, 0x48000000, 0x48FFFFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

static void periph_unsecure(void)
{
    GPIOA->PCNS = 0xFFFFFFFF;
    GPIOA->ICNS = 0xFFFFFFFF;
}
#endif

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
#ifdef TZEN
    periph_unsecure();
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

#if defined(TZEN) && !defined(NONSECURE_APP)
    hal_sau_init();
#endif
}

static void write_flash_qword(uint32_t *dst, const uint32_t *src)
{
    /* Wait for non-busy */
    while (!(FMU0->FSTAT & 0x00000080)) {}
    /* Clear errors */
    FMU0->FSTAT = 0x74;
    /* Set command "program phrase" */
    FMU0->FCCOB[0] = 0x24;
    /* Start command */
    FMU0->FSTAT = 0x80;
    /* Wait for write enabled */
    while (!(FMU0->FSTAT & 0x01000000)) {}
    /* Write the 4 words */
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    /* Wait for operation ready to execute */
    while (!(FMU0->FSTAT & 0x80000000)) {}
    /* Start operation */
    FMU0->FSTAT = 0x80000000;
    /* Wait for completion */
    while (!(FMU0->FSTAT & 0x00000080)) {}
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int w = 0;
    const uint32_t flash_word_size = 16;
    const uint32_t empty_qword[4] = {
        0xFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
    };

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
                write_flash_qword((uint32_t *)address_align, aligned_qword);
            }
            address += i;
            len -= i;
        }
        else {
            uint32_t i;
            uint32_t len_align = len - (len & 0x0F);

            for (i = 0; i < len_align; i += 16) {
                write_flash_qword((uint32_t *)(address + i),
                                  (const uint32_t *)(data + w + i));
            }
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

static void erase_flash_sector(uint32_t *dst) {
    /* Wait for non-busy */
    while (!(FMU0->FSTAT & 0x00000080)) {}
    /* Clear errors */
    FMU0->FSTAT = 0x74;
    /* Set command "erase sector" */
    FMU0->FCCOB[0] = 0x42;
    /* Start command */
    FMU0->FSTAT = 0x80;
    /* Wait for write enabled */
    while (!(FMU0->FSTAT & 0x01000000)) {}
    /* Write 4 words to specify sector to erase */
    dst[0] = 0;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    /* Wait for operation ready to execute */
    while (!(FMU0->FSTAT & 0x80000000)) {}
    /* Start operation */
    FMU0->FSTAT = 0x80000000;
    /* Wait for completion */
    while (!(FMU0->FSTAT & 0x00000080)) {}
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    if (address % pflash_sector_size)
        address -= address % pflash_sector_size;
    while (len > 0) {
        erase_flash_sector((uint32_t *)address);
        address += WOLFBOOT_SECTOR_SIZE;
        len -= WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}

