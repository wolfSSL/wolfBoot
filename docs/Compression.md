# Compressed firmware updates

With `COMPRESSED_UPDATE=1` the update partition holds a gzip-compressed, signed image, and wolfBoot inflates it into the boot partition, which goes on executing in place. The update partition is then sized for the compressed container rather than for a second full copy of the application, and the flash that frees goes to the application instead.

This is a different mechanism from [delta updates](firmware_update.md), which shrink the update that has to be transferred but still install into a same-size partition, and from [encrypted partitions](encrypted_partitions.md), which protect the update partition at rest.

## What it gives up

There is no local copy of the outgoing image. Three properties bound that:

- The container's signature is verified **before the first erase**, so a corrupt or unauthentic image never touches the running one.
- The update partition is only ever read during an install, so a power cut leaves an intact source. The next boot resumes from the first destination sector not yet recorded, re-inflating from the beginning and discarding output below that point rather than rewriting it.
- The payload is a complete signed image, so after installing, the boot partition is an ordinary wolfBoot partition whose own manifest the normal boot path verifies. The inflater does not have to be trusted for the installed image to be authentic.

What remains is that an image which is valid, boots, and then misbehaves cannot be rolled back on the device; it has to be replaced by pushing another update. That is the same trade [`DISABLE_BACKUP`](compile.md) makes without the flash saving. If rollback matters more than flash, use an ordinary A/B layout or delta updates, which keep it.

## How it works

On a boot where the update partition is armed, wolfBoot:

1. Reads the compression tags and refuses an image whose window is larger than this build's decode buffer, or whose uncompressed length will not fit the boot partition.
2. Verifies the integrity and signature of the container.
3. Inflates it into the boot partition, erasing each sector the first time it is written and recording each one as it completes.
4. Marks the boot partition `IMG_STATE_SUCCESS` and consumes the update.
5. Boots the boot partition through the usual path, which verifies the inner manifest.

The decoder uses a bounded LZ77 window and writes through a sink, so it needs twice the window in RAM regardless of how large the image is.

## Manifest fields

Three TLVs are added to the image header (see [firmware_image.md](firmware_image.md)):

| Tag | Name | Length | Contents |
|-----|------|--------|----------|
| 0x08 | `HDR_IMG_COMPRESSION` | 2 | Algorithm id, little-endian. `0x0001` is gzip/DEFLATE. |
| 0x09 | `HDR_IMG_UNCOMPRESSED_SIZE` | 4 | Uncompressed length in bytes, little-endian. |
| 0x0A | `HDR_IMG_COMP_WINDOW` | 2 | LZ77 window as a power of two, little-endian. |

Compression is detected by the presence of `HDR_IMG_COMPRESSION`, not by a bit in `HDR_IMG_TYPE`. A manifest carrying the tag with an unknown algorithm, or without a valid uncompressed-size tag, is rejected rather than installed as a raw payload.

A gzip header has no field for the window the stream was compressed with, and the decoder sizes its buffer from it, so it travels in the signed manifest instead. An image whose window exceeds what the bootloader was built for is refused before the inflate starts, rather than failing part way through with a distance error that looks like corruption.

## Preparing an image

The signing tool compresses, because it has to sign twice: once over the image that will live in the boot partition, and once over the compressed container.

```sh
./tools/keytools/sign --compress test-app/image.bin wolfboot_signing_private_key.der 2
```

This produces `test-app/image_v2_signed_compressed.bin`, which is what gets written to the update partition. `--compress=<bits>` selects the window, 12 to 15; it defaults to whatever `WOLFBOOT_COMPRESSED_WINDOW` the bootloader was configured with, so the two cannot disagree.

`--compress` needs the tool built against zlib, which supplies the DEFLATE encoder; the decoder on the device is wolfBoot's own `src/gzip.c`. The dependency is optional, so a tool built without the zlib headers still builds and only rejects this one option. Install them (`zlib1g-dev` on Debian and Ubuntu) and rebuild `tools/keytools`.

## Sizing the partitions

The boot partition holds the uncompressed image, the update partition only the container, and both must be a whole number of sectors:

