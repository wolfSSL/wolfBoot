# Microchip PolarFire SoC

Everything wolfBoot does on PolarFire SoC, in one document: the quick start, every supported boot configuration, and the device-specific features (sNVM/PUF key storage, the SBI vendor extension, boot mode 2). The PolarFire SoC is a 64-bit RISC-V SoC with a five-core CPU cluster (1x E51 monitor core and 4x U54 application cores) and FPGA fabric; wolfBoot is tested on MPFS250.

wolfBoot runs here either as a payload of Microchip's Hart Software Services or as a full replacement for it, in which case it owns LPDDR4 bring-up, image verification and the SBI the OS runs on. [Targets.md](Targets.md#microchip-polarfire-soc) carries the summary tables; this document is the detail behind them.

> **Filesystem boot:** this target can also read its signed image from a **file** on a read-only FAT32 or ext4 partition instead of from raw offset 0. Build with `DISK_FS=fat32|ext4|both` and set `BOOT_FILE_A` / `BOOT_FILE_B`. See [Disk boot from a read-only filesystem (FAT32 / ext4)](compile.md#disk-boot-from-a-read-only-filesystem-fat32--ext4).

## Contents

- [Quick start](#quick-start)
- [Features](#features)
- [Boot configurations](#boot-configurations)
- [M-mode build flags](#m-mode-build-flags)
- [Building wolfBoot](#building-wolfboot)
- [QSPI](#qspi)
- [HSS S-mode with L2-LIM (no DDR)](#hss-s-mode-with-l2-lim-no-ddr)
- [M-mode (bare-metal eNVM boot)](#m-mode-bare-metal-envm-boot)
- [M-mode + DDR: booting Linux (minimal SBI)](#m-mode--ddr-booting-linux-minimal-sbi)
- [Boot mode 2: User Boot Loader stub](#boot-mode-2-user-boot-loader-stub)
- [Encryption](#encryption)
- [PQC (ML-DSA)](#pqc-ml-dsa)
- [Writing a card](#writing-a-card)
- [Building Hart Software Services (HSS)](#building-hart-software-services-hss)
- [Building Yocto-SDK Linux](#building-yocto-sdk-linux)
- [Debugging](#debugging)
- [Example boot output](#example-boot-output)
- [Performance comparison](#performance-comparison)
- [Source files](#source-files)
- [SBI vendor extension for the OS](#sbi-vendor-extension-for-the-os)
- [Hardware root of trust: PUF KEK, sNVM keystore, wrapped image key](#hardware-root-of-trust-puf-kek-snvm-keystore-wrapped-image-key)

## Quick start

Three ways to get wolfBoot running on an MPFS250T board, shortest first. Each path is self-contained: build, program, and what the console must print for the step to have worked. The sections after this one are the reference material behind them.

| Path | What it proves | eNVM | Reversible by |
|---|---|---|---|
| [1. HSS payload](#path-1-run-wolfboot-as-an-hss-payload) | wolfBoot verifies and boots an image on this board | untouched (HSS stays) | swapping the SD card |
| [2. wolfBoot replaces HSS](#path-2-wolfboot-replaces-hss-and-boots-linux) | the full configuration: M-mode, LPDDR4, signed FIT, Linux | reprogrammed | reprogramming HSS |
| [3. Yocto](#path-3-let-yocto-build-it) | the same as path 2, built and packaged by bitbake | reprogrammed | reprogramming HSS |

Path 1 needs no FPGA design files and never writes the device, so it is the right first contact with an unfamiliar board. Path 2 is the configuration the target is built for.

### Before you start

| Need | Where it comes from |
|---|---|
| RISC-V bare-metal toolchain | SoftConsole (`/opt/Microchip/SoftConsole-v2022.2-RISC-V-747/riscv-unknown-elf-gcc/bin`), or any `riscv64-unknown-elf-gcc` |
| `mpfsBootmodeProgrammer.jar` + a JRE | SoftConsole (`extras/mpfs/`, `eclipse/jre/bin/java`); paths 2 and 3 only |
| FlashPro programmer | on-board on the Video Kit, over the same USB cable as the consoles |
| `dtc`, `mkimage` | `device-tree-compiler`, `u-boot-tools` |
| `hss-payload-generator` | `tools/hss-payload-generator` in the [hart-software-services](https://github.com/polarfire-soc/hart-software-services) tree; path 1 only |
| `fpga_design_config/` for the board | an HSS build (`build/boards/<board>/fpga_design_config`) or a Libero MSS Configurator export; paths 2 and 3 only |

```sh
git clone --recursive https://github.com/wolfSSL/wolfBoot.git
cd wolfBoot
export PATH=/opt/Microchip/SoftConsole-v2022.2-RISC-V-747/riscv-unknown-elf-gcc/bin:$PATH
```

Two things that cause most first-attempt failures:

* **Keys.** The first build generates `wolfboot_signing_private_key.der` and compiles its public half into `src/keystore.c`, so the bootloader only accepts images signed with that key. Back the private key up before anything else; `make keysclean` discards both halves and the next build makes a new pair, which invalidates every image already signed.
* **`make clean` between paths.** Object files are configuration-specific. Reusing them across configs produces link errors naming symbols that are compiled out (`undefined reference to wolfBoot_get_dts_size`) or, worse, a bootloader built half from each config.

Console: the E51 prints on MMUART0, the first of the four USB-serial interfaces the board exposes, 115200 8N1.

### Path 1: run wolfBoot as an HSS payload

HSS keeps the eNVM and loads wolfBoot as its S-mode payload from the SD card, so nothing about the device changes and the original card restores the board exactly.

```sh
cp config/examples/polarfire_mpfs250.config .config
make clean
make
dtc -I dts -O dtb hal/mpfs.dts -o hal/mpfs.dtb
hss-payload-generator -c hal/mpfs.yaml wolfboot-payload.bin
```

`make` produces `wolfboot.elf`, the raw `wolfboot.bin`, a signed test application and `factory.bin`. The payload generator reads `wolfboot.elf` and `hal/mpfs.dtb` as named in `hal/mpfs.yaml`, so build the dtb first; the output name is yours to choose (about 63 KB for a 61 KB wolfBoot). Do not call it `wolfboot.bin` or it overwrites the raw binary.

Write `wolfboot-payload.bin` to the card's HSS boot partition - the GPT entry with type GUID `21686148-6449-6E6F-744E-656564454649`, or sector 0 on a card with no GPT. With the card in the board, HSS's `usbdmsc` command exports it over USB; `lsblk` then names the partition:

```sh
sudo dd if=wolfboot-payload.bin of=/dev/sdX1 bs=512
sudo cmp wolfboot-payload.bin /dev/sdX1
```

Power-cycle. HSS prints its own banner, then wolfBoot's:

```
wolfBoot Version: ...
```

That line is the whole point of the path: the board runs wolfBoot. Signing an image and putting it in a slot for wolfBoot to verify follows the same steps as path 2.

### Path 2: wolfBoot replaces HSS and boots Linux

wolfBoot runs from eNVM on the E51, brings up the PLLs and LPDDR4, loads a signed FIT (kernel + dtb) from the SD card, verifies it with ECDSA P-384 over SHA-384, and hands a U54 into S-mode while staying resident as the SBI.

#### Build

```sh
cp config/examples/polarfire_mpfs250_m.config .config
make clean
make LIBERO_FPGA_CONFIG_DIR=/path/to/boards/mpfs-video-kit/fpga_design_config wolfboot.elf
```

About 74 KB of text. The build stops if `LIBERO_FPGA_CONFIG_DIR` is empty: an S-mode OS runs from DDR, and those headers carry the board's DDR, clock and SEG settings, which cannot be guessed. Add `CFLAGS_EXTRA+=-DDEBUG_DDR` for register-level training traces during bring-up.

Append any extra `CFLAGS_EXTRA` as a line in `.config`, never as `make CFLAGS_EXTRA=...`. A make command-line assignment overrides the `CFLAGS_EXTRA+=` lines the config already carries instead of adding to them, which for this target silently drops `-DWOLFBOOT_MMODE_SMODE_BOOT` and with it the whole SBI runtime, so the banner appears and Linux never starts.

#### Card

Three GPT partitions: the active boot slot, the update slot, and the rootfs. GPT entries 0, 1 and 2 are what `BOOT_PART_A`, `BOOT_PART_B` and the kernel's root device refer to.

```sh
sudo parted -s /dev/sdX mklabel gpt \
    mkpart boot   1MiB  33MiB \
    mkpart update 33MiB 65MiB \
    mkpart rootfs 65MiB 100%
```

Sign the FIT the Linux build produced, then write both:

```sh
./tools/keytools/sign --ecc384 --sha384 fitImage wolfboot_signing_private_key.der 1
sudo dd if=fitImage_v1_signed.bin of=/dev/sdX1 bs=1M status=progress
sudo dd if=core-image-...rootfs.ext4 of=/dev/sdX3 bs=4M status=progress
sync
```

The signed file is the input plus a 512-byte manifest header. Slot sizes above suit a 19 MB kernel FIT; grow them for a larger image, or for a FIT that carries the whole rootfs as an initramfs. For where the Linux FIT itself comes from, see [Building Yocto-SDK Linux](#building-yocto-sdk-linux).

#### Program the eNVM

Keep a copy of what you are about to replace - see "Restoring HSS" below - and do a `--dryrun` pass first.

```sh
export SC_INSTALL_DIR=/opt/Microchip/SoftConsole-v2022.2-RISC-V-747

$SC_INSTALL_DIR/eclipse/jre/bin/java -jar \
    $SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar \
    --bootmode 1 --die MPFS250T --package FCG1152 --workdir $PWD wolfboot.elf
```

`--bootmode 1` is non-secure boot from eNVM: the System Controller releases the harts at the eNVM reset vector. The die and package must match the part; `--die` defaults to `MPFS250T_ES` and `--package` to `FCVG484`, neither of which is the Video Kit.

#### Boot

```
wolfBoot Version: ...
Running on E51 (hart 0) in M-mode
DDR: Training+MTC PASS
DDR: Initialization COMPLETE
Valid GPT partition table
Versions, A:1 B:0
Checking image integrity...
Verifying image signature...
Firmware Valid.
M->S handoff: entry=0x80200000 hart=0 dtb=0x8A000000
[    0.000000] Linux version ...
[    0.000000] SBI implementation ID=0x776f6c66 Version=0x1
...
login:
```

Four lines are the ones to check. `DDR: Initialization COMPLETE` means the memory came up; `Firmware Valid.` means the signature verified against the keystore in the image; `M->S handoff` means wolfBoot released a U54; and `SBI implementation ID=0x776f6c66` - ASCII `"wolf"` - means the kernel is talking to wolfBoot's own SBI rather than OpenSBI. The full annotated log is in [Example boot output](#example-boot-output).

### Path 3: let Yocto build it

The `meta-wolfssl` layer builds the bootloader, signs the FIT with the same keytools and writes a `.wic` with the partition layout above. Set `WOLFBOOT_CONFIG = "polarfire_mpfs250_m.config"`, point `WOLFBOOT_EXTRA_MAKE_FLAGS` at the `fpga_design_config` directory and supply a pre-generated ECC384 key through `WOLFBOOT_SIGNING_KEY`; the recipes deliberately never generate one. The PolarFire section of `recipes-wolfssl/wolfboot/README.md` in that layer has the full `local.conf` block. The eNVM programming step is still the `mpfsBootmodeProgrammer` command from path 2.

### Restoring HSS

Programming the eNVM is not one-way, and the boot mode does not change: both images are boot mode 1.

```sh
$SC_INSTALL_DIR/eclipse/jre/bin/java -jar \
    $SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar \
    --bootmode 1 --die MPFS250T --package FCG1152 --workdir $PWD hss-envm-wrapper.elf
```

`hss-envm-wrapper.elf` comes from an HSS build for the board (`make BOARD=mpfs-video-kit`, output in `build/`). Build it before you need it.

### Secure dies

The programmer's `--die` list covers `MPFS025T` through `MPFS460T` and their `_ES` variants only - a secure ("S") die is not in it. For those parts `tools/scripts/mpfs_bootmode2.sh` drives Libero's `fpgenprog` directly: `--envm` programs the eNVM client only and leaves the boot mode alone, and the default run puts a signed User Boot Loader in sNVM and selects boot mode 2. See [Boot mode 2: User Boot Loader stub](#boot-mode-2-user-boot-loader-stub).

### When it does not work

| Symptom | Cause |
|---|---|
| `undefined reference to ...` after switching config | stale objects; `make clean` and rebuild |
| no output at all | wrong serial interface - the E51 is on MMUART0, the first of the four USB-serial interfaces |
| `Invalid elf, falling back to raw binary` | normal: a FIT is not an ELF, and wolfBoot says so before parsing it as a FIT |
| `SD Card: Card init failed (-1)` | the card was not ready: re-seat it and power-cycle. The board routes card-detect through the fabric, so this build assumes a card is present (`SDHCI_FORCE_CARD_DETECT`) and cannot report its absence |
| `Firmware Invalid` or a version of 0 | the image was signed with a key whose public half is not the one in `src/keystore.c`; re-sign, or rebuild the bootloader with the matching key |
| banner, then silence | the FIT loaded but Linux has no console: the dtb's own `/chosen/bootargs` wins unless `LINUX_BOOTARGS` is set at build time. See [bootargs](compile.md#linux-kernel-command-line-bootargs) |
| banner, then silence, and no `[SBI]` output at all | the image was built with `make CFLAGS_EXTRA=...`, which overrode the config's `-DWOLFBOOT_MMODE_SMODE_BOOT`. Check with `nm wolfboot.elf | grep sbi`; an M-mode build that boots an S-mode OS must have those symbols |
| DDR training stops or reports a failure | build with `CFLAGS_EXTRA+=-DDEBUG_DDR` and compare against the board's Libero settings |

### Next

* [All seven boot configurations](#boot-configurations) - SD, eMMC, QSPI, L2-LIM with no DDR, M-mode with and without DDR, and the boot mode 2 stub.
* [Build flags](#m-mode-build-flags) - watchdog policy, QSPI clock divider, stack-overflow detection.
* [Hardware root of trust](#hardware-root-of-trust-puf-kek-snvm-keystore-wrapped-image-key) - keystore in sNVM, PUF-derived KEK, wrapped image-encryption key.
* [SBI vendor extension](#sbi-vendor-extension-for-the-os) - device serial, PUF-derived keys and System Controller services for the OS.

## Features

* RISC-V 64-bit: E51 monitor core `rv64imac` (no FPU), U54 application cores `rv64gc`
* Five-core CPU: 1x E51 monitor + 4x U54 application cores
* Integrated DDR3/4, LPDDR3/4 controller and PHY
* eNVM, L2 scratchpad and LIM on-chip memories, any of which wolfBoot can run from
* System Controller with secure NVM, an SRAM-PUF and the TeraFire (Athena F5200) crypto engine
* MSS and System Controller QSPI controllers, SD/eMMC

## Boot configurations

Seven ready-to-use config templates cover all supported boot mode / storage / memory combinations:

| Configuration | Config File | Boot Mode | Storage | Memory | HSS |
|---------------|-------------|-----------|---------|--------|-----|
| **SDCard** | `polarfire_mpfs250.config` | S-mode (U54 via HSS) | SD Card | DDR | Yes |
| **eMMC** | `polarfire_mpfs250.config` + `DISK_EMMC=1` | S-mode (U54 via HSS) | eMMC | DDR | Yes |
| **QSPI (S-mode)** | `polarfire_mpfs250_qspi.config` | S-mode (U54 via HSS) | MSS or SC QSPI | DDR | Yes |
| **QSPI + L2-LIM** | `polarfire_mpfs250_hss_l2lim.config` | S-mode (U54 via HSS) | SC QSPI | L2-LIM (no DDR) | Yes |
| **M-Mode (no HSS)** | `polarfire_mpfs250_m_qspi.config` | M-mode (E51, no HSS) | SC QSPI | L2 Scratchpad | No |
| **M-Mode + DDR** | `polarfire_mpfs250_m.config` | M-mode (E51, no HSS) | SD Card | LPDDR4 (DDR) | No |
| **Boot mode 2 UBL** | `polarfire_mpfs250_ubl.config` | System Controller -> sNVM stub | eNVM (verifies it) | L2-LIM | No |

The **M-Mode + DDR** configuration brings up the LPDDR4 controller from
the E51 in M-mode (no HSS), then loads a signed FIT image from SD card,
verifies it (SHA384 + ECC384) and hands off to a U54 hart in S-mode.
wolfBoot includes a minimal SBI runtime (`src/riscv_sbi.c`) so the
hand-off target can be a Linux kernel: tested booting 4-CPU SMP Yocto
Linux to a login prompt in ~40 s from power-on on the MPFS250T Video
Kit. Because all
LIBERO_SETTING_\* values are board-specific, this build pulls them from
a Libero/HSS-generated `fpga_design_config.h` pointed at by the
`LIBERO_FPGA_CONFIG_DIR` makefile variable - typical sources are an
HSS Video Kit build at
`<hss>/build/boards/mpfs-video-kit/fpga_design_config` or a Libero MSS
Configurator export. Setting `LIBERO_FPGA_CONFIG_DIR` automatically
defines `MPFS_DDR_INIT` and adds the directory to the include path
(see `arch.mk`); when unset, the DDR HAL is excluded and the build
still produces a working M-mode wolfBoot without DDR. Add
`-DDEBUG_DDR` to `CFLAGS_EXTRA` for verbose register-level traces
during bring-up.

For GitHub Actions, `tools/ci/gen_mpfs_libero_stub.sh` generates a
compile-only stub (every `LIBERO_SETTING_*` symbol referenced by the
HAL, defined to `0`; loop-bound settings to `1U`) so the M-Mode + DDR
build path stays under continuous integration. The generated header is
**not** committed and **not** runnable - real boards must point
`LIBERO_FPGA_CONFIG_DIR` at the actual Libero / HSS output.

Key build settings that differ between configurations:

| Setting | SDCard | eMMC | QSPI | L2-LIM | M-Mode | M-Mode + DDR |
|---------|--------|------|------|--------|--------|--------------|
| `WOLFBOOT_ORIGIN` | `0x80000000` | `0x80000000` | `0x80000000` | `0x08040000` | `0x0A000000` | `0x0A000000` |
| `WOLFBOOT_LOAD_ADDRESS` | `0x8E000000` | `0x8E000000` | `0x8E000000` | `0x08060000` | `0x0A010200` | `0x82000000` |
| `WOLFBOOT_LOAD_DTS_ADDRESS` | `0x8A000000` | `0x8A000000` | `0x8A000000` | - | - | `0x8A000000` |
| `EXT_FLASH` | 0 | 0 | 1 | 1 | 1 | 0 |
| `DISK_SDCARD` | 1 | 0 | 0 | 0 | 0 | 1 |
| `DISK_EMMC` | 0 | 1 | 0 | 0 | 0 | 0 |
| `MPFS_L2LIM` | - | - | - | 1 | - | - |
| `RISCV_MMODE` | - | - | - | - | 1 | 1 |
| `LIBERO_FPGA_CONFIG_DIR` | - | - | - | - | - | required |
| `WOLFBOOT_MMODE_SMODE_BOOT` | - | - | - | - | - | 1 |
| Linker script | `mpfs250.ld` | `mpfs250.ld` | `mpfs250.ld` | `mpfs250-hss.ld` | `mpfs250-m.ld` | `mpfs250-m.ld` |
| HSS YAML | `mpfs.yaml` | `mpfs.yaml` | `mpfs.yaml` | `mpfs-l2lim.yaml` | N/A | N/A |
| `ELF` output | 1 | 1 | 1 | 0 (raw .bin) | 1 | 1 |

> **Note:** the MPFS250 U54/E51 cores have no RISC-V crypto extensions (Zknh), so none of the
> configurations use them. The SD-card, QSPI and M-Mode + DDR configs build the portable RISC-V
> SHA assembly (`NO_ASM=0`); the size-constrained L2-LIM and M-mode QSPI configs set `NO_ASM=1`
> and use the C implementations.

## M-mode build flags

These flags apply to `polarfire_mpfs250_m_qspi.config` and are added via `CFLAGS_EXTRA+=-D...`.

| Flag | Default | Description |
|------|---------|-------------|
| `WATCHDOG` | undefined (disabled) | When defined, the E51 watchdog timer is **kept enabled** during wolfBoot operation with a generous timeout. When undefined, the WDT is **disabled** in `hal_init()` and re-enabled with the boot ROM default in `hal_prepare_boot()` before jumping to the application. Either way, the application receives a normal WDT. |
| `WATCHDOG_TIMEOUT_MS` | `30000` (30 s) | Watchdog timeout in milliseconds when `WATCHDOG` is defined. ECDSA P-384 verification on E51 with portable C math is bounded at ~5 s; the default 30 s avoids any need to refresh the WDT during the long verify call. |
| `MPFS_WDT_NODES_KEEP` (make variable, `=1`) | undefined | Linux boot only: keep the five MSS watchdog nodes enabled in the dtb and stop the parked E51 from refreshing them, so the OS owns the watchdogs. See "Watchdog policy" below. |
| `MPFS_QSPI_CLK_DIV` (make variable) | `QSPI_CLK_DIV_8` | QSPI SCK divider, a `QSPI_CLK_DIV_*` name from `hal/mpfs250.h`. See "PolarFire SoC QSPI". |
| `MPFS_SNVM_DUMP` (make variable, `=1`) | undefined | Read-only: at boot, print the admin word, state and a checksum of `MPFS_SNVM_DUMP_COUNT` sNVM pages from `MPFS_SNVM_DUMP_FIRST` (defaults 2 from 200, the keystore and the wrapped image key). Page contents are never printed. M-mode only. |

### Stack overflow detection

The trap handler in `src/boot_riscv.c` automatically detects stack overflow on synchronous exceptions (requires `DEBUG_BOOT`). When a trap fires with `SP < _main_hart_stack_bottom`, it prints:

```
TRAP: cause=2 epc=A000740 tval=0
      sp=A02FFE8
STACK OVERFLOW: under by 24
```

This is helpful for diagnosing illegal-instruction TRAPs at random valid `.text` addresses, which are the classic signature of stack overflow corrupting the return address.

`STACK_SIZE` and `STACK_SIZE_PER_HART` come from the `.config` and are substituted into
`hal/mpfs250-m.ld`: 64 KB for the boot hart in `polarfire_mpfs250_m.config`, which is sized for an
ML-DSA-87 verify on top of two `IMAGE_HEADER_SIZE` stack buffers. An ECC384 + SHA384 verify peaks
at about 6 KB.

## Building wolfBoot

All build settings come from .config file. For this platform use `TARGET=mpfs250` and `ARCH=RISCV64`.

See example configuration at `config/examples/polarfire_mpfs250.config`.

```sh
# Setup .config (build settings)
cp config/examples/polarfire_mpfs250.config .config

# build boot loader
make wolfboot.elf
```

To assemble this as a flashable image you need the 0x100 byte HART header added:

```sh
git clone https://github.com/polarfire-soc/hart-software-services.git
cd hart-software-services
cd tools/hss-payload-generator
make
# install tool
sudo cp hss-payload-generator /usr/local/bin/
```

The HSS MMC boot source looks for GPT with GUID "21686148-6449-6E6F-744E-656564454649" or sector "0" if no GPT found. That GUID is the default "BIOS" boot partition.

The resulting image from `hss-payload-generator` can be directly placed into GPT BIOS partition.

Use this command to assemble a bootable wolfboot image. The output name is yours to choose; do
not reuse `wolfboot.bin`, which is the raw binary the build just produced:

```sh
hss-payload-generator -vvv -c ./hal/mpfs.yaml wolfboot-payload.bin
```

You must generated the Device Tree Binary using:

```sh
dtc -I dts -O dtb hal/mpfs.dts -o hal/mpfs.dtb
```

Write that payload to the card's HSS boot partition - see [Writing a card](#writing-a-card).

## QSPI

PolarFire SoC has two CoreQSPI v2 controllers with identical register layouts. The selection
is made at build time via `MPFS_SC_SPI` and affects which QSPI base address wolfBoot uses:

```text
            +-------------------+                         +----------------------+
            |      U54 cores    |                         |      U54 cores       |
            |      (wolfBoot)   |                         |      (wolfBoot)      |
            +---------+---------+                         +----------+-----------+
                      |                                              |
                      | direct register access                       | direct register access
                      | (MSS QSPI @ 0x2100_0000)                    | (SC QSPI @ 0x3702_0100)
                      v                                              v
            +-------------------+                         +----------------------+
            |  MSS QSPI IP      |                         |  SC QSPI IP          |
            |  (CoreQSPI v2)    |                         |  (CoreQSPI v2)       |
            +---------+---------+                         +----------+-----------+
                      |                                              |
                      v                                              v
              External QSPI flash                           Fabric-connected flash
```

Build options:

- MSS QSPI controller (direct register access at 0x21000000, read/write/erase)
  - `EXT_FLASH=1`
  - Do not set `MPFS_SC_SPI`
  - Example config: `config/examples/polarfire_mpfs250_qspi.config` with `CFLAGS_EXTRA` line removed.

- SC QSPI controller (direct register access at 0x37020100, read/write/erase)
  - `EXT_FLASH=1`
  - `CFLAGS_EXTRA+=-DMPFS_SC_SPI`
  - Example config: `config/examples/polarfire_mpfs250_qspi.config` as-is.
  - Both controllers share the same CoreQSPI v2 register interface.
    The only difference is that SC QSPI does not need MSS clock/reset setup.

Notes:
- Both modes support full read, write, and erase operations.
- SCK is the controller clock divided by the CoreQSPI `CLKRATE` field, SCK = PCLK / (2 * CLKRATE).
  `MPFS_QSPI_CLK_DIV` selects the divider by its `QSPI_CLK_DIV_*` name from `hal/mpfs250.h`
  (`QSPI_CLK_DIV_2` through `QSPI_CLK_DIV_30`, CLKRATE 1..15). The default `QSPI_CLK_DIV_8` is
  about 18.75 MHz from the 150 MHz APB clock, where read throughput stops improving: from there on
  the polled FIFO transfer rather than SCK sets the rate, so a faster SCK buys nothing. The MSS QSPI
  PCLK is the MSS APB clock, which is 40 MHz at the M-mode reset clock until wolfBoot's PLL
  bring-up, so an M-mode build that starts QSPI before `mss_pll_init()` runs SCK that much slower;
  the SC QSPI is clocked by the System Controller and is unaffected by the MSS PLL.
- For QSPI-based boot flows, disable SD/eMMC in the config (`DISK_SDCARD=0`, `DISK_EMMC=0`) unless you
  explicitly want wolfBoot to load from disk and the application from QSPI.
- The MSS QSPI path expects external flash on the MSS QSPI pins; the SC QSPI path is for
  fabric-connected flash (design flash) accessed via the System Controller's QSPI instance.

## HSS S-mode with L2-LIM (no DDR)

wolfBoot can run in S-mode via HSS without DDR by targeting the on-chip **L2 Loosely Integrated
Memory (L2-LIM)**. HSS loads wolfBoot into L2-LIM on a U54 application core, and wolfBoot loads
the signed application from SC QSPI into L2-LIM as well. This is the configuration for systems
that keep HSS and run without DDR.

**Features:**
* S-mode on U54 application core (hart 1), loaded by HSS
* wolfBoot and application both reside in L2-LIM, in the 1.25 MB above the HSS resident region
  (`WOLFBOOT_ORIGIN=0x08040000`, `WOLFBOOT_L2LIM_SIZE=0x140000`)
* No DDR required
* SC QSPI flash for both wolfBoot payload and signed application image
* Raw binary output (`ELF=0`) required - ELF with debug symbols is too large for L2-LIM

**Relevant files:**

| File | Description |
|------|-------------|
| `config/examples/polarfire_mpfs250_hss_l2lim.config` | HSS S-mode + SC QSPI + L2-LIM |
| `hal/mpfs250-hss.ld` | Linker script for S-mode with L2-LIM |
| `hal/mpfs-l2lim.yaml` | HSS payload generator YAML for L2-LIM load target |

**Build:**
```sh
cp config/examples/polarfire_mpfs250_hss_l2lim.config .config
make clean && make wolfboot.bin
dtc -I dts -O dtb hal/mpfs.dts -o hal/mpfs.dtb
hss-payload-generator -vvv -c ./hal/mpfs-l2lim.yaml wolfboot-payload.bin
```

Flash the HSS payload to the card's boot partition - see [Writing a card](#writing-a-card).

**Build and sign the test application:**
```sh
make test-app/image_v1_signed.bin
```

**Flash the signed application to QSPI** using the UART programmer (`UART_QSPI_PROGRAM=1`, which
this config sets, over the U54 console on MMUART1; needs `pyserial`):
```sh
python3 tools/scripts/mpfs_qspi_prog.py /dev/ttyUSB1 \
    test-app/image_v1_signed.bin 0x20000
```

**Notes:**
- `UART_QSPI_PROGRAM=1` adds a 3-second boot pause every time. Set to `0` once the flash contents are stable.
- `ELF=0` is required: the test-app linker script (`test-app/RISCV64-mpfs250.ld`) places `.init`
  (containing `_reset()`) first so the raw binary entry point is at offset 0. The full ELF with
  debug symbols exceeds L2-LIM capacity.
- wolfBoot is placed at `0x08040000` (above the HSS L2-LIM resident region) and the application
  is loaded at `0x08060000`. The stack resides at the top of the 1.25 MB region.
- HSS must be built and programmed to eNVM separately (see [Building Hart Software Services](#building-hart-software-services-hss)).
- **LIM instruction fetch caveat:** Ensure `L2_WAY_ENABLE` leaves enough cache ways unallocated
  to back the LIM SRAM region. See the M-mode section for a detailed explanation.
- UART output appears on MMUART1 (`/dev/ttyUSB1`), same as other S-mode configurations.

## M-mode (bare-metal eNVM boot)

wolfBoot supports running directly in Machine Mode (M-mode) on PolarFire SoC, replacing the Hart
Software Services (HSS) as the first-stage bootloader. wolfBoot runs on the E51 monitor core from
eNVM and loads a signed application from SC QSPI flash into L2 Scratchpad (on-chip RAM) -- no HSS
or DDR required. This is the minimal on-chip-only configuration.

**Features:**
* Runs on E51 monitor core (hart 0) directly from eNVM
* Executes from L2 Scratchpad SRAM (256 KB at `0x0A000000`)
* Loads signed application from SC QSPI flash to L2 Scratchpad (`0x0A010200`)
* No HSS or DDR required - boots entirely from on-chip memory
* Parks and releases secondary U54 harts via CLINT IPI
* ECC384 + SHA384 signature verification

**Relevant files:**

| File | Description |
|------|-------------|
| `config/examples/polarfire_mpfs250_m_qspi.config` | M-mode + SC QSPI configuration |
| `hal/mpfs250-m.ld` | M-mode linker script (eNVM + L2 SRAM) |
| `hal/mpfs250.c` | HAL with QSPI driver, UART, L2 cache init |
| `src/boot_riscv_start.S` | M-mode assembly startup |

**Boot flow:**
1. **eNVM reset vector** (`0x20220100`): CPU starts, startup code copies wolfBoot to L2 Scratchpad
2. **L2 Scratchpad execution** (`0x0A000000`): wolfBoot runs from scratchpad
3. **Hardware init**: L2 cache configuration, UART setup
4. **QSPI init**: SC QSPI controller (`0x37020100`), JEDEC ID read, 4-byte address mode
5. **Image load**: Read signed image from QSPI flash (`0x20000`) to L2 Scratchpad (`0x0A010200`)
6. **Verify & boot**: SHA384 integrity check, ECC384 signature verification, jump to app

**Build:**
```sh
cp config/examples/polarfire_mpfs250_m_qspi.config .config
make clean && make wolfboot.elf
```

**Flash wolfBoot to eNVM** with `mpfsBootmodeProgrammer`, exactly as in
[quick start path 2](#path-2-wolfboot-replaces-hss-and-boots-linux) - the only difference is
which `wolfboot.elf` is programmed. Restoring HSS is the same command with the HSS ELF.

**Build and sign the test application:**
```sh
make test-app/image_v1_signed.bin
```

**Flash the signed application to QSPI** using the UART programmer (`UART_QSPI_PROGRAM=1`, which
this config sets, over the E51 console on MMUART0; needs `pyserial`):
```sh
python3 tools/scripts/mpfs_qspi_prog.py /dev/ttyUSB0 \
    test-app/image_v1_signed.bin 0x20000
```

The script:
1. Waits for wolfBoot to print the `QSPI-PROG: Press 'P'` prompt (power-cycle the board)
2. Sends `P` to enter programming mode
3. Transfers the binary in 256-byte ACK-driven chunks
4. wolfBoot erases, writes, and then continues booting the new image

Use `0x20000` for the boot partition and `0x02000000` for the update partition.

**QSPI partition layout** (Micron MT25QL01G, 128 MB):

| Region | Address | Size |
|--------|---------|------|
| Boot partition | `0x00020000` | ~32 MB |
| Update partition | `0x02000000` | ~32 MB |
| Swap partition | `0x04000000` | 64 KB |

**UART mapping:**

| Hart | Core | MMUART | USB device |
|------|------|--------|------------|
| 0 | E51 | MMUART0 | /dev/ttyUSB0 |
| 1 | U54_1 | MMUART1 | /dev/ttyUSB1 |
| 2 | U54_2 | MMUART2 | N/A |
| 3 | U54_3 | MMUART3 | N/A |
| 4 | U54_4 | MMUART4 | N/A |

**Expected serial output on successful boot:**
```
wolfBoot Version: X.Y.Z (...)
Running on E51 (hart 0) in M-mode
QSPI: Using SC QSPI Controller (0x37020100)
QSPI: Flash ID = 0x20 0xBA 0x21
QSPI-PROG: Press 'P' within 3s to program flash
QSPI-PROG: No trigger (got 0x00 ...), booting
Versions: Boot 1, Update 0
...
Firmware Valid
Booting at 0x...
```

**Notes:**
- The E51 is `rv64imac` (no FPU or crypto extensions). wolfBoot is compiled with `NO_ASM=1` to
  use portable C crypto implementations and `-march=rv64imac -mabi=lp64` for correct code
  generation. The `rdtime` CSR instruction is not available in bare-metal M-mode; wolfBoot uses a
  calibrated busy-loop for all delays (`udelay()` in `hal/mpfs250.c`).
- `UART_QSPI_PROGRAM=1` adds a 3-second boot pause every time. Set to `0` once the flash
  contents are stable.
- The config uses `WOLFBOOT_LOAD_ADDRESS=0x0A010200` to place the application in L2 Scratchpad
  above wolfBoot code (~64 KB at `0x0A000000`), with the stack at the top of the 256 KB region.
- **LIM instruction fetch limitation:** The on-chip LIM (`0x08000000`, 2 MB) is backed by L2
  cache ways. When `L2_WAY_ENABLE` is set to `0x0B` (all cache ways 0-7 active for caching),
  no ways remain for LIM backing SRAM. Data reads from LIM work through the L2 cache, but
  instruction fetch silently hangs - the CPU stalls with no trap generated. For this reason the
  application is loaded into L2 Scratchpad (`0x0A000000`), which is always accessible regardless
  of `L2_WAY_ENABLE`. To use LIM, reduce `L2_WAY_ENABLE` to free cache ways for LIM backing.
- **Strip debug symbols** before signing the test-app ELF. The debug build is ~150 KB but the
  stripped ELF is ~5 KB. L2 Scratchpad has ~150 KB available between wolfBoot code and the stack:
  `riscv64-unknown-elf-strip --strip-debug test-app/image.elf`
- **DDR support:** software LPDDR4 initialization is included via the **M-Mode + DDR**
  configuration (`polarfire_mpfs250_m.config`, requires `LIBERO_FPGA_CONFIG_DIR`) for use cases
  that require loading larger images (e.g. a Linux FIT) to DDR memory. See
  [M-mode + DDR](#m-mode--ddr-booting-linux-minimal-sbi).

## M-mode + DDR: booting Linux (minimal SBI)

The **M-Mode + DDR** configuration (`config/examples/polarfire_mpfs250_m.config`) replaces both
HSS and OpenSBI: wolfBoot performs the LPDDR4 init/training on the E51, loads and verifies a
signed Yocto FIT image (kernel + dtb) from SD card into DDR, applies device-tree fixups, releases
U54 hart 1 into S-mode at the kernel entry, and then remains resident as a minimal M-mode SBI
runtime. Validated on the MPFS250T Video Kit: 4-CPU SMP Yocto Linux to login in ~40 s from
power-on.

**Minimal SBI runtime** (`src/riscv_sbi.c`, a clean-room implementation of the OpenSBI/SBI spec; generic RISC-V with HAL hooks; enabled by
`WOLFBOOT_MMODE_SMODE_BOOT`):
* SBI v0.3 extensions: BASE, TIME (per-hart `mtimecmp`, MTIP-to-STIP injection), IPI (SSIP
  injection via CLINT MSIP), RFENCE (remote `fence.i` / `sfence.vma` with completion wait),
  HSM (`hart_start`/`hart_stop`/`hart_status` backed by per-hart start mailboxes), DBCN and the
  legacy console putchar (shared with the wolfBoot UART), SRST (reset types 1 and 2 assert the
  MSS reset; type 0, shutdown, parks every hart in `wfi`, as the MSS has no power-off path).
* `rdtime` emulation: the U54/E51 have no `time` CSR, so `rdtime` is emulated from CLINT MTIME.
  The M-mode HAL starts the MTIME time base before hand-off via SYSREG `RTC_CLOCK_CR` at 1 MHz
  (`mpfs_enable_mtime()`), matching the device-tree `timebase-frequency`.
* Misaligned load/store emulation (not delegatable on these harts), including compressed forms,
  for the kernel's unaligned copy tails.
* Per-hart M-mode trap stacks live in the `hss-buffer` reserved (nomap) DDR region; cross-hart
  state (HSM mailboxes, IPI flags, the hart-release gate flag) lives in the E51 DTIM at
  `0x01000000`, which is uncached and coherent for all harts. Cacheable L2-scratchpad memory
  must not be used for cross-hart signalling (stores can be lost on dirty-line eviction).

**Device-tree fixups** applied to the loaded dtb (`hal/mpfs250.c`): bootargs/root device
(the DTB's own `/chosen/bootargs` wins unless `LINUX_BOOTARGS`/`LINUX_BOOTARGS_ROOT` is set;
see "Linux kernel command line (bootargs)" in `docs/compile.md`),
MAC addresses from the device serial number, and all five MSS watchdog nodes are disabled.

**Watchdog policy:** the MSS watchdogs always count and reset the chip on timeout (they cannot
be disabled in hardware, and `CONTROL=0` does not prevent the reset). After hand-off the parked
E51 acts as a monitor and refreshes all five watchdogs; the OS watchdog driver is disabled via
the dtb fixup so the two never conflict. `MPFS_WDT_NODES_KEEP=1` hands the watchdogs to the OS
instead: the dtb nodes stay enabled and the parked E51 stops refreshing, so the OS driver arms
them at probe and something on the OS side (a watchdog daemon, or the design's own fabric
scheme) has to service them from then on. If nothing does, the MSS watchdog expires and resets
the board shortly after Linux reaches userspace, which presents as a boot loop with no error
message: the knob must be paired with a servicer in the same image, for example systemd's
`RuntimeWatchdogSec`. A design with its own fabric watchdog needs neither setting; wolfBoot
never touches fabric logic.

**Hand-off / SMP flow:** secondary harts park in eNVM until the E51 signals image-copy
completion (DTIM gate flag), then park in a WFI loop. The boot hart is released with a staged
mailbox {entry, dtb} plus MSIP; Linux brings up the remaining harts through SBI HSM
`hart_start`, which uses the same mailbox + MSIP path. The release path must stay fast
(no UART access): the kernel allows roughly one second for a started hart to come online.

**Read-first requirement:** the LPDDR4 controller on this board needs the first CPU-side
transaction after DDR initialization to be a read. When a write comes first on a cold boot it
lands in DDR displaced by several bus beats, and every later cached-port write in that boot
follows it. `mpfs_ddr_init()` therefore reads one block through the non-cached alias before
anything is written, after which CPU, L2 write-back and SDMA writes all land where they should.

**Driver structure:** the licensed Cadence DDR controller logic (controller CSR programming, the
Memory Test Controller engine, and the LPDDR4 mode-register protocol) lives in the
target-independent `src/ddr_cadence.c` / `include/ddr_cadence.h` (controller base overridable via
`DDR_CADENCE_CTRL_BASE`). The Microchip-specific PHY, PLL, clock mux and training, plus the
board's `LIBERO_SETTING_*` values, stay in `hal/mpfs250_ddr.c`, which builds the controller
register table and composes the generic calls. Both compile only when `MPFS_DDR_INIT` is set.

## Boot mode 2: User Boot Loader stub

Boot mode 2 ("user secure boot from sNVM") lets the System Controller authenticate the first code the MSS runs: it copies a User Boot Loader (UBL) out of authenticated sNVM pages into the LIM and releases all five harts at its entry. wolfBoot provides that UBL as a small verifier target, `TARGET=mpfs250_ubl`: it checks an ECC384/SHA384 wolfBoot manifest over the eNVM wolfBoot image against the public key compiled into the stub, then enters the eNVM wolfBoot at its reset vector on every hart exactly as a boot mode 1 boot would. The eNVM wolfBoot itself is unchanged and stays field-updatable; what the sNVM pages pin is the key that must have signed it.

**eNVM layout** (`hal/mpfs250-m.ld`, `hal/mpfs250.h`)

| Address | Content |
|---------|---------|
| `0x20220000` | boot ROM meta information (written by `mpfsBootmodeProgrammer`) |
| `0x20220100` | wolfBoot image, reset vector first |
| `0x2023FC00` | wolfBoot manifest header (`MPFS_ENVM_MANIFEST=1`), signed by the stub's key |

The manifest is detached from the image it signs (the `WOLFBOOT_SELF_HEADER` form), so the image does not move and a boot mode 1 device boots the same eNVM contents. The last two eNVM pages stay free for the programmer.

**Relevant files:**

| File | Description |
|------|-------------|
| `config/examples/polarfire_mpfs250_ubl.config` | UBL stub configuration |
| `hal/mpfs250_ubl.c`, `hal/mpfs250_ubl.h` | stub HAL: watchdog hold, MMUART0 at the reset clock, manifest verify, hart hand-off |
| `hal/mpfs250_ubl.ld` | LIM linker script with the sNVM size limit |
| `src/boot_riscv_start.S` | `TARGET_mpfs250_ubl` startup: no copy, U54s park on a DTIM mailbox |
| `tools/scripts/mpfs_bootmode2.sh` | sNVM + boot mode programming on the secure dies, eNVM-only update |

**Build the stub** (its own signing key; keep it out of the tree that signs application images):
```sh
cp config/examples/polarfire_mpfs250_ubl.config .config
make clean && make
# wolfboot.elf is the UBL; wolfboot_signing_private_key.der is the UBL key
```
The image must fit the 221 authenticated sNVM pages (52,096 bytes after the 60-byte UBL header); the linker script enforces it. The sNVM pages above the stub stay available for the sNVM keystore and wrapped keys of [Hardware root of trust](#hardware-root-of-trust-puf-kek-snvm-keystore-wrapped-image-key).

**Build and sign the eNVM wolfBoot** for the stub (any M-mode configuration):
```sh
cp config/examples/polarfire_mpfs250_m.config .config
make clean && make LIBERO_FPGA_CONFIG_DIR=... MPFS_ENVM_MANIFEST=1 \
    MPFS_UBL_KEY=/path/to/ubl/wolfboot_signing_private_key.der \
    wolfboot_envm_signed.bin
```
`MPFS_ENVM_MANIFEST=1` links a manifest placeholder into the last used eNVM page; the rule signs the image without it (`wolfboot_envm.bin`, the ELF's loadable bytes with alignment gaps zero-filled, which is what the programmer writes), writes the resulting header into the placeholder and produces `wolfboot_envm_signed.elf`, which programs image and manifest together. `MPFS_UBL_KEY` defaults to the tree's own key, `MPFS_ENVM_VERSION` (default 1) is the manifest version, and `-DMPFS_UBL_MIN_VERSION=N` in the stub build refuses older manifests.

**Program** (requires `SC_INSTALL_DIR` and `FPGENPROG`, see the M-mode section):
```sh
# 1. eNVM: image + manifest.  This also selects boot mode 1, so the device
#    boots the new eNVM contents before the stub is in place.
java -jar $SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar \
    --bootmode 1 --die MPFS250T --package FCG1152 --workdir $PWD \
    wolfboot_envm_signed.elf

# 2. sNVM: the stub, and boot mode 2.  Non-secure dies: the programmer
#    itself (--bootmode 2 --snvm_page 0 --usk 0 wolfboot.elf).  Secure dies
#    (MPFS250TS...), which the programmer does not list but which an
#    authenticated sNVM client requires:
tools/scripts/mpfs_bootmode2.sh --die MPFS250TS --workdir $PWD /path/to/ubl/wolfboot.elf
```
The script runs the programmer in `--dryrun` for the UBL image and the User sNVM Key (USK), replays its `fpgenprog` steps for the requested die and programs. Keep the USK file it names: the sNVM pages can only be rewritten with it.

The UBL lives in an authenticated sNVM client, which the System Controller keys from the device's sNVM Master Key (SMK)
and the client's 12-byte USK. A device whose SMK has never been initialized cannot hold such a client, and
`mpfsBootmodeProgrammer` never asks for one, so `--regenerate-smk` passes `--regenerate_smk` to the `snvm_client` step
and the generated bitstream carries a new SMK. A new SMK invalidates every authenticated or encrypted sNVM client
already on the device; wolfBoot's own keystore and wrapped image key are plaintext clients and are not SMK-keyed.

Updating the eNVM wolfBoot under boot mode 2 goes through the same script with `--envm`, which programs the eNVM client only and leaves the boot mode and sNVM alone:
```sh
tools/scripts/mpfs_bootmode2.sh --envm --die MPFS250TS --workdir $PWD wolfboot_envm_signed.elf
```
`mpfsBootmodeProgrammer --bootmode 1` restores boot mode 1.

**Expected serial output:**
```
wolfBoot UBL 2.10.0: eNVM image 0x20220100, manifest 0x2023FC00
UBL: eNVM wolfBoot signature OK (1051 ms), entering 0x20220100
UBL: 5 of 5 harts released
wolfBoot Version: ...
```
A manifest that does not verify prints which check failed (`no manifest`, `digest mismatch`, `signature invalid`) and `UBL: eNVM wolfBoot verify FAILED`; the stub panics and nothing in eNVM runs.

**Notes:**
- The stub runs at the reset clock and changes no clock, L2 way or peripheral the eNVM wolfBoot expects to find in its reset state. It holds the five MSS watchdogs in the no-reset, maximum-window state for the verify and puts the E51's back before the jump, so the eNVM wolfBoot's own watchdog snapshot is what the boot ROM left.
- The U54s wait in the stub on a DTIM mailbox (`hal/mpfs250_ubl.h`) for the verified entry, acknowledge and jump; the E51 follows last. All five harts then run the eNVM startup as in boot mode 1.
- Immutability of the stub is a Libero decision: the ROM flag on its sNVM pages and a frozen boot configuration. wolfBoot does not program either.
- The chain is: the System Controller authenticates the stub out of sNVM against the device SMK, the stub's compiled-in public key authenticates the eNVM wolfBoot, and that wolfBoot's own keystore authenticates the OS image. Each link holds the next one's key, so the stub carries a keystore of its own (`.keystore` in `hal/mpfs250_ubl.ld`) and never consults the one in eNVM.
- Give the stub a key no application image is ever signed with. `MPFS_UBL_KEY` defaults to the tree's signing key, and under that default anything already signed for the device satisfies the eNVM manifest check, so a party who can rewrite eNVM can have the stub enter a blob of their choosing.
- The stub enforces no rollback floor unless it is built with `-DMPFS_UBL_MIN_VERSION=N`: its sNVM pages are meant to be ROM-flagged, so it has nowhere to keep a counter and the floor is a property of the stub image instead.
- Nothing before wolfBoot initialises the PLIC under boot mode 2, where boot mode 1 reaches it with every priority already set, so the OS inherits the controller in its reset state. That is harmless: Linux's `irq-sifive-plic` driver clears the enable bit and writes the priority for every source on every context at probe, and the Video Kit declares `riscv,ndev = <186>`, which is the whole of `PLIC_NUM_SOURCES`, so no source is left untouched. wolfBoot therefore does not program the PLIC.

## Encryption

PolarFire SoC boots from disk with `WOLFBOOT_NO_PARTITIONS`, so there is no partition-resident key store: the encryption key is held in RAM for the life of the boot. A production build keeps it in secure NVM instead, wrapped by the PUF-derived KEK - see [Hardware root of trust](#hardware-root-of-trust-puf-kek-snvm-keystore-wrapped-image-key).

Enable encryption in your configuration with `ENCRYPT=1` and one of: `ENCRYPT_WITH_AES256=1`, `ENCRYPT_WITH_AES128=1`, or `ENCRYPT_WITH_CHACHA=1`.

| Algorithm | Key Size | Nonce/IV Size |
|-----------|----------|---------------|
| ChaCha20  | 32 bytes | 12 bytes      |
| AES-128   | 16 bytes | 16 bytes      |
| AES-256   | 32 bytes | 16 bytes      |

The `libwolfboot` API provides the following functions for managing the encryption key:

```c
int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce);
int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce);
int wolfBoot_erase_encrypt_key(void);  /* called automatically by wolfBoot_success() */
```

To use your own implementation for getting the encryption key use `CUSTOM_ENCRYPT_KEY` and `OBJS_EXTRA=src/my_custom_encrypt_key.o`.
Then provide your own implementation of `int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce);`

Example:

```c
int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    int i;
    /* Test key: "0123456789abcdef0123456789abcdef" (32 bytes for AES-256) */
    const char test_key[] = "0123456789abcdef0123456789abcdef";
    /* Test nonce: "0123456789abcdef" (16 bytes) */
    const char test_nonce[] = "0123456789abcdef";

    for (i = 0; i < ENCRYPT_KEY_SIZE && i < (int)sizeof(test_key); i++) {
        key[i] = (uint8_t)test_key[i];
    }
    for (i = 0; i < ENCRYPT_NONCE_SIZE && i < (int)sizeof(test_nonce); i++) {
        nonce[i] = (uint8_t)test_nonce[i];
    }
    return 0;
}
```

To sign and encrypt an image, create a key file with the concatenated key and nonce, then use the sign tool:

```sh
# Create key file (32-byte key + 16-byte nonce for AES-256)
printf "0123456789abcdef0123456789abcdef0123456789abcdef" > /tmp/enc_key.der

# Sign and encrypt
./tools/keytools/sign --ecc384 --sha384 --aes256 --encrypt /tmp/enc_key.der \
    fitImage wolfboot_signing_private_key.der 1
```

The result is `fitImage_v1_signed_and_encrypted.bin`, which gets placed into your OFP_A or OFP_B partitions.

```sh
sudo dd if=fitImage_v1_signed_and_encrypted.bin of=/dev/sdc2 bs=512 status=progress && sudo cmp fitImage_v1_signed_and_encrypted.bin /dev/sdc2
sudo dd if=fitImage_v1_signed_and_encrypted.bin of=/dev/sdc3 bs=512 status=progress && sudo cmp fitImage_v1_signed_and_encrypted.bin /dev/sdc3
```

During boot, wolfBoot decrypts the image headers from disk to select the best candidate, loads and decrypts the full image to RAM, then verifies integrity and authenticity before booting. On successful boot, `wolfBoot_success()` clears the key from RAM.

See the [Encrypted Partitions](encrypted_partitions.md) documentation for additional details.

## PQC (ML-DSA)

### Configuration

Update your `.config` file with the following ML-DSA settings, or pass them as `make` arguments on top of `config/examples/polarfire_mpfs250_m.config` (the standalone M-mode E51 target verifies ML-DSA-87 this way, with SHA-384 as the image hash):

```makefile
# ML-DSA 87 (Category 5)
SIGN=ML_DSA
HASH=SHA384
ML_DSA_LEVEL=5
IMAGE_SIGNATURE_SIZE=4627
IMAGE_HEADER_SIZE=12288
WOLFBOOT_SECTOR_SIZE?=0x4000
```

**Important:**
- The `sign` tool requires `IMAGE_HEADER_SIZE` to be set as an environment variable, even if it's already configured in `.config`. This is because the sign tool reads the environment variable separately to determine the header size for padding. Without this, the sign tool may use a smaller default header size, causing a mismatch with wolfBoot's expected header size.
- The `WOLFBOOT_SECTOR_SIZE` must be larger than the `IMAGE_HEADER_SIZE`.

### Signing and Encryption

```sh
# Sign and Encrypt with PQ ML-DSA 5 (87)
# NOTE: IMAGE_HEADER_SIZE must match the value in .config
IMAGE_HEADER_SIZE=12288 ML_DSA_LEVEL=5 ./tools/keytools/sign --ml_dsa --sha256 --aes256 --encrypt /tmp/enc_key.der \
    fitImage wolfboot_signing_private_key.der 1
```

**ML-DSA Parameter Reference:**

| ML_DSA_LEVEL | Security Category | Signature Size | Private Key | Public Key | Recommended IMAGE_HEADER_SIZE |
|--------------|-------------------|----------------|-------------|------------|------------------------------ |
| 2            | Category 2        | 2420           | 2560        | 1312       | 8192                          |
| 3            | Category 3        | 3309           | 4032        | 1952       | 8192                          |
| 5            | Category 5        | 4627           | 4896        | 2592       | 12288                         |

For other ML-DSA levels, adjust `ML_DSA_LEVEL`, `IMAGE_SIGNATURE_SIZE`, and `IMAGE_HEADER_SIZE` accordingly in both `.config` and the signing command.

## Writing a card

Two card layouts are in use, one per boot flow. They are not interchangeable: the partition a
build reads from is fixed at compile time.

| Layout | Used by | Partitions |
|---|---|---|
| HSS | every S-mode configuration (`polarfire_mpfs250.config`, `_qspi`, `_hss_l2lim`) | p1 8 MB BIOS boot (type GUID `21686148-6449-6E6F-744E-656564454649`) holding the HSS payload, p2/p3 64 MB `OFP_A`/`OFP_B` image slots, p4 rootfs |
| M-mode | `polarfire_mpfs250_m.config` (`BOOT_PART_A=0`, `BOOT_PART_B=1`) | p1 32 MB boot slot holding the signed FIT, p2 32 MB update slot, p3 rootfs; no BIOS partition, because nothing loads wolfBoot from the card |

The [example boot output](#example-boot-output) below is the M-mode layout: three GPT entries, the
first two 32 MB.

### HSS layout

Replace `/dev/sdc` with the device `lsblk` reports. Adjust `+64M` for larger image slots.

```sh
sudo fdisk /dev/sdc <<EOF
g
n
1

+8M

n
2

+64M
n
3

+64M
n
4


t
1
4
x
n
2
OFP_A
n
3
OFP_B
r
p
w
EOF
```

The result:

```
Device      Start      End  Sectors  Size Type
/dev/sdc1    2048    18431    16384    8M BIOS boot
/dev/sdc2   18432   149503   131072   64M Linux filesystem
/dev/sdc3  149504   280575   131072   64M Linux filesystem
/dev/sdc4  280576 62332927 62052352 29.6G Linux filesystem
```

### M-mode layout

```sh
sudo parted -s /dev/sdc mklabel gpt \
    mkpart boot   1MiB  33MiB \
    mkpart update 33MiB 65MiB \
    mkpart rootfs 65MiB 100%
```

### Copying images

With the card in the board, the HSS tinyCLI `usbdmsc` command exports it over USB, so no card
reader is needed; `lsblk` then names the partitions. Do not move an SD-Wire mux while `usbdmsc`
is running - the HSS MMC layer then serves one stale block for every LBA and does not recover
until `usbdmsc` is restarted, which makes a populated card read as blank.

```sh
# HSS layout: the HSS payload into the BIOS partition
sudo dd if=wolfboot-payload.bin of=/dev/sdc1 bs=512 && sudo cmp wolfboot-payload.bin /dev/sdc1

# a signed image into an image slot (OFP_A here, p1 in the M-mode layout)
sudo dd if=test-app/image_v1_signed.bin of=/dev/sdc2 bs=512 status=progress
sudo cmp test-app/image_v1_signed.bin /dev/sdc2

# the rootfs
sudo dd if=<image>.rootfs.ext4 of=/dev/sdc4 bs=4M status=progress
sync
```

Build and sign the test application first:

```sh
make test-app/image.elf
./tools/keytools/sign --ecc384 --sha384 test-app/image.elf wolfboot_signing_private_key.der 1
```

With the HSS layout, insert the card and let HSS start wolfBoot. If HSS defaults to eMMC, select
the card with `boot sdcard` at the HSS console, or build HSS with MMC disabled and SD enabled.

## Building Hart Software Services (HSS)

The Hart Software Services (HSS) is the PolarFire SoC zero-stage bootloader (E51 monitor core); it is required only for the HSS-based S-mode configurations above, not for the M-mode + DDR path which replaces it.

```sh
git clone https://github.com/polarfire-soc/hart-software-services.git
cd hart-software-services
make clean
make BOARD=mpfs-video-kit
make BOARD=mpfs-video-kit program
```

## Building Yocto-SDK Linux

The signed Yocto FIT image booted by wolfBoot is produced from Microchip's `meta-mchp` Yocto layer (BSP, drivers, and kernel for the board).

See:
* https://github.com/linux4microchip/meta-mchp/blob/scarthgap/meta-mchp-common/README.md
* https://github.com/linux4microchip/meta-mchp/blob/scarthgap/meta-mchp-polarfire-soc/README.md
* https://github.com/polarfire-soc/polarfire-soc-documentation/blob/master/reference-designs-fpga-and-development-kits/mpfs-video-kit-embedded-software-user-guide.md

Building mchp-base-image Yocto Linux:

```sh
mkdir ../yocto-dev-polarfire
cd ../yocto-dev-polarfire
repo init -u https://github.com/linux4microchip/meta-mchp-manifest.git -b refs/tags/linux4microchip+fpga-2025.10 -m polarfire-soc/default.xml
repo sync
export TEMPLATECONF=${TEMPLATECONF:-../meta-mchp/meta-mchp-polarfire-soc/meta-mchp-polarfire-soc-bsp/conf/templates/default}
source openembedded-core/oe-init-build-env
# A Microchip base image with standard Linux utilities, as well as some Microchip apps and examples
MACHINE=mpfs-video-kit bitbake mchp-base-image
OR
# A Microchip base image with additional support for software development, including toolchains and debug tools
MACHINE=mpfs-video-kit bitbake mchp-base-image-sdk
```

Build images are output to: `./tmp-glibc/deploy/images/mpfs-video-kit/`

### Custom FIT image, signing and copying to SDCard

wolfBoot can either decompress a gzipped kernel at boot time (`GZIP=1`,
the default for `polarfire_mpfs250.config` and `polarfire_mpfs250_qspi.config`)
or accept a pre-decompressed kernel inside the FIT (`GZIP=0`). Pick one path.

`src/gzip.c` decodes with lookup tables by default; `GZIP_SMALL=1` selects
its bit-serial decoder instead, with the same checks and return codes, a
few KB less code and no static tables, at a fraction of the rate. The
difference is largest on cores without a data cache, where the bit-serial
decoder can make the inflate the longest phase of a compressed boot.

#### Option A - Compressed FIT (`GZIP=1`, default)

Set `compression = "gzip"` and point `data` at the gzipped kernel directly
in `hal/mpfs250.its`:

```dts
images {
    kernel-1 {
        data = /incbin/("../yocto-dev-polarfire/build/tmp-glibc/work/mpfs_video_kit-oe-linux/linux-mchp/6.12.22+git/build/linux.bin.gz");
        compression = "gzip";
        load = <0x80200000>;
        entry = <0x80200000>;
        hash-1 { algo = "sha256"; };
        ...
    };
};
```

Then build the FIT directly - no manual `gzip -cdvk` step:

```sh
mkimage -f hal/mpfs250.its fitImage
```

At boot, wolfBoot decompresses the kernel into `0x80200000` directly out of
the FIT `data` blob. The outer wolfBoot signature covers the whole FIT
(including the compressed `data`), and gzip's CRC32 + ISIZE trailer covers
the decompressed output; the per-image `hash-1` subnodes are not re-checked
at runtime as they would duplicate the outer signature.

#### Option B - Uncompressed FIT (`GZIP=0`)

Build wolfBoot with `GZIP=0` and pre-decompress the kernel on the host.
Keep `compression = "none"` in `hal/mpfs250.its`:

```sh
# Extract GZIP compressed linux kernel to wolfboot root
gzip -cdvk ../yocto-dev-polarfire/build/tmp-glibc/work/mpfs_video_kit-oe-linux/linux-mchp/6.12.22+git/build/linux.bin > kernel.bin

# Create custom FIT image
mkimage -f hal/mpfs250.its fitImage
FIT description: PolarFire SoC MPFS250T
Created:         Tue Dec 23 11:29:02 2025
 Image 0 (kernel-1)
  Description:  Linux Kernel
  Created:      Tue Dec 23 11:29:02 2025
  Type:         Kernel Image
  Compression:  uncompressed
  Data Size:    19745280 Bytes = 19282.50 KiB = 18.83 MiB
  Architecture: RISC-V
  OS:           Linux
  Load Address: 0x80200000
  Entry Point:  0x80200000
  Hash algo:    sha256
  Hash value:   800ce147fa91f367ec620936a59a1035c49971ed4b9080c96bdc547471e80487
 Image 1 (fdt-1)
  Description:  Flattened Device Tree blob
  Created:      Tue Dec 23 11:29:02 2025
  Type:         Flat Device Tree
  Compression:  uncompressed
  Data Size:    19897 Bytes = 19.43 KiB = 0.02 MiB
  Architecture: RISC-V
  Load Address: 0x8a000000
  Hash algo:    sha256
  Hash value:   0b4efca8c0607c9a8f4f9a00ccb7691936e019f3181aab45e6d52dae91975039
 Default Configuration: 'conf1'
 Configuration 0 (conf1)
  Description:  Linux kernel and FDT blob
  Kernel:       kernel-1
  FDT:          fdt-1
  Hash algo:    sha256
  Hash value:   unavailable

# Sign FIT image with version 1
./tools/keytools/sign --ecc384 --sha384 fitImage wolfboot_signing_private_key.der 1
```

Then write `fitImage_v1_signed.bin` to the image slots and the Yocto rootfs
(`tmp-glibc/deploy/images/mpfs-video-kit/*.rootfs.ext4`) to the rootfs partition - see
[Writing a card](#writing-a-card).

## Debugging

Start GDB server:

```sh
$SC_INSTALL_DIR/openocd/bin/openocd --command "set DEVICE MPFS" --file board/microsemi-riscv.cfg
```

Start GDB Client: `riscv64-unknown-elf-gdb`

```
file wolfboot.elf
tar rem:3333
add-symbol-file ../hart-software-services/build/hss-l2scratch.elf
set pagination off
foc c

set $target_riscv=1
set mem inaccessible-by-default off
set architecture riscv:rv64
#load wolfboot.elf
#thread apply 2 set $pc=_reset
#thread apply all set $pc=_start
```

## Example boot output

```
wolfBoot Version: 2.10.0
Running on E51 (hart 0) in M-mode
Boot RESET_SR: 1FF (bit0=PERIPH bit1=MSS bit2=CPU bit3=DBG bit4=FABRIC bit5=WDOG bit6=GPIO bit7=BUS bit8=SOFT)
========================================
DDR: Training+MTC PASS
DDR: Initialization COMPLETE
SDHCI: platform init
Reading MBR...
Found GPT PTE at sector 1
Valid GPT partition table
Max number of partitions: 128
  GPT part 0: 0_2000000h @ 0_100000
  GPT part 1: 0_2000000h @ 0_2100000
  GPT part 2: 7_6A300000h @ 0_4100000
Total partitions on disk0: 3
Checking primary OS image in 0,0...
Checking secondary OS image in 0,1...
Versions, A:1 B:0
Load block size: 512KB
Load address 0x82000000
Attempting boot from P:A
Boot partition: 0xA03FD90 (sz 19766364, ver 0x1, type 0x601)
Loading image from disk...
done
Checking image integrity...
done
Verifying image signature...
done
Firmware Valid.
Flattened uImage Tree: Version 17, Size 19766364
Loading Image kernel-1: 0x820000C8 -> 0x80200000 (19745280 bytes)
Loading Image fdt-1: 0x832D4BCC -> 0x8A000000 (19897 bytes)
Loading DTS: 0x8A000000 -> 0x8A000000 (19897 bytes)
Booting at 80200000
FDT: Set chosen, bootargs=earlycon=sbi root=/dev/mmcblk0p3 rootwait uio_pdrv_genirq.of_id=generic-uio
FDT: Device serial: 219A437C-6AE1F1C2-8EDC4324-685B2288
FDT: MAC0 = 00:04:A3:5B:22:88
FDT: Set watchdog@20001000 status=disabled   (... all five MSS WDT nodes)
M->S handoff: entry=0x80200000 hart=0 dtb=0x8A000000
Releasing hart 1 into S-mode at 0x80200000 (dtb=0x8A000000)
[    0.000000] Linux version 6.12.22-linux4microchip+fpga-2025.07 ... #1 SMP
[    0.000000] Machine model: Microchip PolarFire-SoC VIDEO Kit
[    0.000000] SBI specification v0.3 detected
[    0.000000] SBI implementation ID=0x776f6c66 Version=0x1
[    0.000000] SBI TIME extension detected
[    0.000000] SBI IPI extension detected
[    0.000000] SBI RFENCE extension detected
[    0.000000] riscv: providing IPIs using SBI IPI extension
[    0.016264] smp: Bringing up secondary CPUs ...
[    0.040738] smp: Brought up 1 node, 4 CPUs
[    3.764474] Run /sbin/init as init process
...
mpfs-video-kit login:
```

The Linux SBI lines confirm the kernel is talking to wolfBoot's own SBI runtime: the implementation ID `0x776f6c66` is ASCII `"wolf"`, distinguishing it from OpenSBI (ID `0x8`).

## Performance comparison

### Binary Size Comparison

The following table compares wolfBoot binary sizes for different signature algorithms on PolarFire SoC (MPFS250):

| Algorithm | Hash   | Text    | Data | BSS     | Total   | Binary Size |
|-----------|--------|---------|------|---------|---------|-------------|
| ECC384    | SHA384 | 67.1 KB | 8 B  | 3.0 KB  | 70.2 KB | 68 KB       |
| ML-DSA 87 | SHA256 | 63.9 KB | 0 B  | 14.5 KB | 78.4 KB | 64 KB       |

### Boot Time Comparison

Boot time measurements on PolarFire SoC (RISC-V 64-bit U54 @ 625 MHz) for a 19MB encrypted FIT image:

| Algorithm   | Hash    | Load Time | Decrypt Time | Integrity Check | Signature Verify | Total Boot Time |
|-------------|---------|-----------|--------------|-----------------|------------------|-----------------|
| ECC384      | SHA384  | ~800 ms   | ~2900 ms     | ~1500 ms        | ~70 ms           | ~5.3 seconds    |
| ML-DSA 87   | SHA256  | ~835 ms   | ~2900 ms     | ~2100 ms        | ~22 ms           | ~5.9 seconds    |

Standalone M-mode (`polarfire_mpfs250_m.config`, E51 @ 600 MHz, no HSS) measured from power-on on the Video Kit with host-side timestamps on the console, plaintext FIT, ECC384/SHA384, software crypto, the same 19.7 MB kernel either gzip-compressed in the FIT (5.9 MB) or stored uncompressed (19.8 MB):

Elapsed time from power-on at each point, not per-phase duration:

| Reached                                       | gzip FIT | uncompressed FIT |
|-----------------------------------------------|----------|------------------|
| DDR trained, SD init, GPT read, FIT loaded, SHA384 integrity and ECC384 verify done | ~2 s | ~4 s |
| gzip inflate of the kernel finished           | ~18 s    | n/a              |
| FIT copies and DTB fixups done                | ~18 s    | ~5 s             |
| M-mode -> S-mode hand-off                     | ~18 s    | ~5 s             |
| Linux login prompt                            | ~41 s    | ~28 s            |

Both columns are the rootfs-on-a-card image; an initramfs image reaches its login sooner because
it mounts no root device.

The inflate runs on the E51 at about 1.2 MB/s of output, so a compressed kernel costs far more than the extra card read it saves; for a short boot, build the FIT with `compression = "none"` for the kernel (`FIT_KERNEL_COMP_ALG = "none"` in Yocto). Neither the SHA384 (about 11 MB/s on the E51) nor the ECC384 verify is a lever. The SD load, the FIT copies, the DTB fixups and the image hash all use DDR directly (see the read-first requirement under [M-mode + DDR](#m-mode--ddr-booting-linux-minimal-sbi)); measured on a buffer in L2 scratch, the Athena SHA384 is about 1.3x the software rate and AES-256-CTR about 4.4x.

## Source files

`hal/mpfs250.c` - Hardware abstraction layer (UART, QSPI, SD/eMMC, multi-hart)
`hal/mpfs250.h` - Register definitions and hardware interfaces
`hal/mpfs250_ddr.c` - LPDDR4 PHY/PLL/training and DDR bring-up (M-mode, no HSS)
`src/ddr_cadence.c` - Generic Cadence DDR controller driver (controller CSR
programming, Memory Test Controller, LPDDR4 mode-register protocol)
`include/ddr_cadence.h` - Generic Cadence DDR controller register map + interface
`hal/mpfs250.ld` - Linker script for S-mode (HSS-based boot)
`hal/mpfs250-m.ld` - Linker script for M-mode (eNVM + L2 SRAM)
`hal/mpfs250-hss.ld` - Linker script for S-mode (HSS with L2-LIM)
`hal/mpfs250_ubl.c` / `hal/mpfs250_ubl.ld` - Boot mode 2 User Boot Loader stub (sNVM-authenticated verifier of the eNVM wolfBoot)
`hal/mpfs.dts` - Device tree source
`hal/mpfs.yaml` - HSS payload generator configuration for use of DDR
`hal/mpfs-l2lim.yaml` - HSS payload generator for the use of L2-LIM
`hal/mpfs250.its` - Example FIT image creation template
`hal/mpfs250_snvm.c` / `hal/mpfs250_snvm.h` - System Controller mailbox, sNVM keystore, PUF KEK
and key-wrap
`src/riscv_sbi.c` / `include/riscv_sbi.h` - minimal SBI runtime and the vendor extension
definitions
`tools/linux/wolfboot-sbi/` - the Linux consumer of the vendor extension (`/dev/wolfboot`)
`tools/scripts/mpfs_bootmode2.sh` - sNVM and boot-mode programming for the secure dies
`tools/scripts/mpfs_qspi_prog.py` - UART QSPI flash programmer
`config/examples/polarfire_mpfs250*.config` - the seven configuration templates

## SBI vendor extension for the OS

The M-mode wolfBoot stays resident as the SBI for the OS. With `MPFS_SBI_VENDOR=1` it also serves the Microchip vendor extension, EID `0x09000029` (`SBI_EXT_VENDOR_START | mvendorid 0x029`), the same number the Hart Software Services register, so an OS probes one extension for either firmware and tells them apart with `GET_VERSION`. The HSS function IDs for inter-hart communication, remoteproc, crypto and IPC are answered with `SBI_ERR_NOT_SUPPORTED`; `HSS_REBOOT` (0x10) performs a cold MSS reset.

The definitions live in `include/riscv_sbi.h`; the handler is `sbi_hal_vendor_ecall()` in `hal/mpfs250.c`. Arguments travel in `a0..a2`, the SBI error returns in `a0` and the value in `a1`. Buffers are addresses in the caller's address space: M-mode reads and writes them through the caller's translation, byte by byte, with a trap vector that turns a bad address into `SBI_ERR_INVALID_ADDRESS` instead of a stopped hart. A buffer is at most 4096 bytes and, while the caller runs with paging off, must lie in DDR.

| FID | Call | Arguments | Result |
|-----|------|-----------|--------|
| 0x40 | `GET_VERSION` | - | `a1` = 1 |
| 0x41 | `GET_SERIAL` | `a0` buffer, `a1` length >= 16 | the 16-byte device serial number |
| 0x42 | `GET_KEY` | `a0` purpose, `a1` buffer, `a2` length == 32 | a PUF-derived key; each purpose is served once per boot, a second call returns `SBI_ERR_DENIED` |
| 0x43 | `DIGEST_CHECK` | `a0` options word | starts the System Controller digest check |
| 0x44 | `IAP_AUTHENTICATE` | `a0` SPI directory index | starts IAP image authentication |
| 0x45 | `IAP_VERIFY` | `a0` SPI directory index | starts IAP verify |
| 0x46 | `IAP_PROGRAM` | `a0` SPI directory index | the device restarts on success; a return is the failure status in `a1` |
| 0x47 | `AUTO_UPDATE` | - | the device restarts on success; a return is the failure status in `a1` |
| 0x48 | `SC_FLASH_ERASE` | `a0` flash offset, `a1` length == one erase block (64 KB), both block aligned | erases that block of the System Controller's SPI flash; `MPFS_SBI_SC_FLASH=1` builds only |
| 0x49 | `SC_FLASH_WRITE` | `a0` flash offset, `a1` buffer, `a2` length <= 4096 | programs the bytes; `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4A | `SC_FLASH_READ` | `a0` flash offset, `a1` buffer, `a2` length <= 4096 | reads the bytes (any offset, the directory included); `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4B | `SC_FLASH_SET_IMAGE` | `a0` directory index 1 (upgrade) or 2 (blank), `a1` image offset | rewrites the SPI directory block with that entry changed (entry 1 also zeroes entry 2); `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4F | `SC_STATUS` | - | `SBI_ERR_ALREADY_STARTED` while the started service runs, else `a1` = 16-bit status with mailbox word 1 (the digest-check `DIGESTERR` bits) in the upper half |

`GET_KEY` purposes name a fixed label; the caller never supplies a challenge, so the OS cannot ask the PUF for wolfBoot's own key-encryption key. Purpose 1 is the MRAM log key (`SHA-384("wolfBoot-mram-log-v1" || PUF(SHA-384("wolfBoot-mram-log-v1")[0..15]))[0..31]`); purpose 2 reproduces the earlier user-space `varlog` derivation. The key is device-bound: the same whether the golden or an operational image booted.

The System Controller services (0x43-0x4F) are served only in a build with `MPFS_SCB_SMODE_DENY=1`, where the OS cannot reach the mailbox itself; in an unfenced build they return `SBI_ERR_DENIED` and the OS uses its own mailbox drivers, which the fence would otherwise race. The long-running ones are started inside the ecall and polled through `SC_STATUS`, because an ecall holds its hart in M-mode with interrupts off. The mailbox is one resource for all harts: a call that finds it taken returns `SBI_ERR_FAILED`.

The fence also covers the System Controller's QSPI flash controller, so a fenced OS cannot stage a bitstream the way the mainline `mpfs-auto-update` driver does (through `mtd` on that controller). `MPFS_SBI_SC_FLASH=1` adds the flash calls (0x48-0x4B) for that build: the OS erases and writes the image region directly, reads back what it wants verified, and points the SPI directory's upgrade entry at the image with `SC_FLASH_SET_IMAGE` before `IAP_AUTHENTICATE` / `IAP_PROGRAM` or `AUTO_UPDATE`. The first erase block (the directory and the start of the design-info area) is never erased or written through `SC_FLASH_ERASE` / `SC_FLASH_WRITE`; `SET_IMAGE` reads it, changes the one entry (the golden entry 0 stays what it was) and writes it back, the same read-erase-write the OS driver performs. Each erase call takes one block and each transfer at most 4 KB, so no ecall holds its hart for more than the erase time of one block; the OS side loops. The flash layout the mainline driver uses (directory in the first 1 KB, design info at 1 KB, bitstream slots from 1 MB + 1 KB) is the OS's business; wolfBoot only protects the directory block.

The Linux side is the `wolfboot-sbi` module in `tools/linux/wolfboot-sbi/`: `/dev/wolfboot` (root only) with one ioctl per call, see `wolfboot_sbi.h`. It probes the extension and `GET_VERSION`, so it unloads cleanly under the HSS, and it never touches the mailbox registers, so the mainline `mpfs-sys-controller`, `mpfs-auto-update` and `mpfs-rng` drivers keep working in an unfenced build. A caller of `IAP_PROGRAM` or `AUTO_UPDATE` syncs its filesystems first: the device restarts inside the call.

Build: `MPFS_SBI_VENDOR=1` on the M-mode target (`RISCV_MMODE=1`); the key derivation is SHA-384 whatever the image hash.

## Hardware root of trust: PUF KEK, sNVM keystore, wrapped image key

The PolarFire SoC MPFS250 hardware-root-of-trust features belong to the wolfBoot E51 M-mode boot flow ([M-mode + DDR](#m-mode--ddr-booting-linux-minimal-sbi)). They let wolfBoot anchor key material in the chip's System Controller instead of compiling it into the bootloader image:

1. A System Controller mailbox driver for secure NVM (sNVM), the SRAM-PUF, and the nonce/TRNG service.
2. An sNVM keystore backend that serves the verification public keys from sNVM at boot.
3. A device-unique Key Encryption Key (KEK) derived from the SRAM-PUF, used to wrap/unwrap individual keys.
4. A PUF-wrapped image-encryption key: the AES image key is stored in sNVM wrapped by the PUF KEK and unwrapped at boot, with the bulk image decrypted on the M-mode disk-boot path.

All features are off by default and gated behind build flags, so a normal build is unaffected.

### Trust model

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

#### Production requirement: the keystore pages must not stay writable

The sNVM write service is reachable from anything that can drive the System Controller mailbox, including the Linux `mpfs-sys-controller` driver, so on the development flow above the trust anchor is only as protected as the OS. A production unit closes this in one of two ways, both opt-in at build time:

`MPFS_SNVM_DUMP=1` surveys a page range read-only at boot and prints each page's admin word (whose bit 23 is the ROM flag), whether the page is blank, zeroized or holds data, and a checksum, so a device's sNVM state can be recorded before and after provisioning or a bitstream change without printing key material. A page that is not a plaintext client refuses the read and reports the System Controller status instead, with its admin word: status 2 is an authentication failure, and an admin word of zero there means the page was never configured as a client at all, which is what distinguishes free pages from occupied ones when planning an sNVM layout.

- Deliver the keystore pages as ROM in the bitstream (Libero sNVM client with the keystore image as content and the ROM attribute set) and build with `SNVM_KEYSTORE=1 SNVM_KEYSTORE_REQUIRE_ROM=1`: wolfBoot reads the page admin word of every keystore module and refuses to boot unless its ROM flag is set. The ROM flag is a bitstream property; the sNVM system services can neither set nor clear it, so there is no on-device step that can lock a page by accident. The on-device provisioning writer is for development only and cannot write a ROM page.
- Build with `MPFS_SCB_SMODE_DENY=1`: every hart's PMP fences S-mode off the System Controller control and mailbox window (`0x37020000`-`0x37020FFF`) before the hand-off, and the DTB fixup disables the `mailbox@37020800`, `spi@37020100` (System Controller QSPI) and `syscontroller` nodes so the OS does not probe hardware it cannot reach. A supervisor access into the window is otherwise redirected to the kernel's trap vector as an access fault (this core cannot delegate access faults, so wolfBoot's SBI forwards them). The sNVM pages stay writable by wolfBoot (M-mode) only. This removes the OS-side System Controller services (device serial number, bitstream services, sNVM access, the zeroize procedure) and anything the OS image builds on them, so any unit that reads a key or the serial number through the mailbox must be dropped from the image or moved to the [SBI vendor extension](#sbi-vendor-extension-for-the-os).

### 1. System Controller mailbox services

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

### 2. sNVM public-key keystore (`SNVM_KEYSTORE`)

`hal/mpfs250_snvm.c` + `hal/mpfs250_snvm.h` implement the five-function keystore API (`keystore_num_pubkeys`, `keystore_get_buffer`, `keystore_get_size`, `keystore_get_mask`, `keystore_get_key_type`) by reading the trust anchor from sNVM. The on-sNVM layout is the same family as the OTP keystore (`src/flash_otp_keystore.c`): a `WOLFBOOT`-magic header followed by packed `struct keystore_slot` entries, read through a linear reader that spans consecutive sNVM modules from `SNVM_KEYSTORE_MODULE` (default 200, `SNVM_KEYSTORE_MAX_MODULES` default 1; an ML-DSA-87 key needs 11). For the on-device provisioning writer every module in the range must be left runtime-writable (not ROM) in the Libero sNVM configuration: on the stock Video Kit design only modules 200 and 201 are. A production bitstream delivers the same page contents as ROM instead (see the trust model above).

Build wiring (mirrors `FLASH_OTP_KEYSTORE`):

- `SNVM_KEYSTORE=1` links `hal/mpfs250_snvm.o` in place of the compiled-in `src/keystore.o`, so the running bootloader contains no public keys and serves them from sNVM.
- `SNVM_KEYSTORE_PROVISION=1` keeps the compiled keystore and adds `snvm_keystore_provision()`, a one-time on-device writer that copies the compiled keystore image into sNVM (called from `hal_init`). Run once on a board, then deploy the `SNVM_KEYSTORE=1` build. Both provisioning writers read the page first and skip it when it already holds the content (`snvm: module N already provisioned`), so the extra boot the programmer triggers after flashing, or a second boot of the provisioning image, does not spend another write cycle.

A keystore page that reads back as all zeros is reported as `sNVM keystore zeroized` and halts the boot. That is the state a zeroize procedure leaves behind (the OS overwrites the keystore and encryption-key pages through the System Controller's plaintext sNVM write service), and it is distinguished from a corrupt header, which merely yields no keys, so a deliberately erased unit is identifiable on the console. Recovery is the provisioning build again.

### 3. PUF KEK and AES key-wrap (`SNVM_KEK`)

The KEK and key-wrap live in `hal/mpfs250_snvm.c` as well:

- `mpfs_puf_kek()` derives the 256-bit KEK as `SHA-384(label || PUF(fixed-challenge))` truncated to 32 bytes. SHA-384 is used because it is the build's hash (SHA-256/HMAC/HKDF are compiled out); the PUF response is already a high-entropy device-unique secret, so a labelled hash provides domain separation without pulling in HKDF.
- `snvm_kek_wrap()` / `snvm_kek_unwrap()` wrap/unwrap a key with the KEK using RFC 3394 AES key-wrap (`wc_AesKeyWrap` / `wc_AesKeyUnWrap`).

`SNVM_KEK=1` adds RFC 3394 key-wrap to the AES that an `ENCRYPT=1 ENCRYPT_WITH_AES256=1 CUSTOM_ENCRYPT_KEY=1` build already compiles; the KEK has no other consumer, so the build refuses the flag without that combination. Determinism and the wrap/unwrap round trip are covered by the host unit test `tools/unit-tests/unit-snvm-kek.c`.

The AES key-wrap uses the wolfCrypt software implementation. The Athena F5200 offload (`MPFS_ATHENA=1`) serves SHA-384 and the bulk AES-256-CTR image decrypt through wolfCrypt crypto callbacks; the key-wrap is a few blocks per boot and stays in software.

### 4. PUF-wrapped image-encryption key

`hal/mpfs250_snvm.c` provides `wolfBoot_get_encrypt_key()` (under `CUSTOM_ENCRYPT_KEY`): it reads `[wrapped AES key (40 B)][nonce (16 B)]` from sNVM module `SNVM_ENCKEY_MODULE` (default 201, outside the keystore range), unwraps the key with the PUF KEK, and returns the plaintext key + nonce to the decrypt path. `snvm_enckey_provision()` (under `SNVM_ENCKEY_PROVISION`) wraps a key with the device KEK and writes the blob to sNVM.

#### Disk-image decryption enablement

Image encryption (`EXT_ENCRYPTED`) needs `EXT_FLASH`, `MMU` or `CUSTOM_ENCRYPT_KEY`. The M-mode disk-FIT path is `WOLFBOOT_NO_PARTITIONS` and has neither external flash nor an MMU, so it qualifies through `CUSTOM_ENCRYPT_KEY`: the platform supplies the key and `hal_set_key()`, the partition-resident key store, is compiled out (`src/libwolfboot.c`). The `DISK_ENCRYPT` path in `src/update_disk.c` zeroes its key material with `ForceZero` on every exit.

### Field-recoverable zeroize

`DISK_FAILOVER_ZEROIZE=1` (with `DISK_GOLDEN_SLOT=1`, `SNVM_KEK=1` and the `WOLFBOOT_SNVM_WRITE_APPROVED=1` gate every sNVM writer carries) turns a failover to the golden slot into a zeroize: when both operational slots are given up on, wolfBoot zeroes sNVM page `SNVM_ENCKEY_MODULE` (the PUF-wrapped image key, read back and checked; a page that already reads as zeros is not written again) and the head of both operational slots, then boots the golden image. The keystore page is never touched, so the golden image still verifies; it must be a plaintext signed image, since the key that decrypted the operational images is gone. Nothing derived from the PUF is lost: the MRAM log key is re-derived on every boot. Re-provisioning the image key afterwards is the `SNVM_ENCKEY_PROVISION` service flow; a brownout during the zeroize write can leave the page read-only, in which case the device cannot be re-keyed.

### Build flags summary

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
| `DISK_FAILOVER_ZEROIZE` | Zero the image-key page and the operational slots when falling back to the golden image |
| `SNVM_ENCKEY_INSECURE_TEST_KEY` | Acknowledges provisioning the built-in public test key |
| `SNVM_ENCKEY_PROVISION_EXTERN` | Provision a production key instead: the integrator defines `snvm_enckey_prov_key[32]` and `snvm_enckey_prov_nonce[16]` in another object |

Example runtime encrypted build (compiled keystore for verification, PUF-wrapped key for decryption):

```sh
make CROSS_COMPILE=riscv64-unknown-elf- LIBERO_FPGA_CONFIG_DIR=<fpga_design_config> \
    ENCRYPT=1 ENCRYPT_WITH_AES256=1 CUSTOM_ENCRYPT_KEY=1 SNVM_KEK=1 wolfboot.elf
```

### Provisioning and validation recipe

1. Build with `SNVM_ENCKEY_PROVISION=1` (and/or `SNVM_KEYSTORE_PROVISION=1`) plus `WOLFBOOT_SNVM_WRITE_APPROVED=1` added to the runtime build above, flash, and boot once: `hal_init` writes the PUF-wrapped AES key into sNVM[201] (and/or the keystore into sNVM[200]).
2. Sign + encrypt the FIT with a key file matching the provisioned key (`prov_aes_key` || `prov_aes_nonce` in `hal/mpfs250_snvm.c`, i.e. a 32-byte key followed by a 16-byte IV):

   ```sh
   IMAGE_HEADER_SIZE=512 ./tools/keytools/sign --ecc384 --sha384 \
       --encrypt enc_key.bin --aes256 fitImage wolfboot_signing_private_key.der 1
   ```
   Note: `make keysclean` regenerates the signing key, so re-sign the image after any keysclean or the compiled keystore will not match.
3. Write `fitImage_v1_signed_and_encrypted.bin` to the SD boot partition. With the stock HSS, boot to the `>>` CLI, run `usbdmsc`, and `dd` to the first partition of the exposed disk. Do not move an SD-Wire mux while `usbdmsc` is running: HSS's MMC layer then serves one stale block for every LBA and does not recover until `usbdmsc` is restarted, which makes a populated card read as blank.
4. Flash the runtime build and cold-boot. Expected UART0: `Disk encryption enabled` -> `Decrypting image... done` -> `Firmware Valid.` -> `M->S handoff`, and UART1 reaches the Linux login with `Brought up 1 node, 4 CPUs` and no `failed to come online`.

### Sample validation logs (MPFS250TS Video Kit)

Verified on the kit: the PUF response is identical for a fixed challenge across cold power cycles while the nonce service is random, and the PUF-derived KEK wraps and unwraps a test key to the same bytes on every boot (`tools/unit-tests/unit-snvm-kek.c` covers the KEK logic). A full encrypted boot reaches `Firmware Valid.` by way of `Disk encryption enabled` and `Decrypting image...done`, and Linux reaches its login with `Brought up 1 node, 4 CPUs` and no `failed to come online`.
