# wolfPKCS11

PKCS#11 library that implements cryptographic algorithms using wolfSSL.


## Project Features

## Building

Build wolfSSL:

```
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --enable-rsapss --enable-keygen --enable-pwdbased --enable-scrypt C_EXTRA_FLAGS="-DWOLFSSL_PUBLIC_MP -DWC_RSA_DIRECT -DWOLFSSL_DH_EXTRA"
make
make check
sudo make install
sudo ldconfig
```

autogen.sh requires: automake and libtool: `sudo apt-get install automake libtool`

Build wolfPKCS11:

```
git clone https://github.com/wolfSSL/wolfPKCS11.git
cd wolfPKCS11
./autogen.sh
./configure
make
make check
```

### Build options and defines

#### Define WOLFPKCS11_NO_STORE

Disables storage of tokens.

#### Define WOLFPKCS11_CUSTOM_STORE

Removes default implementation of storage functions.
See wolfpkcs11/store.h for prototypes of functions to implement.

#### Define WOLFPKCS11_KEYPAIR_GEN_COMMON_LABEL

Sets the private key's label against the public key when generating key pairs.

## Environment variables

### WOLFPKCS11_TOKEN_PATH

Path into which files are stored that contain token data.
When not set, defaults to: /tmp

### WOLFPKCS11_NO_STORE

Set to any value to stop storage of token data.

## Release Notes

### wolfPKCS11 Release 1.0 (10/20/2021)


