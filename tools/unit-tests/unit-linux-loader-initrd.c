/* unit-linux-loader-initrd.c
 *
 * Tests linux_initrd_place(), the pure helper that chooses the initrd load
 * address for the Linux bzImage loader. It must place the initrd as high as
 * possible, page-aligned, below the smaller of the kernel's initrd_addr_max and
 * the top of usable low RAM, without overlapping the kernel, and must never
 * underflow past its own fit check.
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

static int fail(const char *msg)
{
    printf("FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    uint64_t out;

    /* Zero size is rejected. */
    if (linux_initrd_place(0x7fffffffu, 0, 0, 0x100000u, &out) == 0)
        return fail("zero size accepted");

    /* Size larger than the limit is rejected (no underflow). */
    if (linux_initrd_place(0x00000fffu, 0, 0x2000u, 0x1000u, &out) == 0)
        return fail("size larger than limit accepted");

    /* Normal fit: placed as high as possible below initrd_addr_max+1,
     * page-aligned. limit 0x80000000 - 0x100000 -> 0x7ff00000. */
    out = 0;
    if (linux_initrd_place(0x7fffffffu, 0, 0x100000u, 0x200000u, &out) != 0)
        return fail("valid placement rejected");
    if (out != 0x7ff00000ull)
        return fail("wrong high placement");
    if ((out & 0xfffu) != 0)
        return fail("placement not page aligned");

    /* initrd_addr_max == 0 falls back to the pre-2.03 default 0x38000000. */
    out = 0;
    if (linux_initrd_place(0, 0, 0x1000u, 0x100000u, &out) != 0)
        return fail("default-max placement rejected");
    if (out != 0x37fff000ull)
        return fail("wrong default-max placement");

    /* ram_limit (tolum) below initrd_addr_max clamps the placement. */
    out = 0;
    if (linux_initrd_place(0x7fffffffu, 0x10000000ull, 0x1000u, 0x100000u,
                           &out) != 0)
        return fail("ram-limited placement rejected");
    if (out != 0x0ffff000ull)
        return fail("ram_limit not honoured");

    /* A placement that would land below the kernel end is rejected. */
    if (linux_initrd_place(0x00300000u, 0, 0x100000u, 0x00280000u, &out) == 0)
        return fail("kernel-overlapping placement accepted");

    /* Size exactly equal to the limit yields address 0, rejected by the
     * kernel-end floor. */
    if (linux_initrd_place(0x00000fffu, 0, 0x1000u, 0x1000u, &out) == 0)
        return fail("zero-address placement accepted");

    printf("PASS\n");
    return 0;
}
