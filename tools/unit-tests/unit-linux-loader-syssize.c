/* unit-linux-loader-syssize.c
 *
 * Regression test for F-4645: load_linux() computed the protected-mode kernel
 * size as the uint32_t product param.hdr.syssize * 16, which wraps for any
 * syssize > 0x0FFFFFFF. The wrapped value (0 or ~4 GiB) was passed straight to
 * memcpy() into KERNEL_LOAD_ADDRESS with no bounds check.
 *
 * linux_kernel_size() now performs the multiplication in 64-bit and rejects a
 * size that is zero or does not fit in the destination window
 * [KERNEL_LOAD_ADDRESS, load_limit). This test exercises that helper directly.
 *
 * Built for x86 32bit (the only target supported by linux_loader.c), without
 * the check framework, since 32bit libcheck is not generally available.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "x86/hob.h"
#include "x86/linux_loader.h"

#include "../../src/x86/hob.c"
#include "../../src/x86/linux_loader.c"

/* A generous low-memory window (256 MiB) used as the destination limit. */
#define LOAD_LIMIT 0x10000000u

int main(void)
{
    uint32_t ksz;
    int ret;

    /* The PoC overflow values: all wrap when computed as uint32_t. */
    const uint32_t wrap_cases[] = {
        0x10000000u, /* 32bit product == 0x00000000 (DoS) */
        0x1FFFFFFFu, /* 32bit product == 0xFFFFFFF0 (~4 GiB) */
        0xFFFFFFFFu, /* 32bit product == 0xFFFFFFF0 (~4 GiB) */
    };
    unsigned i;

    for (i = 0; i < sizeof(wrap_cases) / sizeof(wrap_cases[0]); i++) {
        uint32_t syssize = wrap_cases[i];

        /* Demonstrate the original wrap: the 32bit product no longer matches
         * the true 64bit size, which is exactly why a bound is required. */
        if ((uint32_t)(syssize * 16u) == (uint64_t)syssize * 16u) {
            printf("FAIL: case 0x%08x does not actually wrap\n", syssize);
            return 1;
        }

        ksz = 0xDEADBEEF;
        ret = linux_kernel_size(syssize, LOAD_LIMIT, &ksz);
        if (ret == 0) {
            printf("FAIL: overflowing syssize 0x%08x accepted (ksz=0x%08x)\n",
                   syssize, ksz);
            return 1;
        }
    }

    /* A size that exceeds the window (but does not wrap) must be rejected. */
    ret = linux_kernel_size(LOAD_LIMIT / 16u, LOAD_LIMIT, &ksz);
    if (ret == 0) {
        printf("FAIL: oversized kernel accepted\n");
        return 1;
    }

    /* Zero-sized kernel must be rejected. */
    if (linux_kernel_size(0, LOAD_LIMIT, &ksz) == 0) {
        printf("FAIL: zero-sized kernel accepted\n");
        return 1;
    }

    /* A legitimate kernel that fits must be accepted with the exact size. */
    ksz = 0;
    ret = linux_kernel_size(0x10000u, LOAD_LIMIT, &ksz);
    if (ret != 0 || ksz != 0x10000u * 16u) {
        printf("FAIL: valid kernel rejected (ret=%d ksz=0x%08x)\n", ret, ksz);
        return 1;
    }

    /* The largest kernel that exactly fills the window must be accepted. */
    ret = linux_kernel_size((LOAD_LIMIT - KERNEL_LOAD_ADDRESS) / 16u,
                            LOAD_LIMIT, &ksz);
    if (ret != 0 || ksz != (LOAD_LIMIT - KERNEL_LOAD_ADDRESS)) {
        printf("FAIL: exact-fit kernel rejected (ret=%d ksz=0x%08x)\n",
               ret, ksz);
        return 1;
    }

    /* load_limit == 0 is the non-FSP case ("no destination window bound"):
     * a legitimate kernel must still be accepted (no spurious panic) ... */
    ksz = 0;
    ret = linux_kernel_size(0x10000u, 0u, &ksz);
    if (ret != 0 || ksz != 0x10000u * 16u) {
        printf("FAIL: valid kernel rejected with no load_limit "
               "(ret=%d ksz=0x%08x)\n", ret, ksz);
        return 1;
    }

    /* ... while the wrapping cases must still be rejected even without a
     * window bound, since the 64-bit size exceeds the uint32_t kernel_size. */
    for (i = 0; i < sizeof(wrap_cases) / sizeof(wrap_cases[0]); i++) {
        if (linux_kernel_size(wrap_cases[i], 0u, &ksz) == 0) {
            printf("FAIL: overflowing syssize 0x%08x accepted with no "
                   "load_limit\n", wrap_cases[i]);
            return 1;
        }
    }

    /* Zero-sized kernel must be rejected even without a window bound. */
    if (linux_kernel_size(0, 0u, &ksz) == 0) {
        printf("FAIL: zero-sized kernel accepted with no load_limit\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
