# wolfBoot SBOM Generation

wolfBoot can emit a Software Bill of Materials (SBOM) in **CycloneDX 1.6** and
**SPDX 2.3** JSON for every configuration and every build system it supports.
An SBOM is one of the software-transparency artifacts useful towards EU Cyber
Resilience Act (CRA) obligations; it does not by itself make a product CRA
compliant (that is a system- and process-level determination for the
manufacturer).

## One engine, many front ends

There is a single SBOM engine. Every build system feeds it the same two inputs
and gets back the same document:

```
build system  ─┐
                ├─►  tools/scripts/wolfboot-sbom.sh  ─►  wolfSSL gen-sbom  ─►  *.cdx.json + *.spdx.json
extractor     ─┘        (srcs list + build config)
```

* **srcs list** – the source files actually compiled into the image.
* **build config** – the effective `-D` macros, normalized through the *host*
  compiler's `-dM -E`. Because macro capture uses the host compiler (never the
  cross-compiler), the SBOM is reproducible across toolchains: GCC, Clang/LLVM,
  IAR `iccarm`, TI `armcl`, Renesas `ccrx` and Microchip `xc32` all converge to
  the same document for the same configuration.

The pieces:

| File | Role |
| --- | --- |
| `tools/scripts/wolfboot-sbom.sh` | Canonical driver (srcs + config → gen-sbom). |
| `cmake/sbom.cmake` | CMake `sbom` target. |
| `tools/scripts/ide-sbom/iar_sbom.py` | Extracts srcs + defines from an IAR `.ewp`. |
| `tools/scripts/ide-sbom/compdb_sbom.py` | Extracts srcs + defines from a `compile_commands.json`. |
| `tools/scripts/ide-sbom/zephyr_sbom.py` | Extracts the Zephyr module sources from `zephyr/CMakeLists.txt`. |
| `tools/scripts/ide-sbom/route_through_sbom.sh` | Stages a config and runs `make sbom` for IDE targets that build through the Makefile. |
| `tools/scripts/ide-sbom/validate_sbom.py` | Structural sanity check used by CI. |
| `make sbom-hal` | Standalone SBOM for the HAL of a given target. |

## Prerequisites

* `python3`
* A host C compiler. The default is `cc`. To use a different compiler, set
  `HOSTCC=...`.
* `gen-sbom`. This tool is part of wolfSSL. The build uses the copy in the
  `lib/wolfssl` submodule. The pinned wolfSSL revision does not include
  `gen-sbom` yet. Until a wolfSSL update adds it, give the path to a copy. Use
  `GEN_SBOM=/path/to/wolfssl/scripts/gen-sbom` for Make and route-through. Use
  `-DGEN_SBOM=...` for CMake. Use `--gen-sbom ...` for the Python tools.

```sh
git submodule update --init lib/wolfssl
```

## Limitations

Obey these limitations when you make an SBOM.

- `gen-sbom` is necessary. If the build does not find the tool, it stops and
  shows an error. Give the path with `GEN_SBOM` or the equivalent option. A
  wolfSSL submodule update removes this step.
- A vendor SDK build lists only the source files that are on disk. If the SDK
  is not in the source tree, the SBOM does not include the SDK files. The SBOM
  always includes the wolfBoot, wolfCrypt, and HAL files.
- The driver is a POSIX shell script. On Windows, run the tools in a POSIX
  shell. Use WSL, MSYS, or Git Bash. As an alternative, use the compilation
  database tool (`compdb_sbom.py`).

## Coverage: the 11 build methods

wolfBoot is built in many ways. Each maps to one of four SBOM routes:

| # | Build method | SBOM route |
| --- | --- | --- |
| 1 | Plain Make / `arch.mk` | Make target |
| 2 | Make + MCUXpresso SDK | Make target |
| 3 | Make + STM32Cube | Make target |
| 4 | Make + PSoC6 / Freedom-E / Vorago SDKs | Make target |
| 5 | CMake (presets / dot-config) | CMake target |
| 6 | Pico SDK (RP2350) | compdb extractor |
| 7 | IAR Embedded Workbench | IAR extractor |
| 8 | TI Code Composer Studio (Hercules TMS570) | route-through Make (or compdb) |
| 9 | Microchip MPLAB X (SAME51 / PIC32) | route-through Make (or compdb) |
| 10 | Renesas e² studio (RX / RA / RZ) | route-through Make (or compdb) |
| 11 | Xilinx SDK / Vitis (Zynq / ZynqMP) | route-through Make (or compdb) |

