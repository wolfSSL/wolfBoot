# PolarFire SoC: PUF Root of Trust, sNVM Keystore, and PUF-Wrapped Encryption Key (M-Mode)

This document describes the PolarFire SoC MPFS250 hardware-root-of-trust features added for the wolfBoot E51 M-mode boot flow (the same flow that brings up LPDDR4 and boots a signed Yocto FIT from SD; see `Targets.md`). They let wolfBoot anchor key material in the chip's System Controller instead of compiling it into the bootloader image:

1. A System Controller mailbox driver for secure NVM (sNVM), the SRAM-PUF, and the nonce/TRNG service.
2. An sNVM keystore backend that serves the verification public keys from sNVM at boot.
3. A device-unique Key Encryption Key (KEK) derived from the SRAM-PUF, used to wrap/unwrap individual keys.
4. A PUF-wrapped image-encryption key: the AES image key is stored in sNVM wrapped by the PUF KEK and unwrapped at boot, with the bulk image decrypted on the M-mode disk-boot path.

All features are off by default and gated behind build flags, so a normal build is unaffected.

## Trust model

The intended chain, from the immutable hardware up:

```
PolarFire boot ROM  (secure boot of wolfBoot: not configured on the Video Kit, bootmode 1)
  -> wolfBoot (M-mode, eNVM)
       -> System Controller SRAM-PUF (fixed challenge) -> SHA-384 -> 256-bit KEK
       -> sNVM keystore: ECC384 verification public key(s)
       -> sNVM: AES-256 image key, wrapped by the PUF KEK
  -> verify + decrypt the signed/encrypted Yocto FIT
  -> 4-CPU SMP Linux
```

The KEK is re-derived on every boot and never leaves the device. The public keys are not secret; storing them in sNVM (rather than the image) lets the trust anchor be provisioned and rotated per device, and binds verification to the System Controller.

### Production requirement: the keystore pages must not stay writable

The sNVM write service is reachable from anything that can drive the System Controller mailbox, including the Linux `mpfs-sys-controller` driver, so on the development flow above the trust anchor is only as protected as the OS. A production unit closes this in one of two ways, both opt-in at build time:

