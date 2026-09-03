/* unit-update-disk-fs.c
 *
 * Integration tests for the disk loader reading its image from a file.
 *
 * Unlike unit-update-disk.c, this test does NOT mock disk_part_read(): it
 * mocks only the raw block layer and runs the real gpt.c, disk.c,
 * disk_fs.c, fat32.c and ext4.c underneath update_disk.c. The synthetic
 * drive carries an MBR with a FAT32 partition, an ext4 partition and a
 * raw one, so slot selection, mixed filesystems and the raw fallback are
 * all exercised through wolfBoot_start().
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#define WOLFBOOT_UPDATE_DISK
#define WOLFBOOT_SELF_UPDATE_MONOLITHIC
#define RAM_CODE
#define WOLFBOOT_SELF_HEADER
#define IMAGE_HEADER_SIZE 256
#define BOOT_PART_A 0
#define BOOT_PART_B 1
#define BOOT_FILE_A "/boot/a.itb"
#define BOOT_FILE_B "/boot/b.itb"
#define MOCK_ADDRESS_BOOT 0xCD000000

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "loader.h"
/* Declarations only. The implementations are included further down, after
 * the mocks they depend on, so the prototypes must be visible up here or
 * every call above becomes an implicit declaration. */
#include "disk.h"
#include "disk_fs.h"
#ifdef ENCRYPT_WITH_CHACHA
#include <wolfssl/wolfcrypt/chacha.h>
#endif

#define TEST_PAYLOAD_SIZE 4096
#define TEST_IMAGE_SIZE   (IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE)

static uint8_t load_buffer[TEST_PAYLOAD_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

/* ---------------------------------------------------------------------
 * Synthetic drive
 *
 * MBR, then a FAT32 volume (which must have at least 65525 clusters, so
 * it cannot be small), then a compact ext4 volume, then a raw partition.
 * --------------------------------------------------------------------- */

#define SEC 512U

/* FAT32 geometry: 512-byte clusters, exactly the minimum cluster count. */
#define F_RSVD        32U
#define F_NFATS        1U
#define F_FATSZ      512U
#define F_CLUSTERS 65525U
#define F_TOTSEC   (F_RSVD + (F_NFATS * F_FATSZ) + F_CLUSTERS)

#define P0_LBA     2048U
#define P0_SECS    F_TOTSEC
#define P1_LBA     (P0_LBA + P0_SECS)
#define P1_SECS    512U                 /* 256 KiB of ext4 */
#define P2_LBA     (P1_LBA + P1_SECS)
#define P2_SECS    64U
#define DISK_SECS  (P2_LBA + P2_SECS)

#define P0_OFF ((uint64_t)P0_LBA * SEC)
#define P1_OFF ((uint64_t)P1_LBA * SEC)
#define P2_OFF ((uint64_t)P2_LBA * SEC)
#define DISK_SZ ((uint64_t)DISK_SECS * SEC)

/* FAT32 layout, relative to the start of partition 0 */
#define F_FAT_OFF  ((uint64_t)F_RSVD * SEC)
#define F_DATA_OFF (F_FAT_OFF + ((uint64_t)F_NFATS * F_FATSZ * SEC))
#define F_CLUS(c)  (F_DATA_OFF + (((uint64_t)(c) - 2U) * SEC))
#define F_ROOT_CLUS 2U
#define F_BOOT_CLUS 3U      /* the /boot directory */
#define F_FILE_CLUS 10U     /* first data cluster of /boot/a.itb */

/* ext4 layout, relative to the start of partition 1 */
#define E_BLK        1024U
#define E_BLOCKS      256U
#define E_SB         1024U
#define E_FDB           1U
#define E_BPG        8192U
#define E_IPG          32U
#define E_INODES       32U
#define E_INODE_SZ    128U
#define E_GDT_OFF    ((uint64_t)(E_FDB + 1U) * E_BLK)
#define E_ITBL_BLK     10U
#define E_ROOT_BLK     20U
#define E_BOOT_BLK     21U
#define E_FILE_BLK     30U
#define E_BOOT_INO     11U
#define E_FILE_INO     12U

static uint8_t *disk = NULL;

static int mock_disk_init_ret;
static int mock_disk_close_called;
static int mock_do_boot_called;
static const uint32_t *mock_boot_address;
static int mock_verify_integrity_ret;
static int mock_verify_authenticity_ret;
static int mock_flash_protect_called;
static haladdr_t mock_flash_protect_addr;
static int mock_flash_protect_len;

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    (void)drv;
    if ((start + count) > DISK_SZ)
        return -1;
    memcpy(buf, disk + start, count);
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

int disk_init(int drv)
{
    (void)drv;
    return mock_disk_init_ret;
}

void disk_close(int drv)
{
    (void)drv;
    mock_disk_close_called++;
}

/* --- byte helpers --- */

static void put16(uint64_t off, uint16_t v)
{
    disk[off] = (uint8_t)(v & 0xFF);
    disk[off + 1] = (uint8_t)(v >> 8);
}

static void put32(uint64_t off, uint32_t v)
{
    put16(off, (uint16_t)(v & 0xFFFF));
    put16(off + 2, (uint16_t)(v >> 16));
}

/* --- optional disk encryption ---
 *
 * The mock keystream is a function of the byte's position within the
 * image, so a wrong IV or a wrong offset yields garbage instead of
 * silently working. That is what makes this a real check that a file's
 * byte stream lines up with the CTR counter exactly as a raw partition's
 * does. */
#ifdef ENCRYPT_WITH_CHACHA
ChaCha chacha;
static uint32_t mock_ctr;

static uint8_t mock_ks(uint64_t off)
{
    return (uint8_t)(((off * 31U) ^ 0xA5U) & 0xFFU);
}

int chacha_init(void)
{
    return 0;
}

int wc_Chacha_SetIV(ChaCha *ctx, const byte *inIv, word32 counter)
{
    (void)ctx;
    (void)inIv;
    mock_ctr = counter;
    return 0;
}

int wc_Chacha_Process(ChaCha *ctx, byte *output, const byte *input,
                      word32 msglen)
{
    uint64_t base = (uint64_t)mock_ctr * ENCRYPT_BLOCK_SIZE;
    word32 i;

    (void)ctx;
    for (i = 0; i < msglen; i++)
        output[i] = (byte)(input[i] ^ mock_ks(base + i));
    return 0;
}

void wc_ForceZero(void *mem, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)mem;
    while (len-- > 0)
        *p++ = 0;
}

