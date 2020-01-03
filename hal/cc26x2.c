/* cc26x2.c
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

#include <stdint.h>

#include "oscillators.h"
#include "ti-lib.h"

#include "target.h" /* For WOLFBOOT_SECTOR_SIZE */
#include "image.h"
extern void clock_init(void);

char uart_read(void)
{
    return (char)UARTCharGet(UART0_BASE);
}

int uart_read_nonblock(char *c)
{
    int ret = UARTCharGetNonBlocking(UART0_BASE);
    if (ret == -1)
        return 0;
    *c = (char)ret;
    return 1;
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    FlashProgram(data, address, len);
    while(FlashCheckFsmForReady() != FAPI_STATUS_FSM_READY)
                ;
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
    int i = 0;
    while (len > 0) {
        FlashSectorErase(address + (WOLFBOOT_SECTOR_SIZE * i++));
        while(FlashCheckFsmForReady() != FAPI_STATUS_FSM_READY)
            ;

        if (len <= WOLFBOOT_SECTOR_SIZE)
            break;
        len -= WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}

void hal_init(void)
{
    /* Enable flash cache and prefetch. */
     ti_lib_vims_mode_set(VIMS_BASE, VIMS_MODE_ENABLED);
     ti_lib_vims_configure(VIMS_BASE, true, true);

     ti_lib_int_master_disable();

     ti_lib_prcm_power_domain_on(PRCM_DOMAIN_PERIPH);
     while((ti_lib_prcm_power_domain_status(PRCM_DOMAIN_PERIPH)
           != PRCM_DOMAIN_POWER_ON))
         ;

     ti_lib_prcm_power_domain_on(PRCM_DOMAIN_SERIAL);
     while ((ti_lib_prcm_power_domain_status(PRCM_DOMAIN_SERIAL)) != PRCM_DOMAIN_POWER_ON)
         ;

     ti_lib_prcm_peripheral_run_enable(PRCM_PERIPH_GPIO);
     ti_lib_prcm_load_set();
          while(!ti_lib_prcm_load_get())
              ;
     ti_lib_prcm_peripheral_run_enable(PRCM_PERIPH_UART0);

     ti_lib_prcm_load_set();
     while(!ti_lib_prcm_load_get())
         ;

     ti_lib_int_master_enable();

    clock_init();
}

void hal_prepare_boot(void)
{
}

