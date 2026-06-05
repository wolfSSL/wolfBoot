/* unit-linux-loader-e820.c
 *
 * Regression test for F-4711: e820_add_entry_cb() must not write past the
 * fixed-size boot_params->e820_table[E820_MAX_ENTRIES_ZEROPAGE] when the FSP
 * HOB list supplies more resource descriptors than the table can hold.
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

/* More descriptors than the table can hold, to force the overflow path. */
#define N_ENTRIES 200
#define CANARY_LEN 2048
#define CANARY_BYTE 0xAA

/* boot_params followed by a canary region: any write past e820_table runs
 * through the rest of boot_params and into the canary. */
static struct {
    struct boot_params bp;
    uint8_t canary[CANARY_LEN];
} obj;

static uint8_t hoblist[(N_ENTRIES + 1) *
                       sizeof(struct efi_hob_resource_descriptor)];

int main(void)
{
    int i;
    uint8_t *p = hoblist;
    struct efi_hob_resource_descriptor *rd;
    struct efi_hob_generic_header *end;

    for (i = 0; i < N_ENTRIES; i++) {
        rd = (struct efi_hob_resource_descriptor *)p;
        memset(rd, 0, sizeof(*rd));
        rd->header.hob_type = EFI_HOB_TYPE_RESOURCE_DESCRIPTOR;
        rd->header.hob_length = sizeof(struct efi_hob_resource_descriptor);
        rd->resource_type = EFI_RESOURCE_SYSTEM_MEMORY;
        rd->physical_start = 0x4141414141414141ULL;
        rd->resource_length = 0x4242424242424242ULL;
        p += sizeof(struct efi_hob_resource_descriptor);
    }
    end = (struct efi_hob_generic_header *)p;
    end->hob_type = EFI_HOB_TYPE_END_OF_HOB_LIST;
    end->hob_length = sizeof(struct efi_hob_generic_header);

    memset(&obj, 0, sizeof(obj));
    memset(obj.canary, CANARY_BYTE, CANARY_LEN);

    (void)memory_map_from_hoblist(&obj.bp, (struct efi_hob *)hoblist);

    printf("e820_entries=%u\n", obj.bp.e820_entries);

    for (i = 0; i < CANARY_LEN; i++) {
        if (obj.canary[i] != CANARY_BYTE) {
            printf("FAIL: e820_table overflow corrupted memory at +%d\n", i);
            return 1;
        }
    }
    if (obj.bp.e820_entries > E820_MAX_ENTRIES_ZEROPAGE) {
        printf("FAIL: e820_entries=%u exceeds max %d\n",
               obj.bp.e820_entries, E820_MAX_ENTRIES_ZEROPAGE);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
