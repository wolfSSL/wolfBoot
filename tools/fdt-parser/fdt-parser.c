/* fdt-parser.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

static int gEnableUnitTest = 0;
#define UNIT_TEST_GROW_SIZE 1024

/* Test case for "nxp_t1024.dtb" */
static int fdt_test(void* fdt)
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

    /* expand total size to allow growth */
    oldsize = fdt_totalsize(fdt);
    fdt_set_totalsize(fdt, oldsize + UNIT_TEST_GROW_SIZE);

    /* fixup the memory region - single bank */
    off = fdt_find_devtype(fdt, -1, "memory");
    if (off != -FDT_ERR_NOTFOUND) {
        /* build addr/size as 64-bit */
        uint8_t ranges[sizeof(uint64_t) * 2], *p = ranges;
        *(uint64_t*)p = cpu_to_fdt64(DDR_ADDRESS);
        p += sizeof(uint64_t);
        *(uint64_t*)p = cpu_to_fdt64(DDR_SIZE);
        p += sizeof(uint64_t);
        ret = fdt_setprop(fdt, off, "reg", ranges, (int)(p - ranges));
        if (ret != 0) goto exit;
        wolfBoot_printf("FDT: Set memory, start=0x%x, size=0x%x\n",
            DDR_ADDRESS, (uint32_t)DDR_SIZE);
    }

    /* fixup CPU status and, release address and enable method */
    off = fdt_find_devtype(fdt, -1, "cpu");
    while (off != -FDT_ERR_NOTFOUND) {
        int core;
        uint64_t core_spin_table_addr;

        reg = (uint32_t*)fdt_getprop(fdt, off, "reg", NULL);
        if (reg == NULL)
            break;
        core = (int)fdt32_to_cpu(*reg);
        if (core >= CPU_NUMCORES) {
            break; /* invalid core index */
        }

        /* calculate location of spin table for core */
        core_spin_table_addr = (uint64_t)((uintptr_t)(
            SPIN_TABLE_ADDR + (core * ENTRY_SIZE)));

        ret = fdt_fixup_str(fdt, off, "cpu", "status", (core == 0) ? "okay" : "disabled");
        if (ret == 0)
            ret = fdt_fixup_val64(fdt, off, "cpu", "cpu-release-addr", core_spin_table_addr);
        if (ret == 0)
            ret = fdt_fixup_str(fdt, off, "cpu", "enable-method", "spin-table");
        if (ret == 0)
            ret = fdt_fixup_val(fdt, off, "cpu", "timebase-frequency", TIMEBASE_HZ);
        if (ret == 0)
            ret = fdt_fixup_val(fdt, off, "cpu", "clock-frequency", PLAT_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(fdt, off, "cpu", "bus-frequency", PLAT_CLK);
        if (ret != 0) goto exit;

        off = fdt_find_devtype(fdt, off, "cpu");
    }

    /* fixup the soc clock */
    off = fdt_find_devtype(fdt, -1, "soc");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(fdt, off, "soc", "bus-frequency", PLAT_CLK);
        if (ret != 0) goto exit;
    }

    /* fixup the serial clocks */
    off = fdt_find_devtype(fdt, -1, "serial");
    while (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(fdt, off, "serial", "clock-frequency", BUS_CLK);
        if (ret != 0) goto exit;
        off = fdt_find_devtype(fdt, off, "serial");
    }

    /* fixup the QE bridge and bus blocks */
    off = fdt_find_devtype(fdt, -1, "qe");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(fdt, off, "qe", "clock-frequency", BUS_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(fdt, off, "qe", "bus-frequency", BUS_CLK);
        if (ret == 0)
            ret = fdt_fixup_val(fdt, off, "qe", "brg-frequency", BUS_CLK/2);
        if (ret != 0) goto exit;
    }

    /* fixup the LIODN */
    for (i=0; i<(int)(sizeof(liodn_tbl)/sizeof(struct liodn_id_table)); i++) {
        off = fdt_node_offset_by_compatible(fdt, -1, liodn_tbl[i].compat);
        if (off >= 0) {
            ret = fdt_fixup_val(fdt, off, liodn_tbl[i].compat, "fsl,liodn",
                liodn_tbl[i].id);
            if (ret != 0) goto exit;
        }
    }

    /* fixup the QMAN portals */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,qman-portal");
    while (off != -FDT_ERR_NOTFOUND) {
        const int *ci = fdt_getprop(fdt, off, "cell-index", NULL);
        uint32_t liodns[2];
        if (!ci)
            break;
        i = fdt32_to_cpu(*ci);

        liodns[0] = qp_info[i].dliodn;
        liodns[1] = qp_info[i].fliodn;
        wolfBoot_printf("FDT: Set %s@%d (%d), %s=%d,%d\n",
            "qman-portal", i, off, "fsl,liodn", liodns[0], liodns[1]);
        ret = fdt_setprop(fdt, off, "fsl,liodn", liodns, sizeof(liodns));
        if (ret != 0) goto exit;

        off = fdt_node_offset_by_compatible(fdt, off, "fsl,qman-portal");
    }

    /* mpic clock */
    off = fdt_find_devtype(fdt, -1, "open-pic");
    if (off != -FDT_ERR_NOTFOUND) {
        ret = fdt_fixup_val(fdt, off, "open-pic", "clock-frequency", BUS_CLK);
        if (ret != 0) goto exit;
    }

    /* resize the device tree */
    fdt_shrink(fdt);

    /* display information */
    printf("FDT Updated: Size %d -> %d\n", oldsize, fdt_totalsize(fdt));

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

