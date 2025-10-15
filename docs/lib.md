# Build wolfBoot as Library

Instead of building as standalone repository, wolfBoot can be built as
a secure-boot library and integrated in third party bootloaders, custom
staging solutions etc.


## Library API

The wolfBoot secure-boot image verification has a very simple interface.
The core object describing the image is a `struct wolfBoot_image`, which
is initialized when `wolfBoot_open_image_address()` is called. The signature is:


`int wolfBoot_open_image_address(struct wolfBoot_image* img, uint8_t* image)`


where `img` is a pointer to a local (uninitialized) structure of type `wolfBoot_image`, and
`image` is a pointer to where the signed image is mapped in memory, starting from the beginning
of the manifest header.


On success, zero is returned. If the image does not contain a valid 'magic number' at the beginning
of the manifest, or if the size of the image is bigger than `WOLFBOOT_PARTITION_SIZE`, -1 is returned.


If the `open_image_address` operation is successful, two other functions can be invoked:


- `int wolfBoot_verify_integrity(struct wolfBoot_image *img)`


This function verifies the integrity of the image, by calculating the SHA hash of the image content,
and comparing it with the digest stored in the manifest header. `img` is a pointer to
an object of type `wolfBoot_image`, previously initialized by `wolfBoot_open_image_address`.


0 is returned if the image integrity could be successfully verified, -1 otherwise.



- `int wolfBoot_verify_authenticity(struct wolfBoot_image *img)`


This function verifies that the image content has been signed by a trusted
counterpart (i.e. that could be verified using one of the public keys available).


`0` is returned in case of successful authentication,  `-1` if anything went wrong during the operation,
and `-2` if the signature could be found, but was not possible to authenticate against its public key.

## Library mode: example application

An example application is provided in `hal/library.c`.

The application `test-lib` opens a file from a path passed as argument
from the command line, and verifies that the file contains a valid, signed
image that can be verified for integrity and authenticity using wolfBoot in library
mode.

## Configuring and compiling the test-lib application

Step 1: use the provided configuration to compile wolfBoot in library mode:

```
cp config/examples/library.config .config
```

Step 2: create a file `target.h` that only contains the following lines:

```
cat > include/target.h << EOF
#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#define WOLFBOOT_NO_PARTITIONS

#define WOLFBOOT_SECTOR_SIZE                 0x20000
#define WOLFBOOT_PARTITION_SIZE              0x20000

#endif /* !H_TARGETS_TARGET_ */

EOF
```

Change `WOLFBOOT_PARTITION_SIZE` accordingly. `wolfBoot_open_image_address()` will discard images larger than
`WOLFBOOT_PARTITION_SIZE` - `IMAGE_HEADER_SIZE`.


Step 3: compile keytools and create keys.

```
make keytools
./tools/keytools/keygen --ed25519 -g wolfboot_signing_private_key.der
```


Step 4: Create an empty file and sign it using the private key.

```
touch empty
./tools/keytools/sign --ed25519 --sha256 empty wolfboot_signing_private_key.der 1
```


Step 5: compile the test-lib application, linked with wolfBoot in library mode, and the
public key from the keypair created at step 4.

```
make test-lib
```

Step 6: run the application with the signed image

```
./test-lib empty_v1_signed.bin
```

If everything went right, the output should be similar to:

```
Firmware Valid
booting 0x5609e3526590(actually exiting)
```

## Library mode: Partition Manager CLI Example

An example application using filesystem access is provided in `hal/library_fs.c`.

The CLI application `lib-fs` allow querying partition states, triggering updates, and marking the boot partition as successful.

### Building the lib-fs example

Step 1: use the example configuration to compile wolfBoot in library mode:

```
cp config/examples/library_fs.config .config
```

Step 2: Adjust the configuration to fit your partition layout and file path.

Step 3: Build the CLI application:

```
make lib-fs
```

This will produce the `lib-fs` executable.

### Using the Partition Manager CLI

The example configuration points the binary to access `/dev/mtd0` for partition data. You can simulate this file path with `modprobe mtdram total_size=16384 erase_size=128`. You may need to adjust the file permissions to allow read/write access.

Run the application with one of the supported commands:

```
./lib-fs <command>
```

Available commands:

- `status`         : Show state of all partitions
- `get-boot`       : Get BOOT partition state
- `get-update`     : Get UPDATE partition state
- `update-trigger` : Trigger an update (sets UPDATE partition to UPDATING)
- `success`        : Mark BOOT partition as SUCCESS
- `help`           : Show usage information

#### Example usage

Show all partition states:
```
./lib-fs status
```

Trigger an update:
```
./lib-fs update-trigger
```

Mark the boot partition as successful:
```
./lib-fs success
```
