#ifndef UNIT_TEST_ARM_CMSE_H
#define UNIT_TEST_ARM_CMSE_H

#include <stddef.h>
#include <stdint.h>

#define CMSE_NONSECURE 0

/* Provided by the unit test, so it can model a Secure region that must never
 * pass a non-secure attribution check. */
void *cmse_check_address_range(void *ptr, size_t size, int flags);

#endif
