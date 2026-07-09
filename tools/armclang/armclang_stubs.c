/* armclang_stubs.c
 *
 * C library glue for building wolfBoot with the ARM Compiler for Embedded
 * (USE_ARMCLANG=1).
 *
 * - ARMCLANG_STUBS_MALLOC (secure image): heap allocator over the RAM_HEAP
 *   scatter region. We can't use the ARM C library malloc because it requires
 *   the C library runtime initialization, which never runs since wolfBoot
 *   enters at isr_reset.
 *
 * - ARMCLANG_STUBS_DEAD_REFS: trap stubs for symbols that are only
 *   referenced from sections that GNU ld garbage-collects; armlink needs
 *   the definitions to exist before its own unused section elimination.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef ARMCLANG_STUBS_MALLOC

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Renamed to the RAM_HEAP region symbols by the -D options in the Makefile */
extern unsigned int _start_heap;
extern unsigned int _heap_size;

struct heap_blk {
    size_t size;
    struct heap_blk *next;
};

#define HEAP_ALIGN(sz) (((sz) + 7U) & ~(size_t)7U)
#define HEAP_MIN_SPLIT (sizeof(struct heap_blk) + 8U)

static struct heap_blk *heap_free_list;
static int heap_initialized;

static void heap_init(void)
{
    heap_free_list = (struct heap_blk *)HEAP_ALIGN((uintptr_t)&_start_heap);
    heap_free_list->size = (size_t)((uintptr_t)&_start_heap
            + (uintptr_t)&_heap_size - (uintptr_t)heap_free_list)
            & ~(size_t)7U;
    heap_free_list->next = NULL;
    heap_initialized = 1;
}

void *malloc(size_t size)
{
    struct heap_blk *blk, **prev;

    if (!heap_initialized)
        heap_init();
    if (size == 0)
        return NULL;
    size = HEAP_ALIGN(size) + sizeof(struct heap_blk);

    for (prev = &heap_free_list; (blk = *prev) != NULL; prev = &blk->next) {
        if (blk->size < size)
            continue;
        if (blk->size - size >= HEAP_MIN_SPLIT) {
            struct heap_blk *rest = (struct heap_blk *)((uint8_t *)blk + size);
            rest->size = blk->size - size;
            rest->next = blk->next;
            blk->size = size;
            *prev = rest;
        } else {
            *prev = blk->next;
        }
        blk->next = NULL;
        return (uint8_t *)blk + sizeof(struct heap_blk);
    }
    return NULL;
}

void free(void *ptr)
{
    struct heap_blk *blk, *cur, **prev;

    if (ptr == NULL)
        return;
    blk = (struct heap_blk *)((uint8_t *)ptr - sizeof(struct heap_blk));

    /* Keep the free list sorted by address */
    for (prev = &heap_free_list; (cur = *prev) != NULL; prev = &cur->next) {
        if (cur > blk)
            break;
    }
    blk->next = cur;
    *prev = blk;

    /* Coalesce with the next and previous blocks when adjacent */
    if (cur != NULL && (uint8_t *)blk + blk->size == (uint8_t *)cur) {
        blk->size += cur->size;
        blk->next = cur->next;
    }
    if (prev != &heap_free_list) {
        struct heap_blk *before = (struct heap_blk *)((uint8_t *)prev
                - offsetof(struct heap_blk, next));
        if ((uint8_t *)before + before->size == (uint8_t *)blk) {
            before->size += blk->size;
            before->next = blk->next;
        }
    }
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr;
    size_t total = nmemb * size;

    if (size != 0 && total / size != nmemb)
        return NULL;
    ptr = malloc(total);
    if (ptr != NULL)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    void *new_ptr;
    struct heap_blk *blk;
    size_t old_payload;

    if (ptr == NULL)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    blk = (struct heap_blk *)((uint8_t *)ptr - sizeof(struct heap_blk));
    old_payload = blk->size - sizeof(struct heap_blk);
    if (old_payload >= size)
        return ptr;
    new_ptr = malloc(size);
    if (new_ptr != NULL) {
        memcpy(new_ptr, ptr, old_payload);
        free(ptr);
    }
    return new_ptr;
}

#endif /* ARMCLANG_STUBS_MALLOC */

#ifdef ARMCLANG_STUBS_DEAD_REFS

#define ARMCLANG_DEAD_REF_STUB(name) \
    void name(void); \
    void name(void) { while (1) ; }

ARMCLANG_DEAD_REF_STUB(wc_CryptKey)
ARMCLANG_DEAD_REF_STUB(Base64_Encode)
ARMCLANG_DEAD_REF_STUB(wc_PKCS12_PBKDF)

#endif /* ARMCLANG_STUBS_DEAD_REFS */
