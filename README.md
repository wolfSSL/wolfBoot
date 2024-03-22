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


### TPM support with wolfTPM

Enables using a TPM for cryptography and keystore.
Tested using `./configure --enable-singlethreaded --enable-wolftpm --disable-dh CFLAGS="-DWOLFPKCS11_TPM_STORE" && make`.

Note: The TPM does not support DH, so only RSA and ECC are supported.


### Build options and defines

#### Define WOLFPKCS11_TPM_STORE

Use `WOLFPKCS11_TPM_STORE` storing objects in TPM NV.

#### Define WOLFPKCS11_NO_STORE

Disables storage of tokens.

#### Define WOLFPKCS11_DEBUG_STORE

Enables debugging printf's for store.

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

### wolfPKCS11 Release 1.3 (Mar 22, 2024)

**Summary**

Added Visual Studio support for wolfPKCS11. Fixes for cast warnings and portability.

**Detail**

* Fixed `C_GetAttributeValue` incorrectly erroring with `CKR_ATTRIBUTE_VALUE_INVALID` when data == NULL. The `C_GetAttributeValue` should set length if data field is NULL. (PR #27)
* Fixed several cast warnings and possible use of uninitialized. (PR #28)
* Fixed portability issues with `WOLFPKCS11_USER_SETTINGS`. (PR #28)
* Added Visual Studio support for wolfPKCS11. (PR #28)
  - This includes wolfTPM support with Windows TBS interface
* Reworked shared library versioning. (PR #29)


### wolfPKCS11 Release 1.2 (Dec 26, 2023)

**Summary**

Adds backend support for TPM 2.0 using wolfTPM. Adds AES CBC key wrap / unwrap support. Portability improvements. Improved testing with GitHub Actions.

**Detail**

* Cleanups for minor cast warning, spelling and ignore for generated test files (PR #14)
* Added support for wrap/unwrap RSA with aes_cbc_pad. (PR #15)
* Fixed setting of label for public key after creation (init ECC objects before decoding) (PR #16)
* Flush writes in key store. (PR #17)
* Added build options for embedded use (PR #18)
  - `WOLFSSL_USER_SETTINGS` to avoid including `wolfssl/options.h`
  - `WOLFPKCS11_USER_SETTINGS` to avoid including `wolfPKCS11/options.h`
  - `WOLFPKCS11_NO_TIME` to make wc_GetTime() optional (it disables brute-force protections on token login)
* Reset failed login counter only with `WOLFPKCS11_NO_TIME` (PR #18)
* Fixed argument passing in `SetMPI`/`GetMPIData` (PR #19)
* Fixed `NO_DH` ifdef gate when freeing PKCS11 object (PR #20)
* Added GitHub CI action (PR #21)
* Fixed warnings from `./autogen.sh`. Updated m4 macros. (PR #21)
* Added additional GitHub CI action tests. (PR #22)
* Added wolfPKCS11 support for using TPM 2.0 module as backend. Uses wolfTPM and supports RSA and ECC. Requires https://github.com/wolfSSL/wolfTPM/pull/311 (PR #23)
* Added CI testing for wolfPKCS11 with wolfTPM backend and single threaded. (PR #23)
* Added PKCS11 TPM NV store (enabled with `WOLFPKCS11_TPM_STORE`). Allow `WOLFPKCS11_NO_STORE` for TPM use case. (PR #23)
* Fixed compiler warnings from mingw. (PR #23)
* Added portability macro `WOLFPKCS11_NO_ENV` when setenv/getenv are not available. (PR #23)
* Fix to only require `-ldl` for non-static builds. (PR #23)
* Portability fixes. Added `NO_MAIN_DRIVER`. Support for `SINGLE_THREADED`. Add `static` to some globals. (PR #24)
* Fixes for portability where `XREALLOC` is not available. (PR #25)
* Added support for custom setenv/get env using `WOLFPKCS11_USER_ENV`. (PR #25)
* Fix for final not being called after init in edge case pin failure. (PR #25)
* Added support for hashing PIN with SHA2-256.
  - PKS11 uses scrypt, which uses multiple MB of memory and is not practical for embedded systems. (PR #25)

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

