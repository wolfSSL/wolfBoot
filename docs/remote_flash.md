## Remote External flash memory support via UART

wolfBoot can emulate external partitions using UART communication with a neighbor system. This feature
is particularly useful in those asynchronous multi-process architectures, where updates can be stored
with the assistance of an external processing unit.

### Bootloader setup

The option to activate this feature is `UART_FLASH=1`. This configuration option depends on the
external flash API, which means that the option `EXT_FLASH=1` is also mandatory to compile the bootloader.

The HAL of the target system must be expanded to include a simple UART driver, that will be used by the
bootloader to access the content of the remote flash using one of the UART controllers on board.

Example UART drivers for a few of the supported platforms can be found in the [hal/uart](hal/uart) directory.

The API exposed by the UART HAL extension for the supported targets is composed by the following functions:

```
int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop);
int uart_tx(const uint8_t c);
int uart_rx(uint8_t *c);
```

Consider implementing these three functions based on the provided examples if you want to use external flash memory
support on your platform, if not officially supported yet.


### Host side: UART flash server

On the remote system hosting the external partition image for the target, a simple protocol can be implemented
on top of UART messages to serve flash-access specific calls.

An example uart-flash-server daemon, designed to run on a GNU/Linux host and emulate the external partition with
a local file on the filesystem, is available in [tools/uart-flash-server](tools/uart-flash-server).


### External flash update mechanism

wolfBoot treats external UPDATE and SWAP partitions in the same way as when they are mapped on a local SPI flash.
Read and write operations are simply translated into remote procedure calls via UART, that can be interpreted by
the remote application and provide read and write access to actual storage elements which would only be accessible
by the host.

This means that after a successful update, a copy of the previous firmware will be stored in the remote partition to
provide exactly the same update mechanism that is available in all the other use cases. The only difference consist
in the way of accessing the physical storage area, but all the mechanisms at a higher level stay the same.




