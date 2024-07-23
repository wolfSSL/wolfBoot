# Renesas wolfBoot

Platforms Supported:
* Renesas RZ (RZN2L) (RSIP)
  - [docs/Targets.md#renesas-rzn2l](/docs/Targets.md#renesas-rzn2l)
  - [IDE/Renesas/e2studio/RZN2L/Readme.md](/IDE/Renesas/e2studio/RZN2L/Readme.md)
  - [IDE/Renesas/e2studio/RZN2L/Readme_wRSIP.md](/IDE/Renesas/e2studio/RZN2L/Readme_wRSIP.md)
* Renesas RA (RA6M4) (SCE)
  - [docs/Targets.md#renesas-ra6m4](/docs/Targets.md#renesas-ra6m4)
  - [IDE/Renesas/e2studio/RA6M4/Readme.md](/IDE/Renesas/e2studio/RA6M4/Readme.md)
  - [IDE/Renesas/e2studio/RA6M4/Readme_withSCE.md](/IDE/Renesas/e2studio/RA6M4/Readme_withSCE.md)
* Renesas RX (RX65N/RX72N) (TSIP)
  - [docs/Targets.md#renesas-rx72n](/docs/Targets.md#renesas-rx72n)
  - [IDE/Renesas/e2studio/RX72N/Readme.md](/IDE/Renesas/e2studio/RX72N/Readme.md)
  - [IDE/Renesas/e2studio/RX72N/Readme_withTSIP.md](/IDE/Renesas/e2studio/RX72N/Readme_withTSIP.md)

All of the Renesas examples support using e2Studio.
The Renesas RX parts support using wolfBoot Makefile's with the rx-elf-gcc cross-compiler and example .config files.

### Security Key Management Tool (SKMT) Key Wrapping

1) Setup a Renesas KeyWrap account and do the PGP key exchange.
https://dlm.renesas.com/keywrap
You will get a public key from Renesas `keywrap-pub.key` that needs imported to PGP/GPG.
Note: You cannot use RSA 4096-bit key, must be RSA-2048 or RSA-3072.

2) Using "Security Key Management Tool" create 32-byte UFPK (User Factory Programming Key). This can be a random 32-byte value.
Example: Random 32-bytes `B94A2B96 1C755101 74F0C967 ECFC20B3 77C7FB25 6DB627B1 BFFADEE0 5EE98AC4`

3) Sign and Encrypt the 32-byte binary file with PGP the `sample.key`. Result is `sample.key.gpg`.
Use GPG4Win and the Sign/Encrypt option. Sign with your own GPG key and encrypt with the Renesas public key.

4) Use https://dlm.renesas.com/keywrap to wrap `sample.key.gpg`.
It will use the Hidden Root Key (HRK) that both Renesas and the RX TSIP have pre-provisioned from Renesas Factory.
Result is `sample.key_enc.key`. Example: `00000001 6CCB9A1C 8AA58883 B1CB02DE 6C37DA60 54FB94E2 06EAE720 4D9CCF4C 6EEB288C`

### RX TSIP

1) Build key tools for Renesas

```sh
# Build keytools for Renesas RX (TSIP)
$ make keytools RENESAS_KEY=2
```

2) wolfBoot public key (create or import existing)

Instructions below for ECDSA P384 (SECP384R1).
For SECP256R1 replace "ecc384" with "ecc256" and "secp384r1" with "secp256r1".

Create new signing key:

```sh
# Create new signing key
$ ./tools/keytools/keygen --ecc384 -g ./pri-ecc384.der
Keytype: ECC384
Generating key (type: ECC384)
Associated key file:   ./pri-ecc384.der
Partition ids mask:   ffffffff
Key type   :           ECC384
Public key slot:       0
Done.

# Export public portion of key as PEM
$ openssl ec -inform der -in ./pri-ecc384.der -pubout -out ./pub-ecc384.pem
```

OR

Import Public Key:

```sh
# Export public portion of key as DER
$ openssl ec -inform der -in ./pri-ecc384.der -pubout -outform der -out ./pub-ecc384.der

# Import public key and populate src/keystore.c
$ ./tools/keytools/keygen --ecc384 -i ./pub-ecc384.der
Keytype: ECC384
Associated key file:   ./pub-ecc384.der
Partition ids mask:   ffffffff
Key type   :           ECC384
Public key slot:       0
Done.
```

3) Create wrapped public key (code files)

Use the Security Key Management Tool (SKMT) command line tool (CLI) to create a wrapped public key.

This will use the user encryption key to wrap the public key and output key_data.c / key_data.h files.

