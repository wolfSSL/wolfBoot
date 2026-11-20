/* lilo.c
 *
 * Installs the x86_64_lilo wolfBoot target on a legacy MBR disk, driven by
 * a LILO configuration file (/etc/lilo.conf).
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

/* Like the original lilo(8), this reads the configuration, checks every
 * image it names, writes a boot map and installs the boot loader in the
 * MBR. Unlike it, the map names files on a FAT32 partition rather than
 * listing their sectors: wolfBoot reads FAT32 itself, so a kernel can be
 * replaced without re-running lilo. Re-run it when lilo.conf changes.
 *
 * The images are checked with wolfBoot's own partition and FAT32 code
 * (src/disk.c, src/disk_fs.c, src/fat32.c), so what lilo accepts is what
 * the boot loader will find. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "gpt.c"
#include "disk.c"
#include "disk_fs.c"
#include "fat32.c"

#include "lilo_map.h"

#define LILO_VERSION        "wolfBoot lilo 1.0"
#define DEFAULT_CONFIG      "/etc/lilo.conf"
#define DEFAULT_INSTALL     "/boot/wolfboot.bin"
#define DEFAULT_FAT_MOUNT   "/boot"

/* wolfBoot image header (include/wolfboot/wolfboot.h) */
#define WB_MAGIC            0x464C4F57UL /* "WOLF" */
#define WB_HDR_SIZE         256
#define WB_HDR_VERSION      0x01
#define WB_HDR_PADDING      0xFF

#define LINUX_HDR_MAGIC     0x53726448UL /* "HdrS" */

#define MAX_TOKEN           1024

struct image_cfg {
    int     type;
    char    label[LILO_LABEL_LEN];
    char    alias[LILO_LABEL_LEN];
    char    path[LILO_PATH_LEN];        /* image= / other= as written */
    char    initrd[LILO_PATH_LEN];
    char   *append;
    char   *addappend;
    char   *literal;
    char   *root;
    int     ro;                         /* 1 ro, 0 rw, -1 unset */
    int     vga;                        /* -1 unset */
    int     optional;
};

struct global_cfg {
    char   *boot;
    char   *install;
    char   *defaultl;
    char   *append;
    char   *addappend;
    char   *root;
    int     ro;
    int     vga;
    int     prompt;
    int     timeout;                    /* -1 unset */
    int     delay;
    int     serial_port;                /* -1 unset */
    uint32_t serial_baud;
    int     fat_partition;              /* MBR slot 1..4, 0 = first FAT32 */
    char   *fat_mount;
    int     verbose;
};

static struct global_cfg g = {
    .ro = -1, .vga = -1, .timeout = -1, .serial_port = -1,
};
static struct image_cfg images[LILO_MAP_MAX_ENTRIES];
static int n_images;
static int verbose;

static int disk_fd = -1;
static uint64_t disk_size;

/* ------------------------------------------------------------------ */

static void die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "Fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void warn(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "Warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static char *xstrdup(const char *s)
{
    char *d = strdup(s);

    if (d == NULL)
        die("out of memory");
    return d;
}

static void copy_str(char *dst, size_t size, const char *src, const char *what)
{
    if (strlen(src) >= size)
        die("%s too long (max %zu characters): %s", what, size - 1, src);
    strcpy(dst, src);
}

/* ---------------------------------------------------------------------
 * Disk access for the wolfBoot partition/FAT32 code
 * --------------------------------------------------------------------- */

int disk_init(int drv)
{
    return (drv == 0 && disk_fd >= 0) ? 0 : -1;
}

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    ssize_t n;

    if (drv != 0 || disk_fd < 0)
        return -1;
    if (start >= disk_size || (uint64_t)count > disk_size - start)
        return -1;
    while (count > 0) {
        n = pread(disk_fd, buf, count, (off_t)start);
        if (n <= 0)
            return -1;
        buf += n;
        start += (uint64_t)n;
        count -= (uint32_t)n;
    }
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv;
    (void)start;
    (void)count;
    (void)buf;
    return -1;
}

void disk_close(int drv)
{
    (void)drv;
}

static void disk_write_exact(uint64_t lba, const uint8_t *buf, size_t len)
{
    ssize_t n;
    off_t off = (off_t)(lba * LILO_SECTOR_SIZE);

    while (len > 0) {
        n = pwrite(disk_fd, buf, len, off);
        if (n <= 0)
            die("write to boot device failed: %s", strerror(errno));
        buf += n;
        off += n;
        len -= (size_t)n;
    }
}

static void disk_attach(const char *path, int writable)
{
    struct stat st;
    off_t end;

    disk_fd = open(path, writable ? O_RDWR : O_RDONLY);
    if (disk_fd < 0)
        die("open %s: %s", path, strerror(errno));
    if (fstat(disk_fd, &st) != 0)
        die("stat %s: %s", path, strerror(errno));
    end = lseek(disk_fd, 0, SEEK_END);
    if (end <= 0)
        die("%s: cannot determine size", path);
    disk_size = (uint64_t)end;
}

