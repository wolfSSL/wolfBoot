/* f28p55x.ld  (TI cl2000 linker command file, tokenized by "make config")
 *
 * wolfBoot linker layout for the TI TMS320F28P550SJ (LAUNCHXL-F28P55X).
 * wolfBoot owns flash bank0 only (@ARCH_FLASH_OFFSET@, @BOOTLOADER_PARTITION_SIZE@
 * bytes); banks 1-4 hold the BOOT/UPDATE/SWAP partitions and are NOT linked
 * here (the resident application is linked separately at its BOOT address).
 *
 * The C28x stack pointer is 16-bit, so .stack must live below 0x10000.
 * Flash program/erase code (RAMFUNCTION -> .TI.ramfunc) is loaded from flash
 * and copied to RAM by Device_init's Ramfuncs memcpy.
 *
 * Copyright (C) 2026 wolfSSL Inc.  GPLv3 - see project headers.
 */

-stack 0x4000   /* 16 KW C28x stack (full RAMLS0-7, 0x8000..0xBFFF); below
                 * 0x10000.  ECC P-256 verify keeps the ecc_key struct and the
                 * SP *_NO_MALLOC digit buffers on the stack (WOLFSSL_NO_MALLOC,
                 * no heap); 8 KW then 12 KW overflowed into the adjacent
                 * .TI.ramfunc region, corrupting saved frames (wild return ->
                 * ITRAP / hang).  Matches the validated wolfCrypt example's
                 * 16 KW stack. */
-heap  0x1000   /* 4 KW heap (unused: WOLFSSL_NO_MALLOC) */

MEMORY
{
   BEGIN            : origin = @ARCH_FLASH_OFFSET@, length = 0x000002

   BOOT_RSVD        : origin = 0x000002, length = 0x000126  /* M0, boot-ROM stack */
   RAMM0            : origin = 0x000128, length = 0x0002D8
   RAMM1            : origin = 0x000400, length = 0x000400

   /* Low RAM (< 0x10000).  The 16-bit-SP stack fills RAMLS0-7 (16 KW).
    * .TI.ramfunc RUNs from RAMGS0 (0xC000): on a cold flash-boot only RAMGS0
    * (and RAMLS0-5) are CPU instruction-fetchable early - RAMLS6-7 default to
    * CLA ownership, so Flash_initModule() (called from Device_init before any
    * MemCfg) ITRAPs if placed there.  This matches the wolfCrypt example. */
   RAMLS_STACK      : origin = 0x008000, length = 0x004000  /* RAMLS0-7, 16 KW */
   RAMGS_RAMCODE    : origin = 0x00C000, length = 0x001000  /* RAMGS0 lo, .TI.ramfunc */
   RAMGS_SURV       : origin = 0x00D000, length = 0x001000  /* survive-log (persists across do_boot) */
   RAMGS_HEAP       : origin = 0x00E000, length = 0x002000  /* RAMGS1, heap (unused) */

   /* High RAM (>= 0x10000): data only. */
   RAMGS_HI         : origin = 0x010000, length = 0x004000  /* RAMGS2-3, 16 KW */
   RAMLS_HI         : origin = 0x014000, length = 0x004000  /* RAMLS8-9, 16 KW */

   /* wolfBoot code + const live in flash bank0 only. */
   FLASH_BANK0      : origin = 0x080002, length = 0x01FFFE

   RESET            : origin = 0x3FFFC0, length = 0x000002
}

SECTIONS
{
   /* _start_text marks the base of the wolfBoot image (used by update_flash.c
    * under RAM_CODE for the bootloader region / self-hash). */
   codestart        : > BEGIN, START(_start_text)

   /* RAM-resident flash program/erase routines: loaded from flash, copied to
    * RAM by Device_init.  --gen_func_subsections=on makes each function its
    * own input section; the ramfunc attribute places them in .TI.ramfunc. */
   .TI.ramfunc      : LOAD = FLASH_BANK0,
                      RUN  = RAMGS_RAMCODE,
                      LOAD_START(RamfuncsLoadStart),
                      LOAD_SIZE(RamfuncsLoadSize),
                      LOAD_END(RamfuncsLoadEnd),
                      RUN_START(RamfuncsRunStart),
                      RUN_SIZE(RamfuncsRunSize),
                      RUN_END(RamfuncsRunEnd),
                      ALIGN(8)

   .text            : > FLASH_BANK0, ALIGN(8), END(_end_text)
   .cinit           : > FLASH_BANK0, ALIGN(8)
   .switch          : > FLASH_BANK0, ALIGN(8)
   .init_array      : > FLASH_BANK0, ALIGN(8)
   .const           : > FLASH_BANK0, ALIGN(8)
   /* wolfBoot public-key keystore (src/keystore.c) */
   .keystore        : > FLASH_BANK0, ALIGN(8)
   .reset           : > RESET, TYPE = DSECT     /* unused; boot ROM owns it */

   .stack           : > RAMLS_STACK

   .bss             : >> RAMGS_HI | RAMLS_HI
   .bss:output      : > RAMGS_HI
   .data            : >> RAMGS_HI | RAMLS_HI
   .sysmem          : > RAMGS_HEAP
   .survivelog      : > RAMGS_SURV, type = NOINIT
}
