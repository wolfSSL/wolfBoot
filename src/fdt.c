/* fdt.c
 *
 * Functions to help with flattened device tree (DTB) parsing
 *
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
 */

#if defined(MMU) && !defined(BUILD_LOADER_STAGE1)

#include "fdt.h"
#include "hal.h"
#include "printf.h"
#include "string.h"
#include <stdint.h>

uint32_t cpu_to_fdt32(uint32_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint32_t)__builtin_bswap32(x);
#endif
}
uint64_t cpu_to_fdt64(uint64_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint64_t)__builtin_bswap64(x);
#endif
}

uint32_t fdt32_to_cpu(uint32_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint32_t)__builtin_bswap32(x);
#endif
}
uint64_t fdt64_to_cpu(uint64_t x)
{
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return (uint64_t)__builtin_bswap64(x);
#endif
}

/* Internal Functions */
static inline const void *fdt_offset_ptr_(const void *fdt, int offset)
{
    return (const char*)fdt + fdt_off_dt_struct(fdt) + offset;
}
static inline int fdt_data_size_(void *fdt)
{
    /* the last portion of a FDT is the DT string, so use its offset and size to
     * determine total size */
    return fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);
}

static const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
{
    unsigned int uoffset = offset;
    unsigned int absoffset = offset + fdt_off_dt_struct(fdt);

    if (offset < 0) {
        return NULL;
    }
    if ((absoffset < uoffset)
        || ((absoffset + len) < absoffset)
        || (absoffset + len) > fdt_totalsize(fdt)) {
        return NULL;
    }
    if (fdt_version(fdt) >= 0x11) {
        if (((uoffset + len) < uoffset)
            || ((offset + len) > fdt_size_dt_struct(fdt))) {
            return NULL;
        }
    }
    return fdt_offset_ptr_(fdt, offset);
}

static uint32_t fdt_next_tag(const void *fdt, int startoffset, int *nextoffset)
{
    const uint32_t *tagp, *lenp;
    uint32_t tag;
    int offset = startoffset;
    const char *p;

    *nextoffset = -FDT_ERR_TRUNCATED;
    tagp = fdt_offset_ptr(fdt, offset, FDT_TAGSIZE);
    if (tagp == NULL) {
        return FDT_END; /* premature end */
    }
    tag = fdt32_to_cpu(*tagp);
    offset += FDT_TAGSIZE;

    *nextoffset = -FDT_ERR_BADSTRUCTURE;
    switch (tag) {
    case FDT_BEGIN_NODE:
        /* skip name */
        do {
            p = fdt_offset_ptr(fdt, offset++, 1);
        } while (p && (*p != '\0'));
        if (p == NULL)
            return FDT_END; /* premature end */
        break;

    case FDT_PROP:
        lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
        if (!lenp) {
            return FDT_END; /* premature end */
        }
        /* skip-name offset, length and value */
        offset += sizeof(struct fdt_property) - FDT_TAGSIZE
            + fdt32_to_cpu(*lenp);
        if (fdt_version(fdt) < 0x10 && fdt32_to_cpu(*lenp) >= 8 &&
            ((offset - fdt32_to_cpu(*lenp)) % 8) != 0) {
            offset += 4;
        }
        break;

    case FDT_END:
    case FDT_END_NODE:
    case FDT_NOP:
        break;

    default:
        return FDT_END;
    }

    if (!fdt_offset_ptr(fdt, startoffset, offset - startoffset)) {
        return FDT_END; /* premature end */
    }
    *nextoffset = FDT_TAGALIGN(offset);
    return tag;
}

static int fdt_check_node_offset_(const void *fdt, int offset)
{
    if ((offset < 0) || (offset % FDT_TAGSIZE)
        || (fdt_next_tag(fdt, offset, &offset) != FDT_BEGIN_NODE)) {
        return -FDT_ERR_BADOFFSET;
    }
    return offset;
}

static int fdt_check_prop_offset_(const void *fdt, int offset)
{
    if ((offset < 0) || (offset % FDT_TAGSIZE)
        || (fdt_next_tag(fdt, offset, &offset) != FDT_PROP)) {
        return -FDT_ERR_BADOFFSET;
    }
    return offset;
}