/* ---------------------------------------------------------------------
 * Configuration parsing (lilo.conf(5) syntax)
 * --------------------------------------------------------------------- */

static FILE *cfg_fp;
static const char *cfg_name;
static int cfg_line = 1;
static int cfg_peek = -2;

static int cfg_getc(void)
{
    int c;

    if (cfg_peek != -2) {
        c = cfg_peek;
        cfg_peek = -2;
        return c;
    }
    c = fgetc(cfg_fp);
    if (c == '\n')
        cfg_line++;
    return c;
}

static void cfg_ungetc(int c)
{
    cfg_peek = c;
}

static void cfg_error(const char *msg, const char *tok)
{
    die("%s near line %d: %s%s%s", cfg_name, cfg_line, msg,
        tok ? " " : "", tok ? tok : "");
}

/* Returns 0 at end of file, '=' for an equals sign, 'w' for a word. */
static int cfg_token(char *buf)
{
    int c, n = 0;

    for (;;) {
        c = cfg_getc();
        if (c == '#') {
            while (c != EOF && c != '\n')
                c = cfg_getc();
        }
        if (c == EOF)
            return 0;
        if (c == '\\') {
            int d = cfg_getc();
            if (d == '\n')
                continue;
            cfg_ungetc(d);
            break;
        }
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
    }
    if (c == '=')
        return '=';
    if (c == '"') {
        for (;;) {
            c = cfg_getc();
            if (c == EOF)
                cfg_error("unterminated string", NULL);
            if (c == '"')
                break;
            if (c == '\\') {
                c = cfg_getc();
                if (c == '\n')
                    continue;
                if (c == EOF)
                    cfg_error("unterminated string", NULL);
            }
            if (n >= MAX_TOKEN - 1)
                cfg_error("string too long", NULL);
            buf[n++] = (char)c;
        }
        buf[n] = '\0';
        return 'w';
    }
    for (;;) {
        if (c == '\\') {
            c = cfg_getc();
            if (c == EOF)
                break;
        }
        if (n >= MAX_TOKEN - 1)
            cfg_error("word too long", NULL);
        buf[n++] = (char)c;
        c = cfg_getc();
        if (c == EOF || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '=' || c == '#') {
            cfg_ungetc(c);
            break;
        }
    }
    buf[n] = '\0';
    return 'w';
}

static int parse_int(const char *v, const char *name)
{
    char *end;
    long n;

    errno = 0;
    n = strtol(v, &end, 0);
    if (errno != 0 || *end != '\0' || n < 0 || n > 0xFFFF)
        die("%s: invalid value '%s'", name, v);
    return (int)n;
}

static int parse_vga(const char *v)
{
    if (strcasecmp(v, "normal") == 0)
        return LILO_VGA_NORMAL;
    if (strcasecmp(v, "extended") == 0 || strcasecmp(v, "ext") == 0)
        return LILO_VGA_EXTENDED;
    if (strcasecmp(v, "ask") == 0)
        return LILO_VGA_ASK;
    return parse_int(v, "vga");
}

/* serial=<port>[,<bps>[<parity>[<bits>]]], as in lilo.conf(5) */
static void parse_serial(const char *v)
{
    char *end;
    long port, bps = 9600;

    port = strtol(v, &end, 10);
    if (end == v || port < 0 || port > 3)
        die("serial: port must be 0..3: %s", v);
    if (*end == ',') {
        const char *s = end + 1;
        bps = strtol(s, &end, 10);
        if (end == s || (115200 % bps) != 0)
            die("serial: unsupported speed: %s", v);
        if (*end == 'n' || *end == 'N') {
            end++;
            if (*end != '\0' && *end != '8')
                die("serial: only 8 data bits without parity are supported");
        }
        else if (*end != '\0') {
            die("serial: only no parity, 8 data bits is supported: %s", v);
        }
    }
    else if (*end != '\0') {
        die("serial: invalid value: %s", v);
    }
    g.serial_port = (int)port;
    g.serial_baud = (uint32_t)bps;
}

static const char *const unsupported[] = {
    "password", "restricted", "mandatory", "bypass", "single-key",
    "lock", "fallback", "keytable", "ramdisk", "map-drive", "change",
    "master-boot", "raid-extra-boot", "bitmap", "menu-title",
    "menu-scheme", "loader", NULL
};

static const char *const ignored[] = {
    "map", "lba32", "linear", "compact", "large-memory", "small-memory",
    "nowarn", "backup", "force-backup", "boot-as", "disk", "bios",
    "sectors", "heads", "cylinders", "geometric", "static-bios-codes",
    "unattended", "noraid", "suppress-boot-time-BIOSes", "table",
    "unsafe", "message", "pause", "nodevcheck", "ignore-table",
    "fix-table", "nokbdefault", NULL
};

static int in_list(const char *const *list, const char *s)
{
    for (; *list != NULL; list++) {
        if (strcmp(*list, s) == 0)
            return 1;
    }
    return 0;
}