---

## Route 1 — Make (methods 1–4)

The Makefile `sbom` target is the primary entry point. It works for the plain
`arch.mk` build and for every build that is really the Makefile with a vendor
SDK bolted on via source/include paths (MCUXpresso, STM32Cube, PSoC6,
Freedom-E-SDK, Vorago).

```sh
make sbom TARGET=<target> SIGN=<alg> HASH=<alg>
```

`TARGET`, `SIGN`, and `HASH` come from the same place as a normal build (command
line, environment, or `.config`) and must match the configuration you ship —
the source set and artifact hash are configuration-specific.

Useful overrides: `HOSTCC`, `GEN_SBOM`, `CRA_PYTHON`.

wolfcrypt sources are compiled directly into the wolfBoot image, so they are
listed as wolfBoot's own sources rather than as a separate component.

## Route 2 — CMake (methods 5–6)

The CMake build exposes an `sbom` target (`cmake/sbom.cmake`) that collects the
compiled source set from the wolfBoot library targets and the effective
configuration from `WOLFBOOT_DEFS` / `USER_SETTINGS`, then calls the shared
driver — producing a document byte-comparable with the Make path.

```sh
cmake -S . -B build-sim -DWOLFBOOT_TARGET=sim ...   # your normal configure
cmake --build build-sim --target sbom
```

Outputs land in the build directory. Overrides: `-DGEN_SBOM=...`, `-DHOSTCC=...`,
`-DSBOM_PYTHON=...`.

> The driver is a POSIX shell script; on Windows run this target from WSL / MSYS
> / Git-Bash, or use the Make path.

### Pico SDK (method 6)

The Pico SDK build under `IDE/pico-sdk/rp2350/` is a standalone CMake project
that pulls in the Pico SDK, so it does not include `cmake/sbom.cmake`. Generate
its SBOM from the compilation database (see Route 4):

```sh
cd IDE/pico-sdk/rp2350/wolfboot
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...   # your normal configure
python3 <wolfboot>/tools/scripts/ide-sbom/compdb_sbom.py build/compile_commands.json
```

## Route 3 — IAR extractor (method 7)

IAR builds happen entirely inside Embedded Workbench and never touch the
Makefile or CMake, so the compiled source set and preprocessor configuration
live in the `.ewp` project file. The extractor reads them out and feeds the
shared driver:

```sh
tools/scripts/ide-sbom/iar_sbom.py IDE/IAR/wolfboot.ewp
```

Options: `--config <name>` (defaults to the configuration with the most defines,
i.e. the real build config), `--gen-sbom`, `--version`, `--cdx-out`,
`--spdx-out`, and `--print-only` to inspect the extracted sources/defines
without generating.

Sources listed in the `.ewp` that are generated at build time (e.g.
`keystore.c`) and are not on disk are reported and excluded, matching the Make
path's `$(wildcard)` behavior.

## Route 4 — route-through & compilation database (methods 8–11)

### Route-through Make (preferred where a `.config` exists)

TI CCS (Hercules TMS570), Microchip MPLAB X (SAME51 / PIC32), Renesas RX, and
Xilinx Zynq / ZynqMP all have wolfBoot `config/examples/*.config` targets and
build through the Makefile on the command line. For these, the SBOM is produced
by the same `make sbom` engine; `route_through_sbom.sh` makes that explicit by
staging the config and forwarding the vendor make variables:

```sh
# TI Hercules (CCS toolchain, built from the command line):
tools/scripts/ide-sbom/route_through_sbom.sh \
    --config config/examples/ti-tms570lc435.config \
    CCS_ROOT=/opt/ti/ccs/tools/compiler/ti-cgt-arm_20.2.7.LTS \
    F021_DIR=/opt/ti/Hercules/F021_Flash_API/02.01.01

# Xilinx ZynqMP:
tools/scripts/ide-sbom/route_through_sbom.sh --config config/examples/zynqmp.config

# Renesas RX72N:
tools/scripts/ide-sbom/route_through_sbom.sh --config config/examples/renesas-rx72n.config

# Microchip SAME51:
tools/scripts/ide-sbom/route_through_sbom.sh --config config/examples/same51.config
```

