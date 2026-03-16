#ifndef WOLFBOOT_STORE_SBRK_H
#define WOLFBOOT_STORE_SBRK_H

#include <stdint.h>

void *wolfboot_store_sbrk(unsigned int incr, uint8_t **heap,
    uint8_t *heap_base, uint32_t heap_size);

#endif
