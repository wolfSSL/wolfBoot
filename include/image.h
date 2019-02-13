#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>
#include <target.h>
#include <wolfboot/wolfboot.h>


#define SECT_FLAG_NEW 0x0F
#define SECT_FLAG_SWAPPING 0x07
#define SECT_FLAG_BACKUP 0x03
#define SECT_FLAG_UPDATED 0x00


#ifdef EXT_FLASH
#   ifndef PART_UPDATE_DRIVER
#       define PART_UPDATE_DRIVER (&internal_flash_driver)
#   endif
#   define flash_lock(x) (x->flash_driver.lock()) 
#   define flash_unlock(x) (x->flash_driver.unlock())
#   define flash_erase(x, addr, len) (x->flash_driver.erase(addr, len)
#   define flash_write(x, addr, data, len) (x->flash_driver.write(addr, data, len)
#   define flash_read(x, addr, data, len) (x->flash_driver.read(addr, data, len)
#else
#   define flash_lock(x) hal_flash_lock()
#   define flash_unlock(x) hal_flash_unlock()
#   define flash_erase(x, addr, len) (x->flash_driver.erase(addr, len)
#   define flash_write(x, addr, data, len) (x->flash_driver.write(addr, data, len)
#   define flash_read(x, addr, data, len) (memcpy(data, addr, len) - data + len)
#endif 

struct wolfBoot_flash_driver {
    int (*write)(uint32_t address, const uint8_t *data, int len);
    int (*read)(uint32_t address, uint8_t *data, int len);
    int (*erase)(uint32_t address, int len);
    void (*lock)(void);
    void (*unlock)(void);
};

extern const struct wolfBoot_flash_driver internal_flash_driver;

struct wolfBoot_image {
    uint8_t *hdr;
    uint8_t *trailer;
    int hdr_ok;
    int signature_ok;
    int sha_ok;
    uint8_t *fw_base;
    uint32_t fw_size;
    uint8_t part;
    struct wolfBoot_flash_driver *flash_driver;
};


int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part);
int wolfBoot_verify_integrity(struct wolfBoot_image *img);
int wolfBoot_verify_authenticity(struct wolfBoot_image *img);
int wolfBoot_set_partition_state(uint8_t part, uint8_t newst);
int wolfBoot_set_sector_flag(uint8_t part, uint8_t sector, uint8_t newflag);
int wolfBoot_get_partition_state(uint8_t part, uint8_t *st);
int wolfBoot_get_sector_flag(uint8_t part, uint8_t sector, uint8_t *flag);

#endif /* IMAGE_H */
