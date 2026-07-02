/* Minimal stand-in for the NXP MCUXpresso SDK header of the same name.
 * hal/mcxa.c only needs the flash config type and a status code from it;
 * the real SDK is not vendored in this source tree. */
#ifndef FSL_COMMON_STUB_H
#define FSL_COMMON_STUB_H

#include <string.h>
#include <stdint.h>

typedef int status_t;
#define kStatus_Success 0

typedef struct {
    int dummy;
} flash_config_t;

#endif /* FSL_COMMON_STUB_H */
