# wolfPKCS11

PKCS#11 library that implements cryptographic algorithms using wolfSSL.


## Project Features

## Building

Build wolfSSL:

```sh
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --enable-aescfb --enable-cryptocb --enable-rsapss --enable-keygen --enable-pwdbased --enable-scrypt C_EXTRA_FLAGS="-DWOLFSSL_PUBLIC_MP -DWC_RSA_DIRECT"
make
make check
sudo make install
sudo ldconfig
```

autogen.sh requires: automake and libtool: `sudo apt-get install automake libtool`

Build wolfPKCS11:

```sh
git clone https://github.com/wolfSSL/wolfPKCS11.git
cd wolfPKCS11
./autogen.sh
./configure
make
make check
```

### Build options and defines

#### TPM support with wolfTPM

Enables using a TPM for cryptography and keystore.
Tested using `./configure --enable-singlethreaded --enable-wolftpm --disable-dh CFLAGS="-DWOLFPKCS11_TPM_STORE" && make`.

Note: The TPM does not support DH, so only RSA and ECC are supported.

##### Define WOLFPKCS11_TPM_STORE

Use `WOLFPKCS11_TPM_STORE` storing objects in TPM NV.


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

### wolfPKCS11 Release 1.1 (May 6, 2022)

* Added support for CKM_AES_CBC_PAD
* Added support for storage of token data.
* Added support encrypted private keys.
* Added CKF_LOGIN_REQUIRED to the slot flags.
* Added RSA X_509 support for signing/verifying
* Added missing `CK_INVALID_SESSION`.
* Added some missing PKCS11 types.
* Fixed building with FIPS 140-2 (fipsv2).
* Fixed `WP11_API` visibility.
* Fixed test pin to be at least 14-characters as required by FIPS HMAC.
* Fixed getting a boolean for the operations flags.
* Fixed misleading indentation fixes.
* Improve the `curve_oid` lookup with FIPS.
* Removed `config.h` from the public pkcs11.h header.
* Convert repository to GPLv3.

### wolfPKCS11 Release 1.0 (October 20, 2021)

* Initial PKCS11 support

