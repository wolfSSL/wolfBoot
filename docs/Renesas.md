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

### RX TSIP

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

5) Create wolfBoot signing key

```
# Build keytools for Renesas RX (TSIP)
$ make keytools RENESAS_KEY=2

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

6) Create wrapped public key

This will use the user encryption key to wrap the public key and output key_data.c / key_data.h files.

```
$ C:\Renesas\SecurityKeyManagementTool\cli\skmt.exe -genkey -ufpk file=./sample.key -wufpk file=./sample.key_enc.key -key file=./pub-ecc384.pem -mcu RX-TSIP -keytype secp384r1-public -output include/key_data.c -filetype csource -keyname enc_pub_key
RX-TSIP -keytype secp384r1-public -output include/key_data.c -filetype csource -keyname enc_pub_key
Output File: include\key_data.h
Output File: include\key_data.c
UFPK: B94A2B961C75510174F0C967ECFC20B377C7FB256DB627B1BFFADEE05EE98AC4
W-UFPK: 000000016CCB9A1C8AA58883B1CB02DE6C37DA6054FB94E206EAE7204D9CCF4C6EEB288C
IV: 6C296A040EEF5EDD687E8D3D98D146D0
Encrypted key: 5DD8D7E59E6AC85AE340BBA60AA8F8BE56C4C1FE02340C49EB8F36DA79B8D6640961FE9EAECDD6BADF083C5B6060C1D0309D28EFA25946F431979B9F9D21E77BDC5B1CC7165DE2F4AE51E418746260F518ED0C328BD3020DEC9B774DC00270B0CFBBE3DD738FDF715342CFBF2D461239
```

7) Edit .config `PKA?=1`.

8) Rebuild wolfBoot. `make clean && make wolfboot.srec`

9) Sign application

Sign application using the created private key above `pri-ecc384.der`:

```
./tools/keytools/sign --ecc384 --sha256 test-app/image.bin pri-ecc384.der 1
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