static int fdt_next_property_(const void *fdt, int offset)
{
    uint32_t tag;
    int nextoffset;

    do {
        tag = fdt_next_tag(fdt, offset, &nextoffset);

        switch (tag) {
        case FDT_END:
            if (nextoffset >= 0)
                return -FDT_ERR_BADSTRUCTURE;
            else
                return nextoffset;

        case FDT_PROP:
            return offset;
        }
        offset = nextoffset;
    } while (tag == FDT_NOP);

    return -FDT_ERR_NOTFOUND;
}

static const struct fdt_property *fdt_get_property(const void *fdt, int offset,
    const char *name, int *lenp, int *poffset)
{
    int namelen = (int)strlen(name);
    for (offset = fdt_first_property_offset(fdt, offset);
         offset >= 0;
         offset = fdt_next_property_offset(fdt, offset))
    {
        int slen, stroffset;
        const char *p;
        const struct fdt_property *prop =
            fdt_get_property_by_offset(fdt, offset, lenp);
        if (prop == NULL) {
            offset = -FDT_ERR_INTERNAL;
            break;
        }
        stroffset = fdt32_to_cpu(prop->nameoff);

        p = fdt_get_string(fdt, stroffset, &slen);
        if (p && (slen == namelen) && (memcmp(p, name, namelen) == 0)) {
            if (poffset)
                *poffset = offset;
            return prop;
        }
    }
    if (lenp) {
        *lenp = offset;
    }
    return NULL;
}

static void fdt_del_last_string_(void *fdt, const char *s)
{
    int newlen = strlen(s) + 1;
    fdt_set_size_dt_strings(fdt, fdt_size_dt_strings(fdt) - newlen);
}

static int fdt_splice_(void *fdt, void *splicepoint, int oldlen, int newlen)
{
    char *p, *end;
    p = splicepoint;
    end = (char*)fdt + fdt_data_size_(fdt);
    if (((p + oldlen) < p) || ((p + oldlen) > end)) {
        return -FDT_ERR_BADOFFSET;
    }
    if ((p < (char*)fdt) || ((end - oldlen + newlen) < (char*)fdt)) {
        return -FDT_ERR_BADOFFSET;
    }
    if ((end - oldlen + newlen) > ((char*)fdt + fdt_totalsize(fdt))) {
        return -FDT_ERR_NOSPACE;
    }
    memmove(p + newlen, p + oldlen, end - p - oldlen);
    return 0;
}

static int fdt_splice_struct_(void *fdt, void *p, int oldlen, int newlen)
{
    int err, delta;

    delta = newlen - oldlen;
    err = fdt_splice_(fdt, p, oldlen, newlen);
    if (err == 0) {
        fdt_set_size_dt_struct(fdt, fdt_size_dt_struct(fdt) + delta);
        fdt_set_off_dt_strings(fdt, fdt_off_dt_strings(fdt) + delta);
    }
    return err;
}

static int fdt_resize_property_(void *fdt, int nodeoffset, const char *name,
    int len, struct fdt_property **prop)
{
    int err, oldlen;

    *prop = (struct fdt_property*)(uintptr_t)
        fdt_get_property(fdt, nodeoffset, name, &oldlen, NULL);
    if (*prop != NULL) {
        err = fdt_splice_struct_(fdt, (*prop)->data, FDT_TAGALIGN(oldlen),
            FDT_TAGALIGN(len));
        if (err == 0) {
            (*prop)->len = cpu_to_fdt32(len);
        }
    }
    else {
        err = oldlen;
    }
    return err;
}

static int fdt_splice_string_(void *fdt, int newlen)
{
    int err;
    void *p = (char*)fdt + fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);

    if ((err = fdt_splice_(fdt, p, 0, newlen))) {
        return err;
    }
    fdt_set_size_dt_strings(fdt, fdt_size_dt_strings(fdt) + newlen);
    return 0;
}

