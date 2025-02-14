# TrustZone-M Integration Guide

## Overview

### TrustZone-M Architecture
ARMv8-M microcontrollers provide hardware-assisted security through domain separation:
- Secure Domain: For trusted code and resources
- Non-secure Domain: For application code
- Non-secure Callable: Interface for secure function calls

### wolfBoot TrustZone Features
wolfBoot leverages TrustZone-M to:
- Isolate cryptographic operations
- Protect sensitive key material
- Provide secure services to applications

## Configuration Options

### Core TrustZone Support
```make
TZEN=1              # Enable TrustZone support
WOLFCRYPT_TZ=1      # Build wolfCrypt in secure domain
```

**Features:**
- Complete wolfCrypt library in secure domain
- Non-secure callable crypto APIs
- Isolated crypto operations from applications

### PKCS#11 Integration
```make
WOLFCRYPT_TZ_PKCS11=1   # Enable PKCS#11 interface
```

**Features:**
- Standard PKCS#11 API for non-secure domain
- Secure flash storage for PKCS#11 objects
- Pre-provisioned keys never exposed to non-secure world
- TLS and application support through standard interface

## STM32L552 Implementation

### Quick Start
1. Copy example configuration:
   ```bash
   cp config/examples/stm32l5-wolfcrypt-tz.config .config
   ```

2. Build project:
   ```bash
   make
   ```
   Outputs:
   - `wolfboot.elf`: Secure bootloader
   - `test-app/image_v1_signed.bin`: Signed application

### Option Bytes Configuration

#### Bank 0: Core Configuration
```
Read Out Protection:
  RDP          : 0xAA (Level 0, no protection)

BOR Level:
  BOR_LEV      : 0x0 (Reset at 1.7V)

TrustZone Settings:
  TZEN         : 0x1 (TrustZone enabled)
  NSBOOTADD0   : 0x100000 (0x8000000)
  NSBOOTADD1   : 0x17F200 (0xBF90000)
  SECBOOTADD0  : 0x180000 (0xC000000)
  BOOT_LOCK    : 0x0 (Option-based boot)

Memory Configuration:
  DB256        : 0x1 (256KB dual-bank flash)
  DBANK        : 0x0 (Single bank, 128-bit width)
  SRAM2_PE     : 0x1 (SRAM2 parity disabled)
  SRAM2_RST    : 0x1 (SRAM2 preserved on reset)

System Settings:
  nRST_STOP    : 0x1 (No reset in Stop mode)
  nRST_STDBY   : 0x1 (No reset in Standby)
  nRST_SHDW    : 0x1 (No reset in Shutdown)
  IWDG_SW      : 0x1 (Software watchdog)
  WWDG_SW      : 0x1 (Software window watchdog)
```

#### Bank 1: Memory Protection
```
Secure Areas:
  SECWM1_PSTRT : 0x0  (0x8000000)
  SECWM1_PEND  : 0x39 (0x8039000)
  SECWM2_PSTRT : 0x7F (0x807F000)
  SECWM2_PEND  : 0x0  (0x8000000)

Write Protection:
  WRP1A/B_PSTRT: 0x7F (0x807F000)
  WRP1A/B_PEND : 0x0  (0x8000000)
  WRP2A/B_PSTRT: 0x7F (0x80BF000)
  WRP2A/B_PEND : 0x0  (0x8040000)
```


### Programming and Testing

#### Flash Programming
```bash
# Program secure bootloader
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000

# Program application image
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08040000
```

#### Verification Steps
Monitor LED sequence:
1. **Red LED**: Secure boot verification passed
2. **Blue LED**: PKCS#11 token initialized
3. **Green LED**: ECDSA test successful

**Note:** LED sequence confirms proper:
- Domain separation
- Secure boot chain
- Cryptographic operations


## STM32H563 Implementation

### Configuration Options

Choose one of these example configurations:
```bash
# Basic TrustZone + PKCS#11
cp config/examples/stm32h5-tz.config .config

# TrustZone + Dual Bank + OTP
cp config/examples/stm32h5-tz-dualbank-otp.config .config

# TrustZone + Dual Bank + OTP + Post-Quantum
cp config/examples/stm32h5-tz-dualbank-otp-lms.config .config
```

### Build Process
```bash
make
```
**Outputs:**
- `wolfboot.elf`: Secure bootloader
- `test-app/image_v1_signed.bin`: Signed application

### Required Option Bytes

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

     SECWM2_STRT  : 0x7F  (0x81FE000)
     SECWM2_END   : 0x0  (0x8100000)

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

### Programming and Testing

#### Flash Programming
```bash
# Program secure bootloader
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000

# Program application image
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08040000
```

#### Verification Steps
Monitor LED sequence:
1. **Red LED**: Secure boot verification passed
2. **Blue LED**: PKCS#11 token initialized
3. **Green LED**: ECDSA test successful

**Note:** LED sequence confirms proper:
- Domain separation
- Secure boot chain
- Cryptographic operations
