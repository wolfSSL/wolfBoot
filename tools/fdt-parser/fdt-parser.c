/* fdt-parser.c
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
 *
 * flattened device tree parser tool
 */

#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "string.h"
#include "fdt.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/* The corpus runner (-f) forks a child per case so a fault in one does not
 * stop the sweep and a sanitizer abort stays observable. That is POSIX
 * only; elsewhere -f reports that it is unavailable and the normal parse
 * modes still build. */
#if defined(__unix__) || defined(__APPLE__)
    #define FDT_CORPUS_RUNNER
    #include <unistd.h>
    #include <sys/wait.h>
#endif

static int gEnableUnitTest = 0;
static int gParseFit = 0;
static int gRunCorpus = 0;
#define UNIT_TEST_GROW_SIZE 1024

/* Test case for "nxp_t1024.dtb" */
static int fdt_test(fdt_ctx* ctx)
{
    int ret = 0, off, i;
    uint32_t *reg, oldsize;

    #define DDR_ADDRESS       0
    #define DDR_SIZE          (2048ULL * 1024ULL * 1024ULL)
    #define CPU_NUMCORES      2
    #define SPIN_TABLE_ADDR   0x7FF01900UL
    #define ENTRY_SIZE        64
    #define SYS_CLK           (100000000) /* 100MHz */
    #define PLAT_CLK          (SYS_CLK * 4)
    #define BUS_CLK           (PLAT_CLK / 2)
    #define TIMEBASE_HZ       (PLAT_CLK / 16)

    struct qportal_info {
        uint16_t   dliodn;    /* DQRR LIODN */
        uint16_t   fliodn;    /* frame data LIODN */
        uint16_t   liodn_offset;
        uint8_t    sdest;
    };

    #define SET_QP_INFO(dqrr, fdata, off, dest) \
        { .dliodn = dqrr, .fliodn = fdata, .liodn_offset = off, .sdest = dest }

    #define QMAN_NUM_PORTALS    10
    const struct qportal_info qp_info[QMAN_NUM_PORTALS] = {
        /* dqrr liodn, frame data liodn, liodn off, sdest */
        SET_QP_INFO(1, 27, 1, 0),
        SET_QP_INFO(2, 28, 1, 0),
        SET_QP_INFO(3, 29, 1, 1),
        SET_QP_INFO(4, 30, 1, 1),
        SET_QP_INFO(5, 31, 1, 2),
        SET_QP_INFO(6, 32, 1, 2),
        SET_QP_INFO(7, 33, 1, 3),
        SET_QP_INFO(8, 34, 1, 3),
        SET_QP_INFO(9, 35, 1, 0),
        SET_QP_INFO(10, 36, 1, 0)
    };

    struct liodn_id_table {
        const char* compat;
        uint32_t id;
    };
    #define SET_LIODN(fdtcomp, liodn) \
        {.compat = fdtcomp, .id = liodn}

    const struct liodn_id_table liodn_tbl[] = {
        SET_LIODN("fsl-usb2-mph", 553),
        SET_LIODN("fsl-usb2-dr", 554),
        SET_LIODN("fsl,esdhc", 552),
        SET_LIODN("fsl,pq-sata-v2", 555),
        SET_LIODN("fsl,tdm1.0", 560),
        SET_LIODN("fsl,qe", 559),
        SET_LIODN("fsl,elo3-dma", 147),
        SET_LIODN("fsl,elo3-dma", 227),

        SET_LIODN("fsl,qman", 62),
        SET_LIODN("fsl,bman", 63),
        SET_LIODN("fsl,qoriq-pcie-v2.4", 148),
        SET_LIODN("fsl,qoriq-pcie-v2.4", 228),
        SET_LIODN("fsl,qoriq-pcie-v2.4", 308),
    };

    /* reserve headroom so the fixups below have somewhere to grow */
    oldsize = fdt_size(ctx);
    ret = fdt_grow(ctx, UNIT_TEST_GROW_SIZE);
    if (ret != 0) goto exit;

    /* fixup the memory region - single bank */
    off = fdt_find_devtype(ctx, -1, "memory");
    if (off != -FDT_ERR_NOTFOUND) {
        /* build addr/size as 64-bit */
        uint8_t ranges[sizeof(uint64_t) * 2], *p = ranges;
        *(uint64_t*)p = cpu_to_fdt64(DDR_ADDRESS);
        p += sizeof(uint64_t);
        *(uint64_t*)p = cpu_to_fdt64(DDR_SIZE);
        p += sizeof(uint64_t);
        ret = fdt_setprop(ctx, off, "reg", ranges, (int)(p - ranges));
        if (ret != 0) goto exit;
        printf("FDT: Set memory, start=0x%x, size=0x%x\n",
            DDR_ADDRESS, (uint32_t)DDR_SIZE);
    }

    /* fixup CPU status and, release address and enable method */
    off = fdt_find_devtype(ctx, -1, "cpu");
    while (off != -FDT_ERR_NOTFOUND) {
        int core;
        uint64_t core_spin_table_addr;

        reg = (uint32_t*)fdt_getprop(ctx, off, "reg", NULL);
        if (reg == NULL)
            break;
        core = (int)fdt32_to_cpu(*reg);
        if (core >= CPU_NUMCORES) {
            break; /* invalid core index */
        }

        /* calculate location of spin table for core */
        core_spin_table_addr = (uint64_t)((uintptr_t)(
            SPIN_TABLE_ADDR + (core * ENTRY_SIZE)));

        ret = fdt_fixup_str(ctx, off, "cpu", "status", (core == 0) ? "okay" : "disabled");
        if (ret == 0)
            ret = fdt_fixup_val64(ctx, off, "cpu", "cpu-release-addr", core_spin_table_addr);
        if (ret == 0)
            ret = fdt_fixup_str(ctx, off, "cpu", "enable-method", "spin-table");
        if (ret == 0)
            ret = fdt_fixup_val(ctx, off, "cpu", "timebase-frequency", TIMEBASE_HZ);
        if (ret == 0)
            ret = fdt_fixup_val(ctx, off, "cpu", "clock-frequency", PLAT_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(ctx, off, "cpu", "bus-frequency", PLAT_CLK);
        if (ret != 0) goto exit;

        off = fdt_find_devtype(ctx, off, "cpu");
    }

    /* fixup the soc clock */
    off = fdt_find_devtype(ctx, -1, "soc");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(ctx, off, "soc", "bus-frequency", PLAT_CLK);
        if (ret != 0) goto exit;
    }

    /* fixup the serial clocks */
    off = fdt_find_devtype(ctx, -1, "serial");
    while (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(ctx, off, "serial", "clock-frequency", BUS_CLK);
        if (ret != 0) goto exit;
        off = fdt_find_devtype(ctx, off, "serial");
    }

    /* fixup the QE bridge and bus blocks */
    off = fdt_find_devtype(ctx, -1, "qe");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(ctx, off, "qe", "clock-frequency", BUS_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(ctx, off, "qe", "bus-frequency", BUS_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(ctx, off, "qe", "brg-frequency", BUS_CLK/2);
        if (ret != 0) goto exit;
    }

    /* fixup the LIODN */
    for (i=0; i<(int)(sizeof(liodn_tbl)/sizeof(struct liodn_id_table)); i++) {
        off = fdt_node_offset_by_compatible(ctx, -1, liodn_tbl[i].compat);
        if (off >= 0) {
            ret = fdt_fixup_val(ctx, off, liodn_tbl[i].compat, "fsl,liodn",
                liodn_tbl[i].id);
            if (ret != 0) goto exit;
        }
    }

    /* fixup the QMAN portals */
    off = fdt_node_offset_by_compatible(ctx, -1, "fsl,qman-portal");
    while (off != -FDT_ERR_NOTFOUND) {
        const int *ci = fdt_getprop(ctx, off, "cell-index", NULL);
        uint32_t portal_idx;
        uint32_t liodns[2];
        if (!ci)
            break;
        portal_idx = fdt32_to_cpu(*ci);
        if (portal_idx >= QMAN_NUM_PORTALS) {
            printf("FDT: Invalid qman-portal cell-index %u at %d\n",
                portal_idx, off);
            ret = -FDT_ERR_BADSTRUCTURE;
            goto exit;
        }
        i = (int)portal_idx;

        liodns[0] = qp_info[i].dliodn;
        liodns[1] = qp_info[i].fliodn;
        printf("FDT: Set %s@%d (%d), %s=%d,%d\n",
            "qman-portal", i, off, "fsl,liodn", liodns[0], liodns[1]);
        /* big-endian on the wire, matching hal/nxp_t10xx.c */
        liodns[0] = cpu_to_fdt32(liodns[0]);
        liodns[1] = cpu_to_fdt32(liodns[1]);
        ret = fdt_setprop(ctx, off, "fsl,liodn", liodns, sizeof(liodns));
        if (ret != 0) goto exit;

        off = fdt_node_offset_by_compatible(ctx, off, "fsl,qman-portal");
    }

    /* mpic clock */
    off = fdt_find_devtype(ctx, -1, "open-pic");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(ctx, off, "open-pic", "clock-frequency", BUS_CLK);
        if (ret != 0) goto exit;
    }

    /* resize the device tree */
    fdt_shrink(ctx);

    /* display information */
    printf("FDT Updated: Size %u -> %u\n",
        (unsigned int)oldsize, (unsigned int)fdt_size(ctx));

exit:
    printf("FDT Test Result: %d\n", ret);
    return ret;
}

