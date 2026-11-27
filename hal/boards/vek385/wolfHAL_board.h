/* wolfHAL_board.h
 *
 * AMD VEK385 (Versal AI Edge Gen 2, Cortex-A78AE) board configuration for
 * the wolfHAL drivers: PL011 console on UART1, and the Micron MT35XU02G
 * OSPI flash on the Cadence OSPI controller with the Versal Gen 2 OSPI DMA
 * for reads. The devices are in wolfHAL_board.c; hal/wolfhal.c uses the
 * BOARD_*_DEV handles below. No internal flash: BOARD_FLASH_DEV is not
 * defined, and all wolfBoot partitions are in the OSPI flash (EXT_FLASH).
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

#ifndef WOLFHAL_BOARD_H
#define WOLFHAL_BOARD_H

#include <wolfHAL/wolfHAL.h>
#include <wolfHAL/platform/amd/versal2.h>
#include <wolfHAL/uart/pl011_uart.h>
#include <wolfHAL/spimem/cadence_ospi_spimem.h>
#include <wolfHAL/dma/versal2_ospi_dma.h>
#include <wolfHAL/flash/spimem_nor_flash.h>
#include <wolfHAL/flash/spimem_nor_mt35x.h>

/* Console: UART1 (UG1712 "UART Interfaces"); DEBUG_UART_NUM=0 picks
 * UART0. */
#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 0
    #define DEBUG_UART_BASE     WHAL_VERSAL2_UART0_BASE
#else
    #define DEBUG_UART_BASE     WHAL_VERSAL2_UART1_BASE
#endif

/* UART clock set by the PLM: 99999001 Hz on the VEK385 (system.dtb). At
 * 115200 this gives IBRD 54, FBRD 16, as the PLM leaves them. */
#ifndef UART_CLK_REF
    #define UART_CLK_REF        99999001UL
#endif

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
#endif

/* OSPI flash (Micron MT35XU02GCBA, 256 MB): 4-byte address opcodes, needed
 * above 16 MB (datasheet p13-14, p32-34). */
#define OSPI_OP_READ4               0x13    /* Read, 4-byte address */
#define OSPI_OP_PAGE_PROGRAM4       0x12    /* Page Program, 4-byte address */
#define OSPI_OP_ERASE4_128K         0xDC    /* Sector erase (128 KB) */

/* Flash ID 2C 5B 1C = Micron, 1.8 V, 2 Gb (datasheet Table 18), byte 0 in
 * bits 7:0. Init accepts only this ID. */
#define VERSAL2_FLASH_JEDEC_ID      0x1C5B2CUL

/* 1-bit OSPI: clock /32 (AM028 reset value; PLM's /2 needs the PHY). Read
 * capture 0x1: the reset value 0 misreads the ID on the board (probe). */
#define VERSAL2_OSPI_BAUD_DIV_32    0xF
#define VERSAL2_OSPI_RD_CAPTURE     0x1
#define VERSAL2_OSPI_CS_LINES       0x0

/* Devices (wolfHAL_board.c), used by hal/wolfhal.c */
extern whal_Uart g_wbUart;
extern whal_SpiMem g_wbOspi;
extern whal_Flash g_wbExtFlash;

#define BOARD_UART_DEV      (&g_wbUart)
#define BOARD_EXT_FLASH_DEV (&g_wbExtFlash)

#endif /* WOLFHAL_BOARD_H */
