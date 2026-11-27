# NXP HSE secure boot

NXP's Hardware Security Engine (HSE) is firmware from NXP that runs on the security core of the S32G, S32R and S32K3 families. With secure boot switched on, HSE verifies the wolfBoot image and only then releases the application core into it, so the chain of trust no longer starts at wolfBoot.

wolfBoot carries a host driver for HSE ([/hal/nxp_hse.c](/hal/nxp_hse.c)) and uses it to provision HSE and to report its state at boot. The first target is the S32G2 ([Targets.md](Targets.md#nxp-s32g2-s32g274a-goldbox)); this document is the detail behind the summary there.

The HSE firmware itself is not part of wolfBoot. It comes from NXP's HSE package and is not redistributable.

## Contents

- [The host driver](#the-host-driver)
- [S32G2: how it fits together](#s32g2-how-it-fits-together)
- [What HSE needs](#what-hse-needs)
- [Build options](#build-options)
- [NOR layout used here](#nor-layout-used-here)
- [Step by step](#step-by-step)
- [Replacing wolfBoot afterwards](#replacing-wolfboot-afterwards)
- [What is permanent](#what-is-permanent)
- [Recovery](#recovery)
- [Status and responses](#status-and-responses)
- [Limits](#limits)

## The host driver

A request to HSE is a service descriptor of at most 256 bytes. The host writes the descriptor's address to a transmit register of the Messaging Unit and HSE answers with one response word. [/hal/nxp_hse.h](/hal/nxp_hse.h) offers the requests secure boot needs: attribute reads (firmware version, lifecycle, anti-rollback updates left, boot result), key catalog format, ECC public key import, SMR and core reset entry install, SMR verify and system image publish.

`NXP_HSE=1` builds it. The driver is not specific to one part; the platform header supplies what differs:

| Setting | Meaning |
|---|---|
| `NXP_HSE_MU_BASE` | Host side of the Messaging Unit |
| `NXP_HSE_DESC_BASE` | Memory HSE reads the service descriptors from |
| `NXP_HSE_ADDR64` | Defined when the requests carry 64-bit addresses (S32G2, S32G3, S32R45). Without it they carry 32-bit addresses (S32K3). |
| `NXP_HSE_MU_CHANNELS` | Service channels per Messaging Unit when not 16 (S32K3 has 4). The driver reads attributes on channel 0, which HSE reserves for administration services, and sends everything else on channel 1. |
| `NXP_HSE_CLEAN`, `NXP_HSE_INVAL` | Data cache clean and invalidate for the buffers HSE reads and writes |
| `NXP_HSE_BARRIER` | Store barrier between writing a descriptor and signalling the request |

What stays with the board and its boot format, here in [/hal/s32g2.c](/hal/s32g2.c) and `s32g2_mkimage.py`: the fuse supply switch, finding the image and its signature on the boot medium, storing the system image, and the IVT fields.

Only the 64-bit layout has run on hardware, on the S32G2. The 32-bit layout follows NXP's structure definitions and is covered by the host test (`unit-nxp-hse-addr32`) but has not run on a part. An S32K3 port also differs in more than the layout:

- Its HSE keeps the key store in internal flash, so there is no system image to store and no IVT pointer to it.
- Formatting the key catalogs replaces the whole catalog and erases every key in it. The catalog [/hal/s32g2.c](/hal/s32g2.c) formats holds one ECC public key slot for secure boot. A product that also keeps HSE keys for its application needs one catalog that covers both, formatted once.

## S32G2: how it fits together

Without HSE the chain of trust starts at wolfBoot: the Boot ROM loads it unchecked, and wolfBoot verifies the FIT. HSE closes that gap: with secure boot switched on it verifies the wolfBoot image and only then releases Cortex-A53 core 0 into it.

```
Boot ROM -> HSE firmware -> verifies wolfBoot (ECDSA P-256) -> releases A53 core 0
                                                               wolfBoot -> verifies the FIT -> Linux
```

Validated on a GoldBox (S32G274A rev 2.1) with the HSE Standard firmware 0.1.0.5, booting from QuadSPI NOR. The provisioning steps below are implemented for NOR only.

## What HSE needs

| Item | Where it lives | Notes |
|---|---|---|
| HSE firmware (`*.bin.pink`) | Boot medium, IVT points at it | From NXP's HSE package. Not part of wolfBoot and not redistributable. |
| Public key | HSE key store | P-256. The Standard HSE package verifies ECDSA up to 256 bits and RSA up to 2048 bits, so this key is separate from the one wolfBoot uses for the FIT. |
| SMR entry (Secure Memory Region) | HSE system image | "These bytes of the boot medium, verified with that key, copied to this address." Covers the bytes the ROM would load: wolfBoot with the linked DDR data. |
| Core reset entry | HSE system image | "Release A53 core 0 at the wolfBoot entry point if the SMR verified, keep it in reset otherwise." |
| System image | Boot medium, IVT points at it | HSE has no flash of its own. It hands the key store and both tables to the host as one encrypted, device-bound image (16 KB here) and loads it at reset. |
| Secure boot bit (`BOOT_SEQ`) | IVT boot configuration word | Tells the Boot ROM to leave the A53 to HSE. |

## Build options

| Option | Effect |
|---|---|
| `NXP_HSE=1` | Builds the HSE host driver ([/hal/nxp_hse.c](/hal/nxp_hse.c)). Prints the HSE status, firmware version, lifecycle, the anti-rollback updates left, and the SMR result when a system image is installed. Switches the fuse programming supply off before the OS starts. |
| `NXP_HSE_PROVISION=1` | Gives HSE the key, the SMR and the core reset entry at boot. On its own this is a rehearsal: HSE loads and verifies wolfBoot from the NOR into spare memory, nothing is stored, and HSE has forgotten all of it at the next reset. |
| `NXP_HSE_SYSIMG_OFFSET=<offset>` | With `NXP_HSE_PROVISION=1`: the real thing. The entries name wolfBoot's load address and entry point, and the system image is written to the NOR at this offset and read back. |

`s32g2_mkimage.py` options:

| Option | Effect |
|---|---|
| `--hse-fw <firmware>` | Places the HSE firmware behind the code and points the IVT at it. |
| `--hse-sign <P-256 key.pem>` | Appends the public key and an ECDSA SHA-256 signature over the code the ROM loads. Needs the Python `cryptography` package. |
| `--hse-sysimg <offset>` | Points the IVT at the system image stored at this offset of the medium. |
| `--hse-efuse` | Switches the fuse programming supply on in the DCD (GPIO25 on the RDB2 and GoldBox). |
| `--hse-boot-seq` | Sets the secure boot bit. Refused without `--hse-sysimg` and `--hse-sign`. |

## NOR layout used here

| Offset | Content |
|---|---|
| 0x0 | IVT, QuadSPI parameters, DCD |
| 0x2200 | wolfBoot (application header and code) |
| behind the code, 16-byte aligned | Signature block: `HSES`, length, public key X and Y, signature R and S |
| next 0x1000 boundary | HSE firmware |
| 0xC0000 | HSE system image (`NXP_HSE_SYSIMG_OFFSET`) |
| 0x100000 | BOOT partition |

## Step by step

Every step uses the same `wolfboot.bin`; only the IVT and DCD written by `s32g2_mkimage.py` change. Keep it that way: the SMR entry records the offset and length of the signed bytes.

```sh
openssl ecparam -name prime256v1 -genkey -noout -out hse_smr_p256.pem

cp config/examples/s32g2-goldbox-qspi.config .config
make wolfboot.bin NXP_HSE=1 NXP_HSE_PROVISION=1 NXP_HSE_SYSIMG_OFFSET=0xC0000
MK="python3 tools/scripts/s32g2/s32g2_mkimage.py wolfboot.bin"
HSE="--qspi --hse-fw s32g2xx_hse_fw.bin.pink --hse-sign hse_smr_p256.pem"

$MK step1_provision.bin $HSE
$MK step2_first_boot.bin $HSE --hse-sysimg 0xC0000 --hse-efuse
$MK step3_secure_boot.bin $HSE --hse-sysimg 0xC0000 --hse-efuse --hse-boot-seq
```

`--hse-sign` produces a new, equally valid signature on every run. Any of them verifies.

**0. Rehearse (optional, costs nothing).** Build once without `NXP_HSE_SYSIMG_OFFSET`, write that image like `step1_provision.bin` and boot it. HSE gets the same key and entries in memory only and is asked to load and verify wolfBoot from the NOR. This proves the firmware runs on the part, the key and signature are accepted and HSE can read the boot medium, before anything is stored.

```
s32g2: fuse supply off
HSE: status 0x920 after 0 ms
HSE: firmware 1.0.5, type 0, SoC 1
HSE: lifecycle 0x4
HSE: rollback counter updates left 159
HSE: image 0x2240 + 0x24400 -> 0x90200000, entry 0x902001C0, signature at 0x26640
HSE: format key catalogs: ok (0, 0x55A5AA33)
HSE: import P-256 public key: ok (0, 0x55A5AA33)
HSE: install SMR 0 (signature checked): ok (0, 0x55A5AA33)
HSE: install core reset entry 0: ok (0, 0x55A5AA33)
HSE: system image size: ok (0, 0x55A5AA33)
HSE: publish system image: ok (0, 0x55A5AA33)
HSE: HSE loads and verifies SMR 0 from the NOR: ok (0, 0x55A5AA33)
HSE: verified in 21 ms, copy matches
HSE: rehearsal only, system image (16384 bytes) not stored
```

**1. Provision.** Write `step1_provision.bin` to NOR offset 0 and boot it once. wolfBoot gives HSE the key and both tables and stores the system image. No fuse is written.

```
s32g2: fuse supply off
HSE: status 0x920 after 0 ms
HSE: firmware 1.0.5, type 0, SoC 1
HSE: lifecycle 0x4
HSE: rollback counter updates left 159
HSE: image 0x2240 + 0x24400 -> 0x3409FE40, entry 0x340A0000, signature at 0x26640
HSE: format key catalogs: ok (0, 0x55A5AA33)
HSE: import P-256 public key: ok (0, 0x55A5AA33)
HSE: install SMR 0 (signature checked): ok (0, 0x55A5AA33)
HSE: install core reset entry 0: ok (0, 0x55A5AA33)
HSE: system image size: ok (0, 0x55A5AA33)
HSE: publish system image: ok (0, 0x55A5AA33)
HSE: system image stored at NOR 0xC0000 (16384 bytes)
```

Boot this image once. While the IVT does not point at a system image, every boot of it formats the key store again and stores a new system image.

The provisioning code stays in the `wolfboot.bin` used for the later steps and does nothing there: it runs only while HSE reports no system image installed and the fuse supply is off. It takes the public key from the image's own signature block, so run it where the NOR content is under your control, as any key provisioning step.

**2. First boot with the system image.** Write `step2_first_boot.bin` over the first 512 KB of the NOR (the system image at 0xC0000 stays) and boot. HSE loads the system image and advances its anti-rollback counter by one. This is the one permanent step, see below. wolfBoot is still started by the ROM, so the console shows the result whatever HSE decides.

```
s32g2: fuse supply ON
HSE: status 0x4B20 after 0 ms
HSE: rollback counter updates left 158
HSE: system image installed (primary), SMR verified 0x0 passed 0x0, cores booted 0x0, SMR installed 0x1 (0)
HSE: not provisioning, the fuse supply is on
...
s32g2: fuse supply switched off
```

**3. Secure boot.** Write `step3_secure_boot.bin`. HSE now verifies wolfBoot and starts it.

```
HSE: status 0x4F20 after 0 ms
HSE: system image installed (primary), SMR verified 0x1 passed 0x1, cores booted 0x1, SMR installed 0x1 (0)
```

A wolfBoot image that does not match its signature is not started: with one byte changed inside the signed range the board prints nothing, because HSE keeps the core in reset.

## Replacing wolfBoot afterwards

The SMR entry in the system image records where the signed bytes are and how long they are, not what they contain. A rebuilt wolfBoot signed with the same key is accepted with the system image already stored, as long as the code length `s32g2_mkimage.py` prints stays the same (it is rounded up to 512 bytes, so small changes fit). Build it with the same options and write it with the step 3 command; no provisioning and no counter step. A build whose code length changed needs the SMR entry installed again, which this port does not do yet.

## What is permanent

HSE protects its key store against being replaced by an older one with a counter kept in one-time-programmable fuses. The first boot that loads a new key store advances that counter by one, and HSE needs the fuse programming supply for it. This happens in step 2, with or without the secure boot bit, and in every lifecycle.

- The counter has 159 steps on a part that never ran a device-bound HSE firmware. It moved once (159 to 158) for the steps above and did not move on later boots of the same system image.
- Changing the SMR or the core reset entry does not use the counter. Changing the key store does: provision once.
- The fuse supply is off by default on the RDB2 and GoldBox. `--hse-efuse` switches it on in the DCD, before HSE starts, and wolfBoot switches it off in `hal_prepare_boot()`. NXP's HSE documentation warns that the system may be held in reset when HSE needs to advance the counter and the supply is off, so do not boot a new system image without it.
- Nothing here changes the lifecycle, the debug settings or the boot image authentication fuses. wolfBoot has no code that writes HSE attributes.

## Recovery

The boot medium is chosen by the SW4 switches, and everything HSE acts on (firmware pointer, system image pointer, secure boot bit) is in the IVT of that medium. A NOR that HSE refuses to start is recovered by booting another medium:

1. Set SW4-7 ON (SD boot) and boot a card with a plain first stage (`s32g2-goldbox.config`, no `--hse-*` options).
2. Rewrite the NOR from Linux (`flash_erase` and `dd` on `/dev/mtd0`).
3. Set SW4-7 OFF.

Measured after the counter had advanced: a plain image with no HSE firmware boots from the NOR and from the SD card, and an image with the HSE firmware but no system image pointer boots with HSE running and no key store (status 0x920).

## Status and responses

The status is the upper half of the Messaging Unit's flag status register:

| Bit | Value | Meaning |
|---|---|---|
| 5 | 0x0020 | Random number generator ready |
| 8 | 0x0100 | HSE initialized |
| 9 | 0x0200 | A system image is installed |
| 10 | 0x0400 | Secure boot finished |
| 11 | 0x0800 | Host has customer super user rights (development lifecycle) |
| 14 | 0x4000 | The primary system image was loaded |

| Response | Meaning |
|---|---|
| 0x55A5AA33 | OK |
| 0x55A5A164 | Verification failed (wrong signature or key) |
| 0x55A5A399 | Invalid parameter |
| 0xAA55A21C | Not allowed in this state or lifecycle |
| 0xBB4457F3 | A fuse write was needed and the fuse supply is off |

`HSE: firmware not running (status 0x0)` means the IVT has no firmware pointer or the Boot ROM did not start the firmware.

## Limits

- NOR boot only. HSE can verify an SMR on SD or eMMC only in a more restricted form, and that path is not implemented.
- Development lifecycle. The IVT itself is not authenticated until the production fuses (boot image authentication, lifecycle) are written, so on a development part the secure boot bit can be cleared by rewriting the IVT. Those fuses are written with NXP's tools, not by wolfBoot.
- A wolfBoot build with a different code length needs the SMR entry installed again (see [Replacing wolfBoot afterwards](#replacing-wolfboot-afterwards)); that is not implemented.
- Only the primary system image is used. HSE also supports a backup copy.
- The lifecycle value is 0x4 on a development part (customer delivery). Later lifecycles restrict the provisioning requests.
