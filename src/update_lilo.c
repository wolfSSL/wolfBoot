/* update_lilo.c
 *
 * LILO-style boot for the x86_64_lilo target: a "boot:" prompt over the
 * entries that tools/lilo wrote to the MBR gap, then a Linux real-mode boot
 * or a chain-loaded partition boot sector.
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

#ifdef WOLFBOOT_LILO

#include <stdint.h>
#include <string.h>

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "printf.h"
#include "disk.h"
#include "disk_fs.h"
#include "gpt.h"
#include "lilo_map.h"
#include "wolfboot/wolfboot.h"
#include "x86/bios.h"
#include "x86/linux_loader.h"

#define BOOT_DISK       0

/* Linux real-mode layout, as in the boot protocol's own example: setup at
 * 0x90000, heap up to 0x9E000, command line right after. */
#define RM_SEG          0x9000
#define RM_BASE         0x90000UL
#define RM_HEAP_END     0xE000
#define RM_CMDLINE      (RM_BASE + RM_HEAP_END)
#define RM_CMDLINE_MAX  0x800
#define RM_TOP          (RM_CMDLINE + RM_CMDLINE_MAX)

/* Protected-mode kernel. The signed image is first staged above the area
 * the kernel is copied to, so the copy never overlaps its source. */
#define KERNEL_HIGH     0x100000UL
#define STAGE_ALIGN     0x100000UL

#define LINUX_HDR_MAGIC 0x53726448UL /* "HdrS" */
#define LOADED_HIGH     0x01
#define CAN_USE_HEAP    0x80

/* Chain loading: MBR copy and boot sector, where an MBR would put them */
#define CHAIN_MBR       0x600UL
#define CHAIN_BOOT      0x7C00UL

#define BIOS_TICKS_PER_DAY 0x1800B0UL
#define PROMPT_MAX        256
#define LOAD_CHUNK      0x100000UL

static uint8_t map_buf[LILO_MAP_SECTORS * LILO_SECTOR_SIZE];
static const struct lilo_map_hdr *map;
static const struct lilo_map_entry *entries;
static uint32_t mem_top;
static uint32_t lowmem_top;
static struct fs_volume vol;
static struct fs_file file;

static inline void *phys(uintptr_t addr)
{
    void *p = (void *)addr;
    __asm__ ("" : "+r"(p));
    return p;
}

static void put_str(const char *s)
{
    uart_write(s, (unsigned int)strlen(s));
}

static void put_chr(char c)
{
    uart_write(&c, 1);
}

static int map_load(void)
{
    const struct lilo_mbr_desc *d = (const struct lilo_mbr_desc *)bios_mbr_desc;
    struct lilo_map_hdr *h = (struct lilo_map_hdr *)map_buf;
    struct lilo_map_entry *e;
    struct gpt_crc32_ctx crc;
    uint32_t want, len, i;

    if (d->magic != LILO_MBR_DESC_MAGIC || d->map_sectors == 0 ||
            d->map_sectors > LILO_MAP_SECTORS) {
        return -1;
    }
    len = (uint32_t)d->map_sectors * LILO_SECTOR_SIZE;
    if (disk_read(BOOT_DISK, d->map_lba * LILO_SECTOR_SIZE, len, map_buf)
            != 0) {
        return -1;
    }
    if (h->magic != LILO_MAP_MAGIC || h->version != LILO_MAP_VERSION ||
            h->entry_size != LILO_ENTRY_SIZE || h->n_entries == 0 ||
            h->n_entries > LILO_MAP_MAX_ENTRIES ||
            h->default_entry >= h->n_entries ||
            LILO_SECTOR_SIZE + (uint32_t)h->n_entries * LILO_ENTRY_SIZE > len) {
        return -1;
    }
    len = LILO_SECTOR_SIZE + (uint32_t)h->n_entries * LILO_ENTRY_SIZE;
    want = h->crc32;
    h->crc32 = 0;
    gpt_crc32_init(&crc);
    gpt_crc32_update(&crc, map_buf, len);
    h->crc32 = want;
    if (gpt_crc32_final(&crc) != want)
        return -1;

    /* The map is unauthenticated: never trust its strings to be bounded. */
    e = (struct lilo_map_entry *)(map_buf + LILO_SECTOR_SIZE);
    for (i = 0; i < h->n_entries; i++) {
        e[i].label[LILO_LABEL_LEN - 1] = '\0';
        e[i].alias[LILO_LABEL_LEN - 1] = '\0';
        e[i].kernel[LILO_PATH_LEN - 1] = '\0';
        e[i].initrd[LILO_PATH_LEN - 1] = '\0';
        e[i].cmdline[LILO_CMDLINE_LEN - 1] = '\0';
    }
    map = h;
    entries = e;
    return 0;
}

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int name_eq(const char *name, const char *tok, uint32_t len)
{
    uint32_t i;

    if (name[0] == '\0')
        return 0;
    for (i = 0; i < len; i++) {
        if (name[i] == '\0' || to_lower(name[i]) != to_lower(tok[i]))
            return 0;
    }
    return name[len] == '\0';
}

