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

/* The boot device holds AHAB containers: 0 has the ELE firmware, System Manager
 * and first A55 image, the next BL31, OP-TEE and BL33. Nothing records where the
 * next starts - it is the end of the previous, rounded up. */

#include <stdint.h>
#include <stddef.h>
#include "hal/imx95_ahab.h"

/* Header, LE: +0 ver, +1 len16, +3 tag, +4 flags, +8 sw_ver16, +10 fuse_ver,
 * +11 num_images, +12 sig off16. Then 128-byte entries: +0 offset, +4 size,
 * +8 dst64, +16 entry64, +24 flags, then hash and IV, read only by the ELE. */
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

/* Parse a container already in memory. Separate from the reading path because
 * once the ELE has blessed a staged copy the table must come from that copy -
 * a medium can answer differently the second time. */
int imx95_ahab_parse_buf(struct imx95_ahab_container *ctnr, uint32_t base,
                         const uint8_t *buf, uint32_t len)
{
    struct imx95_ahab_image *img;
    const uint8_t *e;
    uint32_t count, table_end, sig_off, sig_len, end, i;

    if (ctnr == NULL || buf == NULL)
        return -1;
    if ((base % IMX95_AHAB_ALIGN) != 0U)
        return -1;
    if (len < AHAB_HDR_SIZE)
        return -1;

    if (buf[3] != IMX95_AHAB_TAG || buf[0] != IMX95_AHAB_VERSION)
        return -1;

    count = buf[11];
    if (count == 0U || count > (uint32_t)IMX95_AHAB_MAX_IMAGES)
        return -1;

    /* The image table has to be inside the bytes handed in, or the entries
     * below would be read from beyond the buffer. */
    table_end = AHAB_HDR_SIZE + (count * AHAB_IMG_ENTRY_SIZE);
    if (table_end > len)
        return -1;

    ctnr->base = base;
    ctnr->count = count;
    ctnr->hdr_len = rd16(&buf[1]);

    /* An entry parked beyond the header's own length would sit outside what a
     * signature covers, so the table has to fit within it. */
    if (table_end > ctnr->hdr_len)
        return -1;

    /* The container ends at the furthest of the header, every image and the
     * signature block - the same rule U-Boot's get_container_size() uses. */
    end = ctnr->hdr_len;

    for (i = 0; i < count; i++) {
        e = &buf[AHAB_HDR_SIZE + (i * AHAB_IMG_ENTRY_SIZE)];
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

    ctnr->sig_end = 0U;
    sig_off = rd16(&buf[12]);
    if (sig_off != 0U) {
        if ((sig_off + 4U) > len)
            return -1;
        /* The table must stop before the signature block starts, or entries
         * would overlap the very bytes that authenticate them. */
        if (table_end > sig_off)
            return -1;
        sig_len = rd16(&buf[sig_off + 1]);
        if (sig_len > (0xFFFFFFFFU - sig_off))
            return -1;
        ctnr->sig_end = sig_off + sig_len;
        if (ctnr->sig_end > end)
            end = ctnr->sig_end;
    }

    if (end > (0xFFFFFFFFU - base))
        return -1;
    ctnr->size = end;
    return 0;
}

/* Parse the container at byte offset base. On success ctnr describes it and
 * ctnr->size is the number of bytes it occupies from base. */
int imx95_ahab_parse(struct imx95_ahab_container *ctnr, uint32_t base,
                     imx95_ahab_read_cb read_cb, void *ctx)
{
    static uint8_t hdr[IMX95_AHAB_HDR_BYTES] __attribute__((aligned(4)));

    if (read_cb == NULL)
        return -1;
    if ((base % IMX95_AHAB_ALIGN) != 0U)
        return -1;
    if (read_cb(ctx, base, (uint32_t)sizeof(hdr), hdr) != 0)
        return -1;

    return imx95_ahab_parse_buf(ctnr, base, hdr, (uint32_t)sizeof(hdr));
}

/* Byte offset of the next container. The round-up is overflow-guarded: a
 * container ending within IMX95_AHAB_ALIGN-1 of 4 GiB wrapped to 0, an aligned
 * and accepted offset, so the next parse read the partition table. */
int imx95_ahab_next(const struct imx95_ahab_container *ctnr, uint32_t *next)
{
    uint32_t end;

    if (ctnr == NULL || next == NULL)
        return -1;

    end = ctnr->base + ctnr->size;
    if (end > (0xFFFFFFFFU - (IMX95_AHAB_ALIGN - 1U)))
        return -1;

    end = (end + (IMX95_AHAB_ALIGN - 1U)) & ~(IMX95_AHAB_ALIGN - 1U);

    /* A container that does not advance would make the walk loop forever. */
    if (end <= ctnr->base)
        return -1;

    *next = end;
    return 0;
}

/* Bytes the ELE has to be given: the header, its table and its signature
 * block. Deliberately not ctnr->size, which spans the image payloads and on a
 * real container is tens of megabytes. */
int imx95_ahab_span(const struct imx95_ahab_container *ctnr, uint32_t *span)
{
    uint32_t n;

    if (ctnr == NULL || span == NULL)
        return -1;

    n = ctnr->hdr_len;
    if (ctnr->sig_end > n)
        n = ctnr->sig_end;
    if ((AHAB_HDR_SIZE + (ctnr->count * AHAB_IMG_ENTRY_SIZE)) > n)
        n = AHAB_HDR_SIZE + (ctnr->count * AHAB_IMG_ENTRY_SIZE);

    /* Attacker-controlled at this point: clamp before anything copies it. */
    if (n == 0U || n > IMX95_AHAB_SPAN_MAX)
        return -1;

    *span = n;
    return 0;
}

/* The ELE hashes exactly size bytes but the device transfers whole blocks, so
 * this rounded length is what gets written and must be bounds-checked. U-Boot
 * rounds the same way in read_auth_image(). */
/* True when the block-padded transfer to dst would touch [lo, hi). The padding
 * is the point: imx95_ahab_load() writes the rounded length, so an image ending
 * just below lo can still have its tail land inside. A rounded length of 0 is a
 * round-up overflow and counts as an overlap. */
int imx95_ahab_overlaps(uint64_t dst, uint32_t size, uint64_t lo, uint64_t hi)
{
    uint32_t len = imx95_ahab_load_len(size);

    if (len == 0U)
        return 1;
    if (dst > (UINT64_MAX - (uint64_t)len))
        return 1;
    return (dst < hi) && ((dst + (uint64_t)len) > lo);
}

uint32_t imx95_ahab_load_len(uint32_t size)
{
    uint32_t rem = size % IMX95_AHAB_BLOCK;

    if (rem == 0U)
        return size;
    if (size > (0xFFFFFFFFU - (IMX95_AHAB_BLOCK - rem)))
        return 0U;
    return size + (IMX95_AHAB_BLOCK - rem);
}

static int range_overlaps(uint64_t a_base, uint64_t a_len,
                          uint64_t b_base, uint64_t b_len)
{
    if (a_len == 0U || b_len == 0U)
        return 0;
    return (a_base < (b_base + b_len)) && (b_base < (a_base + a_len));
}

/* Reject every unsafe destination before writing any of them: an image streams
 * straight into img->dst, so by the time the ELE could object the bytes have
 * landed. Runs whether or not authentication is enabled. */
int imx95_ahab_check_dst(const struct imx95_ahab_container *ctnr,
                         uint64_t dram_base, uint64_t dram_size,
                         const struct imx95_ahab_region *excl,
                         uint32_t excl_count)
{
    const struct imx95_ahab_image *img;
    uint64_t len, other_len;
    uint32_t i, j;

    if (ctnr == NULL)
        return -1;
    if (dram_size == 0U || dram_base > (0xFFFFFFFFFFFFFFFFULL - dram_size))
        return -1;
    if (excl_count != 0U && excl == NULL)
        return -1;

    for (i = 0; i < ctnr->count; i++) {
        img = &ctnr->img[i];

        /* A zero-length image is not something a real container carries, and
         * skipping one would desynchronize "loaded" from "verified". */
        if (img->size == 0U)
            return -1;

        len = (uint64_t)imx95_ahab_load_len(img->size);
        if (len == 0U)
            return -1;
        if (img->dst > (0xFFFFFFFFFFFFFFFFULL - len))
            return -1;

        /* Inside DRAM, all of it. */
        if (img->dst < dram_base)
            return -1;
        if ((img->dst + len) > (dram_base + dram_size))
            return -1;

        /* Clear of the staging window, this loader, and the carveouts their
         * owners are already using. */
        for (j = 0; j < excl_count; j++) {
            if (range_overlaps(img->dst, len, excl[j].base, excl[j].size))
                return -1;
        }

        /* Disjoint from every other image, or a later load would silently
         * replace one the ELE has already verified. */
        for (j = 0; j < i; j++) {
            other_len = (uint64_t)imx95_ahab_load_len(ctnr->img[j].size);
            if (range_overlaps(img->dst, len, ctnr->img[j].dst, other_len))
                return -1;
        }
    }

    return 0;
}

/* The image stage 1 is going to branch into has to be an A55 executable, and
 * its entry has to be inside the bytes that were loaded and verified. */
int imx95_ahab_check_exec(const struct imx95_ahab_container *ctnr,
                          uint32_t index)
{
    const struct imx95_ahab_image *img;

    if (ctnr == NULL || index >= ctnr->count)
        return -1;

    img = &ctnr->img[index];
    if (IMX95_AHAB_TYPE(img->flags) != IMX95_AHAB_TYPE_EXEC)
        return -1;
    if (IMX95_AHAB_CORE(img->flags) != IMX95_AHAB_CORE_A55)
        return -1;
    if (img->entry < img->dst)
        return -1;
    if (img->entry >= (img->dst + (uint64_t)img->size))
        return -1;

    return 0;
}

/* Copy one image from the boot device to dst. The offset must be block-aligned
 * as the image tool emits it; a container that is not is rejected rather than
 * silently loaded from the wrong place. */
int imx95_ahab_load(const struct imx95_ahab_container *ctnr, uint32_t index,
                    void *dst, imx95_ahab_read_cb read_cb, void *ctx)
{
    const struct imx95_ahab_image *img;
    uint32_t len;

    if (ctnr == NULL || dst == NULL || read_cb == NULL)
        return -1;
    if (index >= ctnr->count)
        return -1;

    img = &ctnr->img[index];
    if (img->size == 0U)
        return -1;
    if (((ctnr->base + img->offset) % IMX95_AHAB_BLOCK) != 0U)
        return -1;

    len = imx95_ahab_load_len(img->size);
    if (len == 0U)
        return -1;

    return read_cb(ctx, ctnr->base + img->offset, len, dst);
}
