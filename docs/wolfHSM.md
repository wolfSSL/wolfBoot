
# wolfHSM Client Integration with wolfBoot

wolfBoot provides tight integration with wolfHSM when used on a select group of supported platforms. When used in this mode, wolfBoot acts as a wolfHSM client, with all cryptographic operations and key storage offloaded to the wolfHSM server as remote procedure calls (RPCs).

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

## Algorithm Support

wolfBoot supports using wolfHSM for the following algorithms:

- RSA with 2048, 3072, and 4096-bit keys for image signature verification
- ECDSA P-256, P-384, and P-521 for image signature verification
- ML-DSA level 2, 3, and 5 (depending on platform) for image signature verification
- SHA256 for image integrity verification

Encrypted images with wolfHSM is not yet supported in wolfBoot. Note that every HAL target may not support all of these algorithms. Consult the platform-specific wolfBoot documentation for details.

## Configuration Options

This section describes the configuration options available for wolfHSM client integration. Note that these options should be configured automatically by the build system for each supported platform when wolfHSM support is enabled. Consult the platform-specific documentation for details on enabling wolfHSM support.

### `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`

This option enables wolfHSM client support in wolfBoot. Without defining this option, support for wolfHSM is not compiled in and the remainder of the options have no effect.

### `WOLFBOOT_USE_WOLFHSM_PUBKEY_ID`

This option enables use of the reserved wolfHSM public key ID for firmware authentication, and is typically the desired behavior for using wolfHSM. When this option is defined, wolfBoot will use the reserved wolfHSM keyId defined by the HAL (`hsmClientKeyIdPubKey`). This option is meant to be used in conjunction with the `--nolocalkeys` keygen option, as the key material in the keystore will not be used.

If this option is not defined, cryptographic operations are still performed on the wolfHSM server, but wolfBoot assumes the key material is present in the keystore and NOT stored on the HSM. This means that wolfBoot will first load keys from the keystore, send the key material to the wolfHSM server at the time of use (cached as ephemeral keys), and finally evict the key from the HSM after usage. This behavior is typically only useful for debugging or testing scenarios, where the keys may not be pre-loaded onto the HSM. The keystore for use in this mode should not be generated with the `--nolocalkeys` option.

## HAL Implementations

In addition to the standard wolfBoot HAL functions, wolfHSM-enabled platforms must also implement or instantiate the following wolfHSM-specific items in the platform HAL:

### HAL Global Variables

- `hsmClientCtx`: A global context for the wolfHSM client. This is initialized by the HAL and passed to wolfBoot, but should not be modified by wolfBoot.
- `hsmClientDevIdHash`: The HSM device ID for hash operations. This is used to identify the HSM device to wolfBoot.
- `hsmClientDevIdPubKey`: The HSM device ID for public key operations. This is used to identify the HSM device to wolfBoot.
- `hsmClientKeyIdPubKey`: The HSM key ID for public key operations. This is used to identify the key to use for public key operations.

### HAL Functions

- `hal_hsm_init_connect()`: Initializes the connection to the wolfHSM server. This is called by wolfBoot during initialization. This should initialize the HSM client context (`hsmClientCtx`) with a valid configuration and initialize the wolfHSM client API.
- `hal_hsm_disconnect()`: Disconnects from the wolfHSM server. This is called by wolfBoot during shutdown. This should clean up the HSM client context (`hsmClientCtx`) and invoke the wolfHSM client API's cleanup function, freeing any additional allocated resources.

## wolfBoot Simulator and wolfHSM

The wolfBoot simulator supports wolfHSM with the POSIX TCP transport. It expects to communicate with the [wolfHSM example POSIX TCP server](https://github.com/wolfSSL/wolfHSM-examples/tree/main/posix/tcp/wh_server_tcp) at `127.0.0.1:1234`. See the [wolfHSM-examples README](https://github.com/wolfSSL/wolfHSM-examples/blob/main/README.md) for more information on the example POSIX TCP server.

### Building the simulator with wolfHSM support

The wolfBoot simulator supports using wolfHSM with all algorithms mentioned in [Algorithm Support](#algorithm-support). To build the simulator configured to use wolfHSM,, ensure you build with the `WOLFHSM_CLIENT=1` makefile option. This will automatically define `WOLFBOOT_USE_WOLFHSM_PUBKEY_ID`, and requires the public key corresponding to the private key that signed the image to be pre-loaded into the HSM at the keyId specified by `hsmClientKeyIdPubKey` in the simulator HAL (see the next section for details on loading keys into the HSM example server).

```sh
# Grab the HSM simulator configuration
cp config/examples/sim-wolfHSM.config .config

# Build wolfBoot with the simulator HAL configured to use wolfHSM, automatically generating keys
make

# Build and sign the test applications used in the simulated update
make test-sim-internal-flash-with-update

# The generated wolfBoot public key can be found at `wolfboot_signing_private_key_pub.der`, and
# should be loaded into the HSM at the keyId specified by `hsmClientKeyIdPubKey` as described
# in the next section
```

### Running the simulator against a wolfHSM server

First, build the wolfHSM POSIX TCP server, following the instructions in the [wolfHSM-examples README](https://github.com/wolfSSL/wolfHSM-examples/blob/main/README.md). 

Next, in a new terminal window, run the wolfHSM POSIX TCP server, loading the public key generated by the wolfBoot build process (`wolfboot_signing_private_key_pub.der`)

```sh
# Build the example server
cd wolfHSM-examples/posix/tcp/wh_server_tcp
make WOLFHSM_DIR=/path/to/wolfHSM/install

# Run the server, loading the wolfBoot public key and using keyId 0xFF (or modify keyId to match value of `hsmClientKeyIdPubKey` in `hal/sim.c`)
./Build/wh_server_tcp.elf --key /path/to/wolfboot_signing_private_key_pub.der --id 0xFF

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

Restart the wolfHSM server with the same arguments, then rerun the wolfBoot simulator to boot the new firmware and verify the update.

```sh
# In the wolfHSM server terminal window
./Build/wh_server_tcp.elf --key /path/to/wolfboot_signing_private_key_pub.der --id 0xFF

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

