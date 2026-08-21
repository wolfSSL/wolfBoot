/* f28p55x_app.cmd  (TI cl2000 linker command file)
 *
 * Linker layout for the wolfBoot test application on the TI TMS320F28P550SJ.
 * The application executes in place from the BOOT partition (flash bank1).  Its
 * codestart is placed at 0xA0100 = WOLFBOOT_PARTITION_BOOT_ADDRESS (0xA0000)
 * + IMAGE_HEADER_SIZE (256 words), which is the firmware base wolfBoot's
 * do_boot() branches to.  The 256-word signed header occupies 0xA0000..0xA00FF
 * (programmed separately from the c2000_flashimg.py header blob).
 *
 * Copyright (C) 2026 wolfSSL Inc.  GPLv3 - see project headers.
 */

-stack 0x2000
-heap  0x1000

MEMORY
{
   HDR              : origin = 0x0A0000, length = 0x000100  /* wolfBoot signed header (256 cells) */
   BEGIN            : origin = 0x0A0100, length = 0x000002  /* app codestart */

   BOOT_RSVD        : origin = 0x000002, length = 0x000126
   RAMM0            : origin = 0x000128, length = 0x0002D8
   RAMM1            : origin = 0x000400, length = 0x000400

   RAMLS_STACK      : origin = 0x008000, length = 0x002000  /* RAMLS0-3, stack */
   RAMGS_RAMCODE    : origin = 0x00A000, length = 0x002000  /* .TI.ramfunc     */
   RAMGS_HEAP       : origin = 0x00C000, length = 0x002000  /* RAMGS0-1, heap  */
   RAMGS_HI         : origin = 0x010000, length = 0x004000  /* RAMGS2-3        */
   RAMLS_HI         : origin = 0x014000, length = 0x004000  /* RAMLS8-9        */

   /* Application flash: rest of BOOT partition bank1 (after codestart), plus
    * bank2 if needed.  Header cells occupy the low 256 words of bank1. */
   APP_FLASH1       : origin = 0x0A0102, length = 0x01FEFE  /* bank1 remainder */
   APP_FLASH2       : origin = 0x0C0000, length = 0x020000  /* bank2           */

   RESET            : origin = 0x3FFFC0, length = 0x000002
}

SECTIONS
{
   /* wolfBoot signed header at the BOOT partition base (0xA0000), one octet
    * per 16-bit cell; the app codestart follows at 0xA0100 (= fw_base). */
   .wolfboot_hdr    : > HDR
   codestart        : > BEGIN
   /* Device_init copies these to RAM (RamfuncsLoadStart -> RamfuncsRunStart). */
   .TI.ramfunc      : LOAD = APP_FLASH1,
                      RUN  = RAMGS_RAMCODE,
                      LOAD_START(RamfuncsLoadStart),
                      LOAD_SIZE(RamfuncsLoadSize),
                      LOAD_END(RamfuncsLoadEnd),
                      RUN_START(RamfuncsRunStart),
                      RUN_SIZE(RamfuncsRunSize),
                      RUN_END(RamfuncsRunEnd),
                      ALIGN(8)
   .text            : >> APP_FLASH1 | APP_FLASH2, ALIGN(8)
   .cinit           : > APP_FLASH1 | APP_FLASH2, ALIGN(8)
   .switch          : > APP_FLASH1 | APP_FLASH2, ALIGN(8)
   .init_array      : > APP_FLASH1 | APP_FLASH2, ALIGN(8)
   .const           : >> APP_FLASH1 | APP_FLASH2, ALIGN(8)
   .reset           : > RESET, TYPE = DSECT

   .stack           : > RAMLS_STACK
   .bss             : >> RAMGS_HI | RAMLS_HI
   .bss:output      : > RAMGS_HI
   .data            : >> RAMGS_HI | RAMLS_HI
   .sysmem          : > RAMGS_HEAP
}
