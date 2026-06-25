# wolfHSM TrustZone Demo for STM32H5

This directory is the canonical entry point for building, running, and
flashing the first open-source wolfHSM TrustZone port. The port itself
lives in wolfBoot; the generic ARMv8-M NSC bridge transport that the
port consumes lives in wolfHSM at `port/armv8m-tz/`.

The demo runs a wolfHSM client in the non-secure world and the wolfHSM
server in the secure world, both on the same Cortex-M33 in an
STM32H563. There is no second core. The client and server context
switch through ARMv8-M TrustZone using a single
`cmse_nonsecure_entry` veneer.

The reference layout is modeled on the wolfHSM SR6 dual-core port in
the wolfHSM-private repository. The folder shape (top-level Makefile,
`load.sh`, `demo-app/`, README) matches that proven pattern. The
contents are simplified for the single-app TrustZone model:

- one MCU, not two
- one application image, not separate client and server applications
- the security boundary is the TrustZone split inside the same core,
  not a shared-memory channel between two cores


## Overview

- **Target MCU:** STM32H563ZI (Arm Cortex-M33 with TrustZone)
- **Target board:** NUCLEO-H563ZI
- **Secure world:** wolfBoot + linked-in wolfHSM server, including
  flash-backed NVM (key cache, keystore). Source:
  `src/wolfhsm_callable.c`, `src/wolfhsm_flash_hal.c`.
- **Non-secure world:** test application that initialises a wolfHSM
  client over the ARMv8-M NSC transport and exercises the server.
  Source: `test-app/app_stm32h5.c`, `test-app/wcs/wolfhsm_test.c`,
  `test-app/wcs/wolfhsm_stub.c`.
- **NSC veneer:** `wcs_wolfhsm_transmit(cmd, cmdSz, rsp, *rspSz)`. The
  wolfHSM client invokes this through the transport callbacks in
  `lib/wolfHSM/port/armv8m-tz/wh_transport_nsc.c`. The
  `cmse_nonsecure_entry`-tagged implementation lives in
  `src/wolfhsm_callable.c`.
- **NVM backend:** persistent flash storage carved out of the H5
  internal flash via the `whFlashCb` adapter in
  `src/wolfhsm_flash_hal.c`.


## Running wolfBoot + wolfHSM on m33mu (Cortex-M33)

This is the section to read if you just want to copy-paste commands
and watch the demo run. It assumes a fresh Linux dev box.

### One-time setup

```bash
# 1. Toolchain
sudo apt install -y gcc-arm-none-eabi git make cmake build-essential

# 2. Rust (m33mu's Rust simulator plugins need cargo >= 1.85 for edition2024)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain stable --profile minimal
. "$HOME/.cargo/env"

# 3. Build m33mu (the Cortex-M33 emulator) and put it on PATH
cd ~
git clone https://github.com/danielinux/m33mu
cd m33mu
cmake -S . -B build -DM33MU_ENABLE_WOLFSSL=OFF
cmake --build build --target m33mu -j
mkdir -p ~/.local/bin
ln -sf "$PWD/build/m33mu" ~/.local/bin/m33mu

# 4. Verify m33mu works
env -u LD_LIBRARY_PATH m33mu --cpu list
# expected: stm32h563 / stm32h533 / stm32u585 / stm32l552 / ...
```

If `env -u LD_LIBRARY_PATH m33mu ...` is awkward, add `unset
LD_LIBRARY_PATH` to your shell rc — it works around Warp's bundled
older libtinfo if you use Warp.

### Build the demo and run it

```bash
# 5. From the wolfBoot checkout containing this PR
cd path/to/wolfBoot/port/stmicro/stm32h5-tz-wolfhsm
make

# 6. Stage a writable copy of the binaries (m33mu --persist rewrites
#    them in-place, so do not run it against the pristine outputs)
mkdir -p /tmp/m33mu-wolfhsm-persist
cp out/wolfboot.bin out/image_v1_signed.bin /tmp/m33mu-wolfhsm-persist/
cd /tmp/m33mu-wolfhsm-persist

# 7. Run wolfBoot booting wolfHSM running on m33mu, with live UART
#    output on your terminal
env -u LD_LIBRARY_PATH m33mu \
    wolfboot.bin image_v1_signed.bin:0x60000 \
    --persist --uart-stdout --timeout 600 --expect-bkpt 0x7d
```

