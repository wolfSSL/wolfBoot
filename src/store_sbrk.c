#include <stddef.h>

#include "store_sbrk.h"

void *wolfboot_store_sbrk(unsigned int incr, uint8_t **heap,
    uint8_t *heap_base, uint32_t heap_size)
{
    uint8_t *heap_limit = heap_base + heap_size;
    void *old_heap = *heap;

    if (((incr >> 2) << 2) != incr)
        incr = ((incr >> 2) + 1) << 2;

    if (*heap == NULL) {
        *heap = heap_base;
        old_heap = *heap;
    }

    if ((uint32_t)(heap_limit - *heap) < incr)
        return (void *)-1;

    *heap += incr;

    return old_heap;
}
