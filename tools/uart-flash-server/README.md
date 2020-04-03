# UART interface for secure local updates

This directory contains a daemon that can be used on a host machine to emulate a remote
non-volatile memory on the target, using a UART channel.

## wolfBoot interface

The [Uart Flash](../../src/uart_flash.c) driver uses the same API as other external flash
memories. The target can be connected through a serial line to a host that can provide update
images by emulating the UPDATE partition on the target.

wolfBoot must be compiled with the option `UART_FLASH=1`. The back-end drivers to access the
UART interfaces are in the [uart drivers](../../hal/uart/) directory on this repository.


## ufserver usage

The 'uart flash server' (ufserver) requires two arguments:

 - The path to the signed firmware update image (e.g. `../../../test-app/image_v2_signed.bin`)
 - The serial port connected to the target (e.g. `/dev/ttyS0`)

When a new image is processed for the first time, an update flag is set automatically to indicate
that the update is available. 

The bootloader will use the image file as its update+swap partition, so the file will be modified
by wolfboot during and after an update.

## Authentication

The daemon does not perform any signature verification, nor it checks the integrity of the firmware
image passed as command line argument. wolfBoot is still in charge of verifying integrity and authenticity
of the received update, and more in general, of every image before it is staged to boot.


## Swap and fail-safe operations

When installing an update using this mechanism, wolfBoot will erase the content of the file on the
filesystem and will overwrite its content with a copy of the older version.
As long as the daemon is running, wolfBoot will still be able to roll-back to a previous version if
booting the update fails.



