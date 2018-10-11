#ifndef H_BOOTUTIL_
#define H_BOOTUTIL_

#include <inttypes.h>

/** Attempt to boot the contents of slot 0. */
#define BOOT_SWAP_TYPE_NONE     1

/** Swap to slot 1.  Absent a confirm command, revert back on next boot. */
#define BOOT_SWAP_TYPE_TEST     2

/** Swap to slot 1, and permanently switch to booting its contents. */
#define BOOT_SWAP_TYPE_PERM     3

/** Swap back to alternate slot.  A confirm changes this state to NONE. */
#define BOOT_SWAP_TYPE_REVERT   4

/** Swap failed because image to be run is not valid */
#define BOOT_SWAP_TYPE_FAIL     5

/** Swapping encountered an unrecoverable error */
#define BOOT_SWAP_TYPE_PANIC    0xff

#define MAX_FLASH_ALIGN         8
#define BOOT_MAX_ALIGN MAX_FLASH_ALIGN

struct image_header;
/**
 * A response object provided by the boot loader code; indicates where to jump
 * to execute the main image.
 */
struct boot_rsp {
    /** A pointer to the header of the image to be executed. */
    const struct image_header *br_hdr;

    /**
     * The flash offset of the image to execute.  Indicates the position of
     * the image header within its flash device.
     */
    uint8_t br_flash_dev_id;
    uint32_t br_image_off;
};

/* This is not actually used by bootloader's code but can be used by apps
 * when attempting to read/write a trailer.
 */
struct image_trailer {
    uint8_t copy_done;
    uint8_t pad1[MAX_FLASH_ALIGN - 1];
    uint8_t image_ok;
    uint8_t pad2[MAX_FLASH_ALIGN - 1];
    uint8_t magic[16];
};

/* you must have pre-allocated all the entries within this structure */
int boot_go(struct boot_rsp *rsp);

int boot_swap_type(void);

int boot_set_pending(int permanent);
int boot_set_confirmed(void);

#define SPLIT_GO_OK                 (0)
#define SPLIT_GO_NON_MATCHING       (-1)
#define SPLIT_GO_ERR                (-2)
int
split_go(int loader_slot, int split_slot, void **entry);

#endif