static const char* fdt_find_string_(const char *strtab, int tabsize, const char *s)
{
    int len = strlen(s) + 1;
    const char *last = strtab + tabsize - len;
    const char *p;

    for (p = strtab; p <= last; p++) {
        if (memcmp(p, s, len) == 0) {
            return p;
        }
    }
    return NULL;
}

static int fdt_find_add_string_(void *fdt, const char *s, int *allocated)
{
    int err, len;
    char *strtab, *new;
    const char *p;

    strtab = (char*)fdt + fdt_off_dt_strings(fdt);
    len = strlen(s) + 1;
    *allocated = 0;
    p = fdt_find_string_(strtab, fdt_size_dt_strings(fdt), s);
    if (p) { /* found it */
        return (p - strtab);
    }
    new = strtab + fdt_size_dt_strings(fdt);
    err = fdt_splice_string_(fdt, len);
    if (err) {
        return err;
    }
    *allocated = 1;

    memcpy(new, s, len);
    return (new - strtab);
}

static int fdt_add_property_(void *fdt, int nodeoffset, const char *name,
    int len, struct fdt_property **prop)
{
    int err, proplen, nextoffset, namestroff, allocated;

    if ((nextoffset = fdt_check_node_offset_(fdt, nodeoffset)) < 0) {
        return nextoffset;
    }
    namestroff = fdt_find_add_string_(fdt, name, &allocated);
    if (namestroff < 0) {
        return namestroff;
    }

    *prop = (void*)(uintptr_t)fdt_offset_ptr_(fdt, nextoffset);
    proplen = sizeof(**prop) + FDT_TAGALIGN(len);

    err = fdt_splice_struct_(fdt, *prop, 0, proplen);
    if (err) {
        /* Delete the string if we failed to add it */
        if (allocated)
            fdt_del_last_string_(fdt, name);
        return err;
    }

    (*prop)->tag = cpu_to_fdt32(FDT_PROP);
    (*prop)->nameoff = cpu_to_fdt32(namestroff);
    (*prop)->len = cpu_to_fdt32(len);
    return 0;
}

/* return: 0=no match, 1=matched */
static int fdt_nodename_eq_(const void *fdt, int offset, const char *s,
    int len)
{
    const char *p = fdt_offset_ptr(fdt, offset + FDT_TAGSIZE, len+1);
    if (p == NULL || memcmp(p, s, len) != 0) {
        return 0;
    }
    if (p[len] == '\0') {
        return 1;
    } else if (!memchr(s, '@', len) && (p[len] == '@')) {
        return 1;
    }
    return 0;
}

static int fdt_subnode_offset_namelen(const void *fdt, int offset,
    const char *name, int namelen)
{
    int depth;
    for (depth = 0;
        (offset >= 0) && (depth >= 0);
         offset = fdt_next_node(fdt, offset, &depth))
    {
        if ((depth == 1) && fdt_nodename_eq_(fdt, offset, name, namelen)) {
            return offset;
        }
    }
    if (depth < 0) {
        return -FDT_ERR_NOTFOUND;
    }
    return offset; /* error */
}



