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
                ├─►  tools/sbom/sbom-driver  ─►  tools/sbom/gen-sbom  ─►  *.cdx.json + *.spdx.json
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
| `tools/sbom/sbom-driver` | Vendored wolfGlass driver (srcs + config → gen-sbom). |
| `tools/sbom/gen-sbom` | Vendored SBOM generator. |
| `cmake/sbom.cmake` | wolfBoot wrapper around the vendored CMake helper. |
| `tools/sbom/frontends/iar_sbom.py` | Extracts srcs + defines from an IAR `.ewp`. |
| `tools/sbom/frontends/compdb_sbom.py` | Extracts srcs + defines from a `compile_commands.json`. |
| `tools/sbom/frontends/zephyr_sbom.py` | Extracts the Zephyr module sources from `zephyr/CMakeLists.txt`. |
| `tools/scripts/ide-sbom/route_through_sbom.sh` | wolfBoot-specific staging helper for IDE targets that build through the Makefile. It intentionally stays outside the vendored tree because it encodes wolfBoot config paths and Makefile entry points. |
| `tools/sbom/validate_sbom.py` | Structural sanity check used by CI. |
| `make sbom-hal` | Standalone SBOM for the HAL of a given target. |

## Prerequisites

* `python3`
* A host C compiler. The default is `cc`. To use a different compiler, set
  `HOSTCC=...`.
* The vendored wolfGlass SBOM set under `tools/sbom/`.

## Limitations

Obey these limitations when you make an SBOM.

- `gen-sbom` is necessary. wolfBoot vendors it under `tools/sbom/`. If you
  override it with `SBOM_GEN`, `-DGEN_SBOM=...`, or `--gen-sbom ...`, the
  replacement copy must support the same flags as the vendored one.
- A vendor SDK build lists only the source files that are on disk. If the SDK
  is not in the source tree, the SBOM does not include the SDK files. The SBOM
  always includes the wolfBoot, wolfCrypt, and HAL files.
- The compiler runtime is not a component. `libgcc`, `newlib`, and the
  equivalent library of any other toolchain are linked into the image, but the
  SBOM records the source set and does not see them.
- Of the six wolfBoot submodules, only wolfSSL becomes a dependency component
  today. wolfTPM, wolfPKCS11, wolfHSM, wolfPSA, and wolfHAL sources are folded
  into the wolfBoot source set, so an advisory against one of those products
  does not match the document. The captured configuration does record the
  macros (`WOLFBOOT_TPM`, and the equivalents), so the build is still visible.
  wolfGlass owns the component table, and this limitation goes away when it
  gains the other entries.
- The document records the source set, not each file. There is no per-file
  hash, and SPDX reports `filesAnalyzed: false`. Use the source-set Merkle
  hash to compare two documents; you cannot decompose it to one file.
- Almost every wolfBoot configuration sets `WOLFCRYPT_ONLY`, so the image holds
  no TLS. A scanner still reports the TLS advisories of the wolfSSL component
  against it. Answer those with a VEX statement (`not_affected` /
  `code_not_present`). wolfBoot does not generate one yet.
- `tools/sbom/sbom-driver` is a thin POSIX launcher for the vendored Python
  engine. On Windows, run the launcher from WSL, MSYS, or Git Bash, or call
  `tools/sbom/sbom-driver.py` directly. As an alternative, use the compilation
  database tool (`compdb_sbom.py`).

### Missing sources

Each object that the build compiles must map to a source file on disk. If one
does not, the SBOM would record fewer sources than the image really holds. The
`sbom` and `sbom-hal` targets stop and list the objects. Run `git submodule
update --init`, or set `SBOM_ALLOW_MISSING=1` to accept a partial document.

`src/keystore.c` is the exception. wolfBoot generates it from the signing key,
so a fresh tree does not hold it yet. It carries the public keys that authorise
a firmware update, so the SBOM must describe it. Both targets generate it first,
exactly as a build does, which means `make sbom` on a fresh tree also creates a
signing key if none exists.

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

Useful overrides: `HOSTCC`, `SBOM_GEN`, `CRA_PYTHON`.