static struct image_cfg *new_image(int type, const char *path)
{
    struct image_cfg *im;

    if (n_images >= LILO_MAP_MAX_ENTRIES)
        die("too many images (max %d)", LILO_MAP_MAX_ENTRIES);
    im = &images[n_images++];
    memset(im, 0, sizeof(*im));
    im->type = type;
    im->ro = -1;
    im->vga = -1;
    copy_str(im->path, sizeof(im->path), path, "path");
    return im;
}

static void cfg_set(struct image_cfg *im, const char *key, const char *val)
{
    int needs = val != NULL;

    if (in_list(unsupported, key))
        die("'%s' is not supported by the wolfBoot boot loader", key);
    if (in_list(ignored, key)) {
        if (verbose)
            printf("Ignoring '%s'\n", key);
        return;
    }

    if (strcmp(key, "image") == 0 || strcmp(key, "other") == 0) {
        if (!needs)
            cfg_error("missing value for", key);
        new_image(key[0] == 'i' ? LILO_ENTRY_LINUX : LILO_ENTRY_OTHER, val);
        return;
    }
    if (strcmp(key, "read-only") == 0 || strcmp(key, "read-write") == 0 ||
            strcmp(key, "prompt") == 0 || strcmp(key, "optional") == 0) {
        if (needs)
            cfg_error("unexpected value for", key);
    }
    else if (!needs) {
        cfg_error("missing value for", key);
    }

    if (im != NULL) {
        if (strcmp(key, "label") == 0)
            copy_str(im->label, sizeof(im->label), val, "label");
        else if (strcmp(key, "alias") == 0)
            copy_str(im->alias, sizeof(im->alias), val, "alias");
        else if (strcmp(key, "append") == 0)
            im->append = xstrdup(val);
        else if (strcmp(key, "addappend") == 0)
            im->addappend = xstrdup(val);
        else if (strcmp(key, "literal") == 0)
            im->literal = xstrdup(val);
        else if (strcmp(key, "root") == 0)
            im->root = xstrdup(val);
        else if (strcmp(key, "initrd") == 0)
            copy_str(im->initrd, sizeof(im->initrd), val, "initrd");
        else if (strcmp(key, "read-only") == 0)
            im->ro = 1;
        else if (strcmp(key, "read-write") == 0)
            im->ro = 0;
        else if (strcmp(key, "vga") == 0)
            im->vga = parse_vga(val);
        else if (strcmp(key, "optional") == 0)
            im->optional = 1;
        else
            cfg_error("unknown per-image option", key);
        return;
    }

    if (strcmp(key, "boot") == 0)
        g.boot = xstrdup(val);
    else if (strcmp(key, "install") == 0)
        g.install = xstrdup(val);
    else if (strcmp(key, "default") == 0)
        g.defaultl = xstrdup(val);
    else if (strcmp(key, "append") == 0)
        g.append = xstrdup(val);
    else if (strcmp(key, "addappend") == 0)
        g.addappend = xstrdup(val);
    else if (strcmp(key, "root") == 0)
        g.root = xstrdup(val);
    else if (strcmp(key, "read-only") == 0)
        g.ro = 1;
    else if (strcmp(key, "read-write") == 0)
        g.ro = 0;
    else if (strcmp(key, "vga") == 0)
        g.vga = parse_vga(val);
    else if (strcmp(key, "prompt") == 0)
        g.prompt = 1;
    else if (strcmp(key, "timeout") == 0)
        g.timeout = parse_int(val, key);
    else if (strcmp(key, "delay") == 0)
        g.delay = parse_int(val, key);
    else if (strcmp(key, "serial") == 0)
        parse_serial(val);
    else if (strcmp(key, "verbose") == 0)
        g.verbose = parse_int(val, key);
    else if (strcmp(key, "fat-partition") == 0)
        g.fat_partition = parse_int(val, key);
    else if (strcmp(key, "fat-mount") == 0)
        g.fat_mount = xstrdup(val);
    else
        cfg_error("unknown option", key);
}

static void parse_config(const char *path)
{
    static char key[MAX_TOKEN], val[MAX_TOKEN];
    int t;

    cfg_fp = fopen(path, "r");
    if (cfg_fp == NULL)
        die("open %s: %s", path, strerror(errno));
    cfg_name = path;
    t = cfg_token(key);
    while (t != 0) {
        struct image_cfg *im = n_images ? &images[n_images - 1] : NULL;
        if (t != 'w')
            cfg_error("syntax error", NULL);
        t = cfg_token(val);
        if (t == '=') {
            if (cfg_token(val) != 'w')
                cfg_error("missing value for", key);
            cfg_set(im, key, val);
            t = cfg_token(key);
        }
        else {
            cfg_set(im, key, NULL);
            if (t == 'w')
                strcpy(key, val);
        }
    }
    fclose(cfg_fp);
}

/* ---------------------------------------------------------------------
 * Image checks, on the FAT32 partition, with wolfBoot's own parser
 * --------------------------------------------------------------------- */