int wolfBoot_initialize_encryption(void)
{
    return 0;
}

int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    memset(key, 0x5A, ENCRYPT_KEY_SIZE);
    memset(nonce, 0xC3, ENCRYPT_NONCE_SIZE);
    return 0;
}
#endif /* ENCRYPT_WITH_CHACHA */

/* --- signed-image blob --- */

static void build_image(uint8_t *image, uint32_t version, uint8_t fill,
                        uint32_t fw_size)
{
    memset(image, 0, TEST_IMAGE_SIZE);
    image[0] = (uint8_t)(WOLFBOOT_MAGIC & 0xFF);
    image[1] = (uint8_t)((WOLFBOOT_MAGIC >> 8) & 0xFF);
    image[2] = (uint8_t)((WOLFBOOT_MAGIC >> 16) & 0xFF);
    image[3] = (uint8_t)((WOLFBOOT_MAGIC >> 24) & 0xFF);
    image[4] = (uint8_t)(fw_size & 0xFF);
    image[5] = (uint8_t)((fw_size >> 8) & 0xFF);
    image[6] = (uint8_t)((fw_size >> 16) & 0xFF);
    image[7] = (uint8_t)((fw_size >> 24) & 0xFF);
    image[IMAGE_HEADER_OFFSET] = (uint8_t)(HDR_VERSION & 0xFF);
    image[IMAGE_HEADER_OFFSET + 1] = (uint8_t)(HDR_VERSION >> 8);
    image[IMAGE_HEADER_OFFSET + 2] = 4;
    image[IMAGE_HEADER_OFFSET + 3] = 0;
    image[IMAGE_HEADER_OFFSET + 4] = (uint8_t)(version & 0xFF);
    image[IMAGE_HEADER_OFFSET + 5] = (uint8_t)((version >> 8) & 0xFF);
    image[IMAGE_HEADER_OFFSET + 6] = (uint8_t)((version >> 16) & 0xFF);
    image[IMAGE_HEADER_OFFSET + 7] = (uint8_t)((version >> 24) & 0xFF);
    memset(image + IMAGE_HEADER_SIZE, fill, TEST_PAYLOAD_SIZE);
}

static uint8_t image_a[TEST_IMAGE_SIZE];
static uint8_t image_b[TEST_IMAGE_SIZE];
/* What actually goes on the media: identical to the image above, unless
 * disk encryption is compiled in. */
static uint8_t media_a[TEST_IMAGE_SIZE];
static uint8_t media_b[TEST_IMAGE_SIZE];

static void to_media(uint8_t *dst, const uint8_t *src)
{
#ifdef ENCRYPT_WITH_CHACHA
    uint32_t i;
    for (i = 0; i < TEST_IMAGE_SIZE; i++)
        dst[i] = (uint8_t)(src[i] ^ mock_ks(i));
#else
    memcpy(dst, src, TEST_IMAGE_SIZE);
#endif
}

/* --- MBR --- */

static void build_mbr(void)
{
    uint64_t e = 0x1BEU;

    memset(disk, 0, SEC);
    disk[e + 4] = 0x0C;                 /* FAT32 LBA */
    put32(e + 8, P0_LBA);
    put32(e + 12, P0_SECS);
    e += 16;
    disk[e + 4] = 0x83;                 /* Linux */
    put32(e + 8, P1_LBA);
    put32(e + 12, P1_SECS);
    e += 16;
    disk[e + 4] = 0x83;
    put32(e + 8, P2_LBA);
    put32(e + 12, P2_SECS);
    disk[0x1FE] = 0x55;
    disk[0x1FF] = 0xAA;
}

/* --- FAT32 volume in partition 0 --- */

static uint32_t f_fat_get(uint32_t clus)
{
    uint64_t off = P0_OFF + F_FAT_OFF + ((uint64_t)clus * 4U);

    return ((uint32_t)disk[off]) | ((uint32_t)disk[off + 1] << 8) |
           ((uint32_t)disk[off + 2] << 16) | ((uint32_t)disk[off + 3] << 24);
}