static void print_bin(const uint8_t* buffer, uint32_t length)
{
    uint32_t i, notprintable = 0;

    if (!buffer || length == 0) {
        printf("NULL");
        return;
    }

    for (i = 0; i < length; i++) {
        if (buffer[i] > 31 && buffer[i] < 127) {
            printf("%c", buffer[i]);
        } else if (i == length-1 && buffer[i] == '\0') {
            printf(" "); /* null term */
        } else {
            printf(".");
            notprintable++;
        }
    }

    if (notprintable > 0) {
        printf("| ");
        for (i = 0; i < length; i++) {
            printf("%02x ", buffer[i]);
        }
    }
}

static int write_bin(const char* filename, const uint8_t *buf, uint32_t bufSz)
{
    int rc = -1;
    FILE* fp = NULL;
    size_t fileSz = 0;

    if (filename == NULL || buf == NULL)
        return -1;

    fp = fopen(filename, "wb");
    if (fp != NULL) {
        fileSz = fwrite(buf, 1, bufSz, fp);
        /* sanity check */
        if (fileSz == (uint32_t)bufSz) {
            rc = 0;
        }
        printf("Wrote %d bytes to %s\n", (int)fileSz, filename);

        fclose(fp);
    }
    return rc;
}

static int load_file(const char* filename, uint8_t** buf, size_t* bufLen)
{
    int ret = 0;
    ssize_t fileSz, readLen;
    FILE* fp;

    if (filename == NULL || buf == NULL || bufLen == NULL)
        return -1;

    /* open file (read-only binary) */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error loading %s\n", filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    fileSz = ftell(fp);
    rewind(fp);
    if (fileSz > 0) {
        if (*buf == NULL) {
            if (gEnableUnitTest)
                *buf = (uint8_t*)malloc(fileSz + UNIT_TEST_GROW_SIZE);
            else
                *buf = (uint8_t*)malloc(fileSz);
            if (*buf == NULL)
                ret = -1;
        }
        else if (*buf != NULL && fileSz > (ssize_t)*bufLen) {
            ret = -1;
        }
        *bufLen = (size_t)fileSz;
        if (ret == 0) {
            readLen = fread(*buf, 1, *bufLen, fp);
            ret = (readLen == (ssize_t)*bufLen) ? 0 : -1;
        }
    }
    else {
        ret = -1;
    }
    fclose(fp);
    return ret;
}