static int fat_part = -1;               /* src/disk.c partition index */
static struct fs_volume fat_vol;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* src/disk.c index of the partition in MBR slot 1..4, or -1. */
static int slot_to_part(const uint8_t *mbr, int slot)
{
    const uint8_t *pte = mbr + LILO_MBR_PTABLE_OFFSET + 16 * (slot - 1);
    uint64_t start = (uint64_t)le32(pte + 8) * LILO_SECTOR_SIZE;
    int i;

    if (pte[4] == 0 || le32(pte + 8) == 0)
        return -1;
    for (i = 0; i < Drives[0].n_parts; i++) {
        if (Drives[0].part[i].start == start)
            return i;
    }
    return -1;
}

static void find_fat_partition(const uint8_t *mbr)
{
    int i, n;

    n = disk_open(0);
    if (n <= 0)
        die("no partitions found on the boot device");
    if (g.fat_partition != 0) {
        if (g.fat_partition < 1 || g.fat_partition > 4)
            die("fat-partition must be an MBR slot, 1..4");
        fat_part = slot_to_part(mbr, g.fat_partition);
        if (fat_part < 0)
            die("partition %d is empty", g.fat_partition);
        if (fs_mount(&fat_vol, 0, fat_part) != WOLFBOOT_FS_OK ||
                fat_vol.type != FS_TYPE_FAT32)
            die("partition %d does not hold a FAT32 filesystem",
                g.fat_partition);
        return;
    }
    for (i = 0; i < n; i++) {
        if (fs_mount(&fat_vol, 0, i) == WOLFBOOT_FS_OK &&
                fat_vol.type == FS_TYPE_FAT32) {
            fat_part = i;
            return;
        }
    }
    die("no FAT32 partition found on the boot device");
}

/* Host path (as in lilo.conf) to a path on the FAT32 volume. */
static void fat_path(char *out, size_t size, const char *path)
{
    const char *mnt = g.fat_mount ? g.fat_mount : DEFAULT_FAT_MOUNT;
    size_t ml = strlen(mnt);

    while (ml > 1 && mnt[ml - 1] == '/')
        ml--;
    if (ml > 1 && strncmp(path, mnt, ml) == 0 && path[ml] == '/')
        path += ml;
    if (path[0] != '/')
        die("%s: path must be absolute", path);
    copy_str(out, size, path, "path");
}

static uint32_t wb_version(const uint8_t *hdr)
{
    uint32_t off = 8;

    while (off + 4 <= WB_HDR_SIZE) {
        uint16_t type, len;
        if (hdr[off] == WB_HDR_PADDING || (off & 1)) {
            off++;
            continue;
        }
        type = le16(hdr + off);
        len = le16(hdr + off + 2);
        if (type == 0)
            break;
        if (off + 4 + len > WB_HDR_SIZE)
            break;
        if (type == WB_HDR_VERSION && len == 4)
            return le32(hdr + off + 4);
        off += 4 + len;
    }
    return 0;
}

/* Returns 0 if the image is usable, -1 if it is missing. */
static int check_kernel(const struct image_cfg *im, char *fpath)
{
    struct fs_file f;
    uint8_t buf[WB_HDR_SIZE + 0x270];
    uint64_t sz;
    uint32_t fw;

    fat_path(fpath, LILO_PATH_LEN, im->path);
    if (fs_open(&fat_vol, &f, fpath, UINT32_MAX) != WOLFBOOT_FS_OK)
        return -1;
    sz = fs_size(&f);
    if (sz < sizeof(buf) ||
            fs_read(&f, 0, sizeof(buf), buf) != (int)sizeof(buf))
        die("%s: too small to be a kernel image", im->path);
    if (le32(buf) != WB_MAGIC)
        die("%s: not a wolfBoot image (sign it with tools/keytools/sign)",
            im->path);
    fw = le32(buf + 4);
    if ((uint64_t)fw + WB_HDR_SIZE > sz)
        die("%s: truncated image", im->path);
    if (le32(buf + WB_HDR_SIZE + 0x202) != LINUX_HDR_MAGIC)
        die("%s: payload is not a Linux bzImage", im->path);
    if (le16(buf + WB_HDR_SIZE + 0x206) < 0x0204 ||
            (buf[WB_HDR_SIZE + 0x211] & 0x01) == 0)
        die("%s: kernel too old (needs boot protocol 2.04, bzImage)",
            im->path);
    if (verbose)
        printf("  %s: wolfBoot image v%u, %u bytes, boot protocol %u.%02u\n",
            fpath, wb_version(buf), fw, buf[WB_HDR_SIZE + 0x207],
            buf[WB_HDR_SIZE + 0x206]);
    return 0;
}

