# wolfBoot demo application for MPLABx IDE

Instructions to compile and test under Windows or Linux OS.


## Target platform

This example application has been configured to work on SAM E51 Curiosity Nano
evaluation board.

wolfBoot is configured in `DUAL_BANK` mode, meaning that it will use the
bank-swapping feature available on SAM E51 to update the firmware. This operation
is much faster than physically swapping the content of two partitions one sector
at a time.


wolfBoot is stored and executed at the beginning of the flash (0x00000000), while the signed
application image starts at address 0x0000 8000.


## Before starting

- Read the instructions in [Targets.md](/docs/Targets.md) before starting. If your
version of xc32 does not have a license for optimizers, wolfBoot binary image
will normally not fit in the first 32B of the flash. If this is the case, you may
want to consider compiling wolfBoot separately, by providing a .config file and
running `make`.

- The key provided in the bootloader project (in the test/keystore.c source file)
is for demo purposes only. This is a 'test' key that should not be used in
production, because in fact, it's very well known as it's distributed with the
wolfBoot sources. Please read the instructions [to generate or import your own
keys](/docs/Signing.md) if you intend to secure your target device. The
`test/keystore.c` source file generates warnings about this when included in the
build. Replace with your automatically generated `keystore.c` when you run `keygen`.


## Instructions

### Creating the keys

This step is required to compile the bootloader with a valid key.

Enter the `tools/keytools` directory and run make. This will create the `keygen` and `sign` tools.
On Windows, you can use the .sln file provided to build the keytools using Visual Studio.

Check [docs/Signing.md](/docs/Signing.md) for the detailed instructions on compiling the tools and
creating keys using the `keygen` tool.

When used with the `-g` option, the `keygen` tool will generate a keypair. The
file `wolfboot_signing_private_key.der` in the root of the repository contains
the private key that will be used to sign valid firmware images.

The file `src/keystore.c` now contains the public key that the bootloader
incorporates, to use it later to verify the image.

After generating your own private + public keys, remember to replace the file
`IDE/MPLAB/test/keystore.c` with the newly created one, `src/keystore.c`:


```
cp src/keystore.c IDE/MPLAB/test
```


### Compiling and linking the images

Now both projects (wolfboot and test-app) can be compiled and linked.
The resulting images will be placed in the output directory `dist/default/production/`
under the main directory of each of the two projects.

The only requirement for the application to be staged by wolfboot is the modified
entry point when setting the origin of the space in ROM.

With the example configuration (ecc384+sha256), the manifest header size is 512B
(0x200), and the main BOOT partition starts at 32 KB (0x8000).

The entry point for the application firmware must take the total offset into
account, by setting `ROM_ORIGIN` to 0x8200. Change this parameter accordingly if
you plan to modify the geometry of your partitions in FLASH.

The FLASH memory configuration for this demo is located in include/MPLAB/target.h.

The file can be changed manually to set a new partitions geometry, or a new target.h
could be created in the include/ directory by running `make`, which will be based
on the chosen configuration via the `.config` file. If a custom `target.h` is created
by `make`, the demo version in `include/MPLAB` can be removed.

The wolfBoot project should build with no modifications to the project, because it
does not use any .c files from the manufacturer's BSP.

On the other hand, the application uses the Harmony libraries to access the UART
USB and other peripherals. To generate the necessary files, once you open the
project in MPLAB X IDE, click on the MCC icon on top, then click on "Generate" to
download the latest Microchip libraries and device support in your local repository.


### Converting to .bin format

MPLAB ide produces executables of the bootloader and the test firmware images in
both `.elf` and `.hex` formats.

Using the `objcopy` tool from the gcc toolchain, we can convert the images to the
binary format.

To convert the wolfBoot image from `.hex` to `.bin` run:


```
arm-none-eabi-objcopy -I ihex -O binary IDE/MPLAB/bootloader/wolfboot-same51.dualbank.X/dist/default/production/wolfboot-same51.dualbank.X.production.hex mplab_wolfboot.bin

```

To convert the application the same way:

```
arm-none-eabi-objcopy -I ihex -O binary IDE/MPLAB/test-app/test-usb-updater.same51.X/dist/default/production/test-usb-updater.same51.X.production.hex mplab_app.bin
```


### Signing the application image

The test application (`mplab_app.bin`) must be now tagged with a version number
and signed. This is done by the `sign` tool.

The tool requires the location of the private key and one numeric argument, stored
as the version tag for the signed image. Running it with version "1":

```
tools/keytools/sign --ecc384 mplab_app.bin wolfboot_signing_private_key.der 1
```

will create a new file `mplab_app_v1_signed.bin`, that can be uploaded into the
flash memory at address 0x8000. The first 512 B in this binary file are
reserved by wolfBoot for the manifest header.


### Uploading the binary images to the target

Follow the instructions in [docs/Targets.md](/docs/Targets.md) to upload the binary
images (`mplab_wolfboot.bin` and `mplab_app_v1_signed.bin`) to the target.

### Verify that the system is up and running

When the application starts, it will send a few bytes through the USB UART
interface associated.

This data can be parsed with a small tool used in wolfBoot's automated tests,
`test-expect-version`.

- Compile the tool via:

```
make -C tools/test-expect-version
```

- Run the program:

```
tools/test-expect-version/test-expect-version
```

- Reset the target.

The tool will display the version of the current firmware running.
This data is sent by the application itself when it boots.

### Update the firmware via test-update-server (Linux and MacOS only)

A demo update server via USB UART is provided in the directory
`tools/test-update-server`.

To test the update mechanism, first compile the tool via:

```
make -C tools/test-update-server
```

To provide a signed update package, sign and tag a different version of the
application, e.g. for version "8" run:


```
tools/keytools/sign --ecc384 mplab_app.bin wolfboot_signing_private_key.der 8
```

Run the update server:

```
tools/test-update-server/server mplab_app_v8_signed.bin
```

Reset the target. The application will initiate an update request, to which
the server will reply and will send the signed image through the USB UART.

The server should display the progress of the update on screen.

At the end of the update, verify that wolfboot has swapped the mapping of the
two FLASH banks, cloned itself so it sits at the beginning of both banks, and
correctly verified and staged the new firmware, by checking via
`test-expect-version` that the current firmware running has version '8'.



