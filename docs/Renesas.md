# Renesas wolfBoot Integration Guide

This guide covers wolfBoot integration with Renesas microcontrollers, including security features and development tools.

## Supported Platforms

### RZ Series
* **RZN2L with RSIP**
  - Hardware Security: RSIP (Renesas Security IP)
  - Documentation:
    - [Target Details](docs/Targets.md#renesas-rzn2l)
    - [e2studio Setup](IDE/Renesas/e2studio/RZN2L/Readme.md)
    - [RSIP Integration](IDE/Renesas/e2studio/RZN2L/Readme_wRSIP.md)

### RA Series
* **RA6M4 with SCE**
  - Hardware Security: SCE (Secure Crypto Engine)
  - Documentation:
    - [Target Details](docs/Targets.md#renesas-ra6m4)
    - [e2studio Setup](IDE/Renesas/e2studio/RA6M4/Readme.md)
    - [SCE Integration](IDE/Renesas/e2studio/RA6M4/Readme_withSCE.md)

### RX Series
* **RX65N/RX72N with TSIP**
  - Hardware Security: TSIP (Trusted Secure IP)
  - Documentation:
    - [Target Details](docs/Targets.md#renesas-rx72n)
    - [e2studio Setup](IDE/Renesas/e2studio/RX72N/Readme.md)
    - [TSIP Integration](IDE/Renesas/e2studio/RX72N/Readme_withTSIP.md)

## Development Environment

### IDE Support
- All platforms: Renesas e2studio IDE

### Build System Options
- RX Series: 
  - Makefile-based build with rx-elf-gcc
  - Example configurations provided

## Security Key Management

### SKMT (Security Key Management Tool) Setup

#### 1. KeyWrap Account Setup
1. Register at [Renesas KeyWrap Portal](https://dlm.renesas.com/keywrap)
2. Complete PGP key exchange process
   - Receive Renesas public key (`keywrap-pub.key`)
   - Import key to PGP/GPG system
   
**Important:** Use RSA-2048 or RSA-3072 keys only (RSA-4096 not supported)

#### 2. UFPK Generation
1. Launch Security Key Management Tool
2. Generate 32-byte UFPK (User Factory Programming Key)
   ```
   Example UFPK:
   B94A2B96 1C755101 74F0C967 ECFC20B3
   77C7FB25 6DB627B1 BFFADEE0 5EE98AC4
   ```

#### 3. Key Encryption
1. Create binary key file (`sample.key`)
2. Using GPG4Win:
   - Sign with your GPG key
   - Encrypt with Renesas public key
   - Output: `sample.key.gpg`

#### 4. Key Wrapping
1. Visit [KeyWrap Portal](https://dlm.renesas.com/keywrap)
2. Upload `sample.key.gpg`
3. System uses factory-provisioned HRK (Hidden Root Key)
4. Download wrapped key (`sample.key_enc.key`)
   ```
   Example wrapped key:
   00000001 6CCB9A1C 8AA58883 B1CB02DE
   6C37DA60 54FB94E2 06EAE720 4D9CCF4C
   6EEB288C
   ```

## RX TSIP Integration

### Building Key Tools

```sh
# Build keytools with Renesas RX (TSIP) support
make keytools RENESAS_KEY=2
```

### Key Management

#### Option 1: Create New Signing Keys

For ECDSA P384 (SECP384R1):
```sh
# Generate private key
./tools/keytools/keygen --ecc384 -g ./pri-ecc384.der

# Export public key as PEM
openssl ec -inform der -in ./pri-ecc384.der -pubout -out ./pub-ecc384.pem
```

**Note:** For SECP256R1, replace:
- `ecc384` with `ecc256`
- `secp384r1` with `secp256r1`

#### Option 2: Import Existing Public Key

```sh
# Export public key as DER
openssl ec -inform der -in ./pri-ecc384.der -pubout \
          -outform der -out ./pub-ecc384.der

# Import to keystore
./tools/keytools/keygen --ecc384 -i ./pub-ecc384.der
```

**Expected Output:**
```
Keytype: ECC384
Associated key file:   ./pub-ecc384.der
Partition ids mask:   ffffffff
Key type   :           ECC384
Public key slot:       0
Done.
```

### Wrapped Key Generation

#### 1. Generate Source Files
Use SKMT CLI to create wrapped key source files:

```sh
C:\Renesas\SecurityKeyManagementTool\cli\skmt.exe -genkey \
    -ufpk file=./sample.key \
    -wufpk file=./sample.key_enc.key \
    -key file=./pub-ecc384.pem \
    -mcu RX-TSIP \
    -keytype secp384r1-public \
    -output include/key_data.c \
    -filetype csource \
    -keyname enc_pub_key
```

**Output Files:**
- `include/key_data.h`
- `include/key_data.c`

#### 2. Generate Flash Image
Create Motorola S-Record (SREC) file for flash programming:

```sh
C:\Renesas\SecurityKeyManagementTool\cli\skmt.exe -genkey \
    -ufpk file=./sample.key \
    -wufpk file=./sample.key_enc.key \
    -key file=./pub-ecc384.pem \
    -mcu RX-TSIP \
    -keytype secp384r1-public \
    -output pub-ecc384.srec \
    -filetype "mot" \
    -address FFFF0000
```

#### Flash Memory Configuration

**Default Address:** `0xFFFF0000`

To modify the default address, update:
1. Build Configuration:
   ```c
   // In user_settings.h
   #define RENESAS_TSIP_INSTALLEDKEY_ADDR 0xFFFF0000
   ```

2. Linker Script:
   ```
   // In hal/rx72n.ld or hal/rx65n.ld
   .rot : {
       /* Update address here */
   }
   ```

### Building and Flashing

#### 1. Configure Build
Edit `.config`:
```make
PKA?=1    # Enable Public Key Acceleration
```

#### 2. Build wolfBoot
```sh
make clean
make wolfboot.srec
```

#### 3. Sign Application
Use private key to sign the application image:
```sh
./tools/keytools/sign --ecc384 --sha256 \
    test-app/image.bin \
    pri-ecc384.der 1
```

**Expected Output:**
```
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2010000
Update type:          Firmware
Input image:          test-app/image.bin
Selected cipher:      ECC384
Selected hash  :      SHA256
Public key:           pri-ecc384.der
Output  image:        test-app/image_v1_signed.bin
Target partition id : 1
...
Output image(s) successfully created.
```

#### 4. Flash Images
Using Renesas Flash Programmer, flash:
1. `wolfboot.srec` (Bootloader)
2. `pub-ecc384.srec` (Public Key)
3. `test-app/image_v1_signed.bin` (Application)


### Performance Benchmarks

#### RX TSIP Signature Verification

| MCU   | Clock  | Algorithm | Hardware Accel | Debug | Release (-Os) | Release (-O2) |
|-------|--------|-----------|----------------|--------|---------------|---------------|
| RX72N | 240MHz | P384 ECDSA| 17.26 ms      | 1570 ms| 441 ms       | 313 ms        |
| RX72N | 240MHz | P256 ECDSA| 2.73 ms       | 469 ms | 135 ms       | 107 ms        |
| RX65N | 120MHz | P384 ECDSA| 18.57 ms      | 4213 ms| 2179 ms      | 1831 ms       |
| RX65N | 120MHz | P256 ECDSA| 2.95 ms       | 1208 ms| 602 ms       | 517 ms        |

**Notes:**
- Hardware acceleration provides 10-90x speedup
- P256 operations are significantly faster than P384
- Higher clock speeds improve performance proportionally
- Optimization levels have significant impact on software implementation
