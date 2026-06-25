
# wolfHSM Integration with wolfBoot

wolfBoot provides tight integration with wolfHSM when used on a select group of supported platforms. wolfBoot can operate in two modes with wolfHSM:

1. **wolfHSM Client Mode**: wolfBoot acts as a wolfHSM client, with all cryptographic operations and key storage offloaded to an external wolfHSM server as remote procedure calls (RPCs).

2. **wolfHSM Server Mode**: wolfBoot runs on the wolfHSM server, directly using the wolfHSM API for operations like secure key storage and certificate chain verification.

For an introduction to wolfHSM, please refer to the [wolfHSM Manual](https://wolfSSL.com/https://www.wolfssl.com/documentation/manuals/wolfhsm/) and [wolfHSM GitHub Repository](https://github.com/wolfssl/wolfHSM.git).

## Key Features

1. Secure key storage: Keys used for authentication and encryption are stored securely on the wolfHSM server
2. Remote cryptographic operations: All cryptographic operations are performed on the wolfHSM server
3. Flexible key management: Keys can be updated or rotated on the wolfHSM server without requiring an update to wolfBoot

## Supported Platforms

wolfBoot supports using wolfHSM on the following platforms:

- wolfBoot simulator (using wolfHSM POSIX TCP transport)
- AURIX TC3xx (shared memory transport)
- STM32H5 TrustZone (the secure-side wolfBoot hosts a wolfHSM server and exposes it to the non-secure application through a single NSC veneer; see [STM32H5 TrustZone Engine](#stm32h5-trustzone-engine) below)

Details on configuring wolfBoot to use wolfHSM on each of these platforms can be found in the wolfBoot (and wolfHSM) documentation specific to that target, with the exception of the simulator, which is documented here. The remainder of this document focuses on the generic wolfHSM-related configuration options.

## Client Algorithm Support

wolfBoot supports using wolfHSM to offlad the following algorithms to the HSM server:

- RSA with 2048, 3072, and 4096-bit keys for image signature verification
- ECDSA P-256, P-384, and P-521 for image signature verification
- ML-DSA security levels 2, 3, and 5 (depending on platform) for image signature verification
- SHA256 for image integrity verification

Encrypted images with wolfHSM is not yet supported in wolfBoot. Note that every HAL target may not support all of these algorithms. Consult the platform-specific wolfBoot documentation for details.

## Additional Features

wolfBoot with wolfHSM also supports the following features:

### Certificate Verification

wolfBoot with wolfHSM supports certificate chain verification for firmware images. In this mode, instead of using raw public keys for signature verification, wolfBoot verifies firmware images using wolfHSM with a public key embedded in a certificate chain that is included in the image manifest header.

The certificate verification process with wolfHSM works as follows:

1. A root CA is created serving as the root of trust for the entire PKI system
2. A signing keypair and corresponding identity certificate is created for signing firmware images
3. The firmware image is signed with the signing private key
4. A certificate chain is created consisting of the signing identity certificate and an optional number of intermediate certificates, where trust is chained back to the root CA.
5. During the signing process, the image is signed with the signer private key and the certificate chain is embedded in the firmware image header.
6. During boot, wolfBoot extracts the certificate chain from the firmware header
7. wolfBoot uses the wolfHSM server (remotely or directly, depending on configuration) to verify the certificate chain against a pre-provisioned root CA certificate stored on the HSM and caches the public key of the leaf certificate if the chain verifies as trusted
8. If the chain is trusted, wolfBoot uses the cached public key from the leaf certificate to verify the firmware signature on the wolfHSM server

To use certificate verification with wolfHSM:

1. Enable `WOLFBOOT_CERT_CHAIN_VERIFY` in your wolfBoot configuration
2. Ensure the wolfHSM server is configured with certificate manager support (`WOLFHSM_CFG_CERTIFICATE_MANAGER`)
3. Pre-provision one or more root CA certificates on the wolfHSM server at the NVM IDs listed in the HAL `hsmNvmIdCertRootCAList`. Verification succeeds if the embedded chain anchors to *any* root in the list (absent NVM IDs are silently skipped). The list length must not exceed `WOLFHSM_CFG_CERT_MAX_VERIFY_ROOTS` (default 8).
4. Sign firmware images with the `--cert-chain` option, providing a DER-encoded certificate chain

To build the simulator using wolfHSM for certificate verification:

- **Client Mode**: Use [config/examples/sim-wolfHSM-client-certchain.config](config/examples/sim-wolfHSM-client-certchain.config)
- **Server Mode**: Use [config/examples/sim-wolfHSM-server-certchain.config](config/examples/sim-wolfHSM-server-certchain.config)

## Configuration Options

This section describes the configuration options available for wolfHSM integration. Note that these options should be configured automatically by the build system for each supported platform when wolfHSM support is enabled. Consult the platform-specific documentation for details on enabling wolfHSM support.

### `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`

This option enables wolfHSM client support in wolfBoot. Without defining this option, support for wolfHSM client mode is not compiled in.

In client mode, wolfBoot always uses HSM-resident public keys for firmware authentication; public keys are never baked into a local `keystore.c`. The key to verify against is referenced either by the reserved key ID defined in the HAL (`hsmKeyIdPubKey`), or, when certificate-chain verification (`WOLFBOOT_CERT_CHAIN_VERIFY`) is enabled, by the leaf key ID resolved from the verified chain.

### `WOLFBOOT_ENABLE_WOLFHSM_SERVER`

This option enables wolfHSM server support in wolfBoot. When defined, wolfBoot includes an embedded wolfHSM server that provides HSM functionality locally within the bootloader. This is mutually exclusive with `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`.

## HAL Implementations

In addition to the standard wolfBoot HAL functions, wolfHSM-enabled platforms must also implement or instantiate the following wolfHSM-specific items in the platform HAL:

### HAL Global Variables

- `hsmClientCtx`: A global context for the wolfHSM client. This is initialized by the HAL and passed to wolfBoot, but should not be modified by wolfBoot. Only used when building with `WOLFHSM_ENABLE_WOLFHSM_CLIENT`.
- `hsmServerCtx`: A global context for the wolfHSM server. This is initialized by the HAL and used by wolfBoot for all HSM operations. Only used when building with `WOLFHSM_ENABLE_WOLFHSM_SERVER`

- `hsmDevIdHash`: The HSM device ID for hash operations. This is used to identify the HSM device to wolfBoot.
- `hsmDevIdPubKey`: The HSM device ID for public key operations. This is used to identify the HSM device to wolfBoot.
- `hsmKeyIdPubKey`: The HSM key ID for public key operations. This is used to identify the key to use for public key operations.
- `hsmNvmIdCertRootCAList` / `hsmNvmIdCertRootCACount`: Array of NVM IDs identifying the trusted root CA certificate(s) and its element count. Only used when building with `WOLFBOOT_CERT_CHAIN_VERIFY`. The chain in the firmware header may anchor to any of the listed roots; the count is bounded by `WOLFHSM_CFG_CERT_MAX_VERIFY_ROOTS` (default 8). Each in-tree HAL provides a default of `{ 1 }`; override the list via the `WOLFHSM_NVM_ROOT_CA_LIST` build option, which takes a comma-separated initializer (no quotes, no spaces) and is propagated to the HAL as `-DWOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST=...`. Set it in `.config` (e.g. `WOLFHSM_NVM_ROOT_CA_LIST=1,2,3`) or on the make command line (`make WOLFHSM_NVM_ROOT_CA_LIST=1,2,3 ...`).

### Client HAL Functions

- `hal_hsm_init_connect()`: Initializes the connection to the wolfHSM server. This is called by wolfBoot during initialization. This should initialize the HSM client context (`hsmClientCtx`) with a valid configuration and initialize the wolfHSM client API.
- `hal_hsm_disconnect()`: Disconnects from the wolfHSM server. This is called by wolfBoot during shutdown. This should clean up the HSM client context (`hsmClientCtx`) and invoke the wolfHSM client API's cleanup function, freeing any additional allocated resources.

### Server HAL Functions

- `hal_hsm_server_init()`: Initializes the embedded wolfHSM server. This is called by wolfBoot during initialization. This should initialize the HSM server context (`hsmServerCtx`) with a valid configuration, initialize the NVM subsystem, and start the wolfHSM server.
- `hal_hsm_server_cleanup()`: Cleans up the embedded wolfHSM server. This is called by wolfBoot during shutdown. This should clean up the HSM server context and free any allocated resources.

## wolfBoot Simulator and wolfHSM

The wolfBoot simulator supports using wolfHSM in both client and server modes:

### wolfHSM Client Mode

The simulator supports wolfHSM client mode with the POSIX TCP transport. It expects to communicate with the [wolfHSM example POSIX TCP server](https://github.com/wolfSSL/wolfHSM-examples/tree/main/posix/tcp/wh_server_tcp) at `127.0.0.1:1234`. See the [wolfHSM-examples README](https://github.com/wolfSSL/wolfHSM-examples/blob/main/README.md) for more information on the example POSIX TCP server.

### wolfHSM Server Mode

The simulator also supports an embedded wolfHSM server mode where wolfBoot includes the complete wolfHSM server functionality. In this mode, no external wolfHSM server is required, and all HSM operations are performed locally within wolfBoot using the file-based NVM simulator for storage.

### Building the simulator with wolfHSM support

The wolfBoot simulator supports using wolfHSM with all algorithms mentioned in [Algorithm Support](#algorithm-support).

#### wolfHSM Client Mode Build

To build the simulator configured to use wolfHSM client mode, ensure you build with the `WOLFHSM_CLIENT=1` makefile option. This requires the public key corresponding to the private key that signed the image to be pre-loaded into the HSM at the keyId specified by `hsmKeyIdPubKey` in the simulator HAL.

```sh
# Grab the HSM client simulator configuration
cp config/examples/sim-wolfHSM-client.config .config

# Build wolfBoot with the simulator HAL configured to use wolfHSM client, automatically generating keys
make

# Build and sign the test applications used in the simulated update
make test-sim-internal-flash-with-update

# The generated wolfBoot public key can be found at `wolfboot_signing_private_key_pub.der`, and
# should be loaded into the HSM at the keyId specified by `hsmKeyIdPubKey` as described
# in the next section
```

#### wolfHSM Server Mode Build

To build the simulator configured to use embedded wolfHSM server mode, use the `WOLFHSM_SERVER=1` makefile option. In this mode, wolfBoot includes the complete wolfHSM server and no external HSM server is required. Currently the wolfHSM server only supports the certificate chain verification mode of authentication.

```sh
# Grab the HSM server simulator configuration (with certificate chain verification)
cp config/examples/sim-wolfHSM-server-certchain.config .config

# Build wolfBoot with the embedded wolfHSM server.
make

# Build and sign the test applications used in the simulated update. This generates a dummy CA and certificate chain for your public key.
make test-sim-internal-flash-with-update

```

### Running the simulator with wolfHSM

#### wolfHSM Client Mode

When using wolfHSM client mode, you need to run an external wolfHSM server.

First, build the wolfHSM POSIX TCP server, following the instructions in the [wolfHSM example README](github.com/wolfSSL/wolfHSM/blob/main/examples/README.md)

Next, in a new terminal window, run the wolfHSM POSIX TCP server, loading the public key generated by the wolfBoot build process (`wolfboot_signing_private_key_pub.der`)

```sh
# Build the example server
cd lib/wolfHSM/examples/posix/tcp/wh_server_tcp
make WOLFSSL_DIR=../../../../../wolfssl

# Run the server, loading the wolfBoot public key. The client ID (--client) must
# match WOLFHSM_CLIENT_ID from the build config (default 1) and the keyId (--id)
# must match hsmKeyIdPubKey in `hal/sim.c` (255 / 0xFF).
./Build/wh_server_tcp.elf --client 1 --id 255 --key ../../../../../../wolfboot_signing_private_key_pub.der &

# The server will now be waiting for connections
```

Run the wolfBoot simulator against the server with the appropriate arguments to report the firmware version and stage an update

```sh
# in the wolfBoot terminal window
./wolfboot.elf update_trigger get_version

# The following output should indicate that the update was staged successfully
Simulator assigned ./internal_flash.dd to base 0x7f7fcbd80000
Boot partition: 0x7f7fcbe00000 (size 745400, version 0x1)
Update partition: 0x7f7fcbf00000 (size 745400, version 0x2)
Boot partition: 0x7f7fcbe00000 (size 745400, version 0x1)
Booting version: 0x1
Simulator assigned ./internal_flash.dd to base 0x7f6665679000
hal_flash_erase addr 0x7f66658f7000 len 4091
1
```

Rerun the wolfBoot simulator to boot the new firmware and verify the update.

```sh
# In the wolfBoot terminal window, run the following to update the image and confirm the update
./wolfboot.elf success get_version

# The output should ultimately print the following, indicating the update was successful

#... lots of output ...
#
Boot partition: 0x7f3a2f96b000 (size 745400, version 0x2)
Update partition: 0x7f3a2fa6b000 (size 745400, version 0x1)
Copy sector 254 (part 0->2)
hal_flash_erase addr 0x7f3a2fb6b000 len 4096
hal_flash_erase addr 0x7f3a2fa69000 len 4096
hal_flash_erase addr 0x7f3a2fa6a000 len 4096
hal_flash_erase addr 0x7f3a2fa69000 len 4096
hal_flash_erase addr 0x7f3a2fb6a000 len 4096
Boot partition: 0x7f3a2f96b000 (size 745400, version 0x2)
Booting version: 0x2
Simulator assigned ./internal_flash.dd to base 0x7fe902d2e000
2
```

#### wolfHSM Server Mode

When using wolfHSM server mode, no external server is required. wolfBoot includes the embedded wolfHSM server functionality. The only requirement is a wolfHSM simulated NVM image must be created that the server can use with the root CA for certificate verification stored at the expected NVM ID.

```sh
# Create a simulated NVM image for the POSIX flash file simulator containing the dummy root CA for cert chain verification.
# You must build whnvmtool first if you haven't already, and ensure the file name matches the simulated NVM image file
# name in hal/sim.c
./lib/wolfHSM/tools/whnvmtool/whnvmtool --image=wolfBoot_wolfHSM_NVM.bin --size 16348 --invert-erased-byte tools/scripts/wolfBoot-wolfHSM-sim-dummy-certchain.nvminit

# Run the wolfBoot simulator with embedded wolfHSM server to stage an update
./wolfboot.elf update_trigger get_version

# Run the simulator again to boot the new firmware and verify the update
./wolfboot.elf success get_version
```

The embedded wolfHSM server will automatically handle all cryptographic operations and key management using the file-based NVM storage(`wolfBoot_wolfHSM_NVM.bin`) that was generated above.

## STM32H5 TrustZone Engine

On STM32H5, wolfBoot can host a wolfHSM server in the secure TrustZone image and expose it to the non-secure application through a single non-secure-callable veneer (`wcs_wolfhsm_transmit`). The non-secure side runs the standard wolfHSM client API, which auto-registers a wolfCrypt cryptocb under `WH_DEV_ID`, so application-level wolfCrypt calls that pass that device ID transparently round-trip to the secure server.

This is a separate deployment shape from the wolfHSM client/server modes documented above; it does not use `WOLFBOOT_ENABLE_WOLFHSM_CLIENT/SERVER` or the `hsmClientCtx`/`hsmServerCtx` HAL hooks, and is mutually exclusive with the other STM32H5 TrustZone engines (`WOLFCRYPT_TZ_PKCS11`, `WOLFCRYPT_TZ_PSA`, `WOLFCRYPT_TZ_FWTPM`).

### Build

```sh
cp config/examples/stm32h5-tz-wolfhsm.config .config
make
```

By default the auto-test prints a UART pass/fail line and idles in `while (1)`, which is safe on real silicon. For emulator or under-debugger runs that signal pass/fail via a breakpoint, add `WOLFBOOT_TZ_TEST_BKPT=1` so the auto-test issues `bkpt` instead (this HardFaults on real silicon without a debugger attached):

```sh
make WOLFBOOT_TZ_TEST_BKPT=1
```

### Flash

The wolfBoot helper programs the option bytes the secure boot path requires (`TZEN`, `SECBOOTADD`, `SECWM1`/`SECWM2`); see [STM32-TZ.md](STM32-TZ.md) for the option-byte details:

```sh
./tools/scripts/set-stm32-tz-option-bytes.sh
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08060000
```

### Test

The non-secure test application runs the wolfHSM auto-test at startup. A successful first boot ends with:

```text
wolfHSM CommInit ok (client=1 server=...)
wolfHSM RNG ok: <16 random bytes>
wolfHSM SHA256 ok
wolfHSM AES ok
wolfHSM first boot path, committing key to NVM
wolfHSM NSC tests passed
```

The default build prints a final `WOLFHSM_TZ_TEST_PASS` UART line on success. The `WOLFBOOT_TZ_TEST_BKPT=1` build raises `bkpt #0x7d` on first-boot success and `bkpt #0x7f` on second-boot success (after the persisted key is reloaded from flash on reset) instead, for emulator and debugger runs. Reset the board (no re-flash) to verify persistence; the second boot prints `wolfHSM second boot path, restored persisted key`.

### Notes

The wolfHSM NVM lives in the existing `FLASH_KEYVAULT` region (112 KiB at `0x0C040000`) shared with the other STM32H5 TrustZone engines. The flash adapter (`src/wolfhsm_flash_hal.c`) caches the affected sector, modifies it, and rewrites the whole 8 KiB sector in one erase + program cycle, mirroring `psa_store.c` / `pkcs11_store.c`. This satisfies the H5 quad-word ECC rule that each 16-byte unit may be programmed exactly once between erases, which wolfHSM's 8-byte-unit writes would otherwise violate.