static int check_initrd(const char *path, char *fpath)
{
    struct fs_file f;

    fat_path(fpath, LILO_PATH_LEN, path);
    if (fs_open(&fat_vol, &f, fpath, UINT32_MAX) != WOLFBOOT_FS_OK)
        return -1;
    if (fs_size(&f) == 0)
        die("%s: empty initrd", path);
    if (verbose)
        printf("  %s: initrd, %llu bytes\n", fpath,
            (unsigned long long)fs_size(&f));
    return 0;
}

/* other=/dev/sda2 (or just "2"): the MBR slot is the trailing number. */
static int other_slot(const char *path)
{
    size_t l = strlen(path);
    long slot;

    while (l > 0 && path[l - 1] >= '0' && path[l - 1] <= '9')
        l--;
    slot = strtol(path + l, NULL, 10);
    if (path[l] == '\0' || slot < 1 || slot > 4)
        die("other=%s: only primary partitions 1..4 can be chain loaded",
            path);
    return (int)slot;
}

static void cmd_add(char *cmd, const char *s, const char *label)
{
    size_t l = strlen(cmd);

    if (s == NULL || *s == '\0')
        return;
    if (l + 1 + strlen(s) >= LILO_CMDLINE_LEN)
        die("%s: command line too long (max %d)", label,
            LILO_CMDLINE_LEN - 1);
    if (l > 0)
        cmd[l++] = ' ';
    strcpy(cmd + l, s);
}

static int build_entry(const struct image_cfg *im, struct lilo_map_entry *e,
                       const uint8_t *mbr)
{
    char rootarg[LILO_CMDLINE_LEN];
    const char *root;
    int ro;

    memset(e, 0, sizeof(*e));
    strcpy(e->label, im->label);
    strcpy(e->alias, im->alias);
    e->type = (uint8_t)im->type;

    if (im->type == LILO_ENTRY_OTHER) {
        e->part = (uint8_t)other_slot(im->path);
        if (slot_to_part(mbr, e->part) < 0)
            die("other=%s: partition %d is empty", im->path, e->part);
        return 0;
    }

    if (check_kernel(im, e->kernel) != 0) {
        if (im->optional) {
            warn("%s: not found, skipping optional image", im->path);
            return -1;
        }
        die("%s: not found on the FAT32 partition", im->path);
    }
    if (im->initrd[0] != '\0' && check_initrd(im->initrd, e->initrd) != 0) {
        if (im->optional) {
            warn("%s: not found, skipping optional image", im->initrd);
            return -1;
        }
        die("%s: not found on the FAT32 partition", im->initrd);
    }
    e->part = (uint8_t)fat_part;
    e->vga = (uint16_t)((im->vga >= 0) ? im->vga :
                        (g.vga >= 0) ? g.vga : LILO_VGA_DEFAULT);

    /* LILO's command line: BOOT_IMAGE, root, ro/rw, then append. */
    snprintf(e->cmdline, sizeof(e->cmdline), "BOOT_IMAGE=%s", im->label);
    if (im->literal != NULL) {
        cmd_add(e->cmdline, im->literal, im->label);
        return 0;
    }
    root = im->root ? im->root : g.root;
    if (root != NULL) {
        snprintf(rootarg, sizeof(rootarg), "root=%s", root);
        cmd_add(e->cmdline, rootarg, im->label);
    }
    ro = (im->ro >= 0) ? im->ro : g.ro;
    if (ro >= 0)
        cmd_add(e->cmdline, ro ? "ro" : "rw", im->label);
    cmd_add(e->cmdline, im->append ? im->append : g.append, im->label);
    cmd_add(e->cmdline, g.addappend, im->label);
    cmd_add(e->cmdline, im->addappend, im->label);
    return 0;
}

static void label_from_path(struct image_cfg *im)
{
    const char *base = strrchr(im->path, '/');

    base = base ? base + 1 : im->path;
    if (strlen(base) >= LILO_LABEL_LEN)
        die("%s: label too long, set label=", im->path);
    strcpy(im->label, base);
}

/* ---------------------------------------------------------------------
 * Map and MBR
 * --------------------------------------------------------------------- */

