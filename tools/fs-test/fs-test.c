/* fs-test.c
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

/* Host-side driver for wolfBoot's read-only FAT32 / ext4 layer.
 *
 * Links the REAL parsers (src/gpt.c, src/disk.c, src/disk_fs.c, src/fat32.c,
 * src/ext4.c) against a file-backed block device, so any disk image -- or a
 * real read-only block device -- can be exercised exactly as the bootloader
 * would read it. Nothing here is mocked except the block layer itself.
 *
 * This complements the unit tests rather than duplicating them: the unit
 * tests pin behaviour against small hand-built and mkfs-built fixtures, and
 * this tool lets you point the same code at arbitrary real-world volumes
 * (large, fragmented, deep, sparse) without adding cases to the suite.
 *
 * Usage:
 *   fs-test info   <image>
 *   fs-test probe  <image> <part>
 *   fs-test cat    <image> <part> <path> [outfile]
 *   fs-test verify <image> <part> <path> <reference-file>
 *   fs-test bench  <image> <part> <path> [iterations]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The parsers under test, compiled as one translation unit exactly as the
 * unit tests do. */
#include "gpt.c"
#include "disk.c"
#include "disk_fs.c"
#ifdef WOLFBOOT_FAT32
#include "fat32.c"
#endif
#ifdef WOLFBOOT_EXT4
#include "ext4.c"
#endif

/* ---------------------------------------------------------------------
 * File-backed block device, plus the counters the benchmark reports.
 * --------------------------------------------------------------------- */

static FILE *g_img;
static uint64_t g_img_sz;

static uint64_t stat_reads;      /* disk_read() calls */
static uint64_t stat_bytes;      /* bytes those calls moved */

static void stats_reset(void)
{
    stat_reads = 0;
    stat_bytes = 0;
}

int disk_init(int drv)
{
    (void)drv;
    return (g_img != NULL) ? 0 : -1;
}

/* Returns 0 on success and -1 on failure, matching what the real HALs do
 * (hal/x86_fsp_tgl.c, src/sdhci.c). This matters more than it looks:
 * disk_part_read() maps a 0 return to "the full requested size was read", so
 * anything that returns a byte count here would report a SHORT read as a
 * full one. A read that runs past the end of the image is an error rather
 * than a clamp, for the same reason. */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    size_t n;

    (void)drv;
    if (g_img == NULL) {
        return -1;
    }
    if (count == 0U) {
        return 0;
    }
    /* Must lie wholly within the image; a partial tail is a failure. */
    if ((start >= g_img_sz) || ((uint64_t)count > (g_img_sz - start))) {
        return -1;
    }
    if (fseeko(g_img, (off_t)start, SEEK_SET) != 0) {
        return -1;
    }
    n = fread(buf, 1, (size_t)count, g_img);
    stat_reads++;
    stat_bytes += (uint64_t)n;
    if (n != (size_t)count) {
        return -1;
    }
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    /* Read-only by design: this tool must never be able to damage an image. */
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

void disk_close(int drv)
{
    (void)drv;
}

static int img_open(const char *path)
{
    g_img = fopen(path, "rb");
    if (g_img == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    if (fseeko(g_img, 0, SEEK_END) != 0) {
        return -1;
    }
    g_img_sz = (uint64_t)ftello(g_img);
    return 0;
}

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1e9);
}

/* ---------------------------------------------------------------------
 * Subcommands
 * --------------------------------------------------------------------- */

/* Bypass the MBR/GPT layer and declare one partition directly.
 *
 * Set FSTEST_PART_OFF and FSTEST_PART_SZ (bytes) to point the filesystem
 * parsers at a region of the image without going through disk_open(). That
 * isolates this layer from the partition-table code, which matters on
 * big-endian hosts: src/disk.c and src/gpt.c read packed on-disk structs
 * natively and so are little-endian only, while the filesystem parsers use
 * byte-wise accessors and are endian-neutral. */