static int find_entry(const char *tok, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < map->n_entries; i++) {
        if (name_eq(entries[i].label, tok, len) ||
                name_eq(entries[i].alias, tok, len)) {
            return (int)i;
        }
    }
    return -1;
}

static void list_entries(void)
{
    uint32_t i, col;

    for (i = 0; i < map->n_entries; i++) {
        put_str(entries[i].label);
        col = (uint32_t)strlen(entries[i].label);
        if (i == map->default_entry) {
            put_chr('*');
            col++;
        }
        if ((i % 4) == 3 || i + 1 == map->n_entries) {
            put_str("\n");
            continue;
        }
        while (col++ < 18)
            put_chr(' ');
    }
}

static uint32_t ticks_since(uint32_t start)
{
    uint32_t now = bios_ticks();

    if (now < start)
        now += BIOS_TICKS_PER_DAY; /* midnight */
    return now - start;
}

static uint32_t tenths_to_ticks(uint32_t tenths)
{
    return (tenths * 182U + 99U) / 100U;
}

/* Returns the line length, or -1 if the timeout expired before the first
 * key. Like LILO, any key cancels the timeout. */
static int read_line(char *buf, uint32_t max, int has_timeout,
                     uint32_t timeout)
{
    uint32_t n = 0, i;
    uint32_t start = bios_ticks();
    int c;

    for (;;) {
        c = hal_console_getc();
        if (c < 0) {
            if (has_timeout && ticks_since(start) >= timeout)
                return -1;
            continue;
        }
        has_timeout = 0;
        if (c == '\r' || c == '\n') {
            put_str("\n");
            buf[n] = '\0';
            return (int)n;
        }
        if (c == '\b' || c == 0x7F) {
            if (n > 0) {
                n--;
                put_str("\b \b");
            }
            continue;
        }
        if (c == 0x15) { /* ^U */
            while (n > 0) {
                n--;
                put_str("\b \b");
            }
            continue;
        }
        if (c == '\t' || (c == '?' && n == 0)) {
            put_str("\n");
            list_entries();
            put_str("boot: ");
            for (i = 0; i < n; i++)
                put_chr(buf[i]);
            continue;
        }
        if (c < 0x20 || c > 0x7E || n + 1 >= max)
            continue;
        buf[n++] = (char)c;
        put_chr((char)c);
    }
}

/* LILO without "prompt": a shift key (or any input) during the delay
 * brings the prompt up instead of booting the default. */
static int wait_shift(uint32_t tenths)
{
    uint32_t start = bios_ticks();
    uint32_t limit = tenths_to_ticks(tenths);

    do {
        if (bios_shift_pressed())
            return 1;
        if (hal_console_getc() >= 0)
            return 1;
    } while (ticks_since(start) < limit);
    return 0;
}

static int read_file(uint64_t off, uint32_t len, uint8_t *dst, int dots)
{
    uint32_t done = 0;
    uint32_t chunk;

    while (done < len) {
        chunk = len - done;
        if (chunk > LOAD_CHUNK)
            chunk = LOAD_CHUNK;
        if (fs_read(&file, off + done, chunk, dst + done) != (int)chunk)
            return -1;
        done += chunk;
        if (dots)
            put_chr('.');
    }
    return 0;
}

