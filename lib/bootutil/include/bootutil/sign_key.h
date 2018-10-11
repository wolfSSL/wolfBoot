#ifndef __BOOTUTIL_SIGN_KEY_H_
#define __BOOTUTIL_SIGN_KEY_H_

#include <stdint.h>

struct bootutil_key {
    const uint8_t *key;
    const unsigned int *len;
};

extern const struct bootutil_key bootutil_keys[];
extern const int bootutil_key_cnt;

#endif /* __BOOTUTIL_SIGN_KEY_H_ */
