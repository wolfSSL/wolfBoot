## wolfCrypt in TrustZone-M secure domain

ARMv8-M microcontrollers support hardware-assisted domain separation for running
software. This TEE mechanism provides two separate domains (secure & non-secure),
and an additional zone that can be used as interface to call into secure
functions from the non-secure domain (non-secure callable).

wolfBoot may optionally export the crypto functions as a non-callable APIs that
are accessible from any software staged in non-secure domain.

### Compiling wolfBoot with wolfCrypt in TrustZone-M secure domain

When wolfBoot is compiled with the options `TZEN=1` and `WOLFCRYPT_TZ_ENGINE=1`,
a more complete set of components of the wolfCrypt crypto library are built-in
the bootloader, and they can be accessed by applications or OSs running in 
non-secure domain through non-secure callable APIs.

This feature is used to isolate the core crypto operations from the applications.

### PKCS11 API in non-secure world

The `WOLFCRYPT_TZ_PKCS11` provides a standard PKCS11 interface,
including a storage for PKCS11 objects in a dedicated flash area in secure mode.

This means that applications, TLS libraries and operating systems running in 
non-secure domain can access wolfCrypt through a standard PKCS11 interface and
use the crypto library with pre-provisioned keys that are never exposed to the
non-secure domain.

### Example using STM32-L552

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



