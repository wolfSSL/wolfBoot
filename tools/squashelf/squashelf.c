/* squashelf.c
 *
 * ELF file squasher
 *
 * Run on HOST machine to preprocess (squash) ELF files for the wolfBoot flash
 * updater by extracting PT_LOAD segments, optionally filtering them based on
 * specified LMA ranges, sorting them by LMA, and writing them to a new,
 * reorganized ELF file. See README.md for more information.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>
#include <stdint.h>
#include <stdarg.h>  /* Needed for variadic macros */
#include <stdbool.h> /* Needed for bool type */
#include "../../include/elf.h"

/* Macro for verbose printing */
#define DEBUG_PRINT(fmt, ...)                    \
    do {                                         \
        if (verbose)                             \
            fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

/* Constants needed from libelf/gelf.h but not in wolfBoot's elf.h */
#define EI_NIDENT 16
#define SHN_UNDEF 0

/* Structure to store an address range */
typedef struct {
    uint64_t min;
    uint64_t max;
} AddressRange;

/*
 * comparePhdr:
 * qsort comparator ordering program headers by load address (p_paddr).
 * Sorts ascending so segments land in increasing memory order.
 */
static int comparePhdr32(const void* a, const void* b)
{
    const elf32_program_header* pa = a;
    const elf32_program_header* pb = b;
    if (pa->paddr < pb->paddr) {
        return -1;
    }
    if (pa->paddr > pb->paddr) {
        return 1;
    }
    return 0;
}

static int comparePhdr64(const void* a, const void* b)
{
    const elf64_program_header* pa = a;
    const elf64_program_header* pb = b;
    if (pa->paddr < pb->paddr) {
        return -1;
    }
    if (pa->paddr > pb->paddr) {
        return 1;
    }
    return 0;
}

/* Function to parse a single range string */
static int parseRange(const char* rangeStr, AddressRange* range)
{
    char* copyStr = strdup(rangeStr);
    if (!copyStr) {
        return 0;
    }

    /* Parse the range string (e.g., "0xA00000000-0xB0000000") */
    char* dashPos = strchr(copyStr, '-');
    if (!dashPos) {
        free(copyStr);
        return 0;
    }

    /* Split the string */
    *dashPos     = '\0';
    char* minStr = copyStr;
    char* maxStr = dashPos + 1;

    /* Check for hex or decimal format and convert */
    if (strncmp(minStr, "0x", 2) == 0 || strncmp(minStr, "0X", 2) == 0) {
        range->min = strtoull(minStr, NULL, 16);
    }
    else {
        range->min = strtoull(minStr, NULL, 10);
    }

    if (strncmp(maxStr, "0x", 2) == 0 || strncmp(maxStr, "0X", 2) == 0) {
        range->max = strtoull(maxStr, NULL, 16);
    }
    else {
        range->max = strtoull(maxStr, NULL, 10);
    }

    free(copyStr);

    if (range->min >= range->max) {
        return 0;
    }

    return 1;
}

/* Function to check if an address is within any of the specified ranges */
static bool isInRanges(uint64_t addr, AddressRange* ranges, int rangeCount)
{
    for (int i = 0; i < rangeCount; i++) {
        if (addr >= ranges[i].min && addr <= ranges[i].max) {
            return true;
        }
    }
    return false;
}

/* Function to check if two ranges overlap */
static bool rangesOverlap(const AddressRange* a, const AddressRange* b)
{
    return (a->min <= b->max && b->min <= a->max);
}

/* Function to check if any ranges in the array overlap */
static bool hasOverlappingRanges(AddressRange* ranges, int rangeCount)
{
    for (int i = 0; i < rangeCount; i++) {
        for (int j = i + 1; j < rangeCount; j++) {
            if (rangesOverlap(&ranges[i], &ranges[j])) {
                return true;
            }
        }
    }
    return false;
}

/* Function to parse range argument and populate ranges array */
static bool parseRangeArgument(const char* optarg, AddressRange** ranges,
                               int* rangeCount, int verbose)
{
    /* First, count the number of ranges (commas + 1) */
    const char* ptr = optarg;
    *rangeCount     = 1;
    while ((ptr = strchr(ptr, ',')) != NULL) {
        (*rangeCount)++;
        ptr++;
    }

    /* Allocate memory for ranges */
    *ranges = malloc(*rangeCount * sizeof(AddressRange));
    if (!*ranges) {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }

    /* Parse each range */
    char* rangeStr = strdup(optarg);
    if (!rangeStr) {
        fprintf(stderr, "Memory allocation failed\n");
        free(*ranges);
        *ranges = NULL;
        return false;
    }

    char* token;
    char* saveptr;
    int   currRange = 0;

    token = strtok_r(rangeStr, ",", &saveptr);
    while (token != NULL && currRange < *rangeCount) {
        if (!parseRange(token, &(*ranges)[currRange])) {
            fprintf(stderr, "Invalid range format in '%s'. Expected: min-max\n",
                    token);
            free(rangeStr);
            free(*ranges);
            *ranges = NULL;
            return false;
        }

        DEBUG_PRINT("Range %d: 0x%lx - 0x%lx\n", currRange + 1,
                    (*ranges)[currRange].min, (*ranges)[currRange].max);

        currRange++;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(rangeStr);

    if (currRange != *rangeCount) {
        fprintf(stderr, "Error parsing ranges\n");
        free(*ranges);
        *ranges = NULL;
        return false;
    }

    /* Check for overlapping ranges */
    if (hasOverlappingRanges(*ranges, *rangeCount)) {
        fprintf(stderr,
                "Warning: Address ranges contain overlapping regions.\n");
    }

    return true;
}

/* Function to print detailed help message */
static void printHelp(const char* programName)
{
    printf("Usage: %s [options] <input.elf> <output.elf>\n\n", programName);
    printf("Process ELF files by extracting PT_LOAD segments, optionally "
           "filtering them based on\n");
    printf("specified Load Memory Address (LMA) ranges, sorting them by LMA, "
           "and writing them to\n");
    printf("a new, reorganized ELF file.\n\n");

    printf("Options:\n");
    printf("  -n, --nosht                 Omit the Section Header Table (SHT) "
           "from the output ELF.\n");
    printf("                              By default, a minimal SHT with a "
           "single NULL section is created.\n");
    printf("                              Omitting the SHT shouldn't have any "
           "effect on loaders that only\n");
    printf("                              use PT_LOAD segments, but may cause "
           "tools like readelf to complain.\n");
    printf("                              Leave it in for max compatibility, "
           "or remove it for the smallest\n");
    printf("                              possible elf file.\n\n");

    printf("  -r, --range <min>-<max>[,<min>-<max>...]\n");
    printf("                              Specify one or more LMA ranges. Only "
           "PT_LOAD segments fully\n");
    printf("                              contained within any of these ranges "
           "(inclusive of min, exclusive\n");
    printf("                              of max) will be included in the "
           "output. Addresses can be provided\n");
    printf("                              in decimal or hexadecimal (using 0x "
           "prefix).\n");
    printf("                              Multiple ranges can be specified by "
           "separating them with commas.\n");
    printf("                              Example: -r "
           "0x10000-0x20000,0x30000-0x40000\n\n");

    printf("  -v, --verbose              Enable verbose output, providing "
           "detailed information about\n");
    printf("                              the processing steps, segment "
           "selection, and file operations.\n\n");

    printf("  -z, --zero-size-segments   Include segments with zero file size "
           "in the output.\n");
    printf("                              By default, these segments are "
           "excluded.\n\n");

    printf(
        "  -h, --help                 Display this help message and exit.\n\n");

    printf("Examples:\n");
    printf("  %s input.elf output.elf\n", programName);
    printf("      Extract all PT_LOAD segments, sort them by LMA, and write to "
           "output.elf\n\n");

    printf("  %s --nosht --range 0x80000000-0x8FFFFFFF input.elf "
           "output_filtered.elf\n",
           programName);
    printf("      Extract segments within the specified range and omit the "
           "SHT\n\n");

    printf("  %s -v --range 0x10000000-0x20000000,0x30000000-0x40000000 "
           "input.elf output_multi.elf\n",
           programName);
    printf("      Extract segments from multiple memory regions with verbose "
           "output\n\n");

    printf("  %s -v -z --range 0x10000000-0x20000000 input.elf "
           "output_with_zeros.elf\n",
           programName);
    printf("      Include zero-size segments and show detailed processing "
           "information\n\n");
}

/* Function to print usage message */
static void printUsage(const char* programName)
{
    fprintf(stderr,
            "Usage: %s [-n | --nosht] [-r | --range "
            "min-max[,min-max,...]] "
            "[-v | --verbose] [-z | --zero-size-segments] "
            "[-h | --help] "
            "<input.elf> <output.elf>\n",
            programName);
}

/* Read ELF header from file */
static bool read_elf_header(int fd, void* ehdr, int* elfClass, bool* is_elf32)
{
    uint8_t ident[EI_NIDENT];

    /* Read ELF identification bytes */
    if (pread(fd, ident, EI_NIDENT, 0) != EI_NIDENT) {
        perror("read ELF identification");
        return false;
    }

    /* Check if this is a valid ELF file */
    if (memcmp(ident, ELF_IDENT_STR, 4) != 0) {
        fprintf(stderr, "Not a valid ELF file\n");
        return false;
    }

    /* Determine ELF class (32 or 64 bit) */
    *elfClass = ident[ELF_CLASS_OFF];
    if (*elfClass != ELF_CLASS_32 && *elfClass != ELF_CLASS_64) {
        fprintf(stderr, "Unsupported ELF class: %d\n", *elfClass);
        return false;
    }

    *is_elf32 = (*elfClass == ELF_CLASS_32);

    /* Read the appropriate header based on class */
    if (*is_elf32) {
        elf32_header* hdr32 = (elf32_header*)ehdr;
        if (pread(fd, hdr32, sizeof(*hdr32), 0) != sizeof(*hdr32)) {
            perror("read ELF header");
            return false;
        }
    }
    else {
        elf64_header* hdr64 = (elf64_header*)ehdr;
        if (pread(fd, hdr64, sizeof(*hdr64), 0) != sizeof(*hdr64)) {
            perror("read ELF header");
            return false;
        }
    }

    return true;
}

/* Read a program header from file */
static bool read_program_header(int fd, void* phdr, bool is_elf32, size_t index,
                                off_t ph_offset, size_t ph_entsize)
{
    off_t offset = ph_offset + (index * ph_entsize);

    if (is_elf32) {
        elf32_program_header* ph32 = (elf32_program_header*)phdr;
        if (pread(fd, ph32, sizeof(*ph32), offset) != sizeof(*ph32)) {
            perror("read program header");
            return false;
        }
    }
    else {
        elf64_program_header* ph64 = (elf64_program_header*)phdr;
        if (pread(fd, ph64, sizeof(*ph64), offset) != sizeof(*ph64)) {
            perror("read program header");
            return false;
        }
    }

    return true;
}

int main(int argCount, char** argValues)
{
    bool          success          = false;
    int           noSht            = 0;
    int           hasRange         = 0;
    int           allowZeroSizeSeg = 0; /* New flag for zero-size segments */
    AddressRange* ranges           = NULL;
    int           rangeCount       = 0;
    const char*   inputFile        = NULL;
    const char*   outputFile       = NULL;
    int           verbose          = 0;
    int           opt;
    int           option_index = 0; /* For getopt_long */
    int           inputFd      = -1;
    int           outputFd     = -1;
    void**        data_buffers = NULL;
    size_t        loadCount    = 0;
    size_t        phdrCount    = 0;
    int           elfClass     = 0;
    bool          is_elf32     = false;
    void*         phdrs        = NULL;
    void*         outPhdrs     = NULL;

    /* Squash maybe uninitialized warnings introduced by -Wextra */
    phdrs = NULL;
    outPhdrs = NULL;

    /* Allocate memory for headers */
    union {
        elf32_header h32;
        elf64_header h64;
    } elfHeader;

    /* Define long options */
    static struct option long_options[] = {
        {"nosht", no_argument, 0, 'n'},       /* --nosht is equivalent to -n */
        {"range", required_argument, 0, 'r'}, /* --range is equivalent to -r */
        {"verbose", no_argument, 0, 'v'}, /* --verbose is equivalent to -v */
        {"zero-size-segments", no_argument, 0, 'z'}, /* --zero-size-segments */
        {"help", no_argument, 0, 'h'}, /* --help is equivalent to -h */
        {0, 0, 0, 0}};

    /* Use getopt_long to parse command-line options */
    optind = 1; /* Reset optind */
    while ((opt = getopt_long(argCount, argValues, "nr:vzh", long_options,
                              &option_index)) != -1) {
        switch (opt) {
            case 'n':
                noSht = 1;
                break;
            case 'r': {
                hasRange = 1;
                if (!parseRangeArgument(optarg, &ranges, &rangeCount,
                                        verbose)) {
                    return EXIT_FAILURE;
                }
            } break;
            case 'v':
                verbose = 1;
                break;
            case 'z':
                allowZeroSizeSeg = 1;
                break;
            case 'h':
                printHelp(argValues[0]);
                return EXIT_SUCCESS;
            case '?': /* getopt_long prints an error message */
                printUsage(argValues[0]);
                if (ranges) {
                    free(ranges);
                }
                return EXIT_FAILURE;
            default:
                /* Should not happen */
                if (ranges) {
                    free(ranges);
                }
                abort();
        }
    }

    /* Check for the correct number of positional arguments */
    if (optind + 2 != argCount) {
        printUsage(argValues[0]);
        if (ranges) {
            free(ranges);
        }
        return EXIT_FAILURE;
    }

    inputFile  = argValues[optind];
    outputFile = argValues[optind + 1];

    /* Print initial configuration if verbose */
    DEBUG_PRINT("Verbose mode enabled.\n");
    DEBUG_PRINT("Input file: %s\n", inputFile);
    DEBUG_PRINT("Output file: %s\n", outputFile);
    DEBUG_PRINT("No SHT: %s\n", noSht ? "yes" : "no");
    DEBUG_PRINT("Allow zero-size segments: %s\n",
                allowZeroSizeSeg ? "yes" : "no");
    if (hasRange) {
        DEBUG_PRINT("Range filter: %d ranges specified\n", rangeCount);
        for (int i = 0; i < rangeCount; i++) {
            DEBUG_PRINT("  Range %d: 0x%lx - 0x%lx\n", i + 1, ranges[i].min,
                        ranges[i].max);
        }
    }

    /* Open input ELF file for reading */
    inputFd = open(inputFile, O_RDONLY);
    if (inputFd < 0) {
        perror("open inputFile");
        goto cleanup;
    }
    DEBUG_PRINT("Opened input file: %s (fd: %d)\n", inputFile, inputFd);

    /* Read ELF header */
    if (!read_elf_header(inputFd, &elfHeader, &elfClass, &is_elf32)) {
        fprintf(stderr, "Failed to read ELF header\n");
        goto cleanup;
    }

    DEBUG_PRINT("Detected ELF class: %s\n", is_elf32 ? "ELF32" : "ELF64");

    /* Get program header count */
    if (is_elf32) {
        phdrCount = elfHeader.h32.ph_entry_count;
        DEBUG_PRINT("Read input ELF header. Program header count: %u\n",
                    elfHeader.h32.ph_entry_count);
    }
    else {
        phdrCount = elfHeader.h64.ph_entry_count;
        DEBUG_PRINT("Read input ELF header. Program header count: %u\n",
                    elfHeader.h64.ph_entry_count);
    }

    DEBUG_PRINT("Confirmed program header count: %zu\n", phdrCount);



    if (is_elf32) {
        phdrs = malloc(phdrCount * sizeof(elf32_program_header));
        if (!phdrs) {
            perror("malloc phdrs");
            goto cleanup;
        }
    }
    else {
        phdrs = malloc(phdrCount * sizeof(elf64_program_header));
        if (!phdrs) {
            perror("malloc phdrs");
            goto cleanup;
        }
    }

    /* Extract only PT_LOAD segments from the input PHT */
    for (size_t i = 0; i < phdrCount; i++) {
        union {
            elf32_program_header h32;
            elf64_program_header h64;
        } ph;

        if (!read_program_header(inputFd, &ph, is_elf32, i,
                                 is_elf32 ? elfHeader.h32.ph_offset
                                          : elfHeader.h64.ph_offset,
                                 is_elf32 ? elfHeader.h32.ph_entry_size
                                          : elfHeader.h64.ph_entry_size)) {
            continue;
        }

        uint32_t p_type = is_elf32 ? ph.h32.type : ph.h64.type;

        if (p_type == ELF_PT_LOAD) {
            uint64_t p_filesz = is_elf32 ? ph.h32.file_size : ph.h64.file_size;
            uint64_t p_paddr  = is_elf32 ? ph.h32.paddr : ph.h64.paddr;
            uint64_t p_memsz  = is_elf32 ? ph.h32.mem_size : ph.h64.mem_size;

            /* Skip segments with zero filesz unless explicitly allowed */
            if (p_filesz == 0 && !allowZeroSizeSeg) {
                DEBUG_PRINT("  Skipping segment %zu (LMA 0x%lx) - "
                            "zero filesz\n",
                            i, (unsigned long)p_paddr);
                continue;
            }

            /* Apply range filter if specified */
            if (hasRange) {
                uint64_t segmentStart = p_paddr;
                uint64_t segmentEnd   = p_paddr + p_memsz - 1;

                /* Check if segment start and end are both within any range */
                bool startInRange =
                    isInRanges(segmentStart, ranges, rangeCount);
                bool endInRange = isInRanges(segmentEnd, ranges, rangeCount);

                if (!startInRange || !endInRange) {
                    DEBUG_PRINT("  Skipping segment %zu (LMA 0x%lx - 0x%lx) - "
                                "outside specified ranges\n",
                                i, (unsigned long)segmentStart,
                                (unsigned long)segmentEnd);
                    continue;
                }
            }

            /* Add the segment to the loadable segments array */
            if (is_elf32) {
                elf32_program_header* ph32_array = (elf32_program_header*)phdrs;
                memcpy(&ph32_array[loadCount], &ph.h32,
                       sizeof(elf32_program_header));

                DEBUG_PRINT(
                    "  Keeping segment %zu (LMA 0x%lx, size 0x%lx/0x%lx, "
                    "offset 0x%lx, align %lu)\n",
                    i, (unsigned long)ph.h32.paddr,
                    (unsigned long)ph.h32.file_size,
                    (unsigned long)ph.h32.mem_size,
                    (unsigned long)ph.h32.offset, (unsigned long)ph.h32.align);
            }
            else {
                elf64_program_header* ph64_array = (elf64_program_header*)phdrs;
                memcpy(&ph64_array[loadCount], &ph.h64,
                       sizeof(elf64_program_header));

                DEBUG_PRINT(
                    "  Keeping segment %zu (LMA 0x%lx, size 0x%lx/0x%lx, "
                    "offset 0x%lx, align %lu)\n",
                    i, (unsigned long)ph.h64.paddr,
                    (unsigned long)ph.h64.file_size,
                    (unsigned long)ph.h64.mem_size,
                    (unsigned long)ph.h64.offset, (unsigned long)ph.h64.align);
            }

            loadCount++;
        }
        else {
            DEBUG_PRINT("  Skipping segment %zu (type %u)\n", i, p_type);
        }
    }

    DEBUG_PRINT("Found %zu PT_LOAD segments matching criteria.\n", loadCount);
    if (loadCount == 0) {
        fprintf(stderr, "No PT_LOAD segments found\n");
        goto cleanup;
    }

    /* Allocate memory for the output program headers */
    if (is_elf32) {
        /* Sort the loadable segments by their LMA (paddr) */
        qsort(phdrs, loadCount, sizeof(elf32_program_header), comparePhdr32);

        outPhdrs = malloc(loadCount * sizeof(elf32_program_header));
        if (!outPhdrs) {
            perror("malloc outPhdrs");
            goto cleanup;
        }
        memcpy(outPhdrs, phdrs, loadCount * sizeof(elf32_program_header));
    }
    else {
        /* Sort the loadable segments by their LMA (paddr) */
        qsort(phdrs, loadCount, sizeof(elf64_program_header), comparePhdr64);

        outPhdrs = malloc(loadCount * sizeof(elf64_program_header));
        if (!outPhdrs) {
            perror("malloc outPhdrs");
            goto cleanup;
        }
        memcpy(outPhdrs, phdrs, loadCount * sizeof(elf64_program_header));
    }

    DEBUG_PRINT("Sorted PT_LOAD segments by LMA.\n");

    /* Allocate storage for segment data */
    data_buffers = calloc(loadCount, sizeof(void*));
    if (!data_buffers) {
        perror("calloc data_buffers");
        goto cleanup;
    }

    /* Read segment data from input file */
    for (size_t i = 0; i < loadCount; i++) {
        uint64_t p_offset, p_filesz;

        if (is_elf32) {
            elf32_program_header* ph32_array = (elf32_program_header*)outPhdrs;
            p_offset                         = ph32_array[i].offset;
            p_filesz                         = ph32_array[i].file_size;
        }
        else {
            elf64_program_header* ph64_array = (elf64_program_header*)outPhdrs;
            p_offset                         = ph64_array[i].offset;
            p_filesz                         = ph64_array[i].file_size;
        }

        if (p_filesz > 0) {
            data_buffers[i] = malloc(p_filesz);
            if (!data_buffers[i]) {
                perror("malloc segment buffer");
                goto cleanup;
            }

            /* Read the data */
            ssize_t bytes_read =
                pread(inputFd, data_buffers[i], p_filesz, p_offset);
            if (bytes_read < 0) {
                perror("pread segment data");
                goto cleanup;
            }
            else if ((size_t)bytes_read != p_filesz) {
                fprintf(stderr,
                        "Short read for segment %zu (expected %lu, got %zd)\n",
                        i, (unsigned long)p_filesz, bytes_read);
                goto cleanup;
            }

            DEBUG_PRINT("Read %zu bytes for segment %zu\n", (size_t)p_filesz,
                        i);
        }
    }

    /* Open output file for writing */
    outputFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outputFd < 0) {
        perror("open outputFile");
        goto cleanup;
    }
    DEBUG_PRINT("Opened output file: %s (fd: %d)\n", outputFile, outputFd);

    /*
     * Now we manually construct the output ELF file in this format:
     * [ELF header][Program Header Table][Loadable Segments][Optional Section
     * Header Table]
     */

    /* Step 1: Calculate file layout */
    size_t ehdr_size = (is_elf32) ? sizeof(elf32_header) : sizeof(elf64_header);
    size_t phdr_size = (is_elf32) ? sizeof(elf32_program_header)
                                  : sizeof(elf64_program_header);

    /* Calculate PHT offset and size */
    size_t pht_offset = ehdr_size;
    size_t pht_size   = loadCount * phdr_size;

    /* Calculate segment offsets and update PHDRs */
    size_t current_offset = pht_offset + pht_size;

    /* No forced global alignment - we'll respect each segment's individual
     * alignment */
    DEBUG_PRINT("Starting segment layout at offset: 0x%lx\n", current_offset);

    /* Update segment offsets */
    for (size_t i = 0; i < loadCount; i++) {
        uint64_t p_align, p_filesz;

        if (is_elf32) {
            elf32_program_header* ph32_array = (elf32_program_header*)outPhdrs;
            p_align                          = ph32_array[i].align;

            /* Align the segment according to its alignment requirement if
             * needed */
            if (p_align > 1) {
                current_offset =
                    (current_offset + p_align - 1) & ~(p_align - 1);
            }

            /* Update the segment's offset */
            ph32_array[i].offset = current_offset;
            p_filesz             = ph32_array[i].file_size;

            DEBUG_PRINT("  Segment %zu offset: 0x%lx\n", i,
                        (unsigned long)current_offset);
        }
        else {
            elf64_program_header* ph64_array = (elf64_program_header*)outPhdrs;
            p_align                          = ph64_array[i].align;

            /* Align the segment according to its alignment requirement if
             * needed */
            if (p_align > 1) {
                current_offset =
                    (current_offset + p_align - 1) & ~(p_align - 1);
            }

            /* Update the segment's offset */
            ph64_array[i].offset = current_offset;
            p_filesz             = ph64_array[i].file_size;

            DEBUG_PRINT("  Segment %zu offset: 0x%lx\n", i,
                        (unsigned long)current_offset);
        }

        /* Move to next position */
        current_offset += p_filesz;
    }

    /* Calculate SHT offset if needed */
    size_t sht_offset = 0;
    if (!noSht) {
        /* Align SHT to 8-byte boundary */
        current_offset = (current_offset + 7) & ~7;
        sht_offset     = current_offset;
    }

    /* Step 2: Prepare and write ELF header */
    /* Update header fields */
    if (is_elf32) {
        elfHeader.h32.ph_offset      = pht_offset;
        elfHeader.h32.ph_entry_count = loadCount;

        if (noSht) {
            elfHeader.h32.sh_offset      = 0;
            elfHeader.h32.sh_entry_count = 0;
            elfHeader.h32.sh_str_index   = SHN_UNDEF;
        }
        else {
            elfHeader.h32.sh_offset      = sht_offset;
            elfHeader.h32.sh_entry_count = 1; /* Just the NULL section */
            elfHeader.h32.sh_str_index   = SHN_UNDEF;
        }

        /* Write ELF header to output file */
        if (write(outputFd, &elfHeader.h32, sizeof(elfHeader.h32)) !=
            sizeof(elfHeader.h32)) {
            perror("write ELF header");
            goto cleanup;
        }
    }
    else {
        elfHeader.h64.ph_offset      = pht_offset;
        elfHeader.h64.ph_entry_count = loadCount;

        if (noSht) {
            elfHeader.h64.sh_offset      = 0;
            elfHeader.h64.sh_entry_count = 0;
            elfHeader.h64.sh_str_index   = SHN_UNDEF;
        }
        else {
            elfHeader.h64.sh_offset      = sht_offset;
            elfHeader.h64.sh_entry_count = 1; /* Just the NULL section */
            elfHeader.h64.sh_str_index   = SHN_UNDEF;
        }

        /* Write ELF header to output file */
        if (write(outputFd, &elfHeader.h64, sizeof(elfHeader.h64)) !=
            sizeof(elfHeader.h64)) {
            perror("write ELF header");
            goto cleanup;
        }
    }

    DEBUG_PRINT("Wrote ELF header to output file.\n");

    /* Step 3: Write Program Header Table */
    if (is_elf32) {
        elf32_program_header* ph32_array = (elf32_program_header*)outPhdrs;

        for (size_t i = 0; i < loadCount; i++) {
            if (write(outputFd, &ph32_array[i], sizeof(ph32_array[i])) !=
                sizeof(ph32_array[i])) {
                perror("write program header");
                goto cleanup;
            }
        }
    }
    else {
        elf64_program_header* ph64_array = (elf64_program_header*)outPhdrs;

        for (size_t i = 0; i < loadCount; i++) {
            if (write(outputFd, &ph64_array[i], sizeof(ph64_array[i])) !=
                sizeof(ph64_array[i])) {
                perror("write program header");
                goto cleanup;
            }
        }
    }

    DEBUG_PRINT("Wrote Program Header Table (%zu entries).\n", loadCount);

    /* Step 4: Write segment data */
    for (size_t i = 0; i < loadCount; i++) {
        uint64_t p_offset, p_filesz;

        if (is_elf32) {
            elf32_program_header* ph32_array = (elf32_program_header*)outPhdrs;
            p_offset                         = ph32_array[i].offset;
            p_filesz                         = ph32_array[i].file_size;
        }
        else {
            elf64_program_header* ph64_array = (elf64_program_header*)outPhdrs;
            p_offset                         = ph64_array[i].offset;
            p_filesz                         = ph64_array[i].file_size;
        }

        /* Seek to the offset where this segment should be written */
        if (lseek(outputFd, p_offset, SEEK_SET) != (off_t)p_offset) {
            perror("lseek to segment offset");
            goto cleanup;
        }

        /* Skip segments with zero file size */
        if (p_filesz == 0) {
            DEBUG_PRINT("  Segment %zu has zero filesz, skipping data write\n",
                        i);
            continue;
        }

        /* Write the segment data */
        ssize_t bytes_written = write(outputFd, data_buffers[i], p_filesz);
        if (bytes_written < 0) {
            perror("write segment data");
            goto cleanup;
        }
        else if ((size_t)bytes_written != p_filesz) {
            fprintf(stderr,
                    "Short write for segment %zu (expected %lu, wrote %zd)\n",
                    i, (unsigned long)p_filesz, bytes_written);
            goto cleanup;
        }

        DEBUG_PRINT("  Wrote segment %zu data (0x%lx bytes at offset 0x%lx)\n",
                    i, (unsigned long)p_filesz, (unsigned long)p_offset);
    }

    /* Step 5: Write Section Header Table if not using --nosht */
    if (!noSht) {
        /* Seek to the Section Header Table offset */
        if (lseek(outputFd, sht_offset, SEEK_SET) != (off_t)sht_offset) {
            perror("lseek to SHT offset");
            goto cleanup;
        }

        /* Write a NULL section header (all zeros) */
        if (is_elf32) {
            elf32_section_header shdr32 = {0}; /* All zeros for NULL section */
            if (write(outputFd, &shdr32, sizeof(shdr32)) != sizeof(shdr32)) {
                perror("write NULL section header");
                goto cleanup;
            }
        }
        else {
            elf64_section_header shdr64 = {0}; /* All zeros for NULL section */
            if (write(outputFd, &shdr64, sizeof(shdr64)) != sizeof(shdr64)) {
                perror("write NULL section header");
                goto cleanup;
            }
        }

        DEBUG_PRINT("Wrote NULL section header at offset 0x%lx\n", sht_offset);
    }

    DEBUG_PRINT("Successfully wrote output ELF file.\n");

    success = true;

cleanup:
    /* Clean up resources */
    if (data_buffers) {
        for (size_t i = 0; i < loadCount; i++) {
            free(data_buffers[i]);
        }
        free(data_buffers);
    }
    if (phdrs) {
        free(phdrs);
    }
    if (outPhdrs) {
        free(outPhdrs);
    }
    if (inputFd >= 0) {
        close(inputFd);
    }
    if (outputFd >= 0) {
        close(outputFd);
    }
    if (ranges) {
        free(ranges);
    }

    return (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
