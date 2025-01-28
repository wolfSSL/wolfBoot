## wolfBoot port for rp2350 (Raspberry pi pico 2)

### Support for TrustZone

By default, TZEN=1 is enabled in the provided configuration. wolfBoot will run
from the Secure domain, and will stage the application in the Non-Secure domain.

The flash memory is divided as follows:

- wolfBoot partition (0x10000000 - 0x1003FFFF), 224 KB
- Non-secure callable partition (for secure gateway) (0x10038000 - 0x1003FFFF), 32 KB
- Boot partition (0x10040000 - 0x1007FFFF), 768 KB
- Update partition (0x10100000 - 0x1013FFFF), 768 KB
- Unused flash space (0x101C1000 - 0x101FFFFF), 252 KB
- Swap space (0x101C0000 - 0x101C0FFF), 4 KB

The SRAM bank0 is assigned to the Secure domain, and enforced using both SAU and `ACCESS_CONTROL` registers.

- Secure SRAM0-3: 0x20000000 - 0x2003FFFF, 256 KB
- Non-secure SRAM4-7: 0x20040000 - 0x2007FFFF, 256 KB
- Non-secure stack for application SRAM8-9: 0x20080000 - 0x20081FFF, 8 KB


### Requirements

#### External debugger

As the two images (bootloader + application) are stored in different areas in
the flash memory, a SWD connector is recommended to upload the binary images
into the flash, as opposed to the default bootloader, allowing to upload
non-signed applications into a storage device.

The scripts used in this example expect a JLink to be connected to the SWD port
as documented [here](https://kb.segger.com/Raspberry_Pi_Pico).

There is documentation below on how to do this with `picotool` instead, the
scripts to error that it cannot file the JLink if you wish to use `picotool`
instead, but this can be ignored.

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

You can now edit the .config file to change partition sizes/offsets, algorithms,
disable trustzone, add/remove features, etc.

When TZEN=0, the application will run in the Secure domain.

When the configuration is complete, run `make`. This will:

- Build the key tools (keygen & sign):
- Generate the configuration header `target.h`
- Generate a new keypair (only once), and place the public key in the
keystore

The environment has now been prepared to build and flash the two images
(wolfBoot + test application).

### Building and uploading wolfBoot.bin

After preparing the configuration and creating the keypair,
return to the `IDE/pico-sdk/rp2350/` directory and run:

```
cd wolfboot
export PICO_SDK_PATH=...
./build-wolfboot.sh
```

The script above will compile wolfboot as rp2350 second-stage bootloader.
This version of wolfboot incorporates the `.boot2` sequence needed to enable
the QSPI device, provided by the pico-sdk and always embedded in all
applications.

wolfboot.bin contains the bootloader, and can be loaded into the RP2350,
starting at address 0x10000000. The script will automatically upload the binary
if a JLink debugger is connected.

If you do not have a JLink you can install the binary using:

```
picotool load build/wolfboot.uf2
```

### Building and uploading the application

```
cd ../test-app
./build-signed-app.sh
```
The script above will compile the test application and sign it with the
wolfBoot private key. The signed application is then uploaded to the boot
partition of the flash memory, at address 0x10040000.

The linker script included is modified to change the application entry point
from 0x10000000 to 0x10040400, which is the start of the application code,
taking into account the wolfBoot header size.

The application is signed with the wolfBoot private key, and the signature is
stored in the manifest header of the application binary.

The output file `build/blink_v1_signed.bin` is automatically uploaded to the
RP2350 if a JLink debugger is connected.
The application image is stored in the boot partition, starting at address
0x10040000.
The entry point of the application (0x10040400), set in the linker script
`hal/rp2350-app.ld`, is the start of the application code, taking into account
the wolfBoot header size.

To use `picotool` instead run:

```
picotool load build/blink_v1_signed.bin -o 0x10040000
```

### Testing the application

The application is a simple blinky example, which toggles the LED on the board
every 500ms.

If the above steps are successful, the LED on the board should start blinking.

The code has been tested on a Seeed studio XIAO RP2350 board and a Raspberry Pi
Pico 2 (non-WiFi version).

