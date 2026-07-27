# share/

This is the only vendorable set. Use `tools/wolfglass-sync` to copy these files
into a product at `tools/sbom/`, together with the pin files (`VERSION` and
`.wolfglass-rev`). Do not copy the `share/` folder name; copy the files.

## Contents

| File | Role |
|---|---|
| `sbom-driver.py` | The product-neutral SBOM engine (Python). |
| `sbom-driver` | Thin shell wrapper that runs `sbom-driver.py`. |
| `validate_sbom.py` | Structural validator for CI (`--name-prefix`). |
| `frontends/compdb_sbom.py` | Extractor for any `compile_commands.json`. |
| `frontends/iar_sbom.py` | Extractor for an IAR Embedded Workbench `.ewp`. |
| `frontends/zephyr_sbom.py` | Extractor for a Zephyr module `CMakeLists.txt`. |
| `build/sbom.mk` | Shared plain-Make fragment and `wolfglass_sbom_rule` macro. |
| `build/sbom.cmake` | Shared CMake helper: `wolfglass_add_sbom()`. |
| `gen-sbom` | The vendored SBOM generator. |
| `sbom.am` | Shared autotools fragment. |

## The driver contract

Every front end produces a composition input and a config input and hands them to
the driver.

Composition (at least one):

- `--srcs-file PATH` — the source files compiled into the artifact (tier E).
- `--lib PATH` — the built library to hash (tier R/L/S).
- `--no-artifact-hash` — record the artifact as-built and do not re-hash. Use it
  with `--lib` for a FIPS canister or a kernel module. Never substitute a source
  list for a certified artifact.

Config (choose one):

- `--cflags="..."` — raw CFLAGS; the driver expands the `-D` tokens through the
  host compiler. Use the `=` form so a leading-dash value is not read as a flag.
- `--options-h PATH` — a pre-expanded flat `#define` header, used verbatim.
- `--user-settings PATH` — a `user_settings.h`; the generator captures it.
- `--source-only` — no build-config macros (for example a Kconfig-driven build).

Dependency (for linkers and bindings): `--dep-wolfssl`, `--dep-openssl`, and
`--dep-version` are passed through only when the generator supports them.

The driver captures macros with the host compiler, so the SBOM is reproducible
across toolchains. It scrubs absolute host paths from the captured macros unless
you pass `--no-scrub`.

The shared driver is product-neutral and calls the vendored `share/gen-sbom` by
default. Pass `--gen-sbom` only when you want to override that copy.

## The manifest contract

A product does not copy logic. It describes itself:

- Make: set `SBOM_NAME`, `SBOM_SRCS`, `SBOM_CFLAGS`, and a version
  (`SBOM_VERSION`, or `SBOM_VERSION_FILE` + `SBOM_VERSION_MACRO`), then
  `include tools/sbom/build/sbom.mk`. For a second target, instantiate
  `$(eval $(call wolfglass_sbom_rule,<target>,<prefix>))`.
- CMake: `include(tools/sbom/build/sbom.cmake)` and call `wolfglass_add_sbom()`
  with `NAME`, `VERSION_FILE`, `VERSION_MACRO`, `TARGETS`, `DEFS`, `LICENSE`.
  `SBOM_GEN` is the canonical generator override; `GEN_SBOM` remains a legacy
  alias for compatibility.
- Autotools: set the `SBOM_*` variables and `include tools/sbom/sbom.am`.

Keep only true product knowledge in the product: the route-through script, the
module extractor, and the HAL source selector.