/* Public Functions */
int fdt_check_header(const void *fdt)
{
    if (fdt_magic(fdt) == FDT_MAGIC) {
        if (fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
            return -FDT_ERR_BADVERSION;
        if (fdt_last_comp_version(fdt) > FDT_LAST_SUPPORTED_VERSION)
            return -FDT_ERR_BADVERSION;
    }
    else if (fdt_magic(fdt) == FDT_SW_MAGIC) {
        if (fdt_size_dt_struct(fdt) == 0)
            return -FDT_ERR_BADSTATE;
    }
    else {
        return -FDT_ERR_BADMAGIC;
    }
    return 0;
}

int fdt_next_node(const void *fdt, int offset, int *depth)
{
    int nextoffset = 0;
    uint32_t tag;

    if (offset >= 0) {
        if ((nextoffset = fdt_check_node_offset_(fdt, offset)) < 0)
            return nextoffset;
    }
    do {
        offset = nextoffset;
        tag = fdt_next_tag(fdt, offset, &nextoffset);

        switch (tag) {
        case FDT_PROP:
        case FDT_NOP:
            break;

        case FDT_BEGIN_NODE:
            if (depth)
                (*depth)++;
            break;

        case FDT_END_NODE:
            if (depth && ((--(*depth)) < 0))
                return nextoffset;
            break;

        case FDT_END:
            if ((nextoffset >= 0)
                || ((nextoffset == -FDT_ERR_TRUNCATED) && !depth))
                return -FDT_ERR_NOTFOUND;
            else
                return nextoffset;
        }
    } while (tag != FDT_BEGIN_NODE);

    return offset;
}

int fdt_first_property_offset(const void *fdt, int nodeoffset)
{
    int offset;
    if ((offset = fdt_check_node_offset_(fdt, nodeoffset)) < 0) {
        return offset;
    }
    return fdt_next_property_(fdt, offset);
}
int fdt_next_property_offset(const void *fdt, int offset)
{
    if ((offset = fdt_check_prop_offset_(fdt, offset)) < 0) {
        return offset;
    }
    return fdt_next_property_(fdt, offset);
}

const struct fdt_property *fdt_get_property_by_offset(const void *fdt,
    int offset, int *lenp)
{
    int err;
    const struct fdt_property *prop;

    if ((err = fdt_check_prop_offset_(fdt, offset)) < 0) {
        if (lenp) {
            *lenp = err;
        }
        return NULL;
    }
    prop = fdt_offset_ptr_(fdt, offset);
    if (lenp) {
        *lenp = fdt32_to_cpu(prop->len);
    }
    return prop;
}

const char* fdt_get_name(const void *fdt, int nodeoffset, int *len)
{
    int err;
    const struct fdt_node_header *nh = fdt_offset_ptr_(fdt, nodeoffset);
    int namelen = 0;
    const char* name = NULL;

    err = fdt_check_header(fdt);
    if (err == 0) {
        err = fdt_check_node_offset_(fdt, nodeoffset);
        if (err >= 0) {
            name = nh->name;
            namelen = strlen(nh->name);
        }
    }
    if (err < 0)
        namelen = err;
    if (len)
        *len = namelen;
    return name;
}

const char* fdt_get_string(const void *fdt, int stroffset, int *lenp)
{
    const char *s = (const char*)fdt + fdt_off_dt_strings(fdt) + stroffset;
    if (lenp) {
        *lenp = strlen(s);
    }
    return s;
}


int fdt_setprop(void *fdt, int nodeoffset, const char *name, const void *val,
    int len)
{
    int err = 0;
    void *prop_data;
    struct fdt_property *prop;

    err = fdt_totalsize(fdt); /* confirm size in header */
    if (err > 0) {
        err = fdt_resize_property_(fdt, nodeoffset, name, len, &prop);
        if (err == -FDT_ERR_NOTFOUND) {
            err = fdt_add_property_(fdt, nodeoffset, name, len, &prop);
        }
    }
    else {
        err = FDT_ERR_BADSTRUCTURE;
    }
    if (err == 0) {
        prop_data = prop->data;
        if (len > 0) {
            memcpy(prop_data, val, len);
        }
    }
    if (err != 0) {
        wolfBoot_printf("FDT: Set prop failed! %d (name %d, off %d)\n",
            err, name, nodeoffset);
    }
    return err;
}

const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name,
    int *lenp)
{
    int poffset;
    const struct fdt_property *prop = fdt_get_property(
        fdt, nodeoffset, name, lenp, &poffset);
    if (prop != NULL) {
        /* Handle alignment */
        if (fdt_version(fdt) < 0x10 &&
            (poffset + sizeof(*prop)) % 8 && fdt32_to_cpu(prop->len) >= 8) {
            return prop->data + 4;
        }
        return prop->data;
    }
    return NULL;
}