static void f_fat(uint32_t clus, uint32_t val)
{
    put32(P0_OFF + F_FAT_OFF + ((uint64_t)clus * 4U), val);
}

static void f_sfn(uint64_t off, const char *name11, uint8_t attr,
                  uint32_t clus, uint32_t size)
{
    memset(disk + off, 0, 32);
    memcpy(disk + off, name11, 11);
    disk[off + 11] = attr;
    put16(off + 20, (uint16_t)(clus >> 16));
    put16(off + 26, (uint16_t)(clus & 0xFFFF));
    put32(off + 28, size);
}

/* Lay down a FAT32 volume holding /boot/A.ITB of the given length. */
static void build_fat32(uint32_t file_size)
{
    uint64_t bs = P0_OFF;
    uint32_t need, i;

    memset(disk + P0_OFF, 0, SEC);
    memset(disk + P0_OFF + F_FAT_OFF, 0, (size_t)F_NFATS * F_FATSZ * SEC);
    memset(disk + P0_OFF + F_CLUS(F_ROOT_CLUS), 0, SEC * 2U);

    disk[bs] = 0xEB;
    disk[bs + 1] = 0x58;
    disk[bs + 2] = 0x90;
    memcpy(disk + bs + 3, "MSWIN4.1", 8);
    put16(bs + 0x0B, (uint16_t)SEC);
    disk[bs + 0x0D] = 1;
    put16(bs + 0x0E, (uint16_t)F_RSVD);
    disk[bs + 0x10] = F_NFATS;
    put16(bs + 0x11, 0);
    put16(bs + 0x13, 0);
    disk[bs + 0x15] = 0xF8;
    put16(bs + 0x16, 0);
    put32(bs + 0x20, F_TOTSEC);
    put32(bs + 0x24, F_FATSZ);
    put16(bs + 0x28, 0);
    put16(bs + 0x2A, 0);
    put32(bs + 0x2C, F_ROOT_CLUS);
    memcpy(disk + bs + 0x47, "SLOTA      ", 11);
    put16(bs + 0x1FE, 0xAA55);

    f_fat(0, 0x0FFFFFF8U);
    f_fat(1, 0x0FFFFFFFU);
    f_fat(F_ROOT_CLUS, 0x0FFFFFFFU);
    f_fat(F_BOOT_CLUS, 0x0FFFFFFFU);

    /* Root: one subdirectory named BOOT. */
    f_sfn(P0_OFF + F_CLUS(F_ROOT_CLUS), "BOOT       ", 0x10, F_BOOT_CLUS, 0);

    /* /boot: ".", ".." and A.ITB. */
    f_sfn(P0_OFF + F_CLUS(F_BOOT_CLUS), ".          ", 0x10, F_BOOT_CLUS, 0);
    f_sfn(P0_OFF + F_CLUS(F_BOOT_CLUS) + 32, "..         ", 0x10, 0, 0);
    f_sfn(P0_OFF + F_CLUS(F_BOOT_CLUS) + 64, "A       ITB", 0x20,
        F_FILE_CLUS, file_size);

    /* Chain and fill exactly as many clusters as the length needs. */
    need = (file_size + SEC - 1U) / SEC;
    if (need == 0U)
        need = 1U;
    for (i = 0; i < (need - 1U); i++)
        f_fat(F_FILE_CLUS + i, F_FILE_CLUS + i + 1U);
    f_fat(F_FILE_CLUS + need - 1U, 0x0FFFFFFFU);
    memset(disk + P0_OFF + F_CLUS(F_FILE_CLUS), 0, (size_t)need * SEC);
    to_media(media_a, image_a);
    /* file_size may deliberately exceed the staging buffer, to declare a
     * file larger than the load region; copy only what exists. */
    memcpy(disk + P0_OFF + F_CLUS(F_FILE_CLUS), media_a,
        (file_size > TEST_IMAGE_SIZE) ? TEST_IMAGE_SIZE : file_size);
}

/* Rewrite slot A's file as a DELIBERATELY FRAGMENTED chain.
 *
 * build_fat32() lays the file out contiguously, so fat32_read() coalesces the
 * whole thing into one media read and the multi-run path never executes. Here
 * every cluster is placed STRIDE apart, so no two are adjacent and the reader
 * is forced to issue one transfer per cluster and stitch them back together.
 * The payload is identical, so any error shows up as a content mismatch. */
#define F_FRAG_STRIDE 3U

