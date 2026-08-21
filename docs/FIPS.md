# wolfBoot with wolfCrypt FIPS 140-3

This page explains how wolfBoot performs its firmware signature verification with the wolfCrypt FIPS 140-3 module, and what a fully CMVP-validated deployment additionally requires. Read it before making any FIPS claim about a wolfBoot deployment.

## Two distinct things: approved algorithms vs. a validated module

FIPS 140-3 has two separate requirements that are easy to conflate:

1. Using FIPS **approved algorithms** for the security-relevant operations (here: image signature verification and hashing).
2. Performing those operations inside the **CMVP-validated wolfCrypt module** boundary, with the power-on self-test (POST), the in-core integrity check, and status gating (`wolfCrypt_GetStatus_fips`).

A stock wolfBoot build only addresses (1): it compiles individual `wolfcrypt/src/*.c` files selected by `SIGN`/`HASH`, with no POST or in-core integrity check. Building with `FIPS=1` (this page) addresses (2): wolfBoot links the wolfCrypt FIPS module boundary, runs the POST + in-core check at boot, and refuses to boot unless the module is operational.

A production-validated deployment still requires the **licensed, validated** wolfCrypt FIPS bundle at the exact validated revision (not the evaluation "FIPS-ready" drop), the validated module version, and adherence to the module's Security Policy. Contact wolfSSL (facts@wolfssl.com) for the current certificate, validated module version, and integration guidance for a specific target.

## Approved algorithms for image authentication

wolfBoot authenticates images with a public-key signature over a hash of the image. The default wolfBoot signature algorithm, **ED25519, is NOT FIPS approved** and must not be used for a FIPS configuration.

Approved pairs (select in the target `.config`):

- Signature (`SIGN=`): `ECC256` / `ECC384` / `ECC521` (ECDSA P-256/P-384/P-521). RSA-PSS is FIPS-approved but is not yet wired into wolfBoot's FIPS module object list (no `rsa.o` in the boundary); the `FIPS=1` build rejects it. Use ECDSA.
- Hash (`HASH=`): `SHA256`, `SHA384`. Match or exceed the signature strength (e.g. P-384 with SHA-384).
- Set `SPMATH=1` (the single-precision math backend the wolfCrypt FIPS module is validated with).

Not approved for signing: `ED25519`, `ED448`. The post-quantum options (`LMS`/`XMSS`/`ML-DSA`) are governed by separate NIST standards and are out of scope here.

## Getting the FIPS source

Obtain a FIPS wolfCrypt source tree. For evaluation, the FIPS-ready bundle can be downloaded from wolfSSL:

```
https://www.wolfssl.com/wolfssl-5.9.2-gplv3-fips-ready.zip
```

Production use requires the licensed, validated FIPS bundle. Unpack it and point wolfBoot at it with `WOLFBOOT_LIB_WOLFSSL`.

## Building wolfBoot with FIPS

The `FIPS=1` build option (`options.mk`) rebuilds the wolfcrypt object list as the validated module boundary in link order (`wolfcrypt_first.o` first, `wolfcrypt_last.o` last, with `fips.o`/`fips_test.o` and the boundary crypto between them - the in-core integrity hash on GCC/ELF is enforced by this link order). Point the build at the FIPS tree and select an approved algorithm pair:

```
cp config/examples/sim-fips.config .config    # or config/examples/cm4.config
make FIPS=1 WOLFBOOT_LIB_WOLFSSL=/path/to/wolfssl-5.9.2-gplv3-fips-ready \
     SIGN=ECC384 HASH=SHA384 SPMATH=1
```

`-DHAVE_FIPS` is added by the `FIPS=1` block. `FIPS=1` also defaults `FIPS_READY=1`, which defines `WOLFSSL_FIPS_READY` (`include/user_settings.h`); that macro forces the evaluation bundle's `HAVE_FIPS_VERSION` to 7 and selects the FIPS-186-4 gating in `settings.h`. `FIPS_READY=1` (the default) builds the evaluation FIPS-ready bundle. A production build with the licensed **validated** bundle (not a FIPS-ready drop) must pass both `FIPS_READY=0` and `FIPS_VERSION=<n>` (for example `FIPS_VERSION=7` for a 140-3 module), which `options.mk` turns into `-DHAVE_FIPS_VERSION=<n>` to pin the module version. This is required because under wolfBoot's `-DWOLFSSL_USER_SETTINGS` build `settings.h` includes only `user_settings.h` and never the configure-generated `wolfssl/options.h`, so the validated bundle does not self-declare `HAVE_FIPS_VERSION`; without it the module would silently build as FIPS v1 (140-2). `options.mk` therefore errors if `FIPS_READY=0` is passed without `FIPS_VERSION`. The `HAVE_FIPS` block in `include/user_settings.h` also enables the module's algorithm set, keeps the RNG/DRBG enabled, and wires the entropy seed (below).

## Entropy source (required)

The FIPS DRBG needs a seed. wolfBoot's lean configuration compiles out the OS seed paths, so a seed is provided via `CUSTOM_RAND_GENERATE_SEED` (the `HAVE_FIPS` block in `include/user_settings.h` keeps the RNG enabled by undoing wolfBoot's `WC_NO_RNG`/`WC_NO_HASHDRBG`). The example wiring points it at `wolfBoot_fips_seed()`, implemented per target: `/dev/urandom` on the simulator (`hal/sim.c`) and the BCM2711 RNG200 hardware TRNG on the CM4 (`hal/cm4.c`). Without a working seed, the ECDSA power-on self-test (which performs a sign) fails with `ECDSA_KAT_FIPS_E` because `wc_GenerateSeed()` returns `NOT_COMPILED_IN`.