static int boot_linux(const struct lilo_map_entry *e, const char *extra)
{
    struct wolfBoot_image img;
    struct setup_header sh;
    struct setup_header *rsh;
    struct bios_jump j;
    uint8_t *stage;
    uint8_t *kernel;
    char *cmd;
    uint64_t fsize, limit, low;
    uint32_t setup_size, pm_size, cmd_max, n, xlen, x;
    uint32_t initrd_addr = 0, initrd_size = 0;

    if (fs_mount(&vol, BOOT_DISK, e->part) != WOLFBOOT_FS_OK ||
            vol.type != FS_TYPE_FAT32) {
        wolfBoot_printf("\nPartition %d is not FAT32\n", e->part);
        return -1;
    }
    if (mem_top <= KERNEL_HIGH + STAGE_ALIGN ||
            fs_open(&vol, &file, e->kernel, mem_top - KERNEL_HIGH)
            != WOLFBOOT_FS_OK) {
        wolfBoot_printf("\nCannot open %s\n", e->kernel);
        return -1;
    }
    fsize = fs_size(&file);
    if (fsize < (uint64_t)IMAGE_HEADER_SIZE + 0x400) {
        wolfBoot_printf("\n%s: too small\n", e->kernel);
        return -1;
    }
    stage = phys(KERNEL_HIGH +
        (((uint32_t)fsize + STAGE_ALIGN - 1) & ~(STAGE_ALIGN - 1)));
    if ((uint64_t)(uintptr_t)stage + fsize > mem_top) {
        wolfBoot_printf("\n%s: does not fit in memory\n", e->kernel);
        return -1;
    }

    /* The header bounds what is read next, so read and parse it first. */
    if (read_file(0, IMAGE_HEADER_SIZE, stage, 0) != 0) {
        wolfBoot_printf("\n%s: read error\n", e->kernel);
        return -1;
    }
    memset(&img, 0, sizeof(img));
    put_str("\n");
    if (wolfBoot_open_image_address(&img, stage) < 0) {
        wolfBoot_printf("%s: not a wolfBoot image\n", e->kernel);
        return -1;
    }
    if ((uint64_t)img.fw_size + IMAGE_HEADER_SIZE > fsize ||
            img.fw_size < 0x1F1 + sizeof(sh)) {
        wolfBoot_printf("%s: truncated image\n", e->kernel);
        return -1;
    }
    if (read_file(IMAGE_HEADER_SIZE, img.fw_size, stage + IMAGE_HEADER_SIZE,
            1) != 0) {
        wolfBoot_printf("\n%s: read error\n", e->kernel);
        return -1;
    }
    put_str("\n");
    if (wolfBoot_verify_integrity(&img) < 0) {
        wolfBoot_printf("%s: integrity check failed\n", e->kernel);
        return -1;
    }
    if (wolfBoot_verify_authenticity(&img) < 0) {
        wolfBoot_printf("%s: authenticity check failed\n", e->kernel);
        return -1;
    }
    wolfBoot_printf("Verified %s, version %u\n", e->kernel,
        (unsigned int)wolfBoot_get_blob_version(stage));

    kernel = stage + IMAGE_HEADER_SIZE;
    memcpy(&sh, kernel + 0x1F1, sizeof(sh));
    if (sh.header != LINUX_HDR_MAGIC || sh.version < 0x0204 ||
            (sh.loadflags & LOADED_HIGH) == 0) {
        wolfBoot_printf("%s: not a bzImage (boot protocol 2.04+)\n",
            e->kernel);
        return -1;
    }
    setup_size = ((sh.setup_sects != 0) ? sh.setup_sects : 4U) + 1U;
    setup_size *= 512U;
    if (setup_size >= img.fw_size || setup_size > RM_HEAP_END - 0x200) {
        wolfBoot_printf("%s: bad setup size\n", e->kernel);
        return -1;
    }
    pm_size = img.fw_size - setup_size;
    /* Where the kernel decompresses itself must exist. */
    if (sh.version >= 0x020A) {
        low = (sh.pref_address > KERNEL_HIGH) ? sh.pref_address : KERNEL_HIGH;
        if (low + sh.init_size > mem_top) {
            wolfBoot_printf("%s: needs more memory\n", e->kernel);
            return -1;
        }
    }

    cmd_max = RM_CMDLINE_MAX;
    if (sh.version >= 0x0206 && sh.cmdline_size + 1U < cmd_max)
        cmd_max = sh.cmdline_size + 1U;
    n = (uint32_t)strlen(e->cmdline);
    xlen = (uint32_t)strlen(extra);
    if (n + 1U + xlen + 1U > cmd_max) {
        wolfBoot_printf("Command line too long\n");
        return -1;
    }

    /* Both copies leave the staging area: the source sits above
     * KERNEL_HIGH + fsize, past the end of either destination. */
    memcpy(phys(RM_BASE), kernel, setup_size);
    memcpy(phys(KERNEL_HIGH), kernel + setup_size, pm_size);

    if (e->initrd[0] != '\0') {
        if (fs_open(&vol, &file, e->initrd, mem_top) != WOLFBOOT_FS_OK ||
                fs_size(&file) == 0) {
            wolfBoot_printf("Cannot open %s\n", e->initrd);
            return -1;
        }
        fsize = fs_size(&file);
        limit = mem_top;
        x = (sh.version >= 0x0203) ? sh.initrd_addr_max : 0x37FFFFFFU;
        if ((uint64_t)x + 1U < limit)
            limit = (uint64_t)x + 1U;
        low = KERNEL_HIGH + pm_size;
        if (sh.version >= 0x020A &&
                (uint64_t)sh.pref_address + sh.init_size > low) {
            low = (uint64_t)sh.pref_address + sh.init_size;
        }
        if (fsize > limit || ((limit - fsize) & ~0xFFFULL) < low) {
            wolfBoot_printf("%s: does not fit in memory\n", e->initrd);
            return -1;
        }
        initrd_addr = (uint32_t)((limit - fsize) & ~0xFFFULL);
        initrd_size = (uint32_t)fsize;
        put_str("Loading initrd");
        if (read_file(0, initrd_size, phys(initrd_addr), 1) != 0) {
            wolfBoot_printf("\n%s: read error\n", e->initrd);
            return -1;
        }
        put_str("\n");
    }

    cmd = phys(RM_CMDLINE);
    memcpy(cmd, e->cmdline, n);
    if (xlen > 0) {
        if (n > 0)
            cmd[n++] = ' ';
        memcpy(cmd + n, extra, xlen);
        n += xlen;
    }
    cmd[n] = '\0';

    rsh = phys(RM_BASE + 0x1F1);
    rsh->type_of_loader = 0xFF;
    rsh->loadflags |= CAN_USE_HEAP;
    rsh->heap_end_ptr = RM_HEAP_END - 0x200;
    rsh->cmd_line_ptr = RM_CMDLINE;
    rsh->ramdisk_image = initrd_addr;
    rsh->ramdisk_size = initrd_size;
    if (e->vga != LILO_VGA_DEFAULT)
        rsh->vid_mode = e->vga;

    wolfBoot_printf("Booting kernel: %s\n", cmd);
    hal_prepare_boot();
    memset(&j, 0, sizeof(j));
    j.cs = RM_SEG + 0x20;
    j.ip = 0;
    j.ds = RM_SEG;
    j.ss = RM_SEG;
    j.sp = RM_HEAP_END;
    bios_realmode_jump(&j);
}