static void map_crc(uint8_t *map, uint32_t n)
{
    struct lilo_map_hdr *h = (struct lilo_map_hdr *)map;
    struct gpt_crc32_ctx crc;

    h->crc32 = 0;
    gpt_crc32_init(&crc);
    gpt_crc32_update(&crc, map, LILO_SECTOR_SIZE + n * LILO_ENTRY_SIZE);
    h->crc32 = gpt_crc32_final(&crc);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long sz;

    if (f == NULL)
        die("open %s: %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) <= 0)
        die("%s: cannot read", path);
    rewind(f);
    buf = malloc((size_t)sz);
    if (buf == NULL)
        die("out of memory");
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        die("%s: short read", path);
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

/* Lowest start LBA of any primary partition. */
static uint64_t first_part_lba(const uint8_t *mbr)
{
    uint64_t first = UINT64_MAX;
    int i;

    for (i = 0; i < 4; i++) {
        const uint8_t *pte = mbr + LILO_MBR_PTABLE_OFFSET + 16 * i;
        if (pte[4] == 0xEE)
            die("GPT disk: only legacy MBR partitioning is supported");
        if (pte[4] != 0 && le32(pte + 8) != 0 && le32(pte + 8) < first)
            first = le32(pte + 8);
    }
    if (first == UINT64_MAX)
        die("no partitions in the MBR");
    return first;
}

static void query_map(const char *dev)
{
    uint8_t mbr[LILO_SECTOR_SIZE];
    static uint8_t map[LILO_MAP_SECTORS * LILO_SECTOR_SIZE];
    struct lilo_mbr_desc d;
    struct lilo_map_hdr *h = (struct lilo_map_hdr *)map;
    struct lilo_map_entry *e = (struct lilo_map_entry *)(map + LILO_SECTOR_SIZE);
    struct gpt_crc32_ctx crc;
    uint32_t want, i;

    disk_attach(dev, 0);
    if (disk_read(0, 0, sizeof(mbr), mbr) != 0)
        die("%s: cannot read the MBR", dev);
    memcpy(&d, mbr + LILO_MBR_DESC_OFFSET, sizeof(d));
    if (d.magic != LILO_MBR_DESC_MAGIC || d.map_sectors == 0 ||
            d.map_sectors > LILO_MAP_SECTORS)
        die("%s: wolfBoot lilo is not installed", dev);
    if (disk_read(0, d.map_lba * LILO_SECTOR_SIZE,
            (uint32_t)d.map_sectors * LILO_SECTOR_SIZE, map) != 0)
        die("%s: cannot read the map", dev);
    if (h->magic != LILO_MAP_MAGIC || h->n_entries == 0 ||
            h->n_entries > LILO_MAP_MAX_ENTRIES ||
            LILO_SECTOR_SIZE + (uint32_t)h->n_entries * LILO_ENTRY_SIZE >
                (uint32_t)d.map_sectors * LILO_SECTOR_SIZE)
        die("%s: invalid map", dev);
    want = h->crc32;
    h->crc32 = 0;
    gpt_crc32_init(&crc);
    gpt_crc32_update(&crc, map, LILO_SECTOR_SIZE +
        h->n_entries * LILO_ENTRY_SIZE);
    if (gpt_crc32_final(&crc) != want)
        die("%s: map checksum mismatch", dev);

    for (i = 0; i < h->n_entries; i++) {
        e[i].label[LILO_LABEL_LEN - 1] = '\0';
        e[i].alias[LILO_LABEL_LEN - 1] = '\0';
        e[i].kernel[LILO_PATH_LEN - 1] = '\0';
        e[i].initrd[LILO_PATH_LEN - 1] = '\0';
        e[i].cmdline[LILO_CMDLINE_LEN - 1] = '\0';
        printf("%-16s%s\n", e[i].label, i == h->default_entry ? " *" : "");
        if (!verbose)
            continue;
        if (e[i].alias[0] != '\0')
            printf("    alias: %s\n", e[i].alias);
        if (e[i].type == LILO_ENTRY_OTHER) {
            printf("    other: partition %u\n", e[i].part);
            continue;
        }
        printf("    kernel: %s (partition index %u)\n", e[i].kernel,
            e[i].part);
        if (e[i].initrd[0] != '\0')
            printf("    initrd: %s\n", e[i].initrd);
        printf("    cmdline: %s\n", e[i].cmdline);
        if (e[i].vga != LILO_VGA_DEFAULT)
            printf("    vga: 0x%x\n", e[i].vga);
    }
    if (verbose) {
        printf("prompt: %s, timeout: ", (h->flags & LILO_MAP_PROMPT) ?
            "yes" : "no");
        if (h->timeout == LILO_TIMEOUT_NONE)
            printf("none");
        else
            printf("%u.%us", h->timeout / 10, h->timeout % 10);
        printf(", delay: %u.%us\n", h->delay / 10, h->delay % 10);
        if (h->flags & LILO_MAP_SERIAL)
            printf("serial: COM%u, %u bps\n", h->serial_port + 1,
                h->serial_baud);
        printf("map: LBA %llu, stage: LBA %llu (%u sectors)\n",
            (unsigned long long)d.map_lba, (unsigned long long)d.stage_lba,
            d.stage_sectors);
    }
}

static void save_sectors(const char *path, uint64_t n)
{
    uint8_t *buf = malloc(n * LILO_SECTOR_SIZE);
    FILE *f;

    if (buf == NULL)
        die("out of memory");
    if (disk_read(0, 0, (uint32_t)(n * LILO_SECTOR_SIZE), buf) != 0)
        die("cannot read the sectors to back up");
    f = fopen(path, "wb");
    if (f == NULL || fwrite(buf, LILO_SECTOR_SIZE, n, f) != n ||
            fclose(f) != 0)
        die("%s: cannot write backup", path);
    free(buf);
    printf("Backup copy of the boot sectors in %s\n", path);
}

/* Like lilo -u: put the saved boot code back, keep today's partition
 * table. */
static void uninstall(const char *dev, const char *save, int test)
{
    uint8_t mbr[LILO_SECTOR_SIZE];
    size_t len;
    uint8_t *old = read_file(save, &len);

    if (len < LILO_SECTOR_SIZE || len % LILO_SECTOR_SIZE != 0)
        die("%s: not a boot sector backup", save);
    disk_attach(dev, !test);
    if (disk_read(0, 0, sizeof(mbr), mbr) != 0)
        die("%s: cannot read the MBR", dev);
    memcpy(mbr, old, LILO_MBR_SIG_OFFSET);
    if (!test)
        disk_write_exact(0, mbr, sizeof(mbr));
    if (len > LILO_SECTOR_SIZE && !test)
        disk_write_exact(1, old + LILO_SECTOR_SIZE, len - LILO_SECTOR_SIZE);
    printf("%s boot sectors of %s from %s\n",
        test ? "Would restore" : "Restored", dev, save);
    free(old);
}

static void usage(const char *prog)
{
    printf("Usage: %s [-C config] [-b boot_device] [-i wolfboot.bin]\n"
        "          [-D label] [-s save_file] [-t] [-v]\n"
        "       %s -q [-C config] [-b boot_device] [-v]\n"
        "       %s -u save_file [-b boot_device] [-t]\n"
        "       %s -V\n\n"
        "  -C file   configuration file (default %s)\n"
        "  -b dev    boot device or disk image (overrides boot=)\n"
        "  -i file   wolfBoot x86_64_lilo binary (overrides install=,\n"
        "            default %s)\n"
        "  -D label  default image (overrides default=)\n"
        "  -s file   save the sectors about to be overwritten\n"
        "  -t        test only: check everything, write nothing\n"
        "  -q        list the images in the installed map\n"
        "  -u file   uninstall: restore the boot sectors saved with -s\n"
        "  -v        verbose (repeat for more)\n"
        "  -V        print the version\n",
        prog, prog, prog, prog, DEFAULT_CONFIG, DEFAULT_INSTALL);
}

int main(int argc, char **argv)
{
    const char *config = DEFAULT_CONFIG;
    const char *bootdev = NULL, *install = NULL, *deflabel = NULL;
    const char *save = NULL, *restore = NULL;
    int test = 0, query = 0, opt, i, j, n, def = 0;
    uint8_t mbr[LILO_SECTOR_SIZE], newmbr[LILO_SECTOR_SIZE];
    static uint8_t map[LILO_MAP_SECTORS * LILO_SECTOR_SIZE];
    struct lilo_map_hdr *h = (struct lilo_map_hdr *)map;
    struct lilo_map_entry *ents = (struct lilo_map_entry *)(map + LILO_SECTOR_SIZE);
    struct lilo_mbr_desc d;
    uint8_t *bin;
    size_t bin_len;
    uint64_t first, stage_lba, map_lba, need;
    uint32_t stage_sectors;

    while ((opt = getopt(argc, argv, "C:b:i:D:s:u:tqvVh")) != -1) {
        switch (opt) {
        case 'C': config = optarg; break;
        case 'b': bootdev = optarg; break;
        case 'i': install = optarg; break;
        case 'D': deflabel = optarg; break;
        case 's': save = optarg; break;
        case 'u': restore = optarg; break;
        case 't': test = 1; break;
        case 'q': query = 1; break;
        case 'v': verbose++; break;
        case 'V': printf("%s\n", LILO_VERSION); return 0;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        return 1;
    }

    if (restore != NULL) {
        if (bootdev == NULL) {
            parse_config(config);
            bootdev = g.boot;
        }
        if (bootdev == NULL)
            die("no boot device: use -b or boot=");
        uninstall(bootdev, restore, test);
        return 0;
    }

    parse_config(config);
    if (g.verbose > verbose)
        verbose = g.verbose;
    if (bootdev == NULL)
        bootdev = g.boot;
    if (bootdev == NULL)
        die("no boot device: use -b or boot=");
    if (query) {
        query_map(bootdev);
        return 0;
    }
    if (install == NULL)
        install = g.install ? g.install : DEFAULT_INSTALL;
    if (deflabel == NULL)
        deflabel = g.defaultl;
    if (n_images == 0)
        die("no images have been defined");

    /* The boot loader binary: MBR template, then the stage. */
    bin = read_file(install, &bin_len);
    memcpy(&d, bin + LILO_MBR_DESC_OFFSET,
        bin_len > LILO_MBR_DESC_OFFSET + sizeof(d) ? sizeof(d) : 0);
    if (bin_len <= LILO_SECTOR_SIZE || d.magic != LILO_MBR_DESC_MAGIC ||
            bin[510] != 0x55 || bin[511] != 0xAA)
        die("%s: not a wolfBoot x86_64_lilo binary", install);
    stage_sectors = (uint32_t)((bin_len - LILO_SECTOR_SIZE +
        LILO_SECTOR_SIZE - 1) / LILO_SECTOR_SIZE);
    if (d.stage_sectors != stage_sectors)
        die("%s: stage size mismatch (%u sectors, header says %u)",
            install, stage_sectors, d.stage_sectors);

    disk_attach(bootdev, !test);
    if (disk_read(0, 0, sizeof(mbr), mbr) != 0)
        die("%s: cannot read the MBR", bootdev);
    if (mbr[510] != 0x55 || mbr[511] != 0xAA)
        die("%s: no partition table", bootdev);
    first = first_part_lba(mbr);
    map_lba = 1;
    stage_lba = map_lba + LILO_MAP_SECTORS;
    need = stage_lba + stage_sectors;
    if (need > first)
        die("%s: the boot loader needs sectors 0..%llu, but the first "
            "partition starts at %llu", bootdev,
            (unsigned long long)need - 1, (unsigned long long)first);
    find_fat_partition(mbr);
    if (verbose)
        printf("Boot device %s, FAT32 partition index %d, "
            "map at LBA %llu, stage at LBA %llu (%u sectors)\n",
            bootdev, fat_part, (unsigned long long)map_lba,
            (unsigned long long)stage_lba, stage_sectors);

    /* Entries: labels must be unique, the default must exist. */
    memset(map, 0, sizeof(map));
    n = 0;
    for (i = 0; i < n_images; i++) {
        struct image_cfg *im = &images[i];
        if (im->label[0] == '\0')
            label_from_path(im);
        for (j = 0; j < i; j++) {
            if (strcasecmp(images[j].label, im->label) == 0 ||
                    (im->alias[0] &&
                     strcasecmp(images[j].alias, im->alias) == 0) ||
                    (im->alias[0] &&
                     strcasecmp(images[j].label, im->alias) == 0) ||
                    (images[j].alias[0] &&
                     strcasecmp(images[j].alias, im->label) == 0))
                die("duplicate label or alias: %s", im->label);
        }
        if (strchr(im->label, ' ') != NULL ||
                (im->alias[0] && strchr(im->alias, ' ') != NULL))
            die("%s: labels cannot contain spaces", im->label);
        if (build_entry(im, &ents[n], mbr) != 0)
            continue;
        if (deflabel != NULL && (strcasecmp(deflabel, im->label) == 0 ||
                (im->alias[0] && strcasecmp(deflabel, im->alias) == 0)))
            def = n + 1;
        n++;
    }
    if (n == 0)
        die("no usable images");
    if (deflabel != NULL && def == 0)
        die("default image '%s' does not exist", deflabel);
    def = def ? def - 1 : 0;
    for (i = 0; i < n; i++)
        printf("Added %s%s\n", ents[i].label, i == def ? " *" : "");

    h->magic = LILO_MAP_MAGIC;
    h->version = LILO_MAP_VERSION;
    h->n_entries = (uint16_t)n;
    h->default_entry = (uint16_t)def;
    h->entry_size = LILO_ENTRY_SIZE;
    h->flags = g.prompt ? LILO_MAP_PROMPT : 0;
    h->timeout = (uint16_t)((g.timeout >= 0) ? g.timeout : LILO_TIMEOUT_NONE);
    h->delay = (uint16_t)g.delay;
    if (g.serial_port >= 0) {
        h->flags |= LILO_MAP_SERIAL;
        h->serial_port = (uint16_t)g.serial_port;
        h->serial_baud = g.serial_baud;
    }
    if (g.timeout >= 0 && !g.prompt && g.delay == 0)
        warn("timeout= has no effect without prompt");
    map_crc(map, (uint32_t)n);

    /* MBR: our code and descriptor, the disk's signature and table. */
    memcpy(newmbr, bin, LILO_MBR_SIG_OFFSET);
    memcpy(newmbr + LILO_MBR_SIG_OFFSET, mbr + LILO_MBR_SIG_OFFSET,
        LILO_SECTOR_SIZE - LILO_MBR_SIG_OFFSET);
    d.map_sectors = LILO_MAP_SECTORS;
    d.stage_lba = stage_lba;
    d.map_lba = map_lba;
    memcpy(newmbr + LILO_MBR_DESC_OFFSET, &d, sizeof(d));

    if (test) {
        printf("The boot sector and the map have NOT been altered.\n");
        return 0;
    }
    if (save != NULL)
        save_sectors(save, need);
    disk_write_exact(map_lba, map, sizeof(map));
    {
        size_t stage_len = (size_t)stage_sectors * LILO_SECTOR_SIZE;
        uint8_t *stage = calloc(1, stage_len);
        if (stage == NULL)
            die("out of memory");
        memcpy(stage, bin + LILO_SECTOR_SIZE, bin_len - LILO_SECTOR_SIZE);
        disk_write_exact(stage_lba, stage, stage_len);
        free(stage);
    }
    disk_write_exact(0, newmbr, sizeof(newmbr));
    if (fsync(disk_fd) != 0)
        die("%s: sync failed: %s", bootdev, strerror(errno));
    close(disk_fd);
    free(bin);
    return 0;
}