static void* dts_fit_image_addr(const fdt_ctx* ctx, int off, const char* prop)
{
    void* val = fdt_getprop_address(ctx, off, prop);
    printf("\t%s: %p\n", prop, val);
    return val;
}

static const void* dts_fit_image_item(const fdt_ctx* ctx, int off, const char* prop)
{
    int len = 0;
    const void* val = fdt_getprop(ctx, off, prop, &len);
    if (val != NULL && len > 0) {
        if (len < 256)
            printf("\t%s (len %d): %s\n", prop, len, (const char*)val);
        else
            printf("\t%s (len %d): not rendering\n", prop, len);
    }
    return val;
}

void dts_parse_fit_image(fdt_ctx* ctx, const char* image, const char* desc)
{
    int off;

    if (image == NULL) {
        return;
    }
    printf("%s Image: %s\n", desc, image);

    off = fdt_find_node_offset(ctx, -1, image);
    if (off >= 0) {
        dts_fit_image_item(ctx, off, "description");
        dts_fit_image_item(ctx, off, "type");
        dts_fit_image_item(ctx, off, "os");
        dts_fit_image_item(ctx, off, "arch");
        dts_fit_image_item(ctx, off, "compression");
        dts_fit_image_addr(ctx, off, "load");
        dts_fit_image_addr(ctx, off, "entry");
        dts_fit_image_item(ctx, off, "padding");
        dts_fit_image_item(ctx, off, "data");
    }
}

