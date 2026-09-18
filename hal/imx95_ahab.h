/* imx95_ahab.h
 *
 * AHAB container-set parsing for the NXP i.MX95.
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

#ifndef IMX95_AHAB_H
#define IMX95_AHAB_H

#include <stdint.h>

#define IMX95_AHAB_BLOCK            512U

/* Two sectors hold the header plus seven image entries, more than any
 * container the SoC's own boot flow uses. */
#define IMX95_AHAB_HDR_BYTES        1024
#define IMX95_AHAB_MAX_IMAGES       7

/* Containers start on a 1 KiB boundary on every i.MX95 boot device. */
#define IMX95_AHAB_ALIGN            1024U

/* Where the ROM reads the first container from on eMMC/SD. */
#define IMX95_AHAB_MMC_OFFSET       0x8000U

#define IMX95_AHAB_TAG              0x87U
#define IMX95_AHAB_VERSION          0x02U

/* Image flags: type in [3:0], the core that runs it in [7:4]. */
#define IMX95_AHAB_TYPE(f)          ((uint32_t)(f) & 0x0FU)
#define IMX95_AHAB_CORE(f)          (((uint32_t)(f) >> 4) & 0x0FU)

#define IMX95_AHAB_TYPE_EXEC        0x03U   /* plain executable */
#define IMX95_AHAB_TYPE_ELE         0x05U   /* EdgeLock Enclave firmware */

#define IMX95_AHAB_CORE_M33         0x01U
#define IMX95_AHAB_CORE_A55         0x02U

struct imx95_ahab_image {
    uint64_t dst;       /* where the image is loaded */
    uint64_t entry;     /* where execution starts */
    uint32_t offset;    /* byte offset from the container header */
    uint32_t size;
    uint32_t flags;
};

struct imx95_ahab_container {
    uint32_t base;      /* byte offset of the header on the boot device */
    uint32_t size;      /* header through the last byte the container owns */
    uint32_t count;
    struct imx95_ahab_image img[IMX95_AHAB_MAX_IMAGES];
};

/* Reads len bytes at byte offset off from the boot device into buf. Both off
 * and len are multiples of the device block size. Returns 0 on success. */
typedef int (*imx95_ahab_read_cb)(void *ctx, uint32_t off, uint32_t len,
                                  void *buf);

int imx95_ahab_parse(struct imx95_ahab_container *ctnr, uint32_t base,
                     imx95_ahab_read_cb read_cb, void *ctx);
uint32_t imx95_ahab_next(const struct imx95_ahab_container *ctnr);
int imx95_ahab_load(const struct imx95_ahab_container *ctnr, uint32_t index,
                    void *dst, imx95_ahab_read_cb read_cb, void *ctx);

#endif /* IMX95_AHAB_H */
