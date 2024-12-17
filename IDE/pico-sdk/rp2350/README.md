## wolfBoot port for rp2350 (Raspberry pi pico 2)

### Requirements 

#### External debugger

As the two images (bootloader + application) are stored in different areas in
the flash memory, a SWD connector is required to upload the binary images into
the flash, as opposed to the default bootloader, allowing to upload non-signed
applications into a storage device

#### PicoSDK

Clone the repository from raspberrypi's github: 

```
git clone https://github.com/raspberrypi/pico-sdk.git
```

Export the `PICO_SDK_PATH` environment variable to point to the pico-sdk directory:

```
export PICO_SDK_PATH=/path/to/pico-sdk
```

### Configuring wolfBoot to build with pico-sdk

From wolfBoot root directory, copy the example configuration:

```
cp config/examples/rp2350.config .config
```

By default, the config file indicates the following partition layout:

```
wolfBoot partition: 256 KB, at address 0x10000000 to 0x1003FFFF
Boot partition:     768 KB, at address 0x10040000 to 0x1007FFFF
Update partition:   768 KB, at address 0x10100000 to 0x1013FFFF
Swap space:           4 KB, at address 0x101C0000 to 0x101C0FFF
Unused flash space: 252 KB, at address 0x101C1000 to 0x101FFFFF
```

You can now edit the .config file to change partition sizes/offsets, algorithms,
add/remove features, etc.

When the configuration is complete, run `make`. This will:

- Build the key tools (keygen & sign):
- Generate the configuration header `target.h`
- Generate a new keypair (only once), and place the public key in the
keystore

The environment has now been prepared to build and flash the two images
(wolfBoot + test application).

### Building and uploading wolfBoot.bin

After preparing the configuration and creating the keypair,
return to this directory and run:

```
cd wolfboot
./build-wolfboot.sh
```

The script above will compile wolfboot as rp2350 second-stage bootloader.
This version of wolfboot incorporates the `.boot2` sequence needed to enable
the QSPI device, provided by the pico-sdk and always embedded in all
applications.

wolfboot.bin contains the bootloader, configured as follows:

### Building and uploading the application

```
cd test-app
./build-signed-app.sh
```
The script above will compile the test application and sign it with the
wolfBoot private key. The signed application is then uploaded to the boot
partition of the flash memory, at address 0x10040000.

The linker script included is modified to change the application entry point
from 0x10000000 to 0x10040400, which is the start of the application code,
taking into account the wolfBoot header size.


### Testing the application

The application is a simple blinky example, which toggles the LED on the board
every 500ms.

If the above steps are successful, the LED on the board should start blinking.

