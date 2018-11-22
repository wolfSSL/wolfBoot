#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>
#include <target.h>
#include <wolfboot/wolfboot.h>


#define SECT_FLAG_NEW 0x0F
#define SECT_FLAG_SWAPPING 0x07
#define SECT_FLAG_BACKUP 0x03
#define SECT_FLAG_UPDATED 0x00


struct wolfBoot_image {
    uint8_t *hdr;
    uint8_t *trailer;
    int hdr_ok;
    int signature_ok;
    int sha_ok;
    uint8_t *fw_base;
    uint32_t fw_size;
    uint8_t part;
};


int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part);
int wolfBoot_verify_integrity(struct wolfBoot_image *img);
int wolfBoot_verify_authenticity(struct wolfBoot_image *img);
int wolfBoot_set_partition_state(uint8_t part, uint8_t newst);
int wolfBoot_set_sector_flag(uint8_t part, uint8_t sector, uint8_t newflag);
int wolfBoot_get_partition_state(uint8_t part, uint8_t *st);
int wolfBoot_get_sector_flag(uint8_t part, uint8_t sector, uint8_t *flag);
int wolfBoot_copy(uint32_t src, uint32_t dst, uint32_t size);

#endif /* IMAGE_H */