That last command is the live run. You will see, in order:
wolfBoot's secure-image boot banner; the bootloader's keystore
hexdump; the NS-side wolfHSM demo opening the NSC bridge
(`wolfHSM CommInit ok (client=1 server=56)`); the
`whTest_ClientServerClientConfig` suite (echo, NVM add/list/destroy,
NVM flags enforcement); then the `whTest_CryptoClientConfig` suite
(RNG, key cache + commit + cross-cache eviction, non-exportable
keystore, key-usage policy enforcement for AES/ECDSA/ECDH/HKDF,
AES-CTR/CBC/GCM, RSA, ECC ephemeral sign/verify and ECDH); then a
final `[BKPT] imm=0x7d` / `[EXPECT BKPT] Success` and m33mu exits 0.

To watch the run in a separate terminal as it streams, run from one
terminal:

```bash
env -u LD_LIBRARY_PATH m33mu \
    wolfboot.bin image_v1_signed.bin:0x60000 \
    --persist --uart-stdout --timeout 600 --expect-bkpt 0x7d \
    2>&1 | tee /tmp/m33mu-wolfhsm.log
```

And from another:

```bash
tail -f /tmp/m33mu-wolfhsm.log
```


## Prerequisites

1. **Toolchain:** `arm-none-eabi-gcc` (any recent GNU Arm Embedded
   toolchain with Cortex-M33 support). Verify with
   `arm-none-eabi-gcc --version`.
2. **wolfBoot keytools:** these build automatically as part of the
   normal `make` flow; no separate install step.
3. **STM32CubeProgrammer / `STM32_Programmer_CLI`** for flashing the
   NUCLEO-H563ZI. Available at https://www.st.com/en/development-tools/stm32cubeprog.html
4. **Serial monitor:** `picocom` (preferred) or `screen`, for reading
   UART output from the board.
5. **Emulator (optional, for CI parity):** `m33mu`, the wolfBoot
   Cortex-M33 emulator. Two ways to get it:
   - **Build from source** (preferred for ongoing local dev):
     ```bash
     git clone https://github.com/danielinux/m33mu ~/m33mu
     cd ~/m33mu
     cmake -S . -B build
     cmake --build build --target m33mu
     ln -sf "$PWD/build/m33mu" ~/.local/bin/m33mu
     ```
     The build picks up `cargo` to compile its Rust-based
     secure-element simulators (ATECC608A, SE050, STSAFE-A120,
     TROPIC01). Those simulators are *not* used by this wolfHSM
     demo, but the build needs `cargo >= 1.85` (for `edition2024`)
     to resolve a few `mm_se050_*` symbols `main.c` references
     unconditionally. Install with `rustup` if your distro ships an
     older toolchain.
   - **Use the CI container** (no build required):
     `ghcr.io/wolfssl/wolfboot-ci-m33mu:latest`. See the bottom of
     the **Running on the m33mu emulator** section below.
6. **Submodules:** wolfBoot consumes wolfHSM as a submodule at
   `lib/wolfHSM`. From the wolfBoot checkout:
   ```bash
   git submodule update --init --recursive
   ```


## Quick Start

From the wolfBoot checkout:

```bash
cd port/stmicro/stm32h5-tz-wolfhsm
make                # build wolfboot.bin + signed test app, stage in ./out
./load.sh           # flash to NUCLEO-H563ZI and open a serial console
```

That is the entire workflow. The Makefile orchestrates everything
needed (config staging, secure-image build, signed test-app build,
binary staging). `load.sh` flashes both binaries to a real board and
drops you into a serial monitor so you can watch the test run.

To run inside the wolfBoot emulator instead of on hardware:

```bash
make emu            # build (if needed) and run wolfBoot's m33mu harness
```


## Make targets

| Target          | Purpose                                                                 |
|-----------------|-------------------------------------------------------------------------|
| `make`          | Build wolfboot + signed test app, stage in `./out/` (default).          |
| `make build`    | Same as the above but without re-staging if outputs are already there.  |
| `make stage`    | Copy fresh `wolfboot.bin` and `image_v1_signed.bin` into `./out/`.      |
| `make flash`    | Build (if needed) and invoke `./load.sh`.                               |
| `make emu`      | Build (if needed) and run wolfBoot's m33mu harness for `TARGET=stm32h563`. |
| `make clean`    | Drop staged artifacts in `./out/`.                                      |
| `make distclean`| Also clean the wolfBoot tree and remove the staged `.config`.           |
| `make help`     | Print this list.                                                        |


## What the build produces

After `make`:

```
out/
  wolfboot.bin            # ~383 KB. Secure-world wolfBoot + wolfHSM server.
  image_v1_signed.bin     # ~200 KB. Non-secure-world test app, signed.
  manifest.env            # WOLFBOOT_BIN, TEST_APP_BIN, BOOT_ADDR for load.sh.
```

