/* fdt.h
 *
 * Flattened device tree (DTB) parser.
 *
 * Written from the Devicetree Specification v0.4, section 5 ("Flattened
 * Devicetree (DTB) Format"). The header field names below are the ones
 * the specification itself defines in section 5.2.
 *
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

#ifndef FDT_H
#define FDT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* On-disk format (Devicetree Specification v0.4, section 5)           */
/* ------------------------------------------------------------------ */

#define FDT_MAGIC 0xD00DFEEDUL

/* Only v17 blobs are accepted. v16 and earlier placed property values at
 * a different alignment; dtc has emitted v17 for many years and wolfBoot
 * has never had a target that needed the older layout. */
#define FDT_SUPPORTED_VERSION 17

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

#define FDT_HEADER_SIZE 40 /* sizeof(struct fdt_header), spec section 5.2 */

/* Byte offset of each header field, in spec order. Used to read/write
 * the header without depending on struct padding. */
#define FDT_H_MAGIC        0U
#define FDT_H_TOTALSIZE    4U
#define FDT_H_OFF_STRUCT   8U
#define FDT_H_OFF_STRINGS  12U
#define FDT_H_OFF_RSVMAP   16U
#define FDT_H_VERSION      20U
#define FDT_H_LAST_COMP    24U
#define FDT_H_BOOT_CPUID   28U
#define FDT_H_SIZE_STRINGS 32U
#define FDT_H_SIZE_STRUCT  36U

struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

#define FDT_RSV_ENTRY_SIZE 16 /* sizeof(struct fdt_reserve_entry) */

/* Structure block tokens, spec section 5.4.1 */
#define FDT_BEGIN_NODE 0x00000001UL
#define FDT_END_NODE   0x00000002UL
#define FDT_PROP       0x00000003UL
#define FDT_NOP        0x00000004UL
#define FDT_END        0x00000009UL

#define FDT_TAGSIZE     ((uint32_t)sizeof(uint32_t))
#define FDT_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define FDT_TAGALIGN(x) (FDT_ALIGN((x), FDT_TAGSIZE))

/* Deepest node nesting accepted. Real trees are well under 10 levels;
 * the cap keeps a hostile blob from driving an unbounded walk in a
 * consumer that tracks depth. */
#ifndef FDT_MAX_DEPTH
#define FDT_MAX_DEPTH 32
#endif

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

/* wolfBoot's own error set and ordering - most frequently tested first.
 *
 * The int-returning functions return 0 or a positive offset on success,
 * and the NEGATED value of one of these on failure. The pointer-returning
 * ones return NULL on failure, reporting the reason through their length
 * output where they have one (fdt_get_name, fdt_get_string, fdt_getprop,
 * fdt_getprop_by_offset). fdt_size() reports a size, not a status: a
 * context that is not open reads as 0. */
#define FDT_ERR_NOTFOUND     1  /* no such node, property or string */
#define FDT_ERR_EXISTS       2  /* node/property already present */
#define FDT_ERR_NOSPACE      3  /* would not fit in the caller's buffer */
#define FDT_ERR_BADOFFSET    4  /* offset does not name the expected tag */
#define FDT_ERR_BADARG       5  /* NULL or otherwise unusable argument */
#define FDT_ERR_BADMAGIC     6  /* header magic is not FDT_MAGIC */
#define FDT_ERR_BADVERSION   7  /* not a v17 blob */
#define FDT_ERR_BADLAYOUT    8  /* header block offsets/sizes inconsistent */
#define FDT_ERR_BADSTRUCTURE 9  /* structure block is malformed */
#define FDT_ERR_INTERNAL     10 /* should not happen; a bug if it does */

/* ------------------------------------------------------------------ */
/* PCI address cell flags (used by the NXP QorIQ dts fixups)           */
/* ------------------------------------------------------------------ */

#define FDT_PCI_PREFETCH (0x40000000)
#define FDT_PCI_MEM32    (0x02000000)
#define FDT_PCI_IO       (0x01000000)
#define FDT_PCI_MEM64    (0x03000000)

/* ------------------------------------------------------------------ */
/* Buffer sizing                                                       */
/* ------------------------------------------------------------------ */