static int boot_other(const struct lilo_map_entry *e)
{
    static uint8_t mbr[LILO_SECTOR_SIZE];
    static uint8_t bs[LILO_SECTOR_SIZE];
    struct bios_jump j;
    uint32_t pte_off, lba;

    put_str("\n");
    if (e->part < 1 || e->part > 4 ||
            disk_read(BOOT_DISK, 0, LILO_SECTOR_SIZE, mbr) != 0) {
        wolfBoot_printf("Cannot read the partition table\n");
        return -1;
    }
    pte_off = LILO_MBR_PTABLE_OFFSET + 16U * (e->part - 1U);
    lba = gpt_le32(mbr + pte_off + 8);
    if (mbr[pte_off + 4] == 0 || lba == 0) {
        wolfBoot_printf("Partition %d is empty\n", e->part);
        return -1;
    }
    if (disk_read(BOOT_DISK, (uint64_t)lba * LILO_SECTOR_SIZE,
            LILO_SECTOR_SIZE, bs) != 0) {
        wolfBoot_printf("Cannot read partition %d\n", e->part);
        return -1;
    }
    if (bs[510] != 0x55 || bs[511] != 0xAA) {
        wolfBoot_printf("Partition %d has no boot sector\n", e->part);
        return -1;
    }
    /* DS:SI points at the partition entry, marked active, as an MBR would
     * leave it for the boot sector. */
    mbr[pte_off] = 0x80;
    memcpy(phys(CHAIN_MBR), mbr, LILO_SECTOR_SIZE);
    memcpy(phys(CHAIN_BOOT), bs, LILO_SECTOR_SIZE);
    wolfBoot_printf("Chain loading partition %d\n", e->part);
    hal_prepare_boot();
    memset(&j, 0, sizeof(j));
    j.cs = 0;
    j.ip = CHAIN_BOOT;
    j.ds = 0;
    j.ss = 0;
    j.sp = CHAIN_BOOT;
    j.dx = bios_boot_drive;
    j.si = (uint16_t)(CHAIN_MBR + pte_off);
    bios_realmode_jump(&j);
}

