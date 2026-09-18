/* imx95_ahab.c
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

/* The boot device holds a set of AHAB containers: container 0 carries the ELE
 * firmware, the System Manager and the first A55 image, the next one BL31,
 * OP-TEE and BL33. Nothing records where that next container starts - it is
 * the end of the previous one (the furthest of its header, its images and its
 * signature block) rounded up. Only offsets are read here; the signature block
 * belongs to the ELE, which verifies containers in hardware. */

#include <stdint.h>
#include <stddef.h>
#include "hal/imx95_ahab.h"

/* Container header, all little-endian:
 *   +0  version   +1 length (16-bit)  +3 tag
 *   +4  flags
 *   +8  sw_version (16-bit)  +10 fuse_version  +11 num_images
 *   +12 signature block offset (16-bit)
 * followed by num_images 128-byte image entries:
 *   +0  offset  +4 size  +8 dst (64-bit)  +16 entry (64-bit)  +24 flags
 * then hash and IV, which only the ELE looks at. */
#define AHAB_HDR_SIZE           16U
#define AHAB_IMG_ENTRY_SIZE     128U

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

/* Parse the container at byte offset base. On success ctnr describes it and
 * ctnr->size is the number of bytes it occupies from base. */
int imx95_ahab_parse(struct imx95_ahab_container *ctnr, uint32_t base,
                     imx95_ahab_read_cb read_cb, void *ctx)
{
    static uint8_t hdr[IMX95_AHAB_HDR_BYTES] __attribute__((aligned(4)));
    struct imx95_ahab_image *img;
    const uint8_t *e;
    uint32_t count, sig_off, sig_len, end, i;

    if (ctnr == NULL || read_cb == NULL)
        return -1;
    if ((base % IMX95_AHAB_ALIGN) != 0U)
        return -1;
    if (read_cb(ctx, base, (uint32_t)sizeof(hdr), hdr) != 0)
        return -1;

    if (hdr[3] != IMX95_AHAB_TAG || hdr[0] != IMX95_AHAB_VERSION)
        return -1;

    count = hdr[11];
    if (count == 0U || count > (uint32_t)IMX95_AHAB_MAX_IMAGES)
        return -1;

    ctnr->base = base;
    ctnr->count = count;

    /* The container ends at the furthest of the header, every image and the
     * signature block - the same rule U-Boot's get_container_size() uses. */
    end = rd16(&hdr[1]);

    for (i = 0; i < count; i++) {
        e = &hdr[AHAB_HDR_SIZE + (i * AHAB_IMG_ENTRY_SIZE)];
        img = &ctnr->img[i];
        img->offset = rd32(e);
        img->size = rd32(e + 4);
        img->dst = rd64(e + 8);
        img->entry = rd64(e + 16);
        img->flags = rd32(e + 24);

        /* A truncated or hostile entry must not wrap the end calculation and
         * make the container look smaller than it is. */
        if (img->size > (0xFFFFFFFFU - img->offset))
            return -1;
        if ((img->offset + img->size) > end)
            end = img->offset + img->size;
    }

    sig_off = rd16(&hdr[12]);
    if (sig_off != 0U) {
        if ((sig_off + 4U) > (uint32_t)sizeof(hdr))
            return -1;
        sig_len = rd16(&hdr[sig_off + 1]);
        if ((sig_off + sig_len) > end)
            end = sig_off + sig_len;
    }

    if (end > (0xFFFFFFFFU - base))
        return -1;
    ctnr->size = end;
    return 0;
}

/* Byte offset of the container that follows this one. */
uint32_t imx95_ahab_next(const struct imx95_ahab_container *ctnr)
{
    uint32_t end = ctnr->base + ctnr->size;

    return (end + (IMX95_AHAB_ALIGN - 1U)) & ~(IMX95_AHAB_ALIGN - 1U);
}

/* Copy one image from the boot device to dst. Image offsets and sizes are
 * block-aligned as the image tool emits them; a container that is not is
 * rejected rather than silently loaded from the wrong place. */
int imx95_ahab_load(const struct imx95_ahab_container *ctnr, uint32_t index,
                    void *dst, imx95_ahab_read_cb read_cb, void *ctx)
{
    const struct imx95_ahab_image *img;

    if (ctnr == NULL || dst == NULL || read_cb == NULL)
        return -1;
    if (index >= ctnr->count)
        return -1;

    img = &ctnr->img[index];
    if (img->size == 0U)
        return -1;
    if (((ctnr->base + img->offset) % IMX95_AHAB_BLOCK) != 0U)
        return -1;
    if ((img->size % IMX95_AHAB_BLOCK) != 0U)
        return -1;

    return read_cb(ctx, ctnr->base + img->offset, img->size, dst);
}