int dts_parse(void* dts_addr)
{
    int ret = 0;
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    const struct fdt_property* prop;
    int nlen, plen, slen;
    int noff, poff, soff;
    const char* nstr = NULL, *pstr = NULL;
    int depth = 0;
    #define MAX_DEPTH 24
    char tabs[MAX_DEPTH+1] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

    /* check header */
    ret = fdt_check_header(fdt);
    if (ret != 0) {
        printf("FDT check failed %d!\n", ret);
        return ret;
    }

    /* display information */
    printf("FDT Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* walk tree */
    for (noff = fdt_next_node(fdt, -1, &depth);
         noff >= 0;
         noff = fdt_next_node(fdt, noff, &depth))
    {
        nstr = fdt_get_name(fdt, noff, &nlen);
        if (depth > MAX_DEPTH)
            depth = MAX_DEPTH;

        if (nlen == 0 && depth == 1)
            nstr = "root";
        printf("%s%s (node offset %d, depth %d, len %d):\n",
            &tabs[MAX_DEPTH-depth+1], nstr, noff, depth, nlen);

        for (poff = fdt_first_property_offset(fdt, noff);
            poff >= 0;
            poff = fdt_next_property_offset(fdt, poff))
        {
            prop = fdt_get_property_by_offset(fdt, poff, &plen);
            if (prop != NULL) {
                soff = fdt32_to_cpu(prop->nameoff);
                pstr = fdt_get_string(fdt, soff, &slen);

                printf("%s%s (prop offset %d, len %d): ",
                    &tabs[MAX_DEPTH-depth], pstr, poff, plen);
                if (plen > 32)
                    printf("\n%s", &tabs[MAX_DEPTH-depth-1]);
                print_bin((const uint8_t*)prop->data, plen);
                printf("\n");
            }
        }
    }

    return ret;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    uint8_t *image = NULL;
    size_t imageSz = 0;
    const char* filename = NULL;

    if (argc >= 2) {
        filename = argv[1];
    }
    while (argc > 2) {
        if (strcmp(argv[argc-1], "-t") == 0) {
            gEnableUnitTest = 1;
        }
        argc--;
    }

    printf("FDT Parser (%s):\n", filename);
    if (filename == NULL) {
        printf("Usage: fdt-parser [filename.dtb]\n");
        return 0;
    }

    ret = load_file(filename, &image, &imageSz);
    if (ret == 0 && gEnableUnitTest) {
        ret = fdt_test(image);
        if (ret == 0) {
            char outfilename[PATH_MAX];
            strncpy(outfilename, filename, sizeof(outfilename)-1);
            strncat(outfilename, ".out",   sizeof(outfilename)-1);

            /* save updated binary file */
            write_bin(outfilename, image, imageSz + UNIT_TEST_GROW_SIZE);
        }
    }
    if (ret == 0) {
        ret = dts_parse(image);
    }
    free(image);

    printf("Return %d\n", ret);

    return ret;
}