/* Size of the DTB staging window at WOLFBOOT_LOAD_DTS_ADDRESS, and so
 * the capacity the boot paths hand to fdt_open() and hal_dts_fixup().
 * MIN is the v17 header size (also enforced by the signer).
 *
 * This is per-target, not a global ceiling: a target whose window is
 * under the 1 MiB default MUST override it (hal/nxp_ppc.h uses 64 KiB).
 * Too large is the dangerous direction - every parser bound is checked
 * against the capacity given, so an over-large value lets an in-place
 * fixup grow the blob past the real region. Pass a tighter bound
 * directly when one is known (a FIT sub-image's declared length, say). */
#ifndef WOLFBOOT_DTS_MAX_SIZE
#define WOLFBOOT_DTS_MAX_SIZE (1024U * 1024U)
#endif
#define WOLFBOOT_DTS_MIN_SIZE ((uint32_t)FDT_HEADER_SIZE)

/* Headroom (bytes) that fdt_grow() reserves before wolfBoot inserts
 * /chosen properties. Sized to comfortably hold a full LINUX_BOOTARGS
 * plus, when WOLFBOOT_FIT_RAMDISK is enabled, two 64-bit
 * linux,initrd-{start,end} cells with property-name overhead. A target
 * whose hal_dts_fixup() inserts more chosen entries can override this
 * with -DWOLFBOOT_FDT_FIXUP_HEADROOM=<bytes>. */
#ifndef WOLFBOOT_FDT_FIXUP_HEADROOM
#define WOLFBOOT_FDT_FIXUP_HEADROOM 768
#endif

/* ------------------------------------------------------------------ */
/* Parse context                                                       */
/* ------------------------------------------------------------------ */

/* A validated view of one blob, produced by fdt_open(). Offsets are held
 * in host byte order, known consistent and known to fit in `capacity`.
 * API node/property offsets are structure-block relative, so the root
 * node is always offset 0. */
struct fdt_ctx {
    uint8_t* blob;       /* base of the blob; NULL when closed */
    uint32_t capacity;   /* usable bytes at blob, supplied by the caller */
    uint32_t totalsize;  /* <= capacity */
    uint32_t off_rsv;    /* memory reservation block, 8-byte aligned */
    uint32_t off_struct; /* structure block, 4-byte aligned */
    uint32_t size_struct;
    uint32_t off_strings; /* strings block */
    uint32_t size_strings;
};
typedef struct fdt_ctx fdt_ctx;

/* ------------------------------------------------------------------ */
/* Byte order                                                          */
/* ------------------------------------------------------------------ */

uint32_t cpu_to_fdt32(uint32_t x);
uint64_t cpu_to_fdt64(uint64_t x);
uint32_t fdt32_to_cpu(uint32_t x);
uint64_t fdt64_to_cpu(uint64_t x);

/* ------------------------------------------------------------------ */
/* Open, size, resize                                                  */
/* ------------------------------------------------------------------ */

/* Validate `blob` and populate `ctx`. `capacity` is the bytes the caller
 * can safely address at `blob`, NOT the blob's own idea of its size:
 * every later bound is checked against it, so an over-large value
 * defeats the parser's memory safety. `blob` must be 4-byte aligned. A
 * property-less tree (empty strings block) is accepted.
 *
 * Returns 0, or a negative FDT_ERR_*. On failure ctx->blob is NULL. */
int fdt_open(fdt_ctx* ctx, void* blob, uint32_t capacity);

/* Declared totalsize from the header alone, for callers that must learn
 * how many bytes to fetch before they hold the whole blob (a two-step
 * read out of external flash, say). Checks magic, version and that
 * totalsize is in [WOLFBOOT_DTS_MIN_SIZE, WOLFBOOT_DTS_MAX_SIZE]; no
 * structural checks, so the complete blob still needs fdt_open(). */
int fdt_peek_size(const void* hdr, uint32_t hdr_len, uint32_t* totalsize);

/* Declared size of the blob (bytes). 0 if ctx is not open. */
uint32_t fdt_size(const fdt_ctx* ctx);

/* Record `extra` bytes of headroom past the tree's content in totalsize,
 * so a later stage sees a blob with room to grow. -FDT_ERR_NOSPACE if the
 * capacity cannot hold it. Mutations are bounded by capacity directly and
 * do not need this. */
int fdt_grow(fdt_ctx* ctx, uint32_t extra);

/* Shrink totalsize down to exactly the bytes the tree occupies. */
int fdt_shrink(fdt_ctx* ctx);

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

