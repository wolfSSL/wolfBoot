
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
3. Pre-provision the root CA certificate on the wolfHSM server at the NVM ID specified by the HAL `hsmNvmIdCertRootCA`
4. Sign firmware images with the `--cert-chain` option, providing a DER-encoded certificate chain

To build the simulator using wolfHSM for certificate verification:

- **Client Mode**: Use [config/examples/sim-wolfHSM-client-certchain.config](config/examples/sim-wolfHSM-client-certchain.config)
- **Server Mode**: Use [config/examples/sim-wolfHSM-server-certchain.config](config/examples/sim-wolfHSM-server-certchain.config)

## Configuration Options

This section describes the configuration options available for wolfHSM integration. Note that these options should be configured automatically by the build system for each supported platform when wolfHSM support is enabled. Consult the platform-specific documentation for details on enabling wolfHSM support.

### `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`

This option enables wolfHSM client support in wolfBoot. Without defining this option, support for wolfHSM client mode is not compiled in.

### `WOLFBOOT_ENABLE_WOLFHSM_SERVER`

This option enables wolfHSM server support in wolfBoot. When defined, wolfBoot includes an embedded wolfHSM server that provides HSM functionality locally within the bootloader. This is mutually exclusive with `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`.

### `WOLFBOOT_USE_WOLFHSM_PUBKEY_ID`

This option enables use of the reserved wolfHSM public key ID for firmware authentication, and is typically the desired behavior for using wolfHSM. When this option is defined, wolfBoot will use the reserved wolfHSM keyId defined by the HAL (`hsmKeyIdPubKey`). This option is meant to be used in conjunction with the `--nolocalkeys` keygen option, as the key material in the keystore will not be used.

If this option is not defined, cryptographic operations are still performed on the wolfHSM server, but wolfBoot assumes the key material is present in the keystore and NOT stored on the HSM. This means that wolfBoot will first load keys from the keystore, send the key material to the wolfHSM server at the time of use (cached as ephemeral keys), and finally evict the key from the HSM after usage. This behavior is typically only useful for debugging or testing scenarios, where the keys may not be pre-loaded onto the HSM. The keystore for use in this mode should not be generated with the `--nolocalkeys` option.

## HAL Implementations

In addition to the standard wolfBoot HAL functions, wolfHSM-enabled platforms must also implement or instantiate the following wolfHSM-specific items in the platform HAL:

### HAL Global Variables

- `hsmClientCtx`: A global context for the wolfHSM client. This is initialized by the HAL and passed to wolfBoot, but should not be modified by wolfBoot. Only used when building with `WOLFHSM_ENABLE_WOLFHSM_CLIENT`.
- `hsmServerCtx`: A global context for the wolfHSM server. This is initialized by the HAL and used by wolfBoot for all HSM operations. Only used when building with `WOLFHSM_ENABLE_WOLFHSM_SERVER`

- `hsmDevIdHash`: The HSM device ID for hash operations. This is used to identify the HSM device to wolfBoot.
- `hsmDevIdPubKey`: The HSM device ID for public key operations. This is used to identify the HSM device to wolfBoot.
- `hsmKeyIdPubKey`: The HSM key ID for public key operations. This is used to identify the key to use for public key operations.

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

To build the simulator configured to use wolfHSM client mode, ensure you build with the `WOLFHSM_CLIENT=1` makefile option. This will automatically define `WOLFBOOT_USE_WOLFHSM_PUBKEY_ID`, and requires the public key corresponding to the private key that signed the image to be pre-loaded into the HSM at the keyId specified by `hsmKeyIdPubKey` in the simulator HAL.

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

# Run the server, loading the wolfBoot public key and using the client ID and keyId matching the values declared in `hal/sim.c`)
./Build/wh_server_tcp.elf --client 12 --id 255 --key ../../../../../../wolfboot_signing_private_key_pub.der &

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
