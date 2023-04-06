#include <stdint.h>
#include "ttbl_aarch64.h"



#define PAGE_TABLE_ENTRIES       512
#define PAGE_SIZE                0x1000                 /* 4KB */
#define ENTRY_MASK               0x0000FFFFFFFFF000
#define PAGE_DESC                (0x3 << 0)             /* Page Descriptor */



/* Page table buffer */
uint64_t page_table_l1[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
uint64_t page_table_l2[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
uint64_t page_table_l3[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

/* Set Memory attribues, currently only support EL3 */
void set_memory_attributes(uint32_t attr_idx, uint64_t mair_value) {
    uint64_t mair;

    asm volatile("mrs %0, mair_el3" : "=r"(mair));

    mair &= ~(0xFF << (attr_idx * 8));
    mair |= (mair_value << (attr_idx * 8));

    asm volatile("msr mair_el3, %0" :: "r"(mair));
}

/* Unmap virutal address */
void unmap_address(uint64_t virtual_address) {
    uint64_t index = (virtual_address >> 12) & (PAGE_TABLE_ENTRIES - 1);
    page_table_l1[index] = 0;
}


/* Map a virtual address to a physical address */
void map_address(uint64_t virtual_address, uint64_t physical_address, uint64_t attributes) {
    uint64_t index = (virtual_address >> 12) & (PAGE_TABLE_ENTRIES - 1);
    page_table_l1[index] = (physical_address & ENTRY_MASK) | attributes | PAGE_DESC;
}

/* Build MMU Translation Table  */
void setup_ttbl(const memory_region_t *memory_layout, uint64_t memory_layout_size) {

    /* Init page table */
    for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        page_table_l1[i] = 0;
    } 

    set_memory_attributes(ATTR_IDX_NORMAL_MEM, MAIR_ATTR_NORMAL_MEM);
    set_memory_attributes(ATTR_IDX_DEVICE_MEM, MAIR_ATTR_DEVICE_MEM);

    /* Confiugre MMU for memory region */
    for (uint64_t i = 0; i < memory_layout_size; ++i) {
        const memory_region_t *region = &memory_layout[i];

        /* Map Virtual address to physical addr */
        for (uint64_t addr = 0; addr < region->size; addr += PAGE_SIZE) {
            map_address(region->virtual_base + addr, region->physical_base + addr, region->attributes);
        }
    }
}

