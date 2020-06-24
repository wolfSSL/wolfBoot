# wolfBoot Key Tools

Instructions for setting up Python, wolfCrypt-py module and wolfBoot for firmware signing and key generation.

Note: There is a pure C version of the key tool available as well. See [C Key Tools](#c-key-tools) below.

## Install Python3

1. Download latest Python 3.x and run installer: https://www.python.org/downloads
2. Check the box that says Add Python 3.x to PATH

## Install wolfCrypt

```sh
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./configure --enable-keygen --enable-rsa --enable-ecc --enable-ed25519 --enable-des3 CFLAGS="-DWOLFSSL_PUBLIC_MP"
make
sudo make install
```

## Install wolfcrypt-py

```sh
git clone https://github.com/wolfSSL/wolfcrypt-py.git
cd wolfcrypt-py
sudo USE_LOCAL_WOLFSSL=/usr/local pip3 install .
```

## Install wolfBoot

```sh
git clone https://github.com/wolfSSL/wolfBoot.git
cd wolfBoot
git submodule update --init
# Setup configuration (or copy template from ./config/examples)
make config
# Build the wolfBoot binary and sign an example test application
make
```

## C Key Tools

A standalone C version of the keygen tools is available in: `./tools/keytools`. 

These can be built in `tools/keytools` using `make` or from the wolfBoot root using `make keytools`. 

If the C version of the key tools exists they will be used by wolfBoot (the default is the Python scripts).

### Windows Visual Studio

Use the `wolfBootSignTool.vcxproj` Visual Studio project to build the `sign.exe` and `keygen.exe` tools for use on Windows.


## Command Line Usage

```sh
./tools/keytools/keygen [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ]  pub_key_file.c
```

```sh
./tools/keytools/sign [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ] [--sha256 | --sha3] [--wolfboot-update] image key.der fw_version
  - or -        ./tools/keytools/sign [--sha256 | --sha3] [--sha-only] [--wolfboot-update] image pub_key.der fw_version
  - or -        ./tools/keytools/sign [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ] [--sha256 | --sha3] [--manual-sign] image pub_key.der fw_version signature.sig
```

## Signing Firmware

1. Load the private key to use for signing into `./rsa2048.der`, `./rsa4096.der` or `./ed25519.der`.
2. Run the signing tool with asymmetric algorithm, hash algorithm, file to sign, key and version.

```sh
./tools/keytools/sign --rsa2048 --sha256 test-app/image.bin rsa2048.der 1
# OR
python3 ./tools/keytools/sign.py --rsa2048 --sha256 test-app/image.bin rsa2048.der 1
```

Note: The last argument is the “version” number.

## Signing Firmware with External Private Key (HSM)

Steps for manually signing firmware using an external key source.

```sh
# Create file with Public Key
openssl rsa -inform DER -outform DER -in rsa2048.der -out rsa2048_pub.der -pubout

# Generate Hash to Sign
./tools/keytools/sign            --rsa2048 --sha-only --sha256 test-app/image.bin rsa2048_pub.der 1
# OR
python3 ./tools/keytools/sign.py --rsa2048 --sha-only --sha256 test-app/image.bin rsa4096_pub.der 1

# Sign hash Example (here is where you would use an HSM)
openssl rsautl -sign -keyform der -inkey rsa2048.der -in test-app/image_v1_digest.bin > test-app/image_v1.sig

# Generate final signed binary
./tools/keytools/sign            --rsa2048 --sha256 --manual-sign test-app/image.bin rsa2048_pub.der 1 test-app/image_v1.sig
# OR
python3 ./tools/keytools/sign.py --rsa2048 --sha256 --manual-sign test-app/image.bin rsa4096_pub.der 1 test-app/image_v1.sig

# Combine into factory image
cat wolfboot-align.bin test-app/image_v1_signed.bin > factory.bin
```
