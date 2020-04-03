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
3. Extact wolfSSL (should be named `lib/wolfssl`)

Directory should look like:

```
wolfBoot
 -> config
 -> docs
 -> hal
 -> IDE
 -> include
 -> lib
   -> wolfssl
     -> src
     -> wolfcrypt/src
 -> src
 -> test-app
 -> tools
```

## Configuration

Use `make config` to walk-through setting up the platform, architecture and partition settings.
OR
Use the `config/examples` as a template to wolfBoot root as `.config`. 
Example: `cp ./config/examples/zynqmp.config .config`

## Setup the Key

Use the key generation tool in `tools/keytool` or get existing keys. 
Copy `rsa4096.der` to wolfBoot root
Copy `rsa4096_pub_key.c` to `./src`

## Building

```sh
make
```

## Building with Cross Compile

QNX Example:

```sh
source ~/qnx700/qnxsdp-env.sh
make CROSS_COMPILE=aarch64-unknown-nto-qnx7.0.0-
```
