#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#define FLASH_DEV_NAME			"flash"
#define FLASH_ALIGN			4

#define FLASH_AREA_IMAGE_0_OFFSET	0x2f000
#define FLASH_AREA_IMAGE_0_SIZE		0x28000
/* Unused page 0x57000:0x58000 */
#define FLASH_AREA_IMAGE_1_OFFSET	0x58000
#define FLASH_AREA_IMAGE_1_SIZE		0x28000
#define FLASH_AREA_IMAGE_NO_SCRATCH
//#define FLASH_AREA_IMAGE_SCRATCH_OFFSET	0x6f000
//#define FLASH_AREA_IMAGE_SCRATCH_SIZE	0x11000

/*
 * Sanity check the target support.
 */
#if !defined(FLASH_DEV_NAME) || \
    !defined(FLASH_ALIGN) ||                  \
    !defined(FLASH_AREA_IMAGE_0_OFFSET) || \
    !defined(FLASH_AREA_IMAGE_0_SIZE) || \
    !defined(FLASH_AREA_IMAGE_1_OFFSET) || \
    !defined(FLASH_AREA_IMAGE_1_SIZE) 
#error "Target support is incomplete; cannot build wolfboot."
#endif

#endif
