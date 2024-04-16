/* spi_drv_nxp.h
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

#ifndef SPI_DRV_NXP_H_INCLUDED
#define SPI_DRV_NXP_H_INCLUDED

#include <stdint.h>

/* Chip select for TPM - defaults */
#ifndef SPI_CS_TPM
    #if defined(PLATFORM_nxp_p1021)
        #define SPI_CS_TPM 2
    #elif defined(PLATFORM_nxp_t1024)
        #define SPI_CS_TPM 1
    #endif
#endif

#endif /* !SPI_DRV_NXP_H_INCLUDED */
