/* hal_fpga.h
 *
 * FPGA/PL bitstream loading HAL interface.
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

/* Split out from hal.h so the per-target HAL implementations (hal/zynq.c,
 * hal/zynq7000.c, ...) can pull in just the FPGA flag constants and the
 * hal_fpga_load() prototype without including hal.h, which also drags in the
 * external-flash and wolfHSM interfaces. */

#ifndef H_HAL_FPGA_
#define H_HAL_FPGA_

#ifdef WOLFBOOT_FPGA_BITSTREAM

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FPGA load mode (flags argument to hal_fpga_load) */
#define HAL_FPGA_FULL    0u  /* full bitstream / device image */
#define HAL_FPGA_PARTIAL 1u  /* partial reconfiguration */

/*
 * Program the PL/FPGA fabric from an in-DDR bitstream/PDI image.
 * addr/size describe the staged image buffer; the implementation
 * is responsible for any required cache maintenance before the
 * configuration engine reads it. Returns 0 on success, negative on
 * error. The weak default returns -1 (not implemented).
 */
int hal_fpga_load(uint32_t flags, uintptr_t addr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_FPGA_BITSTREAM */

#endif /* H_HAL_FPGA_ */