static int part_override(void)
{
    const char *off_s = getenv("FSTEST_PART_OFF");
    const char *sz_s = getenv("FSTEST_PART_SZ");
    uint64_t off, sz;

    if ((off_s == NULL) || (sz_s == NULL)) {
        return 0;
    }
    off = strtoull(off_s, NULL, 0);
    sz = strtoull(sz_s, NULL, 0);

    memset(Drives, 0, sizeof(Drives));
    Drives[0].drv = 0;
    Drives[0].is_open = 1;
    Drives[0].n_parts = 1;
    Drives[0].part[0].drv = 0;
    Drives[0].part[0].part_no = 0;
    Drives[0].part[0].start = off;
    /* end is the LAST valid byte, inclusive: disk_part_size() computes
     * (end - start) + 1, so off + sz would be one byte too many. */
    Drives[0].part[0].end = (off + sz) - 1U;
    return 1;
}


static int cmd_info(void)
{
    uint64_t sz;
    int n, i;

    if (part_override() == 0 && disk_open(0) < 0) {
        fprintf(stderr, "no usable partition table\n");
        return 1;
    }
    n = disk_part_count(0);
    printf("partitions: %d\n", n);
    for (i = 0; i < n; i++) {
        sz = 0;
        if (disk_part_size(0, i, &sz) != 0) {
            continue;
        }
        printf("  p%-2d size %10llu bytes (%llu MiB)\n", i,
            (unsigned long long)sz, (unsigned long long)(sz / (1024U * 1024U)));
    }
    return 0;
}

static int mount_part(struct fs_volume *vol, int part)
{
    int ret;

    if (part_override() == 0 && disk_open(0) < 0) {
        fprintf(stderr, "no usable partition table\n");
        return -1;
    }
    ret = fs_mount(vol, 0, part);
    if (ret != WOLFBOOT_FS_OK) {
        fprintf(stderr, "fs_mount(p%d) failed: %d\n", part, ret);
        return -1;
    }
    return 0;
}

static int cmd_probe(int part)
{
    struct fs_volume vol;

    if (mount_part(&vol, part) != 0) {
        return 1;
    }
    printf("p%d: %s\n", part, fs_type_name(&vol));
    printf("  partition size : %llu bytes\n",
        (unsigned long long)vol.part_sz);
#ifdef WOLFBOOT_FAT32
    if (vol.type == FS_TYPE_FAT32) {
        printf("  cluster size   : %u bytes\n", vol.u.fat.clus_sz);
        printf("  clusters       : %u\n", vol.u.fat.clus_count);
        printf("  data offset    : %llu\n",
            (unsigned long long)vol.u.fat.data_off);
    }
#endif
#ifdef WOLFBOOT_EXT4
    if (vol.type == FS_TYPE_EXT4) {
        printf("  block size     : %u bytes\n", vol.u.ext.block_sz);
        printf("  blocks         : %u\n", vol.u.ext.blocks_count);
        printf("  groups         : %u\n", vol.u.ext.groups);
        printf("  inode size     : %u\n", (unsigned)vol.u.ext.inode_sz);
        printf("  desc size      : %u\n", (unsigned)vol.u.ext.desc_sz);
    }
#endif
    return 0;
}

/* Read a whole file through the parser into a freshly allocated buffer. */
static uint8_t *read_all(struct fs_volume *vol, const char *path,
                         uint64_t *out_len)
{
    struct fs_file f;
    uint8_t *buf;
    uint64_t sz, done;
    int r;

    r = fs_open(vol, &f, path, (uint64_t)-1);
    if (r != WOLFBOOT_FS_OK) {
        fprintf(stderr, "fs_open(%s) failed: %d\n", path, r);
        return NULL;
    }
    sz = fs_size(&f);
    buf = (uint8_t *)malloc((size_t)(sz ? sz : 1U));
    if (buf == NULL) {
        return NULL;
    }
    done = 0;
    while (done < sz) {
        r = fs_read(&f, done, sz - done, buf + done);
        if (r < 0) {
            fprintf(stderr, "fs_read at %llu failed: %d\n",
                (unsigned long long)done, r);
            free(buf);
            return NULL;
        }
        if (r == 0) {
            break;
        }
        done += (uint64_t)r;
    }
    if (done != sz) {
        fprintf(stderr, "short read: %llu of %llu\n",
            (unsigned long long)done, (unsigned long long)sz);
        free(buf);
        return NULL;
    }
    *out_len = sz;
    return buf;
}