static void build_fat32_fragmented(uint32_t file_size)
{
    uint32_t need, i, clus, next, src_off, chunk, avail;

    build_fat32(file_size);

    need = (file_size + SEC - 1U) / SEC;
    if (need == 0U)
        need = 1U;

    /* Free the contiguous chain build_fat32() just made. */
    for (i = 0; i < need; i++) {
        f_fat(F_FILE_CLUS + i, 0);
    }

    /* Relink the same clusters with a gap between each, and move the data
     * to match. Written back to front so a later cluster is never clobbered
     * by an earlier copy.
     *
     * The copy is clamped to what media_a actually holds: need rounds UP to
     * a whole cluster, so the final one covers fewer bytes than SEC and a
     * blind SEC-sized copy would read past the end of the array. The tail of
     * that cluster is zeroed, which is what a real filesystem leaves there. */
    for (i = need; i > 0U; i--) {
        clus = F_FILE_CLUS + ((i - 1U) * F_FRAG_STRIDE);
        src_off = (i - 1U) * SEC;
        avail = (src_off < (uint32_t)TEST_IMAGE_SIZE) ?
            ((uint32_t)TEST_IMAGE_SIZE - src_off) : 0U;
        chunk = (avail < SEC) ? avail : SEC;
        memset(disk + P0_OFF + F_CLUS(clus), 0, SEC);
        if (chunk > 0U) {
            memcpy(disk + P0_OFF + F_CLUS(clus),
                   media_a + (size_t)src_off, (size_t)chunk);
        }
    }
    for (i = 0; i < need; i++) {
        clus = F_FILE_CLUS + (i * F_FRAG_STRIDE);
        if (i == (need - 1U)) {
            f_fat(clus, 0x0FFFFFFFU);
        }
        else {
            next = F_FILE_CLUS + ((i + 1U) * F_FRAG_STRIDE);
            f_fat(clus, next);
        }
    }
}

/* --- ext4 volume in partition 1 --- */

static uint64_t e_ino_off(uint32_t ino)
{
    return P1_OFF + ((uint64_t)E_ITBL_BLK * E_BLK) +
        ((uint64_t)(ino - 1U) * E_INODE_SZ);
}

static void e_extent(uint32_t ino, uint32_t len, uint32_t pblk)
{
    uint64_t off = e_ino_off(ino) + 0x28U;

    memset(disk + off, 0, 60);
    put16(off + 0, 0xF30AU);
    put16(off + 2, 1);
    put16(off + 4, 4);
    put16(off + 6, 0);
    put32(off + 12, 0);
    put16(off + 16, (uint16_t)len);
    put16(off + 18, 0);
    put32(off + 20, pblk);
}

static void e_inode(uint32_t ino, uint16_t mode, uint32_t size)
{
    uint64_t off = e_ino_off(ino);

    memset(disk + off, 0, E_INODE_SZ);
    put16(off + 0x00, mode);
    put32(off + 0x04, size);
    put32(off + 0x20, 0x00080000U);     /* EXTENTS_FL */
}

static void e_dirent(uint64_t off, uint32_t ino, uint32_t rec_len,
                     const char *name, uint8_t ftype)
{
    uint32_t nlen = (uint32_t)strlen(name);

    memset(disk + off, 0, rec_len);
    put32(off, ino);
    put16(off + 4, (uint16_t)rec_len);
    disk[off + 6] = (uint8_t)nlen;
    disk[off + 7] = ftype;
    memcpy(disk + off + 8, name, nlen);
}

/* Lay down an ext4 volume holding /boot/b.itb of the given length. */
static void build_ext4(uint32_t file_size)
{
    uint64_t root = P1_OFF + ((uint64_t)E_ROOT_BLK * E_BLK);
    uint64_t boot = P1_OFF + ((uint64_t)E_BOOT_BLK * E_BLK);
    uint32_t need;

    memset(disk + P1_OFF, 0, (size_t)P1_SECS * SEC);

    put32(P1_OFF + E_SB + 0x00, E_INODES);
    put32(P1_OFF + E_SB + 0x04, E_BLOCKS);
    put32(P1_OFF + E_SB + 0x14, E_FDB);
    put32(P1_OFF + E_SB + 0x18, 0);
    put32(P1_OFF + E_SB + 0x1C, 0);
    put32(P1_OFF + E_SB + 0x20, E_BPG);
    put32(P1_OFF + E_SB + 0x28, E_IPG);
    put16(P1_OFF + E_SB + 0x38, 0xEF53U);
    put16(P1_OFF + E_SB + 0x3A, 1);
    put32(P1_OFF + E_SB + 0x4C, 1);
    put32(P1_OFF + E_SB + 0x54, 11);
    put16(P1_OFF + E_SB + 0x58, E_INODE_SZ);
    put32(P1_OFF + E_SB + 0x60, 0x42U);     /* EXTENTS | FILETYPE */
    memcpy(disk + P1_OFF + E_SB + 0x78, "SLOTB", 5);

    put32(P1_OFF + E_GDT_OFF + 0x08, E_ITBL_BLK);

    e_inode(2, 0x41EDU, E_BLK);
    e_extent(2, 1, E_ROOT_BLK);
    e_dirent(root, 2, 12, ".", 2);
    e_dirent(root + 12, 2, 12, "..", 2);
    e_dirent(root + 24, E_BOOT_INO, E_BLK - 24U, "boot", 2);

    e_inode(E_BOOT_INO, 0x41EDU, E_BLK);
    e_extent(E_BOOT_INO, 1, E_BOOT_BLK);
    e_dirent(boot, E_BOOT_INO, 12, ".", 2);
    e_dirent(boot + 12, 2, 12, "..", 2);
    e_dirent(boot + 24, E_FILE_INO, E_BLK - 24U, "b.itb", 1);

    need = (file_size + E_BLK - 1U) / E_BLK;
    if (need == 0U)
        need = 1U;
    e_inode(E_FILE_INO, 0x81A4U, file_size);
    e_extent(E_FILE_INO, need, E_FILE_BLK);
    to_media(media_b, image_b);
    memcpy(disk + P1_OFF + ((uint64_t)E_FILE_BLK * E_BLK), media_b,
        (file_size > TEST_IMAGE_SIZE) ? TEST_IMAGE_SIZE : file_size);
}

