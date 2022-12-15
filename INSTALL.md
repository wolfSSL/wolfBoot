# wolfBoot Setup Instructions

## Gathering Sources

### Git Command Line

```sh
git clone https://github.com/wolfSSL/wolfBoot.git
cd wolfBoot
git submodule init
git submodule update
```

### Using browser only

Download these:
https://github.com/wolfSSL/wolfBoot/archive/master.zip
https://github.com/wolfSSL/wolfssl/archive/master.zip

1. Extract wolfBoot
2. `cd wolfBoot/lib`
3. Extract wolfSSL (should be named `lib/wolfssl`)

Directory should look like:

```
wolfBoot
 -> config
   -> examples (sample configurations)
 -> docs (markdown docs)
 -> hal (Hardware target abstractions)
   -> spi
   -> uart
 -> IDE
 -> include
 -> lib (wolfSSL and wolfTPM submodules)
   -> wolfssl
     -> src
     -> wolfcrypt/src
 -> src
   -> image.c (crypto verify/hash)
   -> loader.c (main)
   -> libwolfboot.c (User application API’s)
   -> update_*.c (flash/ram wolfBoot_start)
-> test-app (example applications)
-> tools
  -> keytools (signing and key generation tools)
```

## Configuration

Use `make config` to walk-through setting up the platform, architecture and partition settings.
OR
Use the `config/examples` as a template to wolfBoot root as `.config`.
Example: `cp ./config/examples/zynqmp.config .config`

## Setup the Key

Build the key tools: `make keytools`

The key generation is handled the first time you use `make`, however we do provide some tools to help use existing keys.
See tools in `tools/keytool`. Public key(s) are populated into the `src/keystore.c`.
The signing key used goes into wolfBoot root (example `rsa4096.der`).

## Building

```sh
make
```

The “make [target]”
* `keytools`: Build the C version of the key tools
* `wolfboot.bin`: Build the .elf and .bin version of the bootloader only
* `test-app/image.bin`: Builds the test application
* `test-app/image_v1_signed.bin`: Builds the test application signed with version 1
* `factory.bin`: Builds bootloader and test application signed and appended together

Note: Default is “factory.bin”


## Building with Cross Compile

QNX Example:

```sh
source ~/qnx700/qnxsdp-env.sh
make CROSS_COMPILE=aarch64-unknown-nto-qnx7.0.0-
```
