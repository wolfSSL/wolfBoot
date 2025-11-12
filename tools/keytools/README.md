# Key Tools for signing and key generation

## Sign

See [code file `./tools/keytools/sign.c`](./sign.c) and documentation in [docs/Signing.md](../../docs/Signing.md).

## KeyGen and KeyStore

See [code file `./tools/keytools/keygen.c`](./keygen.c) and documentation [docs/keystore.md](../../docs/keystore.md).

## Flash OTP Keystore Generation, Primer, Startup

See documentation [docs/flash-OTP.md](../../docs/flash-OTP.md).

### Keystore Generation

Pack public keys into a single binary (`otp.bin`) formatted the way wolfBoot expects for
provisioning the device’s OTP/NVM keystore. No signing, no encryption—just a correctly laid-out image
with a header plus fixed-size "slots" for each key.

See [code file `./tools/keytools/otp/otp-keystore-gen.c`](./otp/otp-keystore-gen.c)

### Flash OTP Primer

See [code file `./tools/keytools/otp/otp-keystore-primer.c`](./otp/otp-keystore-primer.c)

## Flash OTP Startup

See [code file `./tools/keytools/otp/startup.c`](./otp/startup.c)


## Quick Start (Linux)

```
make wolfboot_signing_private_key.der SIGN=ED25519

# or

./tools/keytools/keygen --ed25519 -g wolfboot_signing_private_key.der
```

## Debugging and Development

### `DEBUG_SIGNTOOL`

Enables additional diagnostic messages that may be useful during development and initial bring-up.

### `WOLFBOOT_SHOW_INCLUDE`

Enables compile-time verbosity to indicate which `user_settings.h` file is being used.