int dts_parse_fit(fdt_ctx* ctx)
{
    const char *conf = NULL, *kernel = NULL, *flat_dt = NULL, *ramdisk = NULL;
    const char *fpga = NULL;

    conf = fit_find_images(ctx, &kernel, &flat_dt, &ramdisk, &fpga);
    if (conf != NULL) {
        printf("FIT: Found '%s' configuration\n", conf);
        dts_fit_image_item(ctx, fdt_find_node_offset(ctx, -1, conf),
            "description");
    }

    /* dump image information */
    dts_parse_fit_image(ctx, kernel, "Kernel");
    dts_parse_fit_image(ctx, flat_dt, "FDT");
    if (ramdisk != NULL) {
        dts_parse_fit_image(ctx, ramdisk, "Ramdisk");
    }
    if (fpga != NULL) {
        dts_parse_fit_image(ctx, fpga, "FPGA");
    }

    return 0;
}

int dts_parse(fdt_ctx* ctx)
{
    int ret = 0;
    int nlen, plen;
    int noff, poff;
    const char *nstr = NULL, *pstr = NULL;
    const void* pval;
    int depth = 0;
    #define MAX_DEPTH 24
    char tabs[MAX_DEPTH+1] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

    /* walk tree */
    for (noff = fdt_next_node(ctx, -1, &depth);
         noff >= 0;
         noff = fdt_next_node(ctx, noff, &depth))
    {
        nstr = fdt_get_name(ctx, noff, &nlen);
        if (depth > MAX_DEPTH)
            depth = MAX_DEPTH;

        if (nlen == 0 && depth == 1)
            nstr = "root";
        printf("%s%s (node offset %d, depth %d, len %d):\n",
            &tabs[MAX_DEPTH-depth+1], nstr, noff, depth, nlen);

        for (poff = fdt_first_property_offset(ctx, noff);
            poff >= 0;
            poff = fdt_next_property_offset(ctx, poff))
        {
            pval = fdt_getprop_by_offset(ctx, poff, &pstr, &plen);
            if (pval != NULL) {
                printf("%s%s (prop offset %d, len %d): ",
                    &tabs[MAX_DEPTH-depth], pstr, poff, plen);
                if (plen > 32)
                    printf("\n%s", &tabs[MAX_DEPTH-depth-1]);
                if (plen > 256) {
                    char file[260+1];
                    snprintf(file, sizeof(file), "%s.%s.bin", nstr, pstr);
                    printf("Saving to file %s\n", file);
                    write_bin(file, (const uint8_t*)pval, plen);
                }
                else {
                    print_bin((const uint8_t*)pval, plen);
                    printf("\n");
                }
            }
        }
    }

    return ret;
}


/* ------------------------------------------------------------------ */
/* Malformed-input corpus (-f)                                         */
/* ------------------------------------------------------------------ */

/* Exit codes used by the forked child below. A distinct value for
 * "accepted" keeps it apart from the 1 a sanitizer exits with. */
#define CORPUS_REJECTED 0
#define CORPUS_ACCEPTED 42

/* Open a corpus blob and, if it is accepted, read everything a real
 * consumer would touch. `len` is the exact allocation size, so any read
 * past it is a genuine heap overrun for ASan to catch. */