static int cmd_cat(int part, const char *path, const char *outfile)
{
    struct fs_volume vol;
    uint8_t *buf;
    uint64_t len = 0;
    FILE *out;

    if (mount_part(&vol, part) != 0) {
        return 1;
    }
    buf = read_all(&vol, path, &len);
    if (buf == NULL) {
        return 1;
    }
    if (outfile != NULL) {
        out = fopen(outfile, "wb");
        if (out == NULL) {
            fprintf(stderr, "cannot write %s\n", outfile);
            free(buf);
            return 1;
        }
        fwrite(buf, 1, (size_t)len, out);
        fclose(out);
        printf("wrote %llu bytes to %s\n", (unsigned long long)len, outfile);
    }
    else {
        fwrite(buf, 1, (size_t)len, stdout);
    }
    free(buf);
    return 0;
}

static int cmd_verify(int part, const char *path, const char *ref)
{
    struct fs_volume vol;
    uint8_t *buf, *rbuf;
    uint64_t len = 0, rlen;
    off_t roff;
    FILE *rf;
    size_t got;

    if (mount_part(&vol, part) != 0) {
        return 1;
    }
    buf = read_all(&vol, path, &len);
    if (buf == NULL) {
        return 1;
    }
    rf = fopen(ref, "rb");
    if (rf == NULL) {
        fprintf(stderr, "cannot open reference %s\n", ref);
        free(buf);
        return 1;
    }
    /* An unseekable reference (a pipe, say) makes ftello return -1, which
     * as an unsigned length would ask malloc for SIZE_MAX and then hand
     * fread a NULL buffer. Check both before using either. */
    if ((fseeko(rf, 0, SEEK_END) != 0) || ((roff = ftello(rf)) < 0)) {
        fprintf(stderr, "cannot size reference %s\n", ref);
        fclose(rf);
        free(buf);
        return 1;
    }
    rlen = (uint64_t)roff;
    fseeko(rf, 0, SEEK_SET);
    rbuf = (uint8_t *)malloc((size_t)(rlen ? rlen : 1U));
    if (rbuf == NULL) {
        fprintf(stderr, "out of memory reading reference %s\n", ref);
        fclose(rf);
        free(buf);
        return 1;
    }
    got = fread(rbuf, 1, (size_t)rlen, rf);
    fclose(rf);

    if ((uint64_t)got != rlen) {
        printf("FAIL: could not read reference\n");
        free(buf); free(rbuf);
        return 1;
    }
    if (len != rlen) {
        printf("FAIL: size %llu, reference %llu\n",
            (unsigned long long)len, (unsigned long long)rlen);
        free(buf); free(rbuf);
        return 1;
    }
    if (memcmp(buf, rbuf, (size_t)len) != 0) {
        printf("FAIL: content differs\n");
        free(buf); free(rbuf);
        return 1;
    }
    printf("OK: %llu bytes identical to %s\n", (unsigned long long)len, ref);
    free(buf); free(rbuf);
    return 0;
}

