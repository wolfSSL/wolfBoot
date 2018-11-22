#ifndef WOLFBOOT_H
#define WOLFBOOT_H
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

void wolfBoot_erase_partition(uint8_t part);
void wolfBoot_update_trigger(void);
void wolfBoot_success(void);

#endif /* IMAGE_H */
