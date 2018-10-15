# Firmware image

## Firmware entry point

WolfBoot can only chain-load and execute firmware images from a specific entry point in memory,
which must be specified as the origin of the FLASH memory in the linker script of the embedded
application. This correspond to the first partition in the flash memory.

Multiple firmware images can be created this way, and stored in different partitions. The bootloader
will take care of moving the selected firmware to the first boot partition before chain-loading the image.

Due to the presence of an image header, the entry point of the application has a fixed additional offset 
of 256B from the beginning of the flash partition.

## Firmware image header

Each (signed) firmware image is pre-pended with a fixed-size **image header**, containing
useful information about the firmware. The **image header** is padded to fit in 256B, in order
to guarantee that the entry point of the actual firmware is stored on the flash starting from
a 256-Bytes aligned address. This ensures that the bootloader can relocate the vector table before
chain-loading the firmware the interrupt continue to work properly after the boot is complete.

![Image header](docs/png/image_header.png)

*The image header is stored at the beginning of the slot and the actual firmware image starts 256 Bytes after it*


## Firmware trailer

At the end of the actual firmware image, the signing tool stores three trailer "TLV" (type-length-value) records,
respectively containing:
    - A hash digest of the firmware, including its firmware header, obtained using SHA-256
    - A hash digest of the public key that can be used by the bootloader to verify the authenticity of the firmware. The key must already be stored with the bootloader, and this field is only used as sanity check.
    - The signature obtained by signing the hash digest of the firmware with the factory private key

These three fields are required by the bootloader to verify the integrity and the origin of the firmware image.

![Image trailers](docs/png/image_tlv.png)

*The trailer of a signed firmware contains a TLV header and three TLV records that are used by the bootloader to verify the image*

## Image signing tool

The image signing tool generates the header and trailers for the compiled image, and add them to the output file that can be then
stored on the primary slot on the device.