static int corpus_parse(uint8_t* buf, uint32_t len)
{
    fdt_ctx ctx;
    int noff, poff, plen, nlen;
    const char *nstr, *pstr;
    const void* pval;
    int depth = 0;
    int nodes = 0;

    if (fdt_open(&ctx, buf, len) != 0) {
        return CORPUS_REJECTED;
    }

    for (noff = fdt_next_node(&ctx, -1, &depth);
         noff >= 0;
         noff = fdt_next_node(&ctx, noff, &depth))
    {
        nstr = fdt_get_name(&ctx, noff, &nlen);
        (void)nstr;
        if (++nodes > 200000) {
            break; /* runaway guard */
        }
        for (poff = fdt_first_property_offset(&ctx, noff);
             poff >= 0;
             poff = fdt_next_property_offset(&ctx, poff))
        {
            pval = fdt_getprop_by_offset(&ctx, poff, &pstr, &plen);
            if (pval == NULL) {
                break;
            }
            (void)pstr;
            /* touch the payload the way a consumer would */
            if (plen > 0) {
                volatile char sink = 0;
                int i;
                for (i = 0; i < plen; i++) {
                    sink = ((const char*)pval)[i];
                }
                (void)sink;
            }
        }
    }
    return CORPUS_ACCEPTED;
}

#ifdef FDT_CORPUS_RUNNER
/* Lookups on a VALID blob, with names far longer than anything in the
 * tree. Search names reach the parser from attacker-influenced places (a
 * FIT's `default` string, for one), so their length is not bounded by the
 * blob - a comparison that trusted it would read past a node name. Run
 * under ASan this is a real out-of-bounds check, not just a return-value
 * check. */
static void probe_hostile_lookups(uint8_t* blob, uint32_t len)
{
    static char big[4096];
    fdt_ctx ctx;
    char path[4200];

    if (fdt_open(&ctx, blob, len) != 0) {
        return;
    }
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    (void)fdt_find_node_offset(&ctx, -1, big);
    (void)fdt_find_prop_offset(&ctx, -1, big, big);
    (void)fdt_node_offset_by_compatible(&ctx, -1, big);
    (void)fdt_getprop(&ctx, 0, big, NULL);
    (void)fdt_subnode_offset(&ctx, 0, big);
    snprintf(path, sizeof(path), "/%s", big);
    (void)fdt_path_offset(&ctx, path);
    snprintf(path, sizeof(path), "/cpus/%s", big);
    (void)fdt_path_offset(&ctx, path);
}

static int run_corpus(const uint8_t* base, uint32_t baselen)
{
    int i, n, accepted = 0, rejected = 0, faulted = 0;

    n = fdt_corpus_count();
    printf("Malformed-DTB corpus: %d cases\n\n", n);
    printf("%-22s %-38s %s\n", "CASE", "WHAT", "RESULT");
    printf("%-22s %-38s %s\n", "----", "----", "------");

    for (i = 0; i < n; i++) {
        uint32_t len = 0;
        uint8_t* buf;
        const char* result;
        pid_t pid;
        int status = 0;

        buf = fdt_corpus_build(i, base, baselen, &len);
        if (buf == NULL) {
            printf("%-22s %-38s %s\n", fdt_corpus_name(i),
                fdt_corpus_desc(i), "SKIP");
            continue;
        }

        /* Each case runs in its own process so a fault in one does not
         * stop the sweep, and so a sanitizer abort is observable. */
        fflush(stdout);
        pid = fork();
        if (pid == 0) {
            _exit(corpus_parse(buf, len));
        }
        free(buf);
        if (pid < 0) {
            printf("fork failed\n");
            return -1;
        }
        waitpid(pid, &status, 0);

        if (WIFSIGNALED(status)) {
            result = "FAULT";
            faulted++;
        }
        else if (WEXITSTATUS(status) == CORPUS_REJECTED) {
            result = "rejected";
            rejected++;
        }
        else if (WEXITSTATUS(status) == CORPUS_ACCEPTED) {
            result = "ACCEPTED";
            accepted++;
        }
        else {
            result = "SANITIZER";
            faulted++;
        }
        printf("%-22s %-38s %s\n", fdt_corpus_name(i), fdt_corpus_desc(i),
            result);
    }

    printf("\nrejected=%d accepted=%d faulted=%d (of %d)\n",
        rejected, accepted, faulted, n);

    {
        uint32_t len = 0;
        uint8_t* good = malloc(baselen);

        if (good != NULL) {
            memcpy(good, base, baselen);
            len = baselen;
            probe_hostile_lookups(good, len);
            free(good);
            printf("hostile-lookup probes on a valid blob: done\n");
        }
    }

    /* Every case in the corpus is invalid, so anything not rejected is
     * a failure of the parser. */
    return (accepted == 0 && faulted == 0) ? 0 : -1;
}
#else /* !FDT_CORPUS_RUNNER */
static int run_corpus(const uint8_t* base, uint32_t baselen)
{
    (void)base;
    (void)baselen;
    printf("-f (malformed-input corpus) needs fork()/waitpid(); "
        "not available on this platform\n");
    return -1;
}
#endif /* FDT_CORPUS_RUNNER */