static int cmd_bench(int part, const char *path, int iters)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t *buf;
    uint64_t sz, done, total_bytes = 0;
    double t0, t1, open_t = 0.0, read_t = 0.0;
    uint64_t open_reads = 0, read_reads = 0, meta_bytes = 0;
    int i, r;

    if (mount_part(&vol, part) != 0) {
        return 1;
    }
    /* Size the buffer once, outside the timed region. */
    r = fs_open(&vol, &f, path, (uint64_t)-1);
    if (r != WOLFBOOT_FS_OK) {
        fprintf(stderr, "fs_open(%s) failed: %d\n", path, r);
        return 1;
    }
    sz = fs_size(&f);
    buf = (uint8_t *)malloc((size_t)(sz ? sz : 1U));
    if (buf == NULL) {
        return 1;
    }

    for (i = 0; i < iters; i++) {
        /* Drop the metadata cache so each iteration is a cold boot. */
        fs_cache_invalidate();

        /* open: path resolution + full chain/extent validation */
        stats_reset();
        t0 = now_sec();
        r = fs_open(&vol, &f, path, (uint64_t)-1);
        t1 = now_sec();
        if (r != WOLFBOOT_FS_OK) {
            fprintf(stderr, "fs_open failed: %d\n", r);
            free(buf);
            return 1;
        }
        open_t += (t1 - t0);
        open_reads += stat_reads;
        meta_bytes += stat_bytes;

        /* read: the payload itself */
        stats_reset();
        t0 = now_sec();
        done = 0;
        while (done < sz) {
            r = fs_read(&f, done, sz - done, buf + done);
            if (r <= 0) {
                break;
            }
            done += (uint64_t)r;
        }
        t1 = now_sec();
        read_t += (t1 - t0);
        read_reads += stat_reads;
        total_bytes += done;
    }

    printf("file            : %s (p%d, %s)\n", path, part,
        fs_type_name(&vol));
    printf("size            : %llu bytes\n", (unsigned long long)sz);
    printf("iterations      : %d\n", iters);
    printf("open  time      : %.3f ms/iter\n", (open_t / iters) * 1000.0);
    printf("read  time      : %.3f ms/iter\n", (read_t / iters) * 1000.0);
    printf("open  disk reads: %.1f /iter (%.0f metadata bytes)\n",
        (double)open_reads / iters, (double)meta_bytes / iters);
    printf("read  disk reads: %.1f /iter\n", (double)read_reads / iters);
    if (read_t > 0.0) {
        printf("throughput      : %.1f MiB/s\n",
            ((double)total_bytes / (1024.0 * 1024.0)) / read_t);
    }
    /* Read amplification: how many media reads it costs to deliver the
     * payload, versus the one contiguous read a raw partition would need. */
    printf("read amplification: %.2fx vs a single raw read\n",
        (double)read_reads / (double)(iters > 0 ? iters : 1));
    free(buf);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  fs-test info   <image>\n"
        "  fs-test probe  <image> <part>\n"
        "  fs-test cat    <image> <part> <path> [outfile]\n"
        "  fs-test verify <image> <part> <path> <reference-file>\n"
        "  fs-test bench  <image> <part> <path> [iterations]\n");
}

int main(int argc, char **argv)
{
    int part, iters;

    if (argc < 3) {
        usage();
        return 2;
    }
    if (img_open(argv[2]) != 0) {
        return 2;
    }

    if (strcmp(argv[1], "info") == 0) {
        return cmd_info();
    }
    if (argc < 4) {
        usage();
        return 2;
    }
    part = atoi(argv[3]);

    if (strcmp(argv[1], "probe") == 0) {
        return cmd_probe(part);
    }
    if ((strcmp(argv[1], "cat") == 0) && (argc >= 5)) {
        return cmd_cat(part, argv[4], (argc >= 6) ? argv[5] : NULL);
    }
    if ((strcmp(argv[1], "verify") == 0) && (argc >= 6)) {
        return cmd_verify(part, argv[4], argv[5]);
    }
    if ((strcmp(argv[1], "bench") == 0) && (argc >= 5)) {
        iters = (argc >= 6) ? atoi(argv[5]) : 20;
        if (iters < 1) {
            iters = 1;
        }
        return cmd_bench(part, argv[4], iters);
    }
    usage();
    return 2;
}