static void build_disk(void)
{
    if (disk == NULL) {
        disk = (uint8_t *)calloc(1, (size_t)DISK_SZ);
        ck_assert(disk != NULL);
    }
    build_image(image_a, 1, 0xA1, TEST_PAYLOAD_SIZE);
    build_image(image_b, 2, 0xB2, TEST_PAYLOAD_SIZE);
    build_mbr();
    build_fat32(TEST_IMAGE_SIZE);
    build_ext4(TEST_IMAGE_SIZE);
    memset(disk + P2_OFF, 0, (size_t)P2_SECS * SEC);
}

static uint32_t test_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i, j, mask;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8U; j++) {
            mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static void g_name(uint64_t off, const char *ascii)
{
    uint32_t i;

    for (i = 0; (i < 36U) && (ascii[i] != '\0'); i++) {
        put16(off + ((uint64_t)i * 2U), (uint16_t)ascii[i]);
    }
}

/* Replace the MBR with a protective MBR + GPT describing the SAME two
 * partitions, and give partition 1 the GPT name "SLOTA" -- which is also
 * partition 0's FAT32 volume label. slot_find_by_label() must prefer the
 * GPT name, so the label has to resolve to partition 1, not 0. */
static void build_gpt(void)
{
    uint64_t hdr = (uint64_t)SEC;
    uint64_t arr = (uint64_t)SEC * 2U;

    memset(disk, 0, (size_t)SEC * 3U);

    /* Protective MBR */
    disk[0x1BE + 4] = 0xEE;
    put32(0x1BE + 8, 1U);
    put32(0x1BE + 12, 0xFFFFFFFFU);
    disk[0x1FE] = 0x55;
    disk[0x1FF] = 0xAA;

    /* GPT header */
    memcpy(disk + hdr, "EFI PART", 8);
    put32(hdr + 0x08, 0x00010000U);     /* revision */
    put32(hdr + 0x0C, 92U);             /* hdr_size */
    put32(hdr + 0x48, 2U);              /* start_array LBA */
    put32(hdr + 0x50, 2U);              /* n_part */
    put32(hdr + 0x54, 128U);            /* array_sz */

    /* Entry 0: the FAT32 volume, GPT name "ESP" (deliberately NOT SLOTA) */
    put32(arr + 0x00, 0x0BADC0DEU);     /* non-zero type GUID */
    put32(arr + 0x20, P0_LBA);
    put32(arr + 0x28, P0_LBA + P0_SECS - 1U);
    g_name(arr + 0x38, "ESP");

    /* Entry 1: the ext4 volume, GPT name "SLOTA" */
    put32(arr + 128 + 0x00, 0x0BADC0DEU);
    put32(arr + 128 + 0x20, P1_LBA);
    put32(arr + 128 + 0x28, P1_LBA + P1_SECS - 1U);
    g_name(arr + 128 + 0x38, "SLOTA");

    put32(hdr + 0x58, test_crc32(disk + arr, 2U * 128U));
    put32(hdr + 0x10, 0U);
    put32(hdr + 0x10, test_crc32(disk + hdr, 92U));
}

/* Build the drive and parse its partition table, for the tests that call
 * slot_prepare() directly rather than going through wolfBoot_start(). */
static void open_disk(void)
{
    ck_assert_int_eq(disk_init(0), 0);
    ck_assert_int_eq(disk_open(0), 3);
}

static void reset_mocks(void)
{
    memset(load_buffer, 0, sizeof(load_buffer));
    mock_disk_init_ret = 0;
    mock_disk_close_called = 0;
    mock_do_boot_called = 0;
    mock_boot_address = NULL;
    mock_verify_integrity_ret = 0;
    mock_verify_authenticity_ret = 0;
    mock_flash_protect_called = 0;
    mock_flash_protect_addr = 0;
    mock_flash_protect_len = 0;
    wolfBoot_panicked = 0;
    build_disk();
}

/* --- image / boot mocks --- */

int wolfBoot_open_image_address(struct wolfBoot_image *img, uint8_t *image)
{
    uint32_t magic;
    uint32_t fw_size;

    memcpy(&magic, image, sizeof(magic));
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    memset(img, 0, sizeof(*img));
    img->hdr = image;
    memcpy(&fw_size, image + sizeof(uint32_t), sizeof(fw_size));
    img->fw_size = fw_size;
    img->fw_base = image + IMAGE_HEADER_SIZE;
    img->hdr_ok = 1;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    if (mock_verify_integrity_ret == 0)
        img->sha_ok = 1;
    return mock_verify_integrity_ret;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    if (mock_verify_authenticity_ret == 0)
        img->signature_ok = 1;
    return mock_verify_authenticity_ret;
}

/* Parses the single version TLV that build_image() writes. */
uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    uint32_t magic;
    uint32_t version;

    memcpy(&magic, blob, sizeof(magic));
    if (magic != WOLFBOOT_MAGIC)
        return 0;
    memcpy(&version, blob + IMAGE_HEADER_OFFSET + 4, sizeof(version));
    return version;
}