Flash mapping (from `config/examples/stm32h5-tz-wolfhsm.config`):

| Address      | Region                                | Notes                           |
|--------------|---------------------------------------|---------------------------------|
| `0x08000000` | wolfBoot secure image                 | `wolfboot.bin` goes here.       |
| `0x08060000` | wolfBoot non-secure test-app slot     | `image_v1_signed.bin` goes here.|
| `0x0C040000` | wolfHSM key vault                     | 112 KB (`WOLFBOOT_KEYVAULT_*`). |
| `0x0C05C000` | NSC veneer region                     | 16 KB.                          |
| `0x0C100000` | Update partition                      |                                 |
| `0x0C1A0000` | Swap partition                        |                                 |


## Expected serial output

A successful first boot prints something close to the following on
USART3 (the NUCLEO-H563ZI's onboard ST-LINK V3E VCP, exposed at
`/dev/ttyACM0` on Linux):

```
========================
wolfBoot - STM32H5 (TrustZone)
========================
wolfHSM CommInit ok (client=1 server=0)
wolfHSM RNG ok: 7f 91 22 e5 ...
wolfHSM SHA256 ok
wolfHSM AES ok
wolfHSM first boot path, committing key to NVM
wolfHSM NSC tests passed
```

The application then halts via `bkpt #0x7d` (first-boot pass). On a
second boot from the same flash state, the persisted key is restored
instead of created and the application halts via `bkpt #0x7f`
(second-boot pass). A failure halts via `bkpt #0x7e`.

The strings above are also the grep targets the CI verifier uses; see
the **Continuous Integration** section.


## Build details

Under the hood, `make` runs:

```bash
cp ../../../config/examples/stm32h5-tz-wolfhsm.config ../../../.config
cd ../../..
make wolfboot.bin                          # secure image
make test-app/image_v1_signed.bin          # non-secure image, signed
```

Then it copies the two binaries into `./out/` and writes a
`manifest.env` describing the boot address.

The build itself is wolfBoot's normal flow with `WOLFCRYPT_TZ=1` and
`WOLFCRYPT_TZ_WOLFHSM=1` set by the staged config. The relevant
ingredients are:

- `hal/stm32h5.c` plus `hal/stm32_tz.c` for the H5 HAL and TrustZone
  setup.
- `src/wolfhsm_callable.c` for the `cmse_nonsecure_entry` veneer
  (`wcs_wolfhsm_transmit`) and the secure-side server bring-up.
- `src/wolfhsm_flash_hal.c` for the `whFlashCb` adapter that backs
  wolfHSM NVM onto STM32H5 internal flash.
- `lib/wolfHSM/port/armv8m-tz/wh_transport_nsc.{c,h}` for the
  generic ARMv8-M NSC bridge transport (linked into both the secure
  and the non-secure binaries).
- `test-app/app_stm32h5.c` for the non-secure entry point; it calls
  `cmd_wolfhsm_test()` from `test-app/wcs/wolfhsm_test.c`.


## Flashing to NUCLEO-H563ZI

```bash
./load.sh
```

By default `load.sh` does the following:

1. Sanity-checks the staged binaries (`out/wolfboot.bin`,
   `out/image_v1_signed.bin`) and the manifest.
2. Mass-erases the chip with `STM32_Programmer_CLI`, then programs
   `wolfboot.bin` at `0x08000000`. Mass-erase is important because
   the same flash range is shared with other configs (PKCS11, PSA,
   fwTPM); a stale partition can mask wolfHSM-side failures.
3. Programs `image_v1_signed.bin` at `0x08060000`.
4. Issues a hardware reset.
5. Opens `picocom` on `/dev/ttyACM0` at 115200-8N1, falling back to
   `screen` if `picocom` is not installed.

Override knobs (all via environment variables):

| Variable                | Default                          | Purpose                                   |
|-------------------------|----------------------------------|-------------------------------------------|
| `STM32_PROGRAMMER_CLI`  | `STM32_Programmer_CLI` on PATH   | Path to STM32CubeProgrammer CLI.          |
| `SERIAL_PORT`           | `/dev/ttyACM0`                   | NUCLEO VCP. On macOS, `/dev/tty.usbmodem*`.|
| `SERIAL_BAUD`           | `115200`                         | USART3 baud.                              |
| `OPEN_SERIAL`           | `1`                              | Set `0` to skip the serial monitor.       |

Example: flash without opening a terminal afterwards:

```bash
OPEN_SERIAL=0 ./load.sh
```


## Running on the m33mu emulator

