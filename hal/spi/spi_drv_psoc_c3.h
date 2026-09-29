/* spi_drv_psoc_c3.h
 *
 * Pinout and configuration for the SPI back-end on Infineon PSOC Control C3.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#ifndef SPI_DRV_PSOC_C3_H_INCLUDED
#define SPI_DRV_PSOC_C3_H_INCLUDED

#include <stdint.h>
#include "hal/psoc_c3.h"

/* SCB instance carrying SPI. The C3 evaluation kit routes its mikroBUS
 * headers (J12 and J16) to SCB2 on port 7. SCB4 and SCB5 are absent on the
 * smaller packages; use SCB0 to SCB3 there. */
#ifndef PSOC_C3_SPI_SCB
#define PSOC_C3_SPI_SCB         2
#endif
#define PSOC_C3_SPI_SCB_BASE    PSOC_C3_SCB_BASE(PSOC_C3_SPI_SCB)

/* Pin group, and which pin of it carries which signal. The order is NOT the
 * same for every SCB: SCB3 and SCB4 lay their signals out as MOSI, MISO, CLK,
 * SEL0 while SCB2 uses CLK, MOSI, MISO, SEL0. Getting these the wrong way
 * round drives MISO against the peripheral and reads back nothing, so they
 * are spelled out per pin rather than assumed. */
#ifndef PSOC_C3_SPI_PORT
#define PSOC_C3_SPI_PORT        7
#endif
#ifndef PSOC_C3_SPI_SCK_PIN
#define PSOC_C3_SPI_SCK_PIN     0
#endif
#ifndef PSOC_C3_SPI_MOSI_PIN
#define PSOC_C3_SPI_MOSI_PIN    1
#endif
#ifndef PSOC_C3_SPI_MISO_PIN
#define PSOC_C3_SPI_MISO_PIN    2
#endif
#ifndef PSOC_C3_SPI_CS_PIN
#define PSOC_C3_SPI_CS_PIN      3
#endif
/* The HSIOM selector for the SPI function differs per SCB: 13 on SCB0, 17 on
 * SCB2 and SCB5, 18 on SCB1, SCB3 and SCB4. */
#ifndef PSOC_C3_SPI_HSIOM_SEL
#define PSOC_C3_SPI_HSIOM_SEL   17
#endif

/* Peripheral clock routing for the SPI SCB.
 *
 * Measured on silicon: within group 4 each SCB's clock sits at index 2*n, so
 * SCB0 is 0, SCB3 (the console) is 6 and SCB4 is 8. That is not what the
 * PSC3M5 headers say, which place SCB0 to SCB4 at indices 0 to 4. SCB5 lives
 * in group 6. Check the index on any new variant rather than assuming it. */
#ifndef PSOC_C3_SPI_PCLK_GR
#define PSOC_C3_SPI_PCLK_GR     4
#endif
#ifndef PSOC_C3_SPI_PCLK_IDX
#define PSOC_C3_SPI_PCLK_IDX    (2 * PSOC_C3_SPI_SCB)
#endif
#ifndef PSOC_C3_SPI_PCLK_DIV
#define PSOC_C3_SPI_PCLK_DIV    2
#endif

/* Deliberately slow for bring-up: a wrong clock looks exactly like a wiring
 * fault. Raise it once the device answers. */
#ifndef PSOC_C3_SPI_HZ
#define PSOC_C3_SPI_HZ          1000000UL
#endif

/* Chip select is a plain GPIO, not the SCB's, because the TIS protocol needs
 * it held asserted across the wait-state poll. */
#define SPI_CS_TPM              PSOC_C3_SPI_CS_PIN
#define SPI_CS_TPM_PIO_BASE     PSOC_C3_SPI_PORT

#endif /* SPI_DRV_PSOC_C3_H_INCLUDED */
