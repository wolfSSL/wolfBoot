/* bin-assemble.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *=============================================================================
 *
 * assemble binary parts based on address
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define BLOCK_SZ 1024

void usage(const char* execname)
{
    fprintf(stderr,
            "%s output [<address> <input>]\n"
            "assemble binary parts with addresses",
            execname);
    exit(1);
}

typedef struct {
    const char* fname;
    size_t      address;
    size_t      nbytes;
} binentry_t;

int binentry_address_compare(const void* a, const void* b)
{
    const binentry_t* ba = (const binentry_t*)a;
    const binentry_t* bb = (const binentry_t*)b;

    if (ba->address < bb->address) {
        return -1;
    } else if (ba->address > bb->address) {
        return 1;
    } else {
        return 0;
    }
}


int main(int argc, const char* argv[]) {
    const char* outname = NULL;
    size_t i = 0;
    size_t num_entries = 0;
    binentry_t* entries = NULL;
    size_t cur_add = 0;
    char fill = '\xff';
    size_t nr = 0;
    size_t nw = 0;
    char data[BLOCK_SZ];
    int err = 0;

    /* require at least 1 input file */
    if (argc < 4 || (argc % 2) != 0) {
        usage(argv[0]);
    }

    outname = argv[1];
    num_entries = (argc - 2) / 2;

    entries = malloc( sizeof(binentry_t) * num_entries);
    if (entries == NULL) {
        fprintf(stderr, "unable to allocate %zu entries\n;", num_entries);
        return EXIT_FAILURE;
    }

    for (i=0; i<num_entries; i++) {
        char* endptr = NULL;
        struct stat st;

        entries[i].address = strtol(argv[2*i + 2], &endptr, 0);
        if (*endptr) {
            fprintf(stderr,
                    "Remaining characters in adderss field %s\n", endptr);
        }
        entries[i].fname = argv[2*i + 3];

        if (stat(entries[i].fname, &st)) {
            fprintf(stderr, "unable to stat %s: %s\n", entries[i].fname,
                    strerror(errno));
            return EXIT_FAILURE;
        }

        entries[i].nbytes = st.st_size;

#if VERBOSE
        printf("%s %zu %zu\n",
               entries[i].fname,
               entries[i].address,
               entries[i].nbytes);
#endif
    }

    qsort(entries, num_entries, sizeof(binentry_t), binentry_address_compare);

    // check for overlap
    for (i=1; i<num_entries; i++) {
        size_t endaddr = entries[i-1].address + entries[i-1].nbytes;
        if (endaddr > entries[i].address) {
            fprintf(stderr,
              "overlap with %s(end address 0x%zx) and %s (start address 0x%zx \n",
                    entries[i-1].fname, endaddr,
                    entries[i].fname, entries[i].address);
            err = 1;
        }
        if (err) {
            return err;
        }

    }

    // TODO: consider handling stdout "-"

    FILE* fo = fopen(outname, "wb");
    if (fo == NULL){
        fprintf(stderr, "opening %s failed %s\n",
                outname, strerror(errno));
        return EXIT_FAILURE;
    }

    cur_add = entries[0].address;
    for (i=0; i<num_entries; i++) {
        FILE* fi = fopen(entries[i].fname, "rb");
        if (fi == NULL){
            fprintf(stderr, "opening %s failed %s\n",
                    entries[i].fname, strerror(errno));
            return EXIT_FAILURE;
        }

        /* fill until address */
        while(cur_add < entries[i].address) {
            nw = fwrite(&fill, 1, 1, fo);
            if (nw != 1) {
                fprintf(stderr,
                  "Failed to write fill bytes at 0x%zu\n",
                        cur_add);
                return EXIT_FAILURE;
            }

            cur_add++;
        }

        while (!feof(fi)) {
            nr = fread(data, 1, BLOCK_SZ, fi);
            nw = fwrite(data, 1, nr, fo);
            cur_add += nw;
            if (nr != nw) {
                fprintf(stderr,
                  "Failed to wrote %zu bytes of the %zu bytes read from %s\n",
                        nw, nr, entries[i].fname);
                return EXIT_FAILURE;
            }

            if (ferror(fi)) {
                fprintf(stderr,
                        "error reading from %s\n",
                        entries[i].fname);
                return EXIT_FAILURE;
            }

            if (ferror(fo)) {
                fprintf(stderr,
                        "error writing to %s\n",
                        entries[i].fname);
                return EXIT_FAILURE;
            }

        }


        fclose(fi);
    }

    fclose(fo);

    return 0;
}