int fdt_find_devtype(void* fdt, int startoff, const char* node)
{
    int len, off;
    const void* val;
    const char* propname = "device_type";
    int nodelen = strlen(node)+1;

    for (off = fdt_next_node(fdt, startoff, NULL);
         off >= 0;
         off = fdt_next_node(fdt, off, NULL))
    {
        val = fdt_getprop(fdt, off, propname, &len);
        if (val && (len == nodelen) && (memcmp(val, node, len) == 0)) {
            return off;
        }
    }
    return off; /* return error from fdt_next_node() */
}

int fdt_node_offset_by_compatible(const void *fdt, int startoffset,
    const char *compatible)
{
    int offset;
    int complen = (int)strlen(compatible);
    for (offset = fdt_next_node(fdt, startoffset, NULL);
         offset >= 0;
         offset = fdt_next_node(fdt, offset, NULL))
    {
        int len;
        const char *prop = (const char*)fdt_getprop(fdt, offset, "compatible",
            &len);
        /* property list may contain multiple null terminated strings */
        while (prop != NULL && len >= complen) {
            const char* nextprop;
            if (memcmp(compatible, prop, complen+1) == 0) {
                return offset;
            }
            nextprop = memchr(prop, '\0', len);
            if (nextprop != NULL) {
                len -= (nextprop - prop) + 1;
                prop = nextprop + 1;
            }
        }
    }
    return offset;
}

int fdt_add_subnode(void* fdt, int parentoff, const char *name)
{
    int err;
    struct fdt_node_header *nh;
    int offset, nextoffset;
    int nodelen;
    uint32_t tag, *endtag;
    int namelen = (int)strlen(name);

    err = fdt_check_header(fdt);
    if (err != 0)
        return err;

    offset = fdt_subnode_offset_namelen(fdt, parentoff, name, namelen);
    if (offset >= 0)
        return -FDT_ERR_EXISTS;
    else if (offset != -FDT_ERR_NOTFOUND)
        return offset;

    /* Find the node after properties */
    /* skip the first node (BEGIN_NODE) */
    fdt_next_tag(fdt, parentoff, &nextoffset);
    do {
        offset = nextoffset;
        tag = fdt_next_tag(fdt, offset, &nextoffset);
    } while ((tag == FDT_PROP) || (tag == FDT_NOP));

    nh = (struct fdt_node_header*)fdt_offset_ptr_(fdt, offset);
    nodelen = sizeof(*nh) + FDT_TAGALIGN(namelen+1) + FDT_TAGSIZE;

    err = fdt_splice_struct_(fdt, nh, 0, nodelen);
    if (err == 0) {
        nh->tag = cpu_to_fdt32(FDT_BEGIN_NODE);
        memset(nh->name, 0, FDT_TAGALIGN(namelen+1));
        memcpy(nh->name, name, namelen);
        endtag = (uint32_t*)((char *)nh + nodelen - FDT_TAGSIZE);
        *endtag = cpu_to_fdt32(FDT_END_NODE);
        err = offset;
    }
    return err;
}


/* adjust the actual total size in the FDT header */
int fdt_shrink(void* fdt)
{
    uint32_t total_size = fdt_data_size_(fdt);
    return fdt_set_totalsize(fdt, total_size);
}

/* FTD Fixup API's */
int fdt_fixup_str(void* fdt, int off, const char* node, const char* name,
    const char* str)
{
    wolfBoot_printf("FDT: Set %s (%d), %s=%s\n", node, off, name, str);
    return fdt_setprop(fdt, off, name, str, strlen(str)+1);
}

int fdt_fixup_val(void* fdt, int off, const char* node, const char* name,
    uint32_t val)
{
    wolfBoot_printf("FDT: Set %s (%d), %s=%u\n", node, off, name, val);
    val = cpu_to_fdt32(val);
    return fdt_setprop(fdt, off, name, &val, sizeof(val));
}

int fdt_fixup_val64(void* fdt, int off, const char* node, const char* name,
    uint64_t val)
{
    wolfBoot_printf("FDT: Set %s (%d), %s=%llu\n",
        node, off, name, (unsigned long long)val);
    val = cpu_to_fdt64(val);
    return fdt_setprop(fdt, off, name, &val, sizeof(val));
}

#endif /* MMU && !BUILD_LOADER_STAGE1 */
