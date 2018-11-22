#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>
#include <target.h>

#define IMAGE_HEADER_SIZE 256
#define IMAGE_HEADER_OFFSET (2 * sizeof(uint32_t))

#define WOLFBOOT_MAGIC          0x464C4F57 /* WOLF */
#define WOLFBOOT_MAGIC_TRAIL    0x544F4F42 /* BOOT */

#define HDR_END         0x00
#define HDR_VERSION     0x01
#define HDR_TIMESTAMP   0x02
#define HDR_SHA256      0x03
#define HDR_PUBKEY      0x10
#define HDR_SIGNATURE   0x20
#define HDR_PADDING     0xFF

#define PART_BOOT   0
#define PART_UPDATE 1
#define PART_SWAP   2

#define IMG_STATE_NEW 0xFF
#define IMG_STATE_UPDATING 0x70
#define IMG_STATE_TESTING 0x10
#define IMG_STATE_SUCCESS 0x00

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
void wolfBoot_erase_partition(uint8_t part);
void wolfBoot_update_trigger(void);
void wolfBoot_success(void);

#endif /* IMAGE_H */
