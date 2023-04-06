#ifndef TLB_AARCH64_H
#define TLB_AARCH64_H

#include <stdint.h>

/* Access permission and shareability */
#define ATTR_SH_IS               (0x3 << 8) /* Inner Shareable */
#define ATTR_SH_OS               (0x2 << 8) /* Outer Shareable */
#define ATTR_UXN                 (0x1 << 54) /* EL0 cannot execute */
#define ATTR_PXN                 (0x1 << 53) /* EL1 cannot execute */
#define ATTR_AF                  (0x1 << 10) /* Access Flag */
#define ATTR_AP_RW_PL1           (0x1 << 6) /* EL1 Read-Write */
#define ATTR_AP_RW_PL0           (0x0 << 6) /* EL0 Read-Write */
#define ATTR_AP_RO_PL1           (0x5 << 6) /* EL1 Read-Only */
#define ATTR_AP_RO_PL0           (0x4 << 6) /* EL0 Read-Only */
#define ATTR_NS                  (0x1 << 5) /* Non-secure */
#define ATTR_AP_RW               (ATTR_AP_RW_PL1 | ATTR_AP_RW_PL0)  

/* Memory attribute MAIR reg cfg */
#define ATTR_IDX_NORMAL_MEM      0
#define MAIR_ATTR_NORMAL_MEM     0xFF /* Normal, Write-Back, Read-Write-Allocate */
#define ATTR_IDX_DEVICE_MEM      1
#define MAIR_ATTR_DEVICE_MEM     0x04 /* Device-nGnRnE */

#define ATTRIBUTE_DEVICE        (ATTR_IDX_DEVICE_MEM << 2) | ATTR_AP_RW | ATTR_SH_IS
#define ATTRIBUTE_NORMAL_MEM    (ATTR_IDX_NORMAL_MEM << 2) | ATTR_AP_RW | ATTR_SH_IS


typedef struct {
    uint64_t virtual_base;
    uint64_t physical_base;
    uint64_t size;
    uint64_t attributes;
} memory_region_t;


void set_memory_attributes(uint32_t attr_idx, uint64_t mair_value);
void map_address(uint64_t virtual_address, uint64_t physical_address, uint64_t attributes);
void unmap_address(uint64_t virtual_address);
void invalidate_tlb_entry(uint64_t virtual_address);
void setup_ttbl(const memory_region_t *memory_layout, uint64_t memory_layout_size);

#endif