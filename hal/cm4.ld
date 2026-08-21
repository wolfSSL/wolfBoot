/* cm4.ld
 *
 * Linker script for the Raspberry Pi Compute Module 4 (BCM2711, Cortex-A72).
 *
 * The VideoCore GPU firmware loads kernel8.img (an ARM64 image with the Linux
 * kernel header) to the 2MB-aligned address 0x200000 and enters it in place at
 * EL2 - it does NOT relocate a header image down to 0x80000. So wolfBoot links
 * at 0x200000. DDR_MEM length (0x3be00000, so the region ends at 0x3c000000 =
 * 960 MB) covers the low DDR window below the 0xFE000000 peripheral block,
 * common to all CM4 RAM variants (1/2/4/8 GB); the peripheral block above is not
 * mapped here. The initial stack grows down from 0x200000 into the ~2MB of free
 * low DDR below the image.
 */
MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x00200000, LENGTH = 0x3be00000
}
ENTRY(_vector_table);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        KEEP(*(.keystore))
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > DDR_MEM
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > DDR_MEM

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DDR_MEM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > DDR_MEM
    . = ALIGN(8);
    /* These symbols exist only to satisfy newlib's link-time references (the
     * default sbrk/heap symbols). hal/cm4.c defines its own _sbrk over a bounded
     * static buffer (cm4_fips_heap[] in .bss), so _heap_start is effectively
     * dead - the real FIPS heap is that fixed static buffer, not this address.
     * The stack lives at END_STACK (0x200000) and grows down. */
    PROVIDE(end = .);
    PROVIDE(__end__ = .);
    PROVIDE(_heap_start = .);
}

END_STACK = _start_text;
/* RAM-boot staging addresses for the non-disk configs: the firmware loads
 * kernel8.img (wolfBoot + concatenated signed app) to 0x200000, so the app
 * lands at 0x200000 + its bin-assemble offset. These sit above the wolfBoot
 * image/heap; the initial stack grows down from 0x200000. (The DISK_EMMC /
 * DISK_SDCARD configs ignore these - they read A/B images from the eMMC/SD to
 * WOLFBOOT_LOAD_ADDRESS instead.) */
kernel_addr  = 0x02C0000;   /* 0x200000 load + 0xC0000 bin-assemble offset */
update_addr  = 0x12C0000;
dts_addr     = 0x0280000;
kernel_load_addr = 0x20000000;
dts_load_addr = 0x21000000;

_wolfboot_partition_boot_address = kernel_addr;
_wolfboot_partition_update_address = update_addr;

/* Guard the RAM-boot staging addresses: the image (.text/.data/.bss, including
 * the 128KB FIPS heap) must stay below dts_addr (the lower of dts_addr and
 * kernel_addr). An overflow would not fail the link - it would silently corrupt
 * the staged DTB or the unverified application at runtime.
 * NOTE: dts_addr/kernel_addr are RAM-boot staging addresses only; the DISK_EMMC/
 * DISK_SDCARD configs read A/B images from disk to WOLFBOOT_LOAD_ADDRESS and do
 * not stage there. A large image (e.g. FIPS) that legitimately exceeds this
 * 512KB RAM-boot cap on a disk config should raise dts_addr/kernel_addr below.
 * (NO_XIP=0 is not supported here: this script defines no _stored_data LMA
 * region, and a copied .data would overlap the RAM-boot staging / cm4_fw_dtb.) */
ASSERT(_end <= dts_addr,
    "cm4: image + heap overruns dts_addr - raise dts_addr/kernel_addr in hal/cm4.ld")