## Sealing the in-core integrity hash

The module verifies an in-core integrity hash (HMAC-SHA-256 over the module's code and read-only data) at startup. A fresh build ships with a placeholder, so the first run reports a mismatch; capture the runtime hash and seal it:

> **Console required for the seal.** The runtime hash is emitted with `wolfBoot_printf`, which compiles to a no-op when the console is disabled (`DEBUG_UART=0`, the default in `config/examples/cm4.config`). Build the seal-capture image with `DEBUG_UART=1` (bare-metal) or on the simulator, or the board just halts silently with no hash to copy. Once `verifyCore[]` is sealed you can rebuild with `DEBUG_UART=0` for production - the seal is independent of the console setting only if it does not shift the module's link addresses (it does not on cm4, where the console code is outside the FIPS boundary; re-confirm the hash after the final production build).

1. Build and run with a FIPS callback registered (wolfBoot does this in `src/loader.c`) and the console enabled (see the note above). On a mismatch the module reports the runtime hash; wolfBoot prints it (`FIPS in-core hash = ...`, from `wolfCrypt_GetCoreHash_fips()`) before halting.
2. Copy the reported 64-hex-character hash into `verifyCore[]` in `wolfcrypt/src/fips_test.c`.
3. Rebuild and re-run. `wolfCrypt_GetStatus_fips()` now returns 0 (operational).

The seal is **specific to the exact binary layout**: any code change that shifts the FIPS module's link addresses changes the in-core hash and requires a re-seal. Re-sealing `verifyCore[]` itself does not shift addresses (same-size rewrite), so once the rest of the build is fixed the seal converges in one pass.

Two practical traps when re-sealing (both cost time on the CM4 bring-up):

- `verifyCore[]` can be sealed via a build define instead of editing the FIPS tree: `CFLAGS_EXTRA="-DWOLFCRYPT_FIPS_CORE_HASH_VALUE=<hash>"` (unquoted; `fips_test.c` stringifies it). But apply it by recompiling **only** `fips_test.o` - `rm "$WOLFBOOT_LIB_WOLFSSL/wolfcrypt/src/fips_test.o"` then rebuild. `verifyCore[]` lives *after* `wolfCrypt_FIPS_last`, so this leaves the hashed region byte-identical and converges in one pass. Passing the define through a **full** rebuild (`make clean` + build) recompiles the whole module and shifts its link addresses, so the hash never stabilizes.
- `make clean` removes `$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/*.o`. If you build with `WOLFBOOT_LIB_WOLFSSL=<fips-tree>` but run `make clean` **without** that variable, it cleans the default `lib/wolfssl` instead, leaving the stale FIPS `fips_test.o` in place - the new seal silently never lands. Pass `WOLFBOOT_LIB_WOLFSSL` to `clean` too, or `rm` the object directly.

## Bare-metal targets

The FIPS module targets a hosted environment; a few things must be provided on bare-metal (the CM4 does all of these):

- **POST entry.** The module registers its POST via a C constructor (`.init_array`), which a hosted runtime runs before `main()`. wolfBoot's bare-metal startup does not run `.init_array`, so build with `NO_ATTRIBUTE_CONSTRUCTOR` and call `fipsEntry()` explicitly (`src/loader.c`).
- **Normal (cacheable) memory.** wolfBoot's simple startup runs with the MMU off, where all memory is Device-nGnRnE and unaligned / 128-bit SIMD accesses fault (the FIPS module and newlib `printf`/`snprintf` do both). The CM4 HAL enables a minimal identity MMU with DDR mapped Normal cacheable before the POST (`cm4_mmu_enable`), and tears it down (clean D-cache, disable MMU/caches) before the boot handoff (`cm4_mmu_disable`) so the loaded image is coherent and the application starts MMU-off.
- **libc.** The module uses malloc/printf; stub the newlib syscalls (`--specs=nosys.specs`) and provide a bounded `_sbrk` so the heap cannot grow into the unverified image (the CM4 HAL allocates from a fixed static buffer in `hal/cm4.c`).

Bring the module up on the simulator (`config/examples/sim-fips.config`) first - it exercises the whole flow (module boundary, POST, in-core seal, verify, A/B update) with no hardware.

## Verifying operation

- POST/CASTs run at module initialization; `wc_RunAllCast_fips()` runs the conditional algorithm self-tests and `wolfCrypt_GetStatus_fips()` reports the module status (0 = operational).
- wolfBoot treats a non-zero FIPS status as a hard failure and refuses to boot (`src/loader.c`).
- A deliberately corrupted module boundary (flip a byte) makes the in-core check fail and blocks the boot - the negative test for the integration.

## See also

- [Targets.md](Targets.md) - Raspberry Pi Compute Module 4 (BCM2711) target. FIPS 140-3 authenticated boot (module operational -> SHA-384 integrity -> ECDSA-P384 verify seeded by the BCM2711 hardware TRNG -> handoff) is validated on CM4 hardware.