wolfcrypt sources are compiled directly into the wolfBoot image. They remain in
the source-set hash **and** are declared as a `wolfcrypt` component, nested
inside the `wolfssl` component that represents the release they ship in
(`SBOM_DEP_WOLFSSL` and `SBOM_DEP_WOLFCRYPT`, both on by default, sharing the
submodule's version). CycloneDX expresses that as a sub-component plus a
`wolfssl → wolfcrypt` dependency edge; SPDX as a `CONTAINS` relationship.

That coat carries machine-resolvable identifiers for both scanner families:

* `cpe:2.3:a:wolfssl:wolfssl:<version>:*:*:*:*:*:*:*` — NVD product for the
  wolfSSL library, and the identifier that actually matches: wolfCrypt
  advisories are filed against this product, not against the wolfcrypt one.
* `cpe:2.3:a:wolfssl:wolfcrypt:<version>:*:*:*:*:*:*:*` — NVD product for
  wolfCrypt. Registered, but no CVE is mapped to it today, so it documents
  provenance rather than driving matches.
* `pkg:github/wolfssl/wolfssl@v<version>-stable` — resolvable PURL for the
  wolfssl release (lowercase per purl-spec; `-stable` is the real tag).
* wolfBoot itself: `cpe:2.3:a:wolfssl:wolfboot:<version>:*:*:*:*:*:*:*` —
  registered in the NVD Official CPE Dictionary (published 2026-08-10).

### Only wolfCrypt is compiled in

`include/user_settings.h` defines `WOLFCRYPT_ONLY` for every configuration
except a wolfHSM server with certificate-chain verification, so the image holds
the crypto subset of wolfSSL and no TLS. `--crypto-only auto` (the default,
`SBOM_CRYPTO_ONLY` to override) reads that macro out of the captured
configuration and records:

```
wolfssl:sbom:wolfssl-subset=wolfcrypt-only
wolfssl:sbom:wolfssl-subset-basis=captured
```

The `wolfssl` component stays in the document regardless. Dropping it would
read as more precise and would take the scan from every wolfSSL advisory to
none. Narrow the TLS-only CVEs with a VEX statement instead, which is the
mechanism designed to say "present but not exploitable here".

### How the configuration is captured

wolfBoot's configuration is derived rather than literal: `include/user_settings.h`
turns `WOLFBOOT_SIGN_ECC256` into `HAVE_ECC`, `HAVE_ECC256`,
`ECC_TIMING_RESISTANT` and the rest. So the capture preprocesses the real
settings header with the real `-D` set, via `SBOM_SETTINGS_H` and
`SBOM_INCLUDE_DIRS`:

```
cc -dM -E $(CFLAGS -D and -I tokens) -include wolfssl/wolfcrypt/settings.h
```

Capturing only the `-D` tokens (against an empty translation unit) records
nothing that the header derives, and capturing only the header (with no `-D`
set) evaluates every `#if` against an empty configuration. Either omission
produces a well-formed SBOM describing a build nobody made — a signing
bootloader with no signature algorithm, in wolfBoot's case. Both halves are
required, and the capture aborts rather than emitting a partial dump.

`include/target.h` is generated and `#include`d by `user_settings.h`, so the
`sbom` target builds it first (`SBOM_PREREQS`).

## Route 2 — CMake (methods 5–6)

The CMake build exposes an `sbom` target (`cmake/sbom.cmake`) that collects the
compiled source set from the wolfBoot library targets and the effective
configuration from `WOLFBOOT_DEFS` / `USER_SETTINGS`, then calls the vendored
driver. The intent is to converge with the Make path for the same
configuration; CI compares the two outputs as an advisory check.

```sh
cmake -S . -B build-sim -DWOLFBOOT_TARGET=sim ...   # your normal configure
cmake --build build-sim --target sbom
```

Outputs land in the build directory. Overrides: `-DGEN_SBOM=...`, `-DHOSTCC=...`.

> `tools/sbom/sbom-driver` is a POSIX launcher; on Windows run this target from
> WSL / MSYS / Git-Bash, or call `tools/sbom/sbom-driver.py` directly.

### Pico SDK (method 6)

The Pico SDK build under `IDE/pico-sdk/rp2350/` is a standalone CMake project
that pulls in the Pico SDK, so it does not include `cmake/sbom.cmake`. Generate
its SBOM from the compilation database (see Route 4):

```sh
cd IDE/pico-sdk/rp2350/wolfboot
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...   # your normal configure
python3 <wolfboot>/tools/sbom/frontends/compdb_sbom.py build/compile_commands.json \
    --name wolfboot \
    --driver <wolfboot>/tools/sbom/sbom-driver \
    --version-file <wolfboot>/include/wolfboot/version.h \
    --version-macro LIBWOLFBOOT_VERSION_STRING
```

## Route 3 — IAR extractor (method 7)

IAR builds happen entirely inside Embedded Workbench and never touch the
Makefile or CMake, so the compiled source set and preprocessor configuration
live in the `.ewp` project file. The extractor reads them out and feeds the
shared driver:

```sh
tools/sbom/frontends/iar_sbom.py IDE/IAR/wolfboot.ewp \
    --name wolfboot \
    --driver tools/sbom/sbom-driver \
    --version-file include/wolfboot/version.h \
    --version-macro LIBWOLFBOOT_VERSION_STRING
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

python3 tools/sbom/frontends/compdb_sbom.py compile_commands.json \
    --name wolfboot \
    --driver tools/sbom/sbom-driver \
    --version-file include/wolfboot/version.h \
    --version-macro LIBWOLFBOOT_VERSION_STRING \
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
tools/sbom/frontends/zephyr_sbom.py \
    --name wolfboot-zephyr \
    --cmakelists zephyr/CMakeLists.txt \
    --driver tools/sbom/sbom-driver \
    --version-file include/wolfboot/version.h \
    --version-macro LIBWOLFBOOT_VERSION_STRING
```

Output: `wolfboot-zephyr-<version>.{cdx,spdx}.json`.

Because the module's configuration is Kconfig-driven (`CONFIG_*` symbols) rather
than a `-D` macro set, this is a **source-inventory** SBOM by default (no
build-config macros; the driver's `--source-only` mode). If you have a real
Zephyr build and want the exact compiled configuration, generate the SBOM from
that build's compilation database instead:

```sh
west build ... -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 tools/sbom/frontends/compdb_sbom.py build/compile_commands.json \
    --driver tools/sbom/sbom-driver \
    --version-file include/wolfboot/version.h \
    --version-macro LIBWOLFBOOT_VERSION_STRING \
    --include 'zephyr/src/' --name wolfboot-zephyr
```

## Output

Every route writes, into the working/build directory:

* `wolfboot-<target>-<sign>-<hash>-<version>.cdx.json` — CycloneDX 1.6
* `wolfboot-<target>-<sign>-<hash>-<version>.spdx.json` — SPDX 2.3

`<version>` is read from `include/wolfboot/version.h`. `<target>`, `<sign>`, and
`<hash>` are the configuration you built. One version of wolfBoot covers about
100 configurations, and each one is a different image with a different source
set, so the configuration is part of the name. Without it, a second target
overwrites the first. These files are ignored by `.gitignore`.

You can sanity-check any output:

```sh
python3 tools/sbom/validate_sbom.py \
    --name-prefix wolfboot \
    wolfboot-*.cdx.json wolfboot-*.spdx.json
```

## Continuous integration

`.github/workflows/test-sbom.yml` is an SBOM canary that runs the Make, CMake,
IAR, and compilation-database routes on every push/PR and validates each output,
so a change to a build system, the shared driver, or an extractor cannot
silently break SBOM generation. It also diffs Make vs CMake output for the same
sim configuration and runs a native-Windows scrub test against
`tools/sbom/sbom-driver.py`. The generated SBOMs are uploaded as build
artifacts.

The same workflow holds `tools/sbom/` to the revision in
`tools/sbom/.wolfglass-rev`. It checks wolfGlass out at that revision and runs
`tools/wolfglass-sync --check`, which fails if a vendored file differs from
wolfGlass `share/`, or if the pinned revision does not resolve. A local patch to
the vendored tooling therefore breaks CI instead of shipping quietly: fix
wolfGlass and re-vendor.

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
