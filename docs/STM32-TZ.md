## wolfCrypt in TrustZone-M secure domain

ARMv8-M microcontrollers support hardware-assisted domain separation for running
software. This TEE mechanism provides two separate domains (secure & non-secure),
and an additional zone that can be used as interface to call into secure
functions from the non-secure domain (non-secure callable).

wolfBoot may optionally export the crypto functions as a non-callable APIs that
are accessible from any software staged in non-secure domain.

### Compiling wolfBoot with wolfCrypt in TrustZone-M secure domain

When wolfBoot is compiled with the options `TZEN=1` and `WOLFCRYPT_TZ=1`,
a more complete set of components of the wolfCrypt crypto library are built-in
the bootloader, and they can be accessed by applications or OSs running in
non-secure domain through non-secure callable APIs.

This feature is used to isolate the core crypto operations from the applications.

### PKCS11 API in non-secure world

The `WOLFCRYPT_TZ_PKCS11` option provides a standard PKCS11 interface,
including a storage for PKCS11 objects in a dedicated flash area in secure mode.

This means that applications, TLS libraries and operating systems running in
non-secure domain can access wolfCrypt through a standard PKCS11 interface and
use the crypto library with pre-provisioned keys that are never exposed to the
non-secure domain.

### Image header size

The `IMAGE_HEADER_SIZE` option has to be carefully tuned to accommodate for the
interrupt vector table alignment requirements. According to the [ARM Cortex-M33
documentation](https://developer.arm.com/documentation/100235/0004/the-cortex-m33-processor/exception-model/vector-table):

> The silicon vendor must configure the required alignment of the vector
> tables, which depends on the number of interrupts implemented. The minimum
> alignment is 32 words, enough for up to 16 interrupts. For more interrupts,
> adjust the alignment by rounding up to the next power of two. For example, if
> you require 21 interrupts, the alignment must be on a 64-word boundary
> because the required table size is 37 words, and the next power of two is 64.

For example, all the STM32H5 series boards have at least 146 interrupt
channels; since the next power of two is 256, they require an alignment of 1024
bytes (256×4). As a result, in this case `IMAGE_HEADER_SIZE` must be set to
`1024` or a multiple of it.

This detail is already taken care of in the configuration files provided in
`config/examples`.

In addition to this, when using the signing tool standalone the appropriate
image header size must be supplied as an environment variable. For example:

```
IMAGE_HEADER_SIZE=1024 ./tools/keytools/sign --sha256 --ecc256 myapp.bin wolfboot_signing_private_key.der 1
```

### Example using STM32L552

  - Copy the example configuration for STM32-L5 with support for wolfCrypt in
    TrustZone-M and PKCS11 interface: `cp config/examples/stm32l5-wolfcrypt-tz.config .config`

  - Run `make`. `wolfboot.elf` and the test applications are built as separate
    objects. The application is signed and stored as `test-app/image_v1_signed.bin`.

  - Ensure that the option bytes on your target device are set as follows:

```
OPTION BYTES BANK: 0

   Read Out Protection:

     RDP          : 0xAA (Level 0, no protection)

   BOR Level:

     BOR_LEV      : 0x0 (BOR Level 0, reset level threshold is around 1.7 V)

   User Configuration:

     nRST_STOP    : 0x1 (No reset generated when entering Stop mode)
     nRST_STDBY   : 0x1 (No reset generated when entering Standby mode)
     nRST_SHDW    : 0x1 (No reset generated when entering the Shutdown mode)
     IWDG_SW      : 0x1 (Software independant watchdog)
     IWDG_STOP    : 0x1 (IWDG counter active in stop mode)
     IWDG_STDBY   : 0x1 (IWDG counter active in standby mode)
     WWDG_SW      : 0x1 (Software window watchdog)
     SWAP_BANK    : 0x0 (Bank 1 and bank 2 address are not swapped)
     DB256        : 0x1 (256Kb dual-bank Flash with contiguous addresses)
     DBANK        : 0x0 (Single bank mode with 128 bits data read width)
     SRAM2_PE     : 0x1 (SRAM2 parity check disable)
     SRAM2_RST    : 0x1 (SRAM2 is not erased when a system reset occurs)
     nSWBOOT0     : 0x1 (BOOT0 taken from PH3/BOOT0 pin)
     nBOOT0       : 0x1 (nBOOT0 = 1)
     PA15_PUPEN   : 0x1 (USB power delivery dead-battery disabled/ TDI pull-up activated)
     TZEN         : 0x1 (Global TrustZone security enabled)
     HDP1EN       : 0x0 (No HDP area 1)
     HDP1_PEND    : 0x0  (0x8000000)
     HDP2EN       : 0x0 (No HDP area 2)
     HDP2_PEND    : 0x0  (0x8000000)
     NSBOOTADD0   : 0x100000  (0x8000000)
     NSBOOTADD1   : 0x17F200  (0xBF90000)
     SECBOOTADD0  : 0x180000  (0xC000000)
     BOOT_LOCK    : 0x0 (Boot based on the pad/option bit configuration)

   Secure Area 1:

     SECWM1_PSTRT : 0x0  (0x8000000)
     SECWM1_PEND  : 0x39  (0x8039000)

   Write Protection 1:

     WRP1A_PSTRT  : 0x7F  (0x807F000)
     WRP1A_PEND   : 0x0  (0x8000000)
     WRP1B_PSTRT  : 0x7F  (0x807F000)
     WRP1B_PEND   : 0x0  (0x8000000)
OPTION BYTES BANK: 1

   Secure Area 2:

     SECWM2_PSTRT : 0x7F  (0x807F000)
     SECWM2_PEND  : 0x0  (0x8000000)

   Write Protection 2:

     WRP2A_PSTRT  : 0x7F  (0x80BF000)
     WRP2A_PEND   : 0x0  (0x8040000)
     WRP2B_PSTRT  : 0x7F  (0x80BF000)
     WRP2B_PEND   : 0x0  (0x8040000)
```


  - Upload `wolfboot.bin` and the test application to the two different domains in flash:

```
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08040000
```

  - After rebooting, the LED on the board should turn on sequentially:
    - Red LED: Secure boot was successful. Application has started.
    - Blue LED: PKCS11 Token has been initialized and stored
    - Green LED: ECDSA Sign/Verify test successful


### Example using STM32H563

  - Copy one of the example configurations for STM32H5 with support for TrustZone and PKCS11 to `.config`:
      `cp config/examples/stm32h5-tz.config .config`
      `cp config/examples/stm32h5-tz-dualbank-otp.config .config` (with Dual Bank)
      `cp config/examples/stm32h5-tz-dualbank-otp-lms.config .config` (with Dual Bank and PQ LMS)

  - Run `make`. `wolfboot.elf` and the test applications are built as separate
    objects. The application is signed and stored as `test-app/image_v1_signed.bin`.

  - Ensure that the option bytes on your target device are set as follows:

```
OPTION BYTES BANK: 0

   Product state:

     PRODUCT_STATE: 0xED (Open)

   BOR Level:

     BOR_LEV      : 0x0 (BOR Level 1, the threshold level is low (around 2.1 V))
     BORH_EN      : 0x0  (0x0)

   User Configuration:

     IO_VDD_HSLV  : 0x0  (0x0)
     IO_VDDIO2_HSLV: 0x0  (0x0)
     IWDG_STOP    : 0x1  (0x1)
     IWDG_STDBY   : 0x1  (0x1)
     BOOT_UBE     : 0xB4 (OEM-iRoT (user flash) selected)
     SWAP_BANK    : 0x0  (0x0)
     IWDG_SW      : 0x1  (0x1)
     NRST_STOP    : 0x1  (0x1)
     NRST_STDBY   : 0x1  (0x1)
OPTION BYTES BANK: 1

   User Configuration 2:

     TZEN         : 0xB4 (Trust zone enabled)
     SRAM2_ECC    : 0x1 (SRAM2 ECC check disabled)
     SRAM3_ECC    : 0x1 (SRAM3 ECC check disabled)
     BKPRAM_ECC   : 0x1 (BKPRAM ECC check disabled)
     SRAM2_RST    : 0x1 (SRAM2 not erased when a system reset occurs)
     SRAM1_3_RST  : 0x1 (SRAM1 and SRAM3 not erased when a system reset occurs)
OPTION BYTES BANK: 2

   Boot Configuration:

     NSBOOTADD    : 0x80400  (0x8040000)
     NSBOOT_LOCK  : 0xC3 (The SWAP_BANK and NSBOOTADD can still be modified following their individual rules.)
     SECBOOT_LOCK : 0xC3 (The BOOT_UBE, SWAP_BANK and SECBOOTADD can still be modified following their individual rules.)
     SECBOOTADD   : 0xC0000  (0xC000000)
OPTION BYTES BANK: 3

   Bank1 - Flash watermark area definition:

     SECWM1_STRT  : 0x0  (0x8000000)
     SECWM1_END   : 0x1F  (0x803E000)

   Write sector group protection 1:

     WRPSGn1      : 0xFFFFFFFF  (0x0)
OPTION BYTES BANK: 4

   Bank2 - Flash watermark area definition:

     SECWM2_STRT  : 0x0  (0x08100000)
     SECWM2_END   : 0x1F  (0x0813e000)

   Write sector group protection 2:

     WRPSGn2      : 0xFFFFFFFF  (0x8000000)
OPTION BYTES BANK: 5

   OTP write protection:

     LOCKBL       : 0x0  (0x0)
OPTION BYTES BANK: 6

   Flash data bank 1 sectors:

     EDATA1_EN    : 0x0 (No Flash high-cycle data area)
     EDATA1_STRT  : 0x0  (0x0)
OPTION BYTES BANK: 7

   Flash data bank 2 sectors :

     EDATA2_EN    : 0x0 (No Flash high-cycle data area)
     EDATA2_STRT  : 0x0  (0x0)
OPTION BYTES BANK: 8

   Flash HDP bank 1:

     HDP1_STRT    : 0x1  (0x2000)
     HDP1_END     : 0x0  (0x0)
OPTION BYTES BANK: 9

   Flash HDP bank 2:

     HDP2_STRT    : 0x1  (0x2000)
     HDP2_END     : 0x0  (0x0)
```

  - Upload `wolfboot.bin` and the test application to the two different domains in flash:

```
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08040000
```

  - After rebooting, the LED on the board should turn on sequentially:
    - Red LED: Secure boot was successful. Application has started.
    - Blue LED: PKCS11 Token has been initialized and stored
    - Green LED: ECDSA Sign/Verify test successful