- Deliver the keystore pages as ROM in the bitstream (Libero sNVM client with the keystore image as content and the ROM attribute set) and build with `SNVM_KEYSTORE=1 SNVM_KEYSTORE_REQUIRE_ROM=1`: wolfBoot reads the page admin word of every keystore module and refuses to boot unless its ROM flag is set. The ROM flag is a bitstream property; the sNVM system services can neither set nor clear it, so there is no on-device step that can lock a page by accident. The on-device provisioning writer is for development only and cannot write a ROM page.
- Build with `MPFS_SCB_SMODE_DENY=1`: every hart's PMP fences S-mode off the System Controller control and mailbox window (`0x37020000`-`0x37020FFF`) before the hand-off, and the DTB fixup disables the `mailbox@37020800`, `spi@37020100` (System Controller QSPI) and `syscontroller` nodes so the OS does not probe hardware it cannot reach. A supervisor access into the window is otherwise redirected to the kernel's trap vector as an access fault (this core cannot delegate access faults, so wolfBoot's SBI forwards them). The sNVM pages stay writable by wolfBoot (M-mode) only. This removes the OS-side System Controller services (device serial number, bitstream services, sNVM access, the zeroize procedure) and anything the OS image builds on them: on the Video Kit Yocto image the `varlog-luks-key.service` unit, which fetches its LUKS key through the System Controller, fails and systemd waits out its device timeout before the login prompt.

## 1. System Controller mailbox services

`hal/mpfs250.c` drives the System Controller services mailbox (`SCBCTRL` at `0x37020000`, mailbox RAM at `0x37020800`). A small generic `mpfs_scb_request()` issues a service and polls the request/busy bits; `mpfs_scb_read()` reads the response. The following services are wrapped:

| Service | Opcode | wolfBoot wrapper |
|---------|--------|------------------|
| Device serial number | `0x00` | `mpfs_read_serial_number()` |
| sNVM write (plaintext / authenticated / ciphertext) | `0x10` / `0x11` / `0x12` | `mpfs_snvm_write()` |
| sNVM read | `0x18` | `mpfs_snvm_read()` |
| PUF emulation | `0x20` | `mpfs_puf_emulation()` |
| Nonce (TRNG) | `0x21` | `mpfs_nonce()` |

sNVM holds 221 modules of 252 bytes (plaintext) or 236 bytes plus a 12-byte User Secret Key (authenticated). The PUF emulation service returns a 32-byte device-unique response for a 16-byte challenge; for a fixed challenge the response is stable within a boot and across cold boots, which is what makes it usable as a KEK seed. The nonce service returns 32 random bytes.

Note: the TRNG (nonce) and PUF services take far longer than serial/sNVM-read, so the busy-completion wait uses a larger bound (`MPFS_SCB_BUSY_TIMEOUT`) than the request-accept wait (`MPFS_SCB_TIMEOUT`).

## 2. sNVM public-key keystore (`SNVM_KEYSTORE`)

`hal/mpfs250_snvm.c` + `hal/mpfs250_snvm.h` implement the five-function keystore API (`keystore_num_pubkeys`, `keystore_get_buffer`, `keystore_get_size`, `keystore_get_mask`, `keystore_get_key_type`) by reading the trust anchor from sNVM. The on-sNVM layout is the same family as the OTP keystore (`src/flash_otp_keystore.c`): a `WOLFBOOT`-magic header followed by packed `struct keystore_slot` entries, read through a linear reader that spans consecutive sNVM modules from `SNVM_KEYSTORE_MODULE` (default 200, `SNVM_KEYSTORE_MAX_MODULES` default 1; an ML-DSA-87 key needs 11). For the on-device provisioning writer every module in the range must be left runtime-writable (not ROM) in the Libero sNVM configuration: on the stock Video Kit design only modules 200 and 201 are. A production bitstream delivers the same page contents as ROM instead (see the trust model above).

Build wiring (mirrors `FLASH_OTP_KEYSTORE`):

- `SNVM_KEYSTORE=1` links `hal/mpfs250_snvm.o` in place of the compiled-in `src/keystore.o`, so the running bootloader contains no public keys and serves them from sNVM.
- `SNVM_KEYSTORE_PROVISION=1` keeps the compiled keystore and adds `snvm_keystore_provision()`, a one-time on-device writer that copies the compiled keystore image into sNVM (called from `hal_init`). Run once on a board, then deploy the `SNVM_KEYSTORE=1` build. Both provisioning writers read the page first and skip it when it already holds the content (`snvm: module N already provisioned`), so the extra boot the programmer triggers after flashing, or a second boot of the provisioning image, does not spend another write cycle.

A keystore page that reads back as all zeros is reported as `sNVM keystore zeroized` and halts the boot. That is the state a zeroize procedure leaves behind (the OS overwrites the keystore and encryption-key pages through the System Controller's plaintext sNVM write service), and it is distinguished from a corrupt header, which merely yields no keys, so a deliberately erased unit is identifiable on the console. Recovery is the provisioning build again.

## 3. PUF KEK and AES key-wrap (`SNVM_KEK`)

The KEK and key-wrap live in `hal/mpfs250_snvm.c` as well:

- `mpfs_puf_kek()` derives the 256-bit KEK as `SHA-384(label || PUF(fixed-challenge))` truncated to 32 bytes. SHA-384 is used because it is the build's hash (SHA-256/HMAC/HKDF are compiled out); the PUF response is already a high-entropy device-unique secret, so a labelled hash provides domain separation without pulling in HKDF.
- `snvm_kek_wrap()` / `snvm_kek_unwrap()` wrap/unwrap a key with the KEK using RFC 3394 AES key-wrap (`wc_AesKeyWrap` / `wc_AesKeyUnWrap`).

`SNVM_KEK=1` adds RFC 3394 key-wrap to the AES that an `ENCRYPT=1 ENCRYPT_WITH_AES256=1 CUSTOM_ENCRYPT_KEY=1` build already compiles; the KEK has no other consumer, so the build refuses the flag without that combination. Determinism and the wrap/unwrap round trip are covered by the host unit test `tools/unit-tests/unit-snvm-kek.c`.

The AES key-wrap uses the wolfCrypt software implementation. The Athena F5200 offload (`MPFS_ATHENA=1`, see `Targets.md`) serves SHA-384 and the bulk AES-256-CTR image decrypt through wolfCrypt crypto callbacks; the key-wrap is a few blocks per boot and stays in software.

## 4. PUF-wrapped image-encryption key

`hal/mpfs250_snvm.c` provides `wolfBoot_get_encrypt_key()` (under `CUSTOM_ENCRYPT_KEY`): it reads `[wrapped AES key (40 B)][nonce (16 B)]` from sNVM module `SNVM_ENCKEY_MODULE` (default 201, outside the keystore range), unwraps the key with the PUF KEK, and returns the plaintext key + nonce to the decrypt path. `snvm_enckey_provision()` (under `SNVM_ENCKEY_PROVISION`) wraps a key with the device KEK and writes the blob to sNVM.

### Disk-image decryption enablement

wolfBoot's image encryption (`EXT_ENCRYPTED`) was previously tied to the partition model and required `EXT_FLASH` or `MMU`. The M-mode disk-FIT path uses `WOLFBOOT_NO_PARTITIONS` and is neither. Two small, target-independent changes enable encrypted disk boot generally (regression-safe; host unit tests pass):

- `src/libwolfboot.c`: the `EXT_ENCRYPTED requires EXT_FLASH or MMU` guard now also accepts `CUSTOM_ENCRYPT_KEY` (the platform supplies the key), and `hal_set_key()` (partition-resident key storage) is folded under `#ifndef CUSTOM_ENCRYPT_KEY`.
- `src/update_disk.c`: the `DISK_ENCRYPT` path gains `ForceZero` for secret cleanup.

### PolarFire M-mode decrypt note

The image is loaded into DDR by the SD controller and decrypted there. CPU stores into that region are not coherent with what the controller wrote (which is why the load uses `SDHCI_BLOCK_VIA_PDMA`), so `polarfire_mpfs250_m.config` sets `DISK_DECRYPT_STAGING=1`: the ciphertext is read through the non-cached DDR alias, decrypted a chunk at a time into a staging buffer and landed with the same PDMA copy the load used.

## Build flags summary

| Flag | Effect |
|------|--------|
| `SNVM_KEYSTORE` | Serve the public-key trust anchor from sNVM (replaces compiled keystore) |
| `SNVM_KEYSTORE_PROVISION` | Add the one-time on-device keystore writer |
| `SNVM_KEK` | Enable the PUF KEK + RFC 3394 AES key-wrap (needs the AES-256 `ENCRYPT` + `CUSTOM_ENCRYPT_KEY` build) |
| `SNVM_ENCKEY_PROVISION` | Add the one-time PUF-wrapped encryption-key writer |
| `SNVM_KEYSTORE_MODULE` / `SNVM_KEYSTORE_MAX_MODULES` / `SNVM_ENCKEY_MODULE` | sNVM module numbers (defaults 200 / 1 / 201; the key module must lie outside the keystore range and be runtime-writable) |
| `WOLFBOOT_SNVM_WRITE_APPROVED` | Required by every knob that writes sNVM; the build fails without it |
| `SNVM_KEYSTORE_REQUIRE_ROM` | Refuse to boot unless every keystore page is ROM-flagged (bitstream-provisioned keystore) |
| `MPFS_SCB_SMODE_DENY` | PMP-fence S-mode off the System Controller mailbox and disable its DTB nodes |
| `SNVM_ENCKEY_INSECURE_TEST_KEY` | Acknowledges provisioning the built-in public test key |
| `SNVM_ENCKEY_PROVISION_EXTERN` | Provision a production key instead: the integrator defines `snvm_enckey_prov_key[32]` and `snvm_enckey_prov_nonce[16]` in another object |

Example runtime encrypted build (compiled keystore for verification, PUF-wrapped key for decryption):

```sh
make CROSS_COMPILE=riscv64-unknown-elf- LIBERO_FPGA_CONFIG_DIR=<fpga_design_config> \
    ENCRYPT=1 ENCRYPT_WITH_AES256=1 CUSTOM_ENCRYPT_KEY=1 SNVM_KEK=1 wolfboot.elf
```

## Provisioning and validation recipe

1. Build with `SNVM_ENCKEY_PROVISION=1` (and/or `SNVM_KEYSTORE_PROVISION=1`) plus `WOLFBOOT_SNVM_WRITE_APPROVED=1` added to the runtime build above, flash, and boot once: `hal_init` writes the PUF-wrapped AES key into sNVM[201] (and/or the keystore into sNVM[200]).
2. Sign + encrypt the FIT with a key file matching the provisioned key (`prov_aes_key` || `prov_aes_nonce` in `hal/mpfs250_snvm.c`, i.e. a 32-byte key followed by a 16-byte IV):

   ```sh
   IMAGE_HEADER_SIZE=512 ./tools/keytools/sign --ecc384 --sha384 \
       --encrypt enc_key.bin --aes256 fitImage wolfboot_signing_private_key.der 1
   ```
   Note: `make keysclean` regenerates the signing key, so re-sign the image after any keysclean or the compiled keystore will not match.
3. Write `fitImage_v1_signed_and_encrypted.bin` to the SD boot partition. With the stock HSS, boot to the `>>` CLI, run `usbdmsc`, and `dd` to the first partition of the exposed disk. Do not move an SD-Wire mux while `usbdmsc` is running: HSS's MMC layer then serves one stale block for every LBA and does not recover until `usbdmsc` is restarted, which makes a populated card read as blank.
4. Flash the runtime build and cold-boot. Expected UART0: `Disk encryption enabled` -> `Decrypting image... done` -> `Firmware Valid.` -> `M->S handoff`, and UART1 reaches the Linux login with `Brought up 1 node, 4 CPUs` and no `failed to come online`.

## Sample validation logs (MPFS250TS Video Kit)

The PUF response is identical for a fixed challenge across cold power cycles while the nonce service is random, and the PUF-derived KEK wraps and unwraps a test key deterministically (the same wrapped bytes on every boot); both were verified on the kit and the KEK logic is covered by `tools/unit-tests/unit-snvm-kek.c`.

Full encrypted boot captured on the Video Kit (UART1, S-mode wolfBoot under HSS; the standalone M-mode target boots the same image through the staged PDMA decrypt): the AES key is unwrapped from sNVM with the PUF KEK, the image is loaded, decrypted and verified.

```
wolfBoot Version: 2.9.0
Disk encryption enabled
...
Load address 0x8E000000
Attempting boot from P:A
Loading image from disk...done
Decrypting image...done
Checking image integrity...done
Verifying image signature...done
Firmware Valid.
Booting at 8E000000
PolarFire SoC MPFS250 wolfBoot demo Application
```

Linux (UART1):

```
[    0.040726] smp: Brought up 1 node, 4 CPUs
OpenEmbedded nodistro.0 mpfs-video-kit ttyS1
mpfs-video-kit login:
```

No `failed to come online`; the only error lines are the known-cosmetic eth-SGMII PHY, i2c clock-divider, and mmc-tuning messages unrelated to this feature.