```
WOLFBOOT_PARTITION_SIZE         = boot partition, the executable budget
WOLFBOOT_PARTITION_UPDATE_SIZE  = update partition, >= container + one sector
```

A safe update still needs the new image in flash before the running one is overwritten, so the floor is `U + C` against `2U` for an uncompressed pair: the saving is the difference between a compressed copy and a full one, not the compression ratio itself. It is largest for payloads carrying lookup tables, fonts, a filesystem or model weights, and smallest for dense code.

Pick the split from the worst compression ratio the product will ever ship, not the best, and measure your own firmware first. The signing tool refuses to emit a container that does not fit the configured update partition, so a release that compresses worse than the split fails at build time rather than on the device - but it still fails, and the split cannot be changed in the field.

`WOLFBOOT_PARTITION_SWAP_ADDRESS` is unused in this mode and may be left at 0: there is no swap step.

## Build options

| Option | Meaning |
|--------|---------|
| `COMPRESSED_UPDATE=1` | Enable the mode. Implies `GZIP=1`, the gzip sink decoder and `DISABLE_BACKUP`. |
| `WOLFBOOT_PARTITION_UPDATE_SIZE` | Update partition size, independent of and no larger than the boot partition. Defaults to the boot partition size. |
| `WOLFBOOT_COMPRESSED_WINDOW` | LZ77 window: 4096, 8192, 16384 (the default) or 32768. The decoder needs twice this in RAM, and a smaller window costs ratio. |
| `GZIP_SMALL=1` | Use the bit-serial Huffman decoder: a few KB less code and no static tables, at a fraction of the table-driven decoder's rate. |

Rejected at build time: `DELTA_UPDATES` (delta patches the partition it is consuming, this one overwrites it from a separate slot), `WOLFBOOT_ELF`, `DUALBANK_SWAP`, `PULL_LINKER_DEFINES` (a single partition-size linker symbol serves both partitions), a self-update signed compressed (wolfBoot cannot inflate itself), and a partition that is not a whole number of sectors.

## An update partition on external flash

`EXT_FLASH=1` with the update partition on a serial flash is supported, and is the layout that gains most: a small internal flash for the application, a cheap external device for the incoming update, and compression shrinking what that device has to hold as well as what has to be transferred.

The container is not addressable there, so wolfBoot pulls it in through the external flash interface as it inflates rather than reading it in place. Nothing else changes: the boot partition is still internal and still executes in place, and the same verification happens before the first erase. `config/examples/sim-compressed-update-ext.config` exercises this under the simulator.

## Encryption at rest

`ENCRYPT=1` composes with this mode, so the container can be stored encrypted on the external device. It needs no separate mechanism: that layer hooks the external flash read interface, which is the same seam the installer and the image hasher already pull the container through, so the plaintext appears on its way in and nothing downstream knows the difference. See [encrypted_partitions.md](encrypted_partitions.md) for the key handling, which is unchanged.

Because it works through the external flash interface, it requires `EXT_FLASH=1`; an internal update partition is read directly and has no such seam. Note also that the boot partition holds plaintext, since it is executed in place: this protects the stored update, not the running image. `config/examples/sim-compressed-update-ext-enc.config` exercises it.

## Flash that programs whole units

`NVM_FLASH_ECC=1`, and the trailer journal it requires, compose with this mode. The installer programs the boot partition in aligned chunks and pads the last one out to a whole write unit, so flash that can only program a full unit is never handed a partial one. `config/examples/sim-compressed-update-ecc.config` exercises it.

## Examples

`config/examples/sim-compressed-update.config` runs the flow under the simulator with a deliberately asymmetric layout. `tools/scripts/sim-compressed-update.sh` asserts the install, that it is consumed rather than repeated, that a corrupt container never reaches the inflater, and that signing refuses a container too big for the update partition:

```sh
cp config/examples/sim-compressed-update.config .config
make keysclean && make
./tools/scripts/sim-compressed-update.sh
```

`config/examples/stm32h7-compressed-update.config` and `config/examples/stm32f439zi-compressed-update.config` configure a 2MB STM32. Note that `hal/stm32f4.c` describes only the first 1MB of flash, so the F4 example keeps both partitions inside it; a layout past that is refused at build time.
