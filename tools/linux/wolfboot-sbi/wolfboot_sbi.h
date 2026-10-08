/* wolfboot_sbi.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * This is the user-space interface of the wolfboot-sbi Linux kernel module and
 * is licensed under version 2 or later of the GNU General Public License,
 * rather than version 3 or later as the rest of wolfBoot is, so that it and
 * the module that includes it match the MODULE_LICENSE("GPL") the kernel
 * requires.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* User-space interface of the wolfboot-sbi Linux module: /dev/wolfboot
 * exposes the wolfBoot SBI vendor calls (include/riscv_sbi.h). */

#ifndef WOLFBOOT_SBI_UAPI_H
#define WOLFBOOT_SBI_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define WOLFBOOT_SBI_SERIAL_LEN 16
#define WOLFBOOT_SBI_KEY_LEN    32

#define WOLFBOOT_SBI_KEY_MRAM_LOG 1
#define WOLFBOOT_SBI_KEY_VARLOG   2

struct wolfboot_sbi_key {
    __u32 purpose;
    __u8  key[WOLFBOOT_SBI_KEY_LEN];
};

/* A started System Controller service; poll with SC_STATUS until it does
 * not return EBUSY.  status is the 16-bit service status, mbox1 the second
 * mailbox word (the DIGESTERR bits for a digest check). */
struct wolfboot_sbi_sc_status {
    __u32 status;
    __u32 mbox1;
};

/* System Controller SPI flash (fenced builds with MPFS_SBI_SC_FLASH).  Offsets
 * are flash offsets; the first erase block (the SPI directory) is reachable
 * only through SET_IMAGE, which points entry 1 (upgrade) or 2 (blank) at an
 * image.  The module splits a range into one-erase-block erases and 4 KB
 * transfers, each its own ecall. */
#define WOLFBOOT_SBI_FLASH_ERASE_BLOCK 65536
#define WOLFBOOT_SBI_FLASH_CHUNK       4096
#define WOLFBOOT_SBI_FLASH_UPGRADE_INDEX 1
#define WOLFBOOT_SBI_FLASH_BLANK_INDEX   2

struct wolfboot_sbi_flash_range {
    __u32 offset;
    __u32 len;
};

struct wolfboot_sbi_flash_io {
    __u32 offset;
    __u32 len;
    __u64 buf;      /* user buffer */
};

struct wolfboot_sbi_flash_image {
    __u32 index;
    __u32 address;
};

#define WOLFBOOT_SBI_IOC_MAGIC 'W'
#define WOLFBOOT_SBI_IOC_GET_VERSION   _IOR(WOLFBOOT_SBI_IOC_MAGIC, 0, __u32)
#define WOLFBOOT_SBI_IOC_GET_SERIAL    _IOR(WOLFBOOT_SBI_IOC_MAGIC, 1, __u8[WOLFBOOT_SBI_SERIAL_LEN])
#define WOLFBOOT_SBI_IOC_GET_KEY       _IOWR(WOLFBOOT_SBI_IOC_MAGIC, 2, struct wolfboot_sbi_key)
#define WOLFBOOT_SBI_IOC_DIGEST_CHECK  _IOW(WOLFBOOT_SBI_IOC_MAGIC, 3, __u32)
#define WOLFBOOT_SBI_IOC_IAP_AUTH      _IOW(WOLFBOOT_SBI_IOC_MAGIC, 4, __u32)
#define WOLFBOOT_SBI_IOC_IAP_VERIFY    _IOW(WOLFBOOT_SBI_IOC_MAGIC, 5, __u32)
#define WOLFBOOT_SBI_IOC_IAP_PROGRAM   _IOW(WOLFBOOT_SBI_IOC_MAGIC, 6, __u32)
#define WOLFBOOT_SBI_IOC_AUTO_UPDATE   _IO(WOLFBOOT_SBI_IOC_MAGIC, 7)
#define WOLFBOOT_SBI_IOC_SC_STATUS     _IOR(WOLFBOOT_SBI_IOC_MAGIC, 8, struct wolfboot_sbi_sc_status)
#define WOLFBOOT_SBI_IOC_SC_FLASH_ERASE     _IOW(WOLFBOOT_SBI_IOC_MAGIC, 9, struct wolfboot_sbi_flash_range)
#define WOLFBOOT_SBI_IOC_SC_FLASH_WRITE     _IOW(WOLFBOOT_SBI_IOC_MAGIC, 10, struct wolfboot_sbi_flash_io)
#define WOLFBOOT_SBI_IOC_SC_FLASH_READ      _IOWR(WOLFBOOT_SBI_IOC_MAGIC, 11, struct wolfboot_sbi_flash_io)
#define WOLFBOOT_SBI_IOC_SC_FLASH_SET_IMAGE _IOW(WOLFBOOT_SBI_IOC_MAGIC, 12, struct wolfboot_sbi_flash_image)

#endif /* WOLFBOOT_SBI_UAPI_H */