static void Usage(void)
{
    printf("Expected usage:\n");
    printf("./tools/fdt-parser/fdt-parser [-t] [-i] filename\n");
    printf("\t* -i: Parse Flattened uImage Tree (FIT) image\n");
    printf("\t* -t: Test several updates (used with nxp_t1024.dtb)\n");
    printf("\t* -f: Check that malformed variants of the file are "
        "rejected\n");
}

int main(int argc, char *argv[])
{
    int ret = 0;
    uint8_t *image = NULL;
    size_t imageSz = 0;
    uint32_t capacity = 0;
    fdt_ctx ctx;
    const char* filename = NULL;

    if (argc == 1 || (argc >= 2 &&
            (strcmp(argv[1], "-?") == 0 ||
             strcmp(argv[1], "-h") == 0 ||
             strcmp(argv[1], "--help") == 0))) {
        Usage();
        return 0;
    }
    while (argc > 1) {
        if (strcmp(argv[argc-1], "-t") == 0) {
            gEnableUnitTest = 1;
        }
        else if (strcmp(argv[argc-1], "-i") == 0) {
            gParseFit = 1;
        }
        else if (strcmp(argv[argc-1], "-f") == 0) {
            gRunCorpus = 1;
        }
        else if (*argv[argc-1] != '-') {
            filename = argv[argc-1];
        }
        else {
            printf("Warning: Unrecognized option: %s\n", argv[argc-1]);
        }
        argc--;
    }

    if (filename == NULL) {
        printf("Usage: fdt-parser [filename.dtb]\n");
        return 0;
    }
    printf("FDT Parser (%s):\n", filename);

    ret = load_file(filename, &image, &imageSz);
    if (ret == 0) {
        /* Must equal what load_file() allocated, which is imageSz plus
         * UNIT_TEST_GROW_SIZE when -t is in use (see load_file). Every
         * bound the parser applies comes from this, not from the blob's
         * own totalsize, so it must neither overstate the allocation nor
         * wrap while being narrowed from size_t. */
        if (imageSz > (size_t)(UINT32_MAX - UNIT_TEST_GROW_SIZE)) {
            printf("File too large to parse (%llu bytes)\n",
                (unsigned long long)imageSz);
            free(image);
            return -1;
        }
        capacity = (uint32_t)imageSz;
        if (gEnableUnitTest)
            capacity += UNIT_TEST_GROW_SIZE;

        ret = fdt_open(&ctx, image, capacity);
        if (ret != 0) {
            printf("FDT check failed %d!\n", ret);
            free(image);
            return ret;
        }

        /* display information */
        printf("FDT Version %d, Size %u\n",
            FDT_SUPPORTED_VERSION, (unsigned int)fdt_size(&ctx));
    }
    if (ret == 0 && gRunCorpus) {
        ret = run_corpus(image, (uint32_t)imageSz);
        free(image);
        printf("Return %d\n", ret);
        return ret;
    }
    if (ret == 0 && gEnableUnitTest) {
        ret = fdt_test(&ctx);
        if (ret == 0) {
            char outfilename[PATH_MAX];
            strncpy(outfilename, filename, sizeof(outfilename)-1);
            outfilename[sizeof(outfilename) - 1] = '\0';
            strncat(outfilename, ".out",
                sizeof(outfilename) - strlen(outfilename) - 1);

            /* save updated binary file */
            write_bin(outfilename, image, imageSz + UNIT_TEST_GROW_SIZE);
        }
    }
    if (ret == 0) {
        if (gParseFit) {
            ret = dts_parse_fit(&ctx);
        }
        else {
            ret = dts_parse(&ctx);
        }
    }
    free(image);

    printf("Return %d\n", ret);

    return ret;
}
