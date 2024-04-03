# wolfBoot Key Tools

`keygen` and `sign` are two command line tools to be used on a PC (or automated
server) environment to manage wolfBoot private keys and sign the initial
firmware and all the updates for the target.

## C or Python

The tools are distributed in two versions, using the same command line syntax,
for portability reasons.

By default, C keytools are compiled. The makefiles and scripts in this
repository will use the C tools.

### C Key Tools

A standalone C version of the key tools is available in: `./tools/keytools`.

These can be built in `tools/keytools` using `make` or from the wolfBoot root using `make keytools`.

If the C version of the key tools exists they will be used by wolfBoot's makefile and scripts.

#### Windows Visual Studio

Use the `wolfBootSignTool.vcxproj` Visual Studio project to build the `sign.exe` and `keygen.exe` tools for use on Windows.

If you see any error about missing `target.h` this is a generated file based on your .config using the make process. It is needed for `WOLFBOOT_SECTOR_SIZE` used in delta updates.

### Python key tools

**Please note that the Python tools are deprecated and will be removed in future versions.**

In order to use the python key tools, ensure that the `wolfcrypt` package is
installed in your python environment. In most systems it's sufficient to run a
command similar to:

`pip install wolfcrypt`

to ensure that the dependencies are met.

## Command Line Usage

### Keygen tool

Usage: `keygen [OPTIONS] [-g new-keypair.der] [-i existing-pubkey.der] [...]`

`keygen` is used to populate a keystore with existing and new public keys.
Two options are supported:

- `-g privkey.der` to generate a new keypair, add the public key to the keystore and save the private key in a new file `privkey.der`
- `-i existing.der` to import an existing public key from `existing.der`

Arguments are not exclusive, and can be repeated more than once to populate a keystore with multiple keys.