```sh
$ C:\Renesas\SecurityKeyManagementTool\cli\skmt.exe -genkey -ufpk file=./sample.key -wufpk file=./sample.key_enc.key -key file=./pub-ecc384.pem -mcu RX-TSIP -keytype secp384r1-public -output include/key_data.c -filetype csource -keyname enc_pub_key
Output File: include\key_data.h
Output File: include\key_data.c
UFPK: B94A2B961C75510174F0C967ECFC20B377C7FB256DB627B1BFFADEE05EE98AC4
W-UFPK: 000000016CCB9A1C8AA58883B1CB02DE6C37DA6054FB94E206EAE7204D9CCF4C6EEB288C
IV: 6C296A040EEF5EDD687E8D3D98D146D0
Encrypted key: 5DD8D7E59E6AC85AE340BBA60AA8F8BE56C4C1FE02340C49EB8F36DA79B8D6640961FE9EAECDD6BADF083C5B6060C1D0309D28EFA25946F431979B9F9D21E77BDC5B1CC7165DE2F4AE51E418746260F518ED0C328BD3020DEC9B774DC00270B0CFBBE3DD738FDF715342CFBF2D461239
```

4) Create wrapped public key (flash file)

Generate Motorola HEX file to write wrapped key to flash.

```sh
$ C:\Renesas\SecurityKeyManagementTool\cli\skmt.exe -genkey -ufpk file=./sample.key -wufpk file=./sample.key_enc.key -key file=./pub-ecc384.pem -mcu RX-TSIP -keytype secp384r1-public -output pub-ecc384.srec -filetype "mot" -address FFFF0000
Output File: Y:\GitHub\wolfboot\pub-ecc384.srec
UFPK: B94A2B961C75510174F0C967ECFC20B377C7FB256DB627B1BFFADEE05EE98AC4
W-UFPK: 000000016CCB9A1C8AA58883B1CB02DE6C37DA6054FB94E206EAE7204D9CCF4C6EEB288C
IV: 9C13402DF1AF631DC2A10C2424182601
Encrypted key: C4A0B368552EB921A3AF3427FD7403BBE6CB8EE259D6CC0692AA72D46F7343F5FFE7DA97A1C811B21BF392E3834B67C3CE6F84707CCB8923D4FBB8DA003EF23C1CD785B6F58E5DB161F575F78D646434AC2BFAF207F6FFF6363C800CFF7E7BFF4857452A70C496B675D08DD6924CAB5E
```

The generated file is a Motorola HEX (S-Record) formatted image containing the wrapped public key with instructions to use the `0xFFFF0000` address.

```
S00E00007075622D65636333737265D5
S315FFFF000000000000000000006CCB9A1C8AA58883C5
S315FFFF0010B1CB02DE6C37DA6054FB94E206EAE720E7
S315FFFF00204D9CCF4C6EEB288C9C13402DF1AF631D7F
S315FFFF0030C2A10C2424182601C4A0B368552EB921EA
S315FFFF0040A3AF3427FD7403BBE6CB8EE259D6CC06AE
S315FFFF005092AA72D46F7343F5FFE7DA97A1C811B27D
S315FFFF00601BF392E3834B67C3CE6F84707CCB8923ED
S315FFFF0070D4FBB8DA003EF23C1CD785B6F58E5DB1F0
S315FFFF008061F575F78D646434AC2BFAF207F6FFF66C
S315FFFF0090363C800CFF7E7BFF4857452A70C496B6D9
S311FFFF00A075D08DD6924CAB5ED6FF44C5E3
S705FFFF0000FC
```

The default flash memory address is `0xFFFF0000`, but it can be changed. The following two places must be set:
a) The `user_settings.h` build macro `RENESAS_TSIP_INSTALLEDKEY_ADDR`
b) The linker script `.rot` section (example `hal/rx72n.ld` or `hal/rx65n.ld`).

5) Edit .config `PKA?=1`.

6) Rebuild wolfBoot. `make clean && make wolfboot.srec`

7) Sign application

Sign application using the created private key above `pri-ecc384.der`:

```sh
$ ./tools/keytools/sign --ecc384 --sha256 test-app/image.bin pri-ecc384.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2010000
Update type:          Firmware
Input image:          test-app/image.bin
Selected cipher:      ECC384
Selected hash  :      SHA256
Public key:           pri-ecc384.der
Output  image:        test-app/image_v1_signed.bin
Target partition id : 1
image header size overridden by config value (1024 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

8) Flash wolfboot.srec, pub-ecc384.srec and signed application binary

Download files to flash using Renesas flash programmer.


#### RX TSIP Benchmarks

| Hardware | Clock  | Algorithm         | RX TSIP  | Debug    | Release (-Os) | Release (-O2) |
| -------- | ------ | ----------------- | -------- | -------- | ------------- | ------------- |
| RX72N    | 240MHz | ECDSA Verify P384 | 17.26 ms | 1570 ms  |  441 ms       |  313 ms       |
| RX72N    | 240MHz | ECDSA Verify P256 |  2.73 ms |  469 ms  |  135 ms       |  107 ms       |
| RX65N    | 120MHz | ECDSA Verify P384 | 18.57 ms | 4213 ms  | 2179 ms       | 1831 ms       |
| RX65N    | 120MHz | ECDSA Verify P256 |  2.95 ms | 1208 ms  |  602 ms       |  517 ms       |