void RAMFUNCTION wolfBoot_start(void)
{
    static char line[PROMPT_MAX];
    const struct lilo_map_entry *e;
    const char *extra;
    uint32_t len;
    int prompt, first = 1, idx, n;

    if (map_load() != 0) {
        wolfBoot_printf("\nNo valid boot map: run tools/lilo\n");
        wolfBoot_panic();
    }
    put_str("O\n"); /* LILO: map loaded */
    if (map->flags & LILO_MAP_SERIAL)
        hal_console_serial(map->serial_port, map->serial_baud);

    mem_top = bios_mem_top();
    lowmem_top = bios_lowmem_top();
    wolfBoot_printf("wolfBoot: %u KB low, %u MB extended memory\n",
        (unsigned int)(lowmem_top / 1024U),
        (unsigned int)((mem_top - KERNEL_HIGH) >> 20));
    if (lowmem_top < RM_TOP) {
        wolfBoot_printf("Not enough low memory for the kernel setup\n");
        wolfBoot_panic();
    }
    if (disk_open(BOOT_DISK) < 0) {
        wolfBoot_printf("Cannot read the boot disk\n");
        wolfBoot_panic();
    }

    prompt = (map->flags & LILO_MAP_PROMPT) || wait_shift(map->delay);
    for (;;) {
        idx = map->default_entry;
        extra = "";
        if (prompt) {
            put_str("boot: ");
            n = read_line(line, sizeof(line),
                first && map->timeout != LILO_TIMEOUT_NONE,
                tenths_to_ticks(map->timeout));
            first = 0;
            if (n < 0) {
                put_str(entries[idx].label);
                put_str("\n");
            }
            else {
                char *tok = line;
                while (*tok == ' ')
                    tok++;
                len = 0;
                while (tok[len] != '\0' && tok[len] != ' ')
                    len++;
                extra = tok + len;
                while (*extra == ' ')
                    extra++;
                if (len > 0) {
                    idx = find_entry(tok, len);
                    if (idx < 0) {
                        put_str("No such image. [Tab] shows a list.\n");
                        continue;
                    }
                }
            }
        }
        e = &entries[idx];
        wolfBoot_printf("Loading %s", e->label);
        if (e->type == LILO_ENTRY_LINUX)
            (void)boot_linux(e, extra);
        else if (e->type == LILO_ENTRY_OTHER)
            (void)boot_other(e);
        else
            wolfBoot_printf("\nUnknown entry type %d\n", e->type);
        /* Only back here on failure: fall back to the prompt. */
        prompt = 1;
    }
}

#endif /* WOLFBOOT_LILO */