One option must be specified to select the algorithm enabled in the keystore (e.g. `--ed25519` or `--rsa3072`. See the section "Public key signature options" for the sign tool for the available options.

The files generate by the keygen tool is the following:

- A C file `src/keystore.c`, which is normally linked with the wolfBoot image, when the keys are provisioned through generated C code.
- A binary file `keystore.img` that can be used to provision the public keys through an alternative storage
- The private key, for each `-g` option provided from command line

For more information about the keystore mechanism, see [keystore.md](keystore.md).


### Sign tool

`sign` produces a signed firmware image by creating a manifest header
in the format supported by wolfBoot.

Usage: `sign [OPTIONS] IMAGE.BIN KEY.DER VERSION`

`IMAGE.BIN`:  A file containing the binary firmware/software to sign
`KEY.DER`:    Private key file, in DER format, to sign the binary image
`VERSION`:    The version associated with this signed software
`OPTIONS`:    Zero or more options, described below

#### Public key signature options

If none of the following arguments is given, the tool will try to guess the key
size from the format and key length detected in KEY.DER.

  * `--ed25519` Use ED25519 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--ed448` Use ED448 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--ecc256` Use ecc256 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--ecc384` Use ecc384 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--ecc521` Use ecc521 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--rsa2048` Use rsa2048 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--rsa3072` Use rsa3072 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--rsa4096` Use rsa4096 for signing the firmware. Assume that the given KEY.DER
file is in this format.

  * `--no-sign` Disable secure boot signature verification. No signature
    verification is performed in the bootloader, and the KEY.DER argument should
    not be supplied.

#### Hash digest options

If none of the following is used, '--sha256' is assumed by default.

  * `--sha256` Use sha256 for digest calculation on binary images and public keys.

  * `--sha348` Use sha384 for digest calculation on binary images and public keys.

  * `--sha3` Use sha3-384 for digest calculation on binary images and public keys.

#### Target partition id (Multiple partition images, "self-update" feature)

If none of the following is used, "--id=1" is assumed by default. On systems
with a single image to verify (e.g. microcontroller with a single active
partition), ID=1 is the default identifier for the firmware image to stage.
ID=0 is reserved for wolfBoot 'self-update', and refers to the partition where
the bootloader itself is stored.

  * `--id N` Set image partition id to "N".

  * `--wolfboot-update` Indicate that the image contains a signed self-update
package for the bootloader. Equivalent to `--id 0`.

#### Encryption using a symmetric key

Although signed to be authenticated, by default the image is not encrypted and
it's distributed as plain text. End-to-end encryption from the firmware
packaging to the update process can be used if the firmware is stored on
external non-volatile memories. Encrypted updates can be produced using a
pre-shared, secret symmetric key,  by passing the following option:

  * `--encrypt SHAREDKEY.BIN` use the file SHAREKEY.BIN to encrypt the image.


The format of the file depends on the algorithm selected for the encryption.
If no format is specified, and the `--encrypt SHAREDKEY.BIN` option is present,
`--chacha` is assumed by default.

See options below.

  * `--chacha` Use ChaCha20 algorithm for encrypting the image. The file
    SHAREDKEY.BIN is expected to be exactly 44 bytes in size, of which 32 will
be used for the key, 12 for the initialization of the IV.

  * `--aes128` Use AES-128 algorithm in counter mode for encrypting the image. The file
    SHAREDKEY.BIN is expected to be exactly 32 bytes in size, of which 16 will
be used for the key, 16 for the initialization of the IV.

  * `--aes256` Use AES-256 algorithm in counter mode for encrypting the image. The file
    SHAREDKEY.BIN is expected to be exactly 48 bytes in size, of which 32 will
be used for the key, 16 for the initialization of the IV.

#### Delta updates (incremental updates from a known version)

An incremental update is created using the sign tool when the following option
is provided:

  * `--delta BASE_SIGNED_IMG.BIN` This option creates a binary diff file between
    `BASE_SIGNED_IMG.BIN` and the new image signed starting from `IMAGE.BIN`. The
result is stored in a file ending in `_signed_diff.bin`.

#### Policy signing (for sealing/unsealing with a TPM)

Provides a PCR mask and digest to be signed and included in the header. The signing key is used to sign the digest.

  * `--policy policy.bin`: This argument is multi-purpose.
  By default the file should contain a 4-byte PCR mask and SHA2-256 PCR digest to be signed.
  If using `--manual-sign` then the file should contain the 4-byte PCR mask and signature.
  The PCR mask and signature will be included in the `HDR_POLICY_SIGNATURE` header tag.
  A copy of the final signed policy (including 4 byte PCR mask) will be output to `[inputname].sig`.
  Note: This may require increasing the `IMAGE_HEADER_SIZE` as two signatures will be stored in the header.

#### Adding custom fields to the manifest header

Provides a value to be set with a custom tag

   * `--custom-tlv tag len val`: Adds a TLV entry to the manifest header, corresponding
   to the type identified by `tag`, with length `len` bytes, and assigns the value `val`.
   Values can be decimal or hex numbers (prefixed by '0x').  The tag is a 16-bit number.
   Valid tags are in the range between 0x0030 and 0xFEFE.

   * `--custom-tlv-buffer tag value`: Adds a TLV entry with arbitrary length to the manifest
   header, corresponding to the type identified by `tag`, and assigns the value `value`. The
   tag is a 16-bit number. Valid tags are in the range between 0x0030 and 0xFEFE. The length
   is implicit, and is the length of the value.
   Value argument is in the form of a hex string, e.g. `--custom-tlv-buffer 0x0030 AABBCCDDEE`
   will add a TLV entry with tag 0x0030, length 5 and value 0xAABBCCDDEE.

   * `--custom-tlv-string tag ascii-string`: Adds a TLV entry with arbitrary length to the manifest
   header, corresponding to the type identified by `tag`, and assigns the value of `ascii-string`. The
   tag is a 16-bit number. Valid tags are in the range between 0x0030 and 0xFEFE. The length
   is implicit, and is the length of the `ascii-string`. `ascii-string` argument is in the form of a string,
   e.g. `--custom-tlv-string 0x0030 "Version-1"` will add a TLV entry with tag 0x0030,
   length 9 and value Version-1.

#### Three-steps signing using external provisioning tools

If the private key is not accessible, while it's possible to sign payloads using
a third-party tool, the sign mechanism can be split in three phases:

- Phase 1: Only create the sha digest for the image, and prepare an intermediate
  file that can be signed by third party tool.

This is done using the following option:

   * `--sha-only` When this option is selected, the sign tool will create an
     intermediate image including part of the manifest that must be signed,
ending in `_digest.bin`. In this case, KEY.DER contains the public part of the
key that will be used to sign the firmware in Phase 2.

- Phase 2: The intermediate image `*_digest.bin` is signed by an external tool,
  an HSM or a third party signing service. The signature is then exported in
its raw format and copied to a file, e.g. IMAGE_SIGNATURE.SIG

- Phase 3: use the following option to build the final authenticated firmware
  image, including its manifest header in front:

  * `--manual-sign` When this option is provided, the KEY.DER argument contains
    the public part of the key that was used to sign the firmware in Phase 2.
This option requires one extra argument at the end, after VERSION, which should
be the filename of the signature that was the output of the previous phase, so
`IMAGE_SIGNATURE.SIG`

For a real-life example, see the section below.

## Examples

### Signing Firmware

1. Load the private key to use for signing into `./wolfboot_signing_private_key.der`
2. Run the signing tool with asymmetric algorithm, hash algorithm, file to sign, key and version.

```sh
./tools/keytools/sign --rsa2048 --sha256 test-app/image.bin wolfboot_signing_private_key.der 1
```

Note: The last argument is the “version” number.

### Signing Firmware with External Private Key (HSM)

Steps for manually signing firmware using an external key source.

```sh
# Create file with Public Key
openssl rsa -inform DER -outform DER -in my_key.der -out rsa2048_pub.der -pubout

# Add the public key to the wolfBoot keystore using `keygen -i`
./tools/keytools/keygen --rsa2048 -i rsa2048_pub.der

# Generate Hash to Sign
./tools/keytools/sign --rsa2048 --sha-only --sha256 test-app/image.bin rsa2048_pub.der 1

# Sign hash Example (here is where you would use an HSM)
openssl pkeyutl -sign -keyform der -inkey my_key.der -in test-app/image_v1_digest.bin > test-app/image_v1.sig

# Generate final signed binary
./tools/keytools/sign --rsa2048 --sha256 --manual-sign test-app/image.bin rsa2048_pub.der 1 test-app/image_v1.sig

# Combine into factory image (0xc0000 is the WOLFBOOT_PARTITION_BOOT_ADDRESS)
tools/bin-assemble/bin-assemble factory.bin 0x0 wolfboot.bin \
                              0xc0000 test-app/image_v1_signed.bin
```