`m33mu` is the Cortex-M33 emulator wolfBoot CI uses. It needs to be
on `PATH`. The wolfBoot CI image
`ghcr.io/wolfssl/wolfboot-ci-m33mu:latest` is what the CI runner
itself is built from; inside that image `m33mu` is a normal native
binary. On a developer machine without m33mu installed, you can drop
into a one-shot container to get the same binary (see the bottom of
this section).

The simplest local run:

```bash
make emu
```

That builds (if needed), then invokes
`test-app/emu-test-apps/test.sh` against the staged binaries. The
script is what the wolfBoot CI lane calls; it produces the same UART
output and BKPT contract.

### How `--persist` writes back

`m33mu --persist` writes the emulated flash bank back to the input
`.bin` files on exit. For wolfHSM that matters because the keyvault
(`0x0C040000`) only carries the committed key forward across reboots
if the same flash image is re-loaded next time. The proof is in the
CI log: the first boot reports `wolfboot.bin: 383056 bytes`, the
second boot reports `wolfboot.bin: 2097152 bytes` against the same
filename. The file grew to fill the bank because that is what
`--persist` writes out.

Two consequences worth knowing:

- Run `--persist` against a *copy* of the build artifacts, not the
  pristine outputs. After one persist run the .bin files are no
  longer re-flashable images.
- The flash state lives inside those .bin files. The only sidecar
  m33mu writes is `stm32h563_OTP.bin` (1 KB of OTP).

`make emu` handles the staging for you. For a manual two-boot test:

```bash
mkdir -p /tmp/m33mu-wolfhsm-persist
cp out/wolfboot.bin out/image_v1_signed.bin /tmp/m33mu-wolfhsm-persist/
cd /tmp/m33mu-wolfhsm-persist

# First boot: expect BKPT 0x7d (commits key to NVM)
m33mu wolfboot.bin image_v1_signed.bin:0x60000 \
    --persist --uart-stdout --timeout 120 --expect-bkpt 0x7d

# Second boot: expect BKPT 0x7f (key restored)
m33mu wolfboot.bin image_v1_signed.bin:0x60000 \
    --persist --uart-stdout --timeout 120 --expect-bkpt 0x7f
```

If the second boot prints `wolfHSM first boot path, committing key
to NVM` instead of `wolfHSM second boot path, restored persisted
key`, the .bin files in the persist directory did not survive the
first run. Check that they are writable and grew to 2 MB after the
first boot.

### Troubleshooting native m33mu

