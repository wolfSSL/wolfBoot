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

/* linux_loader.c references the wolfBoot extent symbols from the linker
 * script. Provide a stand-in region for the host test; _end_wb is defined by
 * the Makefile via --defsym at _start_wolfboot + this size (1 MB). */
#define TEST_WB_SIZE 0x100000
uint8_t _start_wolfboot[TEST_WB_SIZE];

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

    /* ranges_overlap: adjacency does not overlap, one byte of overlap does,
     * and a zero-length range never overlaps. */
    if (ranges_overlap(0x1000, 0x1000, 0x2000, 0x1000) != 0)
        return fail("adjacent ranges reported as overlapping");
    if (ranges_overlap(0x1000, 0x1001, 0x2000, 0x1000) == 0)
        return fail("one-byte overlap missed");
    if (ranges_overlap(0x2000, 0, 0x2000, 0x1000) != 0)
        return fail("zero-length range reported as overlapping");

    /* linux_load_conflicts: the wolfBoot region is [_start_wolfboot, _end_wb);
     * a target inside it, or inside the payload, conflicts; one clear of both
     * does not. */
    {
        static uint8_t payload[0x1000];
        uint64_t wb = (uint64_t)(uintptr_t)_start_wolfboot;
        if (linux_load_conflicts(wb + 0x1000, 0x1000, payload,
                                 (uint32_t)sizeof(payload)) == 0)
            return fail("overlap with wolfBoot not caught");
        if (linux_load_conflicts((uint64_t)(uintptr_t)payload, 0x10, payload,
                                 (uint32_t)sizeof(payload)) == 0)
            return fail("overlap with payload not caught");
        if (linux_load_conflicts(0x1000, 0x1000, payload,
                                 (uint32_t)sizeof(payload)) != 0)
            return fail("false conflict for a clear target");
    }

    printf("PASS\n");
    return 0;
}