int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity)
{
    (void)dts_addr;
    (void)capacity;
    return -1;
}

void hal_prepare_boot(void)
{
}

void do_boot(const uint32_t *address)
{
    mock_do_boot_called++;
    mock_boot_address = address;
}

int hal_flash_protect(haladdr_t address, int len)
{
    mock_flash_protect_called++;
    mock_flash_protect_addr = address;
    mock_flash_protect_len = len;
    return 0;
}

#include "gpt.c"
#include "disk.c"
#include "disk_fs.c"
#include "fat32.c"
#include "ext4.c"
#include "update_disk.c"

/* --- tests --- */

/* Slot A is a file on FAT32 at version 1, slot B a file on ext4 at
 * version 2. The higher version must win, and the payload that lands in
 * the load region must be the one from the ext4 file. */
START_TEST(test_fs_boots_higher_version_across_filesystems)
{
    reset_mocks();
    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(boot_slots[0].vol.type, FS_TYPE_FAT32);
    ck_assert_int_eq(boot_slots[1].vol.type, FS_TYPE_EXT4);
    ck_assert_int_eq(memcmp(load_buffer, image_b + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

START_TEST(test_fs_boots_from_fat32_when_it_is_newer)
{
    reset_mocks();
    build_image(image_a, 9, 0xA1, TEST_PAYLOAD_SIZE);
    build_fat32(TEST_IMAGE_SIZE);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* A missing file on the newer slot must fall back to the other one rather
 * than reading the filesystem raw. */
START_TEST(test_fs_falls_back_when_file_is_missing)
{
    reset_mocks();
    /* Rename b.itb so BOOT_FILE_B no longer resolves. */
    disk[P1_OFF + ((uint64_t)E_BOOT_BLK * E_BLK) + 24U + 8U] = 'x';

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* A file too short to hold what its header declares must fail its slot,
 * not produce a truncated load.
 *
 * Both slots carry the same version here: with anti-rollback enabled the
 * loader will not fall back from a newer slot to an older one, so equal
 * versions are what isolates the truncation handling from that rule. */
START_TEST(test_fs_rejects_truncated_file)
{
    uint32_t probe_i;

    reset_mocks();
    /* Slot B must be the NEWER one. The loader picks the higher version
     * first, so with equal versions it would boot A and never look at B,
     * leaving the guard below unexercised. */
    build_image(image_a, 5, 0xA1, TEST_PAYLOAD_SIZE);
    build_image(image_b, 6, 0xB2, TEST_PAYLOAD_SIZE);
    build_fat32(TEST_IMAGE_SIZE);
    build_ext4(TEST_IMAGE_SIZE);
    /* Shrink b.itb to a single block while its header still claims a
     * full-size payload. */
    put32(e_ino_off(E_FILE_INO) + 0x04, E_BLK);
    e_extent(E_FILE_INO, 1, E_FILE_BLK);

    wolfBoot_start();

    /* B is refused by the fw_size-vs-file-size guard in update_disk.c, and
     * the fallback to the older A is then refused by anti-rollback, so
     * nothing boots at all. The property under test is that a truncated
     * image is never loaded: do_boot must not be reached, and the load
     * buffer must not contain B's payload. */
    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_ne(memcmp(load_buffer, image_b + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
    /* Stronger: the guard rejects B from its header alone, so not one byte
     * of the truncated payload is ever copied. Without the guard the loader
     * would start the transfer and only fail on the short read, leaving
     * partial data here. reset_mocks() zeroed the buffer. */
    for (probe_i = 0; probe_i < TEST_PAYLOAD_SIZE; probe_i++) {
        ck_assert_uint_eq(load_buffer[probe_i], 0);
    }
}
END_TEST

/* With no usable image on either slot the loader must panic rather than
 * boot something it could not verify. */
START_TEST(test_fs_panics_when_no_slot_is_usable)
{
    reset_mocks();
    memset(disk + P0_OFF, 0, SEC);
    memset(disk + P1_OFF, 0, (size_t)P1_SECS * SEC);

    wolfBoot_start();

    /* wolfBoot_panic() only counts under UNIT_TEST rather than halting, so
     * the loader may reach it more than once on the way out. */
    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 0);
}
END_TEST

/* A recognized filesystem this build refuses must fail the slot, never
 * fall through to a raw read of its boot sector. */
START_TEST(test_fs_unsupported_filesystem_fails_the_slot)
{
    reset_mocks();
    /* Turn the ext4 volume into ext2 by clearing the extents feature. */
    put32(P1_OFF + E_SB + 0x60, 0x02U);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(boot_slots[1].ready, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* A partition holding no filesystem must still be read raw, exactly as a
 * build without DISK_FS would. */
START_TEST(test_fs_raw_partition_still_works)
{
    reset_mocks();
    /* Make slot B a raw partition: destroy the ext4 superblock and put a
     * signed image at offset 0 of the same partition instead. */
    memset(disk + P1_OFF, 0, (size_t)P1_SECS * SEC);
    to_media(media_b, image_b);
    memcpy(disk + P1_OFF, media_b, TEST_IMAGE_SIZE);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(boot_slots[1].vol.type, FS_TYPE_RAW);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_b + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* A slot whose header claims more than WOLFBOOT_RAMBOOT_MAX_SIZE must be
 * refused before it can drive a read into the load region. */
START_TEST(test_fs_respects_ramboot_max_size)
{
    reset_mocks();
    /* Equal versions, so anti-rollback permits the other slot; see
     * test_fs_rejects_truncated_file. */
    build_image(image_a, 5, 0xA1, TEST_PAYLOAD_SIZE);
    /* Slot B declares a payload far larger than the cap. */
    build_image(image_b, 5, 0xB2, 0x7FFFFFFFU);
    build_fat32(TEST_IMAGE_SIZE);
    build_ext4(TEST_IMAGE_SIZE);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* A file that is ITSELF larger than the load region must be refused by
 * fs_open() before its header is even read. Distinct from
 * test_fs_respects_ramboot_max_size, which uses an in-bounds file whose
 * HEADER lies about fw_size and so exercises the later size check. */
START_TEST(test_fs_rejects_oversized_file_at_open)
{
    reset_mocks();
    /* Equal versions, so anti-rollback permits falling back to slot A. */
    build_image(image_a, 5, 0xA1, TEST_PAYLOAD_SIZE);
    build_image(image_b, 5, 0xB2, TEST_PAYLOAD_SIZE);
    build_fat32(TEST_IMAGE_SIZE);
    /* slot_max is WOLFBOOT_RAMBOOT_MAX_SIZE + IMAGE_HEADER_SIZE, which is
     * exactly TEST_IMAGE_SIZE here, so one byte more must be refused. */
    build_ext4(TEST_IMAGE_SIZE + 1U);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* Both slots oversized leaves nothing bootable: the loader must panic
 * rather than load a file it already judged too large. */
START_TEST(test_fs_panics_when_every_file_is_oversized)
{
    reset_mocks();
    build_image(image_a, 5, 0xA1, TEST_PAYLOAD_SIZE);
    build_image(image_b, 5, 0xB2, TEST_PAYLOAD_SIZE);
    build_fat32(TEST_IMAGE_SIZE + 1U);
    build_ext4(TEST_IMAGE_SIZE + 1U);

    wolfBoot_start();

    /* wolfBoot_panic() counts rather than halting under UNIT_TEST, so the
     * loader may reach it more than once on the way out. */
    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 0);
}
END_TEST

/* A fragmented file must read back byte-identical to a contiguous one. This
 * is the run-coalescing path in fat32_read(): with no two clusters adjacent
 * it issues one read per cluster instead of one for the whole file. */
START_TEST(test_fs_reads_fragmented_fat32_file)
{
    reset_mocks();
    /* Slot A newer, so the fragmented FAT32 file is the one that boots. */
    build_image(image_a, 9, 0xA1, TEST_PAYLOAD_SIZE);
    build_image(image_b, 1, 0xB2, TEST_PAYLOAD_SIZE);
    build_fat32_fragmented(TEST_IMAGE_SIZE);
    build_ext4(TEST_IMAGE_SIZE);

    /* Prove the fixture really is fragmented, so this test cannot pass by
     * accident if the layout silently became contiguous again. */
    ck_assert_uint_ne(f_fat_get(F_FILE_CLUS) & 0x0FFFFFFFU,
        F_FILE_CLUS + 1U);
    ck_assert_uint_eq(f_fat_get(F_FILE_CLUS) & 0x0FFFFFFFU,
        F_FILE_CLUS + F_FRAG_STRIDE);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, image_a + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* --- partition selection by name --- */

/* MBR disks have no partition names at all, so the GPT lookup always
 * misses and the filesystem volume label is what resolves the slot. That
 * fallback is the part this branch adds; the GPT-name lookup itself is
 * covered directly in unit-disk.c. */
START_TEST(test_fs_label_selects_partition_by_volume_label)
{
    struct boot_slot s;

    reset_mocks();
    open_disk();

    /* Deliberately pass a nonsense index, so only the label can be what
     * resolves the partition. */
    ck_assert_int_eq(slot_prepare(&s, 99, "SLOTA", "/boot/a.itb",
        1U << 20), 0);
    ck_assert_int_eq(s.part, 0);
    ck_assert_int_eq(s.vol.type, FS_TYPE_FAT32);
    ck_assert_int_eq(s.ready, 1);

    ck_assert_int_eq(slot_prepare(&s, 99, "SLOTB", "/boot/b.itb",
        1U << 20), 0);
    ck_assert_int_eq(s.part, 1);
    ck_assert_int_eq(s.vol.type, FS_TYPE_EXT4);
    ck_assert_int_eq(s.ready, 1);
}
END_TEST

/* FAT labels are case-insensitive, ext4 labels are not. */
START_TEST(test_fs_label_case_rules_match_each_filesystem)
{
    struct boot_slot s;

    reset_mocks();
    open_disk();
    ck_assert_int_eq(slot_prepare(&s, 99, "slota", "/boot/a.itb",
        1U << 20), 0);
    ck_assert_int_eq(s.part, 0);

    ck_assert_int_eq(slot_prepare(&s, 99, "slotb", "/boot/b.itb",
        1U << 20), -1);
    ck_assert_int_eq(s.ready, 0);
}
END_TEST

/* No match must fail the slot, not fall through to partition 0. */
START_TEST(test_fs_label_no_match_fails_the_slot)
{
    struct boot_slot s;
    uint8_t scratch[8];

    reset_mocks();
    open_disk();
    ck_assert_int_eq(slot_prepare(&s, 99, "NOSUCH", "/boot/a.itb",
        1U << 20), -1);
    ck_assert_int_eq(s.ready, 0);
    ck_assert_int_eq(slot_read(&s, 0, sizeof(scratch), scratch), -1);
}
END_TEST

/* The GPT partition name takes precedence over the filesystem volume label.
 * Here partition 1's GPT name and partition 0's FAT32 volume label are the
 * same string, so the two lookups disagree and the ordering is observable:
 * a regression that reordered them would boot the wrong partition. */
START_TEST(test_fs_gpt_name_wins_over_volume_label)
{
    struct boot_slot s;

    reset_mocks();
    build_gpt();
    ck_assert_int_eq(disk_init(0), 0);
    ck_assert_int_eq(disk_open(0), 2);   /* the GPT layout has two */

    ck_assert_int_eq(slot_prepare(&s, 99, "SLOTA", "/boot/b.itb",
        1U << 20), 0);
    /* Partition 0's FAT32 volume label is also "SLOTA"; the GPT name on
     * partition 1 must win. */
    ck_assert_int_eq(s.part, 1);
    ck_assert_int_eq(s.vol.type, FS_TYPE_EXT4);
    ck_assert_int_eq(s.ready, 1);
}
END_TEST

/* --- fail-closed defaults --- */

/* A partition holding a real filesystem with no boot file configured must
 * fail the slot. Reading it raw would parse the filesystem's own boot
 * sector or superblock as a wolfBoot image header. */
START_TEST(test_fs_filesystem_without_boot_file_fails_closed)
{
    struct boot_slot s;
    uint8_t buf[16];

    reset_mocks();
    open_disk();

    ck_assert_int_eq(slot_prepare(&s, 0, NULL, NULL, 1U << 20), -1);
    ck_assert_int_eq(s.vol.type, FS_TYPE_FAT32);
    ck_assert_int_eq(s.ready, 0);
    ck_assert_int_eq(slot_read(&s, 0, sizeof(buf), buf), -1);

    ck_assert_int_eq(slot_prepare(&s, 1, NULL, NULL, 1U << 20), -1);
    ck_assert_int_eq(s.vol.type, FS_TYPE_EXT4);
    ck_assert_int_eq(s.ready, 0);
    ck_assert_int_eq(slot_read(&s, 0, sizeof(buf), buf), -1);
}
END_TEST

/* A partition with no filesystem and no boot file is the pre-existing raw
 * case, and must still work. */
START_TEST(test_fs_raw_slot_without_boot_file_still_reads)
{
    struct boot_slot s;
    uint8_t buf[8];

    reset_mocks();
    open_disk();
    to_media(media_b, image_b);
    memset(disk + P2_OFF, 0, (size_t)P2_SECS * SEC);
    memcpy(disk + P2_OFF, media_b, sizeof(buf));

    ck_assert_int_eq(slot_prepare(&s, 2, NULL, NULL, 1U << 20), 0);
    ck_assert_int_eq(s.vol.type, FS_TYPE_RAW);
    ck_assert_int_eq(s.ready, 1);
    ck_assert_int_eq(slot_read(&s, 0, sizeof(buf), buf), (int)sizeof(buf));
    ck_assert_int_eq(memcmp(buf, media_b, sizeof(buf)), 0);
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-update-disk-fs");
    TCase *tc = tcase_create("update-disk-fs");
    TCase *tc_slot = tcase_create("update-disk-fs-slot");

    /* The FAT32 volume alone is 32 MB, rebuilt for every test. */
    tcase_set_timeout(tc, 60);

    tcase_add_test(tc, test_fs_boots_higher_version_across_filesystems);
    tcase_add_test(tc, test_fs_boots_from_fat32_when_it_is_newer);
    tcase_add_test(tc, test_fs_falls_back_when_file_is_missing);
    tcase_add_test(tc, test_fs_rejects_truncated_file);
    tcase_add_test(tc, test_fs_panics_when_no_slot_is_usable);
    tcase_add_test(tc, test_fs_unsupported_filesystem_fails_the_slot);
    tcase_add_test(tc, test_fs_raw_partition_still_works);
    tcase_add_test(tc, test_fs_respects_ramboot_max_size);
    tcase_add_test(tc, test_fs_rejects_oversized_file_at_open);
    tcase_add_test(tc, test_fs_reads_fragmented_fat32_file);
    tcase_add_test(tc, test_fs_panics_when_every_file_is_oversized);
    suite_add_tcase(s, tc);

    tcase_set_timeout(tc_slot, 60);
    tcase_add_test(tc_slot, test_fs_label_selects_partition_by_volume_label);
    tcase_add_test(tc_slot, test_fs_label_case_rules_match_each_filesystem);
    tcase_add_test(tc_slot, test_fs_label_no_match_fails_the_slot);
    tcase_add_test(tc_slot, test_fs_gpt_name_wins_over_volume_label);
    tcase_add_test(tc_slot, test_fs_filesystem_without_boot_file_fails_closed);
    tcase_add_test(tc_slot, test_fs_raw_slot_without_boot_file_still_reads);
    suite_add_tcase(s, tc_slot);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
