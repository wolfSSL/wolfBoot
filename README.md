# wolfPKCS11

PKCS#11 library that implements cryptographic algorithms using wolfSSL.


## Project Features

## Building

Build wolfSSL:

```
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --enable-rsapss --enable-keygen --enable-pwdbased --enable-scrypt C_EXTRA_FLAGS="-DWOLFSSL_PUBLIC_MP"
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


### Build options and defines


## Release Notes

### wolfPKCS11 Release 1.0 (10/20/2021)


