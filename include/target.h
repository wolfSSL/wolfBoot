#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#define FLASH_DEV_NAME			"flash"
#define FLASH_ALIGN			4

#define FLASH_AREA_IMAGE_0_OFFSET	0x20000
#define FLASH_AREA_IMAGE_0_SIZE		0x20000
#define FLASH_AREA_IMAGE_1_OFFSET	0x40000
#define FLASH_AREA_IMAGE_1_SIZE		0x20000
#define FLASH_AREA_IMAGE_SCRATCH_OFFSET	0x60000
#define FLASH_AREA_IMAGE_SCRATCH_SIZE	0x20000

/*
 * Sanity check the target support.
 */
#if !defined(FLASH_DEV_NAME) || \
    !defined(FLASH_ALIGN) ||                  \
    !defined(FLASH_AREA_IMAGE_0_OFFSET) || \
    !defined(FLASH_AREA_IMAGE_0_SIZE) || \
    !defined(FLASH_AREA_IMAGE_1_OFFSET) || \
    !defined(FLASH_AREA_IMAGE_1_SIZE) || \
    !defined(FLASH_AREA_IMAGE_SCRATCH_OFFSET) || \
    !defined(FLASH_AREA_IMAGE_SCRATCH_SIZE)
#error "Target support is incomplete; cannot build wolfboot."
#endif

#endif