/* Next node in tree order; offset < 0 starts. Returns the offset, or
 * -FDT_ERR_NOTFOUND at the end of the tree or when the walk leaves the
 * subtree it started in.
 *
 * `depth`, if given, is incremented on entering a node and decremented on
 * leaving one, so it counts levels BELOW the starting point. Initialise it
 * to 0. Starting from offset < 0 the root is therefore reported at depth 1
 * and its children at depth 2; starting from a node offset, that node's
 * direct children are reported at depth 1, which is how fdt_subnode_offset()
 * distinguishes them from deeper descendants. */
int fdt_next_node(const fdt_ctx* ctx, int offset, int* depth);

/* Property iteration within one node. */
int fdt_first_property_offset(const fdt_ctx* ctx, int nodeoffset);
int fdt_next_property_offset(const fdt_ctx* ctx, int propoffset);

/* Value, name and length of the property at `propoffset`. Either output
 * pointer may be NULL. Returns the value, or NULL on error (in which
 * case *lenp, if given, holds the negative FDT_ERR_*). */
const void* fdt_getprop_by_offset(const fdt_ctx* ctx, int propoffset,
    const char** namep, int* lenp);

/* Node name. *len receives its length, or a negative FDT_ERR_* if the
 * offset is not a node. The root node's name is the empty string. */
const char* fdt_get_name(const fdt_ctx* ctx, int nodeoffset, int* len);

/* String from the strings block by its offset within that block. */
const char* fdt_get_string(const fdt_ctx* ctx, int stroffset, int* lenp);

/* Value of a named property, or NULL. *lenp receives the length, or a
 * negative FDT_ERR_* on failure. */
const void* fdt_getprop(const fdt_ctx* ctx, int nodeoffset,
    const char* name, int* lenp);

/* A 4- or 8-byte property interpreted as an address. NULL if absent or
 * not one of those two widths. */
void* fdt_getprop_address(const fdt_ctx* ctx, int nodeoffset,
    const char* name);

/* Resolve an absolute path such as "/" or "/soc/serial@21c0500". Path
 * components match either the full node name or the part before the
 * "@" unit address. Returns the node offset or a negative FDT_ERR_*. */
int fdt_path_offset(const fdt_ctx* ctx, const char* path);

/* Direct child of `parentoff` by name. */
int fdt_subnode_offset(const fdt_ctx* ctx, int parentoff, const char* name);

/* Search the whole tree from `startoff` (< 0 for the beginning) for an
 * exact node-name match, at any depth - prefer fdt_path_offset() when
 * the location is known. This and the two searches below report a
 * genuine no-match as exactly -FDT_ERR_NOTFOUND and never as some other
 * walk error, so an `off != -FDT_ERR_NOTFOUND` test cannot mistake one
 * for an offset. A NULL argument or closed ctx still gives
 * -FDT_ERR_BADARG. */
int fdt_find_node_offset(const fdt_ctx* ctx, int startoff,
    const char* nodename);

/* Search the whole tree for a node carrying `propname` with the exact
 * NUL-terminated string value `propval`. */
int fdt_find_prop_offset(const fdt_ctx* ctx, int startoff,
    const char* propname, const char* propval);

/* Shorthand for fdt_find_prop_offset(..., "device_type", devtype). */
int fdt_find_devtype(const fdt_ctx* ctx, int startoff, const char* devtype);

/* Search the whole tree for a node whose "compatible" string list
 * contains `compatible` as a complete entry. */
int fdt_node_offset_by_compatible(const fdt_ctx* ctx, int startoff,
    const char* compatible);

/* ------------------------------------------------------------------ */
/* Writing (in place)                                                  */
/* ------------------------------------------------------------------ */

/* Set (or add) a property. Bounded by ctx->capacity. */
int fdt_setprop(fdt_ctx* ctx, int nodeoffset, const char* name,
    const void* val, int len);

/* Add a child node. Returns its offset, -FDT_ERR_EXISTS if already
 * present, or another negative FDT_ERR_*. */
int fdt_add_subnode(fdt_ctx* ctx, int parentoff, const char* name);

/* Remove a node and its entire subtree. */
int fdt_del_node(fdt_ctx* ctx, int nodeoffset);

/* Append an entry to the memory reservation block. */
int fdt_add_mem_rsv(fdt_ctx* ctx, uint64_t address, uint64_t size);

