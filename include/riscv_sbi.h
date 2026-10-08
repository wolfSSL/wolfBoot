/* riscv_sbi.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Interface between the M-mode SBI runtime (src/riscv_sbi.c) and a platform
 * HAL that serves the vendor extension. */

#ifndef RISCV_SBI_H
#define RISCV_SBI_H

#include <stdint.h>

#define SBI_SUCCESS              0L
#define SBI_ERR_FAILED         (-1L)
#define SBI_ERR_NOT_SUPPORTED  (-2L)
#define SBI_ERR_INVALID_PARAM  (-3L)
#define SBI_ERR_DENIED         (-4L)
#define SBI_ERR_INVALID_ADDRESS (-5L)
#define SBI_ERR_ALREADY_AVAIL  (-6L)
#define SBI_ERR_ALREADY_STARTED (-7L)

/* Register-frame indices of the argument registers (regs[N] == xN). */
#define SBI_REG_A0 10
#define SBI_REG_A1 11
#define SBI_REG_A2 12

/* Vendor extension: SBI_EXT_VENDOR_START | Microchip mvendorid 0x029, the
 * EID the Hart Software Services use, so an OS probes one number for both
 * firmwares and tells them apart with SBI_WOLF_FID_GET_VERSION. */
#define SBI_EXT_VENDOR_MCHP      0x09000029UL

/* HSS function IDs on that extension.  Only HSS_REBOOT is served here (as a
 * cold reset); the IHC, remoteproc, crypto and IPC calls are not. */
#define SBI_MCHP_FID_HSS_REBOOT  0x10UL

/* wolfBoot function IDs.  a0..a2 carry the arguments, a0 returns the SBI
 * error, a1 the value.  S-mode buffers are passed as addresses in the
 * caller's address space. */
#define SBI_WOLF_FID_GET_VERSION      0x40UL  /* -> a1 = SBI_WOLF_VERSION */
#define SBI_WOLF_FID_GET_SERIAL       0x41UL  /* (buf, len>=16) */
#define SBI_WOLF_FID_GET_KEY          0x42UL  /* (purpose, buf, len==32), once per boot */
#define SBI_WOLF_FID_DIGEST_CHECK     0x43UL  /* (options) start, poll SC_STATUS */
#define SBI_WOLF_FID_IAP_AUTHENTICATE 0x44UL  /* (spi index) start, poll SC_STATUS */
#define SBI_WOLF_FID_IAP_VERIFY       0x45UL  /* (spi index) start, poll SC_STATUS */
#define SBI_WOLF_FID_IAP_PROGRAM      0x46UL  /* (spi index) the device restarts on success */
#define SBI_WOLF_FID_AUTO_UPDATE      0x47UL  /* () the device restarts on success */
/* System Controller SPI flash, fenced builds with MPFS_SBI_SC_FLASH only:
 * offsets are flash offsets, the first erase block (the SPI directory) is
 * reachable only through SET_IMAGE. */
#define SBI_WOLF_FID_SC_FLASH_ERASE   0x48UL  /* (offset, len == one erase block) */
#define SBI_WOLF_FID_SC_FLASH_WRITE   0x49UL  /* (offset, buf, len <= SBI_SMODE_BUF_MAX) */
#define SBI_WOLF_FID_SC_FLASH_READ    0x4AUL  /* (offset, buf, len <= SBI_SMODE_BUF_MAX) */
#define SBI_WOLF_FID_SC_FLASH_SET_IMAGE 0x4BUL /* (directory index 1|2, image offset) */
#define SBI_WOLF_FID_SC_STATUS        0x4FUL  /* -> a1 = status | (mailbox word 1 << 32) */

#define SBI_WOLF_VERSION         1UL

/* GET_KEY purposes: each names a fixed label, so the caller never chooses
 * what the PUF is asked. */
#define SBI_WOLF_KEY_MRAM_LOG    1UL  /* "wolfBoot-mram-log-v1" */
#define SBI_WOLF_KEY_VARLOG      2UL  /* "wolfLUKS-key-v1:varlog" (the earlier user-space derivation) */
#define SBI_WOLF_KEY_LEN         32UL

/* Buffers an S-mode caller hands to the vendor calls are bounded. */
#define SBI_SMODE_BUF_MAX        4096UL

/* Platform hook serving SBI_EXT_VENDOR_MCHP.  regs is the trap frame
 * (SBI_REG_A0..A2 in, written back by the caller), *val the a1 result.
 * Weak default: SBI_ERR_NOT_SUPPORTED. */
long sbi_hal_vendor_ecall(unsigned long fid, unsigned long *regs,
    unsigned long *val);

/* Copies between an S-mode address (translated like the trapped context)
 * and M-mode memory.  0 on success, -1 when the access faulted or the range
 * is not acceptable (sbi_smode_range_ok). */
int sbi_smode_range_ok(unsigned long addr, unsigned long len);
int sbi_copy_from_smode(uint8_t *dst, unsigned long src, unsigned long len);
int sbi_copy_to_smode(unsigned long dst, const uint8_t *src, unsigned long len);

#endif /* RISCV_SBI_H */
