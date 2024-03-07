# Firmware image

## Firmware entry point

WolfBoot can only chain-load and execute firmware images from a specific entry point in memory,
which must be specified as the origin of the FLASH memory in the linker script of the embedded
application. This corresponds to the first partition in the flash memory.

Multiple firmware images can be created this way, and stored in two different partitions. The bootloader
will take care of moving the selected firmware to the first (BOOT) partition before chain-loading the image.

Due to the presence of an image header, the entry point of the application has a fixed additional offset
of 256B from the beginning of the flash partition.

## Firmware image header

Each (signed) firmware image is prepended with a fixed-size **image header**, containing
useful information about the firmware. The **image header** is padded to fit in 256B, in order
to guarantee that the entry point of the actual firmware is stored on the flash starting from
a 256-Bytes aligned address. This ensures that the bootloader can relocate the vector table before
chain-loading the firmware the interrupt continue to work properly after the boot is complete.

![Image header](png/image_header.png)

*The image header is stored at the beginning of the slot and the actual firmware image starts 256 Bytes after it*

### Image header: Tags

The **image header** is prepended with a single 4-byte magic number, followed by a 4-byte field indicating the
firmware image size (excluding the header). All numbers in the header are stored in Little-endian format.

The two fixed fields are followed by one or more tags. Each TAG is structured as follows:

  - 2 bytes indicating the **Type**
  - 2 bytes indicating the **size** of the tag, excluding the type and size bytes
  - ***N*** bytes of tag content

With the following exception:
  - A '0xFF' in the Type field indicate a simple padding byte. The 'padding' byte has no **size** field, and the next byte should be processed as **Type** again.

Each **Type** has a different meaning, and integrate information about the firmware. The following Tags are mandatory for validating the firmware image:

  - A 'version' Tag (type: 0x0001, size: 4 Bytes) indicating the version number for the firmware stored in the image
  - A 'timestamp' Tag (type: 0x0002, size 8 Bytes) indicating the timestamp in unix seconds for the creation of the firmware
  - A 'sha digest' Tag (type: 0x0003, size: digest size (32 Bytes for SHA256)) used for integrity check of the firmware
  - A 'firmware signature' Tag (type: 0x0020, size: 64 Bytes) used to validate the signature stored with the firmware against a known public key
  - A 'firmware type' Tag (type: 0x0030, size: 2 Bytes) used to identify the type of firmware, and the authentication mechanism in use.

A 'public key hint digest' tag is transmitted in the header (type: 0x10, size:32 Bytes). This tag contains the SHA digest of the public key used
by the signing tool. The bootloader may use this field to locate the correct public key in case of multiple keys available.

wolfBoot will, in all cases, refuse to boot an image that cannot be verified and authenticated using the built-in digital signature authentication mechanism.

### Adding custom fields to the manifest header

It is possible to add custom fields to the manifest header, by using the `--custom-tlv` option in the signing tool.

In order for the fields to be secured (checked by wolfBoot for integrity and authenticity),
their value is placed in the manifest header before the signature is calculated. The signing tool takes care of the alignment and padding of the fields.

The custom fields are identified by a 16-bit tag, and their size is indicated by a 16-bit length field. The tag and length fields are stored in little-endian format.

At runtime, the values stored in the manifest header can be accessed using the `wolfBoot_find_header` function.

The syntax for `--custom-tlv` option is also documented in [docs/Signing.md](/docs/Signing.md#adding-custom-fields-to-the-manifest-header).

### Image header: Example

This example adds a custom field when the signing tool is used to sign the firmware image:

```bash
./tools/keytools/sign --ed25519 --custom-tlv 0x34 4 0xAABBCCDD test-app/image.bin wolfboot_signing_private_key.der 4
```

The output image `test-app/image_v4_signed.bin` will contain the custom field with tag `0x34` with length `4` and value `0xAABBCCDD`.

From the bootloader code, we can then retrieve the value of the custom field using the `wolfBoot_find_header` function:

```c
uint32_t value;
uint8_t* ptr = NULL;
uint16_t tlv = 0x34;
uint8_t* imageHdr = (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS; /* WOLFBOOT_PARTITION_UPDATE_ADDRESS */
uint16_t size = wolfBoot_find_header(imageHdr, tlv, &ptr);
if (size != sizeof(uint32_t) || ptr == NULL) {
    /* Error: the field is not present or has the wrong size */
}

/* From here, the value 0xAABBCCDD is at ptr */
memcpy(&value, ptr, size);
printf("TLV 0x%x=0x%x\n", tlv, value);
```

### Image signing tool

The image signing tool generates the header with all the required Tags for the compiled image, and add them to the output file that can be then
stored on the primary slot on the device, or transmitted later to the device through a secure channel to initiate an update.

### Storing firmware image

Firmware images are stored with their full header at the beginning of any of the partitions on the system.
wolfBoot can only boot images from the BOOT partition, while keeping a second firmware image in the UPDATE partition.

In order to boot a different image, wolfBoot will have to swap the content of the two images.

For more information on how firmware images are stored and managed within the two partitions, see [Flash partitions](flash_partitions.md)