If `m33mu` exits immediately with `symbol lookup error: ... libncursesw.so.6:
undefined symbol: _nc_safe_fopen`, your shell has an `LD_LIBRARY_PATH` set
to an older ncurses install (Warp's bundled tmux does this). Clear it:

```bash
unset LD_LIBRARY_PATH
# or for a single invocation:
env -u LD_LIBRARY_PATH m33mu ...
```

### Running on a host without m33mu installed

If `m33mu` is not on `PATH`, run it from the CI image directly:

```bash
docker run --rm -v "$PWD":/persist -w /persist \
    ghcr.io/wolfssl/wolfboot-ci-m33mu:latest \
    m33mu wolfboot.bin image_v1_signed.bin:0x60000 \
    --persist --uart-stdout --timeout 120 --expect-bkpt 0x7d
```

This is identical to what the CI runs, just from a one-shot
container. Output and persistence behaviour are the same as a native
install.


## Continuous Integration

The wolfBoot workflow at `.github/workflows/trustzone-emulator-tests.yml`
runs this demo on every PR. The wolfHSM lane:

1. Builds via this port directory (`cd port/stmicro/stm32h5-tz-wolfhsm
   && make`).
2. Runs the m33mu emulator with `--persist` twice (first boot,
   second boot).
3. Verifies the expected log strings appear in the captured UART
   output and that the right BKPT immediate value fired:

   ```text
   First boot grep targets (must all appear):
     wolfHSM CommInit ok
     wolfHSM RNG ok:
     wolfHSM SHA256 ok
     wolfHSM AES ok
     wolfHSM first boot path, committing key to NVM
     wolfHSM NSC tests passed
     [BKPT] imm=0x7d
     [EXPECT BKPT] Success

   Second boot grep targets:
     wolfHSM second boot path, restored persisted key
     wolfHSM NSC tests passed
     [BKPT] imm=0x7f
     [EXPECT BKPT] Success
   ```

If any of those strings is missing, the CI step fails with a clear
pointer to which boot path went wrong.


## Reference: minimal non-secure integration

`demo-app/main.c` is a reference implementation of the smallest
non-secure entry point a wolfHSM ARMv8-M TZ client needs. It is not
the file wolfBoot links by default (that role is filled by
`test-app/app_stm32h5.c`), but it documents the canonical pattern in
one short source file. An integrator porting the wolfHSM ARMv8-M NSC
transport to a different ARMv8-M part can read `demo-app/main.c` and
the comments at the top to see exactly what the client side has to do:

1. Allocate a static `whTransportNscClientContext`.
2. Fill in `whCommClientConfig` with the NSC transport callbacks
   (`WH_TRANSPORT_NSC_CLIENT_CB` from
   `lib/wolfHSM/port/armv8m-tz/wh_transport_nsc.h`).
3. Wrap that in a `whClientConfig`.
4. Call `whTest_ClientConfig(&clientCfg)` (from wolfHSM's test
   sources) for full client-suite coverage, or call individual
   `whTest_*ClientConfig` variants for narrower coverage.
5. Signal pass/fail via `bkpt #0x7d` / `bkpt #0x7e` to match the m33mu
   CI contract.


## Troubleshooting

### Build fails with "sign: not found"

The wolfBoot sign step resolves `./tools/keytools/sign` against the
caller's working directory. Always invoke this Makefile from the port
directory itself (`cd port/stmicro/stm32h5-tz-wolfhsm && make`). The
Makefile already `cd`s into the wolfBoot root before invoking the
top-level build to keep relative paths correct.

If you reproduced the failure outside this Makefile, run the build
from the wolfBoot root directly:

```bash
cd /path/to/wolfBoot
cp config/examples/stm32h5-tz-wolfhsm.config .config
make wolfboot.bin
make test-app/image_v1_signed.bin
```

### `STM32_Programmer_CLI` not found

Either install STM32CubeProgrammer or point `load.sh` at the binary:

```bash
STM32_PROGRAMMER_CLI=/path/to/STM32_Programmer_CLI ./load.sh
```

On macOS the binary is typically at
`/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI`.

### Board never opens `/dev/ttyACM0`

Confirm the NUCLEO is connected and that udev sees the ST-LINK V3E:

```bash
ls /dev/ttyACM* /dev/serial/by-id/ 2>/dev/null
dmesg | tail | grep -i stlink
```

On Linux you may need to be in the `dialout` (Debian/Ubuntu) or
`uucp` (Arch) group to read the serial port without sudo. On macOS,
look for `/dev/tty.usbmodem*` and pass it through:

```bash
SERIAL_PORT=/dev/tty.usbmodemXXXX ./load.sh
```

### TrustZone not enabled on the part

Some NUCLEO-H563ZI boards ship with TrustZone disabled in option
bytes (`TZEN` bit). If wolfBoot panics during secure init, enable
TrustZone with STM32CubeProgrammer (Option Bytes pane) before
re-flashing. Reference: ST AN5347.

### Second boot fails with "first boot path" output

The emulator (`--persist`) or board carried stale state from a
previous incompatible build (PKCS11 / PSA / fwTPM). Either:

- For emulator: delete the persistence directory and re-run.
  ```bash
  rm -rf /tmp/m33mu-wolfhsm-persist && mkdir /tmp/m33mu-wolfhsm-persist
  ```
- For hardware: re-run `./load.sh`, which mass-erases before
  programming.


## Layout of this directory

```
port/stmicro/stm32h5-tz-wolfhsm/
  README.md                    # this file
  Makefile                     # top-level convenience wrapper
  load.sh                      # flash + serial console driver
  demo-app/
    main.c                     # reference NS-side integration pattern
  out/                         # build outputs (created by `make`)
    wolfboot.bin
    image_v1_signed.bin
    manifest.env
```


## Relationship to wolfHSM

The ARMv8-M NSC bridge transport this demo consumes lives in wolfHSM
itself:

```
wolfHSM/port/armv8m-tz/wh_transport_nsc.{c,h}
```

That transport is generic across any ARMv8-M part. wolfBoot pulls it
in through the `lib/wolfHSM` submodule and compiles it into both the
secure image (server side) and the non-secure image (client side).
The veneer `wcs_wolfhsm_transmit()` that the transport calls is the
one platform-specific piece, and it lives in this repository at
`src/wolfhsm_callable.c`.

If you are porting to a different ARMv8-M part:

1. Provide your own `wcs_wolfhsm_transmit()` implementation as a
   `cmse_nonsecure_entry` function in the secure image.
2. Bring up the secure-side server using the same pattern wolfBoot
   uses in `src/wolfhsm_callable.c`.
3. Drop the wolfHSM NSC client transport into your non-secure-world
   application and call `whTest_ClientConfig()` (or your real
   workload) over it.

The `demo-app/main.c` in this directory is a deliberate one-file
reference for steps 2 and 3.