### Compilation-database extractor (any IDE / toolchain)

When a target is built *strictly inside* an IDE (e.g. Renesas RA/RZ e² studio,
an MPLAB X GUI build, or a Vitis build) and you want an SBOM of exactly what the
IDE compiled, capture a Clang compilation database and use the universal
extractor. This is toolchain- and IDE-independent:

```sh
# CMake emits it natively:
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...

# Make-based IDE projects (MPLAB X nbproject, CCS, Vitis) via Bear:
bear -- make            # produces compile_commands.json

python3 tools/scripts/ide-sbom/compdb_sbom.py compile_commands.json \
    --exclude 'test-app/'      # optional: drop test sources
```

The extractor takes the exact file list and `-D` set the compiler saw, so the
SBOM reflects the real IDE build regardless of how sources and defines were
configured in the GUI.

## Per-artifact SBOMs

The routes above describe the wolfBoot **bootloader** image. wolfBoot also has
sub-components you may want to inventory separately. Each gets its own SBOM file
whose component is named `wolfboot-<artifact>`, so nothing collides.

### Per-HAL SBOM

The hardware abstraction layer for a target (`hal/hal.c`, `hal/<target>.c`, and
any target flash / UART / board drivers) is already included in the full
bootloader SBOM. To emit it as a **standalone** component — e.g. to track the
board-support portion of the supply chain on its own — use:

```sh
make sbom-hal TARGET=<target> SIGN=<alg>
```

This reuses the real build `CFLAGS`, so the captured configuration matches the
bootloader build. Output: `wolfboot-hal-<target>-<version>.{cdx,spdx}.json`.
Run it once per target.

### Zephyr TEE / PSA module

The `zephyr/` directory is **not** the bootloader — it is a Zephyr module that
compiles a small TEE/PSA non-secure client shim into a Zephyr application
(`zephyr_library_sources(...)`, gated on `CONFIG_WOLFBOOT_TEE`). It is built by
Zephyr/west, so neither the Make nor the CMake SBOM target sees it. The
extractor reads the module's source list straight from `zephyr/CMakeLists.txt`
(staying in sync automatically):

```sh
tools/scripts/ide-sbom/zephyr_sbom.py
```

Output: `wolfboot-zephyr-<version>.{cdx,spdx}.json`.

Because the module's configuration is Kconfig-driven (`CONFIG_*` symbols) rather
than a `-D` macro set, this is a **source-inventory** SBOM by default (no
build-config macros; the driver's `--source-only` mode). If you have a real
Zephyr build and want the exact compiled configuration, generate the SBOM from
that build's compilation database instead:

```sh
west build ... -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 tools/scripts/ide-sbom/compdb_sbom.py build/compile_commands.json \
    --include 'zephyr/src/' --name wolfboot-zephyr
```

## Output

Every route writes, into the working/build directory:

* `wolfboot-<version>.cdx.json` — CycloneDX 1.6
* `wolfboot-<version>.spdx.json` — SPDX 2.3

`<version>` is read from `include/wolfboot/version.h`. These are ignored by
`.gitignore`.

You can sanity-check any output:

```sh
python3 tools/scripts/ide-sbom/validate_sbom.py wolfboot-*.cdx.json wolfboot-*.spdx.json
```

## Continuous integration

`.github/workflows/test-sbom.yml` is an SBOM canary that runs the Make, CMake,
IAR, and compilation-database routes on every push/PR and validates each output,
so a change to a build system, the shared driver, or an extractor cannot
silently break SBOM generation. The generated SBOMs are uploaded as build
artifacts.

## Reproducibility

`gen-sbom` supports deterministic output (e.g. `SOURCE_DATE_EPOCH` and stable
UUIDs). Combined with host-compiler macro capture, the same wolfBoot
configuration yields the same SBOM regardless of the build system or
cross-toolchain used to produce the firmware.

The driver also scrubs absolute host paths from the captured macros. For
example, `arch.mk` passes `-DPICO_SDK_PATH=$(PICO_SDK_PATH)`. Without the scrub,
the local path enters the SBOM. This makes the SBOM machine-specific and leaks
the local file system. The driver redacts the path but keeps the macro name, so
the configuration record stays complete. Use `--no-scrub` for debug only.
