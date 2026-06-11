#ifndef UNIT_TEST_ARM_CMSE_H
#define UNIT_TEST_ARM_CMSE_H

#include <stdint.h>

#define CMSE_NONSECURE 0
#define cmse_check_address_range(ptr, size, flags) \
    ((void *)(uintptr_t)(ptr))

#endif