/* Logging wrappers around fdt_setprop() used by the HAL dts fixups. */
int fdt_fixup_str(fdt_ctx* ctx, int off, const char* node, const char* name,
    const char* str);
int fdt_fixup_val(fdt_ctx* ctx, int off, const char* node, const char* name,
    uint32_t val);
int fdt_fixup_val64(fdt_ctx* ctx, int off, const char* node, const char* name,
    uint64_t val);

/* Write /chosen/linux,initrd-{start,end} as 64-bit big-endian values,
 * creating /chosen if needed. Returns 0 or a negative FDT_ERR_*. */
int fdt_fixup_initrd(fdt_ctx* ctx, uint64_t start, uint64_t size);

/* ------------------------------------------------------------------ */
/* Flattened uImage Tree (FIT)                                         */
/* ------------------------------------------------------------------ */

/* Boot configuration and the sub-image names it references. Output
 * pointers may be NULL. Returns the configuration name, or NULL.
 *
 * Sub-images resolve as direct children of /images, configurations as
 * direct children of /configurations. A tree-wide search by bare name is
 * used only when the FIT has no /images node at all, so a node planted
 * elsewhere cannot stand in for the real sub-image. */
const char* fit_find_images(fdt_ctx* ctx, const char** pkernel,
    const char** pflat_dt, const char** pramdisk, const char** pfpga);

/* A sub-image's "compatible" property, or NULL. NOTE: this returns the
 * raw property data, i.e. only the FIRST string of the list. To inspect
 * every entry, take the length from fdt_getprop() and walk the NUL
 * separators (fit_load_fpga() does that instead of using this). */
const char* fit_get_compatible(fdt_ctx* ctx, const char* image);

/* Stage a sub-image to its FIT-declared `load` address, decompressing
 * if it declares compression="gzip", and return its entry address. */
void* fit_load_image(fdt_ctx* ctx, const char* image, int* lenp);

/* As fit_load_image(), with an explicit ceiling on the decompressed
 * size at the FIT-declared destination. */
void* fit_load_image_ex(fdt_ctx* ctx, const char* image, int* lenp,
    uint32_t out_max);

/* Load (and, if compressed, decompress) a sub-image directly to a
 * caller-supplied destination, overriding the FIT image's `load` and
 * `entry` properties. dst is both the destination and the value
 * returned; dst_max bounds the decompressed size. */
void* fit_load_image_to(fdt_ctx* ctx, const char* image, void* dst,
    uint32_t dst_max, int* lenp);

#ifdef WOLFBOOT_FIT_RAMDISK
/* Load a FIT ramdisk sub-image (optionally relocated to
 * WOLFBOOT_LOAD_RAMDISK_ADDRESS) and patch /chosen/linux,initrd-* in
 * `dts`. Pass dts as NULL to skip the fixup. Returns 0 on success, -1
 * on load failure. Callers typically log and continue. */
int fit_load_ramdisk(fdt_ctx* ctx, const char* ramdisk_node, fdt_ctx* dts);
#endif

#ifdef WOLFBOOT_FPGA_BITSTREAM
/* Locate a FIT fpga sub-image, stage it and program the PL via
 * hal_fpga_load(). Returns 0 on success (including when fpga_node is
 * NULL), negative on failure. When WOLFBOOT_FPGA_NONFATAL is defined a
 * programming failure is logged and 0 is returned instead. */
int fit_load_fpga(fdt_ctx* ctx, const char* fpga_node);
#endif


/* ------------------------------------------------------------------ */
/* Malformed-blob corpus (test builds only)                            */
/* ------------------------------------------------------------------ */

#ifdef WOLFBOOT_FDT_CORPUS
/* Number of cases; short name and one-line description of each. */
int fdt_corpus_count(void);
const char* fdt_corpus_name(int idx);
const char* fdt_corpus_desc(int idx);

/* Build case idx, mutating the supplied known-good blob or synthesizing
 * one. Returns a malloc'd buffer of exactly *outlen bytes - so a read
 * past the declared size is a real heap overrun - or NULL. Caller frees. */
uint8_t* fdt_corpus_build(int idx, const uint8_t* base, uint32_t baselen,
    uint32_t* outlen);
#endif

#ifdef __cplusplus
}
#endif

#endif /* !FDT_H */
