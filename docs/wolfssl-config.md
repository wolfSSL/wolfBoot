# wolfBoot wolfCrypt Configuration

This document describes how wolfBoot configures wolfCrypt: the layout of
`include/user_settings.h` and `include/user_settings/`, the
`WOLFBOOT_NEEDS_*` marker convention, the contract between the build
system (`options.mk`) and these headers, and how to read, modify, or
extend the configuration when adding a new feature.

> **Audience.** Developers adding or modifying wolfBoot features that
> touch wolfCrypt. If you only consume wolfBoot via a `.config` file or
> an IDE project, you do not need to read this document — the high-level
> `WOLFBOOT_*` flags work the same way they always have.

---

## Table of Contents

1. [Overview & Goals](#1-overview--goals)
2. [Architecture](#2-architecture)
3. [The NEEDS_* Marker Convention](#3-the-needs_-marker-convention)
4. [Marker Reference](#4-marker-reference)
5. [Fragment Reference](#5-fragment-reference)
6. [`cascade.h` Detail](#6-cascadeh-detail)
7. [`finalize.h` Detail](#7-finalizeh-detail)
8. [Adding a New Feature](#8-adding-a-new-feature)
9. [Reading an Existing Configuration](#9-reading-an-existing-configuration)
10. [Relationship to options.mk](#10-relationship-to-optionsmk)
11. [Alternative Build Paths (CMake, IDE, Zephyr)](#11-alternative-build-paths-cmake-ide-zephyr)
12. [Compatibility Notes](#12-compatibility-notes)

---

## 1. Overview & Goals

wolfBoot pulls in wolfCrypt as a submodule and configures it via
`-DWOLFSSL_USER_SETTINGS`, which directs wolfSSL to load
`include/user_settings.h`. Historically that file was a single 781-line
header that mixed system defines, every SIGN family's curve/size config,
every wolfBoot feature's wolfCrypt requirements, and a long block of
"strip the rest" disables — all interconnected by deeply nested negated
`#ifdef` chains. Adding a new feature meant editing every chain that
needed to opt out of a default disable.

The current scheme replaces that with a **shim + fragments + one
reconciliation header** layout where:

- `include/user_settings.h` is a 73-line shim that does nothing but
  `#include` fragment headers in a fixed order.
- Each high-level wolfBoot feature owns a fragment header that declares
  *only its own* wolfCrypt configuration. Fragments compose by
  `#define`-only contributions; they never `#undef` and never test each
  other's internal state.
- `WOLFBOOT_NEEDS_*` markers are the single contract between fragments
  and the final reconciliation. Each marker is a positive intent
  ("this build needs RNG") and `finalize.h` translates the absence of a
  marker into the corresponding wolfCrypt negative flag (`WC_NO_RNG`).

The benefits, listed concretely:

- An IDE or CMake user can build wolfBoot by setting only the
  high-level `WOLFBOOT_*` flags. The cascade and reconciliation that
  used to live in `options.mk` now lives in `cascade.h` and runs from
  the preprocessor.
- Adding a feature no longer requires editing the disable sites in
  `finalize.h`. The new fragment declares its needs once, and every
  affected disable picks up the change automatically.
- The full set of "what does this configuration disable?" is one grep
  away (`grep WOLFBOOT_NEEDS_ include/user_settings/finalize.h`); the
  full set of "what does my feature need?" is one fragment file away.

---

## 2. Architecture

### 2.1. The Shim

`include/user_settings.h` is the canonical entry point that wolfSSL
loads when `-DWOLFSSL_USER_SETTINGS` is set. It contains no wolfCrypt
configuration of its own, just an ordered series of `#include`s.

```c
/* include/user_settings.h (excerpt) */
#ifdef WOLFBOOT_PKCS11_APP
# include "test-app/wcs/user_settings.h"
#else

#include <target.h>

#include "user_settings/cascade.h"
#include "user_settings/base.h"
#include "user_settings/sign_dispatch.h"
#include "user_settings/hash_dispatch.h"
#include "user_settings/encrypt.h"
#include "user_settings/trustzone.h"
#include "user_settings/tpm.h"
#include "user_settings/wolfhsm.h"
#include "user_settings/cert_chain.h"
#include "user_settings/renesas.h"
#include "user_settings/platform.h"
#include "user_settings/test_bench.h"
#include "user_settings/finalize.h"

#endif
```

The `WOLFBOOT_PKCS11_APP` branch is a pre-existing redirect for the
PKCS#11 test app, which has its own user_settings.h shipped alongside
its build harness; it is not part of the production wolfBoot path.

### 2.2. Fragment Headers

Fragment headers live in `include/user_settings/`. Each is a small,
self-contained `.h` file that contributes wolfCrypt configuration when
its activation flag (a `WOLFBOOT_*` or wolfCrypt-side flag) is set, and
expands to nothing otherwise. The 22 fragments are grouped by concern:

| Group | Files |
| --- | --- |
| Foundation | `cascade.h`, `base.h`, `finalize.h` |
| SIGN dispatch + families | `sign_dispatch.h`, `sign_ecc.h`, `sign_rsa.h`, `sign_ed25519.h`, `sign_ed448.h`, `sign_ml_dsa.h`, `sign_lms.h`, `sign_xmss.h` |
| HASH dispatch + families | `hash_dispatch.h`, `hash_sha384.h`, `hash_sha3.h` |
| Feature blocks | `encrypt.h`, `trustzone.h`, `tpm.h`, `wolfhsm.h`, `cert_chain.h` |
| Platform / hardware | `renesas.h`, `platform.h` |
| Test/Bench | `test_bench.h` |

Each fragment is described in detail in [Section 5](#5-fragment-reference).

### 2.3. Inclusion Order (the contract)

The order in `user_settings.h` is **not arbitrary**. It is the contract
that lets fragments stay decoupled. The phases:

1. **`cascade.h`** runs first. It derives implied flags
   (`WOLFBOOT_TPM` from `WOLFBOOT_TPM_VERIFY`/`MEASURED_BOOT`/etc.,
   `WOLFBOOT_TPM_PARMENC` from `TPM_KEYSTORE`/`TPM_SEAL`,
   `WOLFBOOT_NEEDS_RSA` from any RSA SIGN flag) and declares all the
   `WOLFBOOT_NEEDS_*` markers that come purely from feature-flag
   testing. Every fragment downstream sees the derived flags as if
   they were emitted by `options.mk`.

2. **`base.h`** sets the always-on foundation: alignment, threading,
   stdlib types, `WC_NO_HARDEN` (wolfBoot does verify-only public
   crypto). It also defines `WOLFCRYPT_ONLY` (skipped only when wolfHSM
   server + cert-chain verify needs the SSL layer).

3. **`sign_dispatch.h`** conditionally includes the appropriate
   `sign_*.h` fragment(s) based on `WOLFBOOT_SIGN_*` and
   `WOLFBOOT_SIGN_SECONDARY_*`. Hybrid signatures (primary + secondary)
   include two SIGN fragments. `sign_rsa.h` is included
   unconditionally; its `#else` branch defines `NO_RSA` when no RSA
   flag is set.

4. **`hash_dispatch.h`** conditionally includes the appropriate
   `hash_*.h` fragment based on `WOLFBOOT_HASH_*`. SHA-256 is the
   foundation default and has no fragment.

5. **Feature fragments** (`encrypt.h`, `trustzone.h`, `tpm.h`,
   `wolfhsm.h`, `cert_chain.h`, `renesas.h`, `platform.h`,
   `test_bench.h`) each contribute their own positive defines and
   declare additional `WOLFBOOT_NEEDS_*` markers when needed. Order
   among these is currently insensitive — fragments don't read each
   other's contributions — but the established order is preserved for
   readability.

6. **`finalize.h`** runs last. It tests every `WOLFBOOT_NEEDS_*`
   marker and applies the corresponding wolfCrypt negative flag
   (`NO_*`, `WC_NO_*`) when the marker is absent. It then defines the
   always-on global "off" block and the memory model.

**Two rules for fragment authors:**
- **Fragments only `#define`, never `#undef`.** All the negative-flag
  decisions belong to `finalize.h`.
- **Fragments declare `WOLFBOOT_NEEDS_*` markers; they don't test for
  the *absence* of features.** If your fragment needs to know whether
  another feature is present, that's a sign the cascade header should
  derive a marker that both fragments can read.

---

## 3. The NEEDS_* Marker Convention

### 3.1. Why It Exists

wolfCrypt has two flavors of feature flags:

| Flavor | Examples | Default |
| --- | --- | --- |
| **Positive** (opt-in) | `HAVE_ECC`, `WOLFSSL_AES_CFB`, `HAVE_HASHDRBG`, `WOLFSSL_SHA384` | off; you set them when you want the capability |
| **Negative** (opt-out) | `NO_AES`, `NO_HMAC`, `WC_NO_RNG`, `NO_ASN`, `NO_KDF`, `NO_CMAC`, `NO_PWDBASED` | on; you set them to strip a capability for code-size |

**Positive flags compose trivially.** If `tpm.h` says `#define
WOLFSSL_AES_CFB` and `trustzone.h` also says `#define WOLFSSL_AES_CFB`,
the second is harmless under an `#ifndef` guard. Multiple fragments can
each declare the same positive flag with no coordination.

**Negative flags don't compose.** If `base.h` defines `NO_AES` and a
later fragment needs AES, the fragment has to either `#undef NO_AES`
(scattering AES-truth across N fragments and requiring strict include
ordering) or the `NO_AES` definition itself has to be conditional on
*every* feature that needs AES — exactly the negated-AND chain pattern
that the old `user_settings.h` was riddled with.

`WOLFBOOT_NEEDS_*` markers thread the needle: fragments stay purely
additive (each declares positive intent), and `finalize.h` is the
**single place** that translates positive intent into negative flags.

### 3.2. The Mechanic

A fragment declares its needs:

```c
/* include/user_settings/tpm.h */
#if defined(WOLFBOOT_TPM) && !defined(WOLFBOOT_TZ_FWTPM)
#  ifdef WOLFBOOT_TPM_PARMENC
#    define WOLFSSL_AES_CFB     /* positive flag, set directly */
#    define WOLFSSL_PUBLIC_MP   /* positive flag, set directly */
   /* RNG and HASHDRBG are needed too, but those translate to
    * negatives in finalize.h, so we declare markers: */
#  endif
#endif

/* (NEEDS_RNG and NEEDS_HASHDRBG for PARMENC are declared in
 * cascade.h — see Section 6.) */
```

In practice most `WOLFBOOT_NEEDS_*` markers are declared in
`cascade.h`, because the conditions that imply them are pure tests of
high-level feature flags. Fragments may also declare additional
markers when they have an internal sub-condition (the prime example is
`test_bench.h` declaring `WOLFBOOT_NEEDS_HASHDRBG` only on the
LPC55S69 sub-path).

`finalize.h` reconciles:

```c
/* include/user_settings/finalize.h */
#ifndef WOLFBOOT_NEEDS_RNG
#  define WC_NO_RNG
#endif
#ifndef WOLFBOOT_NEEDS_AES
#  define NO_AES
#endif
/* ... etc ... */
```

### 3.3. Properties

- **Idempotent.** Every marker `#define` is `#ifndef`-guarded, so
  multiple fragments declaring the same marker is safe.
- **Order-independent among feature fragments.** A fragment that
  declares `WOLFBOOT_NEEDS_RNG` does not care which other fragments
  declare it too. `finalize.h` only checks "was the marker defined by
  *anyone*?".
- **Cheap.** Pure preprocessor; no runtime or code-size cost.
- **Searchable.** Every disable in `finalize.h` is a `#ifndef
  WOLFBOOT_NEEDS_*` block. Every opt-in is a `#define
  WOLFBOOT_NEEDS_*` line. Both are greppable.

---

## 4. Marker Reference

Markers are declared by `cascade.h` (and a few by individual fragments)
and tested by `finalize.h`. The table below is the canonical
vocabulary; every marker is pure preprocessor and has no runtime cost.

| Marker | Without it, finalize.h sets | Declared when | Where declared |
| --- | --- | --- | --- |
| `WOLFBOOT_NEEDS_RSA` | `NO_RSA` | Any `WOLFBOOT_SIGN_RSA*` / `RSAPSS*` / `SECONDARY_RSA*` flag, or `WOLFCRYPT_SECURE_MODE && !PKCS11_SMALL` | `cascade.h` |
| `WOLFBOOT_NEEDS_RNG` | `WC_NO_RNG` | `WOLFBOOT_TPM_PARMENC`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK`, `WOLFBOOT_ENABLE_WOLFHSM_SERVER`, `WOLFBOOT_ENABLE_WOLFHSM_CLIENT && WOLFBOOT_SIGN_ML_DSA` | `cascade.h` |
| `WOLFBOOT_NEEDS_HASHDRBG` | `WC_NO_HASHDRBG` (else: defines `HAVE_HASHDRBG`) | `WOLFBOOT_TPM_PARMENC`, `WOLFCRYPT_SECURE_MODE`, `(WOLFCRYPT_TEST‖WOLFCRYPT_BENCHMARK) && (LPC55S69_*HWACCEL)` | `cascade.h` |
| `WOLFBOOT_NEEDS_AES` | `NO_AES` | `ENCRYPT_WITH_AES128`, `ENCRYPT_WITH_AES256`, `WOLFBOOT_TPM_PARMENC`, `WOLFCRYPT_SECURE_MODE`, `SECURE_PKCS11`, `WOLFCRYPT_TZ_PSA`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK` | `cascade.h` |
| `WOLFBOOT_NEEDS_AES_CBC` | `NO_AES_CBC` | `WOLFBOOT_TPM_PARMENC`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK` | `cascade.h` |
| `WOLFBOOT_NEEDS_HMAC` | `NO_HMAC` | `WOLFBOOT_TPM`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK` | `cascade.h` |
| `WOLFBOOT_NEEDS_DEV_RANDOM` | `NO_DEV_RANDOM` | `WOLFBOOT_TPM`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK` | `cascade.h` |
| `WOLFBOOT_NEEDS_ECC_KEY_EXPORT` | `NO_ECC_KEY_EXPORT` | `WOLFBOOT_TPM`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK`, `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`, `WOLFBOOT_ENABLE_WOLFHSM_SERVER` | `cascade.h` |
| `WOLFBOOT_NEEDS_ASN` | `NO_ASN` | `WOLFBOOT_NEEDS_RSA`, `WOLFBOOT_TPM`, `WOLFCRYPT_SECURE_MODE`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK`, `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`, `WOLFBOOT_ENABLE_WOLFHSM_SERVER` | `cascade.h` |
| `WOLFBOOT_NEEDS_BASE64` | `NO_CODING` (else: defines `WOLFSSL_BASE64_ENCODE`) | `WOLFBOOT_TPM_SEAL && WOLFBOOT_ATA_DISK_LOCK`, `WOLFBOOT_ENABLE_WOLFHSM_CLIENT`, `WOLFBOOT_ENABLE_WOLFHSM_SERVER` | `cascade.h` |
| `WOLFBOOT_NEEDS_CMAC` | `NO_CMAC` | `WOLFCRYPT_TZ_PSA`, `WOLFBOOT_TZ_FWTPM` | `cascade.h` |
| `WOLFBOOT_NEEDS_KDF` | `NO_KDF` | `WOLFCRYPT_TZ_PSA`, `WOLFBOOT_TZ_FWTPM` | `cascade.h` |
| `WOLFBOOT_NEEDS_MALLOC` | `NO_WOLFSSL_MEMORY`, `WOLFSSL_NO_MALLOC` (in the `!SMALL_STACK` branch of memory model) | `SECURE_PKCS11`, `WOLFCRYPT_TZ_PSA`, `WOLFBOOT_ENABLE_WOLFHSM_SERVER`, `WOLFCRYPT_TEST`, `WOLFCRYPT_BENCHMARK` | `cascade.h` |

Two markers have a **bipolar** reconciliation: `NEEDS_HASHDRBG` and
`NEEDS_BASE64` switch between the positive flag (`HAVE_HASHDRBG` /
`WOLFSSL_BASE64_ENCODE`) when present and the negative flag
(`WC_NO_HASHDRBG` / `NO_CODING`) when absent. The rest are unipolar:
absent ⇒ define the negative flag, present ⇒ skip the negative flag
(the positive flag is set elsewhere by the relevant fragment).

---

## 5. Fragment Reference

Each fragment is documented with: **purpose**, **activation gate**,
**markers it declares**, **positive defines it sets**, and any **notes**.

### 5.1. `base.h`

**Purpose.** Foundation defines that every wolfBoot build needs
regardless of features.

**Activation.** Always (unconditional).

**Sets:**
- `WOLFSSL_GENERAL_ALIGNMENT 4`, `SINGLE_THREADED`, `WOLFSSL_USER_MUTEX`
- `WOLFCRYPT_ONLY` (only when not `(WOLFHSM_SERVER && CERT_CHAIN_VERIFY)`)
- `SIZEOF_LONG_LONG 8`, `HAVE_EMPTY_AGGREGATES 0`,
  `HAVE_ANONYMOUS_INLINE_AGGREGATES 0`
- `CTYPE_USER`, `XTOUPPER`/`XTOLOWER` (unless `WOLFSSL_ARMASM`)
- `WC_NO_HARDEN` if `USE_FAST_MATH` (verify-only crypto needs no
  timing-resistance hardening)

**Notes.** The `WOLFCRYPT_ONLY` carve-out for the wolfHSM-server
cert-chain mode lives here for now. A future cleanup could move that to
`cert_chain.h` via a `WOLFBOOT_NEEDS_TLS_LAYER` marker.

### 5.2. `cascade.h`

**Purpose.** Single source of truth for feature-flag → derived-flag
cascades and for `WOLFBOOT_NEEDS_*` markers that come purely from
feature-flag tests.

See [Section 6](#6-cascadeh-detail) for full content.

### 5.3. `sign_dispatch.h`

**Purpose.** Conditionally include the right `sign_*.h` fragment(s).

Each branch tests the SIGN family activation flags and pulls in the
matching fragment. `sign_rsa.h` is included unconditionally because
its `#else` branch defines `NO_RSA` when no RSA flag is set; that
fallback must run regardless of the active SIGN.

### 5.4. `sign_ed25519.h`

**Activation.** `WOLFBOOT_SIGN_ED25519 || WOLFBOOT_SIGN_SECONDARY_ED25519`.

**Sets:**
- `HAVE_ED25519`, `ED25519_SMALL`, `USE_SLOW_SHA512`, `WOLFSSL_SHA512`
- `NO_ED25519_SIGN`, `NO_ED25519_EXPORT` when **not**
  `WOLFBOOT_ENABLE_WOLFHSM_SERVER` (HSM server is the only build that
  also signs with ED25519).

### 5.5. `sign_ed448.h`

**Activation.** `WOLFBOOT_SIGN_ED448 || WOLFBOOT_SIGN_SECONDARY_ED448`.

**Sets:**
- `HAVE_ED448`, `HAVE_ED448_VERIFY`, `ED448_SMALL`
- `WOLFSSL_SHA3`, `WOLFSSL_SHAKE256`, `WOLFSSL_SHA512`
- `NO_ED448_SIGN`, `NO_ED448_EXPORT` when not `WOLFHSM_SERVER`.

### 5.6. `sign_ecc.h`

**Activation.** Any `WOLFBOOT_SIGN_ECC{256,384,521}`,
`WOLFBOOT_SIGN_SECONDARY_ECC*`, `WOLFCRYPT_SECURE_MODE`,
`WOLFCRYPT_TEST`, or `WOLFCRYPT_BENCHMARK`.

**Sets:**
- `HAVE_ECC`, `ECC_TIMING_RESISTANT`, `ECC_USER_CURVES`,
  `WOLFSSL_PUBLIC_MP`
- Per-curve: `HAVE_ECC256`, `HAVE_ECC384`/`WOLFSSL_SP_384`,
  `HAVE_ECC521`/`WOLFSSL_SP_521`
- `FP_MAX_BITS` per the largest active curve
- `WOLFSSL_SP_NO_*` for unselected curves (when SP math is in use)
- SP math: `WOLFSSL_SP_MATH`, `WOLFSSL_SP_SMALL`, `WOLFSSL_HAVE_SP_ECC`
- Verify-only carve-outs (`NO_ECC_SIGN`, `NO_ECC_DHE`, `NO_ECC_EXPORT`,
  `NO_ECC_KEY_EXPORT`) when no TPM/SECURE_MODE/TEST/BENCH/HSM is in use
- Sign-and-verify defines (`HAVE_ECC_SIGN`, `HAVE_ECC_VERIFY`,
  `HAVE_ECC_KEY_EXPORT`/`IMPORT`, `WOLFSSL_KEY_GEN`) under SECURE_MODE/
  TEST/BENCH/HSM
- Freescale LTC bindings under `FREESCALE_USE_LTC`

**Notes.** The internal carve-outs for verify-only vs sign-capable are
the most complex piece of any SIGN fragment. They are kept inline
(rather than collapsed via NEEDS markers) because the conditions are
ECC-specific and don't fit the global marker vocabulary cleanly. The
fragment is self-contained.

### 5.7. `sign_rsa.h`

**Activation.** Always included by `sign_dispatch.h`. Inside the file,
the outer `#if` tests the same RSA / RSAPSS / SECONDARY / SECURE_MODE
condition as `sign_dispatch.h` would; the `#else` defines `NO_RSA`.

**Sets when RSA is active:**
- `WC_RSA_BLINDING`, `WC_RSA_DIRECT`, `RSA_LOW_MEM`,
  `WC_ASN_HASH_SHA256`
- `WC_RSA_PSS` for any RSAPSS flag
- Verify-only carve-outs (`WOLFSSL_RSA_VERIFY_INLINE`,
  `WOLFSSL_RSA_VERIFY_ONLY`, `WOLFSSL_RSA_PUBLIC_ONLY`,
  `WC_NO_RSA_OAEP`, `NO_RSA_BOUNDS_CHECK`) when not under TPM /
  SECURE_MODE / TEST / BENCH / HSM
- Per-size: `FP_MAX_BITS`, `SP_INT_BITS`, `RSA_MIN_SIZE`,
  `RSA_MAX_SIZE`, `WOLFSSL_SP_<size>`, `WOLFSSL_SP_NO_<other_sizes>`
- SP math: `WOLFSSL_HAVE_SP_RSA`, `WOLFSSL_SP`, `WOLFSSL_SP_SMALL`,
  `WOLFSSL_SP_MATH`
- Under `WOLFCRYPT_SECURE_MODE`: re-`#define`s `FP_MAX_BITS` and SP
  size flags to span 2048-4096 (the secure side may serve any RSA
  size)

**Sets when RSA is not active:** `NO_RSA`.

**Notes.** This is the only SIGN fragment included unconditionally,
because the `NO_RSA` fallback must still run for ECC/Ed25519/etc.
configurations to avoid linking the RSA code path.

### 5.8. `sign_ml_dsa.h`

**Activation.** `WOLFBOOT_SIGN_ML_DSA || WOLFBOOT_SIGN_SECONDARY_ML_DSA`.

**Sets:**
- `HAVE_DILITHIUM`, `WOLFSSL_WC_DILITHIUM`,
  `WOLFSSL_EXPERIMENTAL_SETTINGS`
- Verify-only and small-mem variants:
  `WOLFSSL_DILITHIUM_VERIFY_ONLY`,
  `WOLFSSL_DILITHIUM_NO_LARGE_CODE`, `WOLFSSL_DILITHIUM_SMALL`,
  `WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM`,
  `WOLFSSL_DILITHIUM_VERIFY_NO_MALLOC`
- `WOLFSSL_DILITHIUM_NO_ASN1` when not WOLFHSM client/server (HSM
  needs ASN.1 for ML-DSA keys traveling over the comm channel)
- `WOLFSSL_SHA3`, `WOLFSSL_SHAKE256`, `WOLFSSL_SHAKE128`,
  `WOLFSSL_SP_NO_DYN_STACK`

### 5.9. `sign_lms.h`

**Activation.** `WOLFBOOT_SIGN_LMS`.

**Sets:**
- `WOLFSSL_HAVE_LMS`, `WOLFSSL_WC_LMS`, `WOLFSSL_WC_LMS_SMALL`,
  `WOLFSSL_LMS_VERIFY_ONLY`
- `WOLFSSL_LMS_MAX_LEVELS LMS_LEVELS`,
  `WOLFSSL_LMS_MAX_HEIGHT LMS_HEIGHT`,
  `LMS_IMAGE_SIGNATURE_SIZE IMAGE_SIGNATURE_SIZE`

**Notes.** The user-set parameter values (`LMS_LEVELS`, `LMS_HEIGHT`,
`LMS_WINTERNITZ`, `IMAGE_SIGNATURE_SIZE`) are still passed by
`options.mk` from the `.config` file. This fragment maps those into
the `WOLFSSL_LMS_*` flags wolfCrypt expects.

### 5.10. `sign_xmss.h`

**Activation.** `WOLFBOOT_SIGN_XMSS`.

**Sets:**
- `WOLFSSL_HAVE_XMSS`, `WOLFSSL_WC_XMSS`, `WOLFSSL_WC_XMSS_SMALL`,
  `WOLFSSL_XMSS_VERIFY_ONLY`, `WOLFSSL_XMSS_MAX_HEIGHT 32`
- `XMSS_IMAGE_SIGNATURE_SIZE IMAGE_SIGNATURE_SIZE`

### 5.11. `hash_dispatch.h`

**Purpose.** Conditionally include the right `hash_*.h` fragment.
SHA-256 is the foundation default and has no fragment.

### 5.12. `hash_sha384.h`

**Activation.** `WOLFBOOT_HASH_SHA384`.

**Sets:** `WOLFSSL_SHA384`. Defines `NO_SHA256` when no other consumer
(no RSA, no TPM, no secure-mode, no test/bench) needs SHA-256. Adds
`WOLFSSL_SHA512`/`WOLFSSL_NOSHA512_224`/`WOLFSSL_NOSHA512_256` if
not already set (SHA-384 is the truncation of SHA-512).

### 5.13. `hash_sha3.h`

**Activation.** `WOLFBOOT_HASH_SHA3_384`.

**Sets:** `WOLFSSL_SHA3`. Defines `NO_SHA256` under the same
"no-other-consumer" condition.

### 5.14. `encrypt.h`

**Activation.** Always included; bodies gate internally on
`EXT_ENCRYPTED` and `SECURE_PKCS11`.

**Sets:**
- `EXT_ENCRYPTED` branch: `HAVE_PWDBASED`
- `SECURE_PKCS11` branch: full PKCS#11 token support
  (`HAVE_PWDBASED`, `HAVE_PBKDF2`, `WOLFPKCS11_*`, `HAVE_AESCTR`,
  `WOLFSSL_AES_GCM`, `GCM_TABLE_4BIT`, `WOLFSSL_AES_128`, `HAVE_SCRYPT`,
  `HAVE_AESGCM`, `HAVE_PKCS8`)
- Always: `HAVE_PKCS11_STATIC`

**Notes.** The `NO_PWDBASED` fallback (when `HAVE_PWDBASED` is not set)
lives in `finalize.h`, not here, because it must run after
`trustzone.h` and `tpm.h` have had a chance to opt in.

### 5.15. `trustzone.h`

**Activation.** Bodies gate on `WOLFCRYPT_SECURE_MODE`,
`WOLFCRYPT_TZ_PSA`, and `WOLFBOOT_TZ_FWTPM`.

**Sets under `WOLFCRYPT_SECURE_MODE`:**
- Custom RNG seed callback (`hal_trng_get_entropy` →
  `CUSTOM_RAND_GENERATE_SEED`)
- `WOLFSSL_AES_CFB`

**Sets under `WOLFCRYPT_TZ_PSA`:** broad PSA cipher / hash / AEAD /
PRF support — `WOLFSSL_AES_COUNTER`, `WOLFSSL_AES_GCM`, `HAVE_AESGCM`,
`HAVE_AESCCM`, `HAVE_AES_ECB`, `WOLFSSL_AES_CFB`, `WOLFSSL_AES_OFB`,
`HAVE_CHACHA`, `HAVE_POLY1305`, `WOLFSSL_CMAC`,
`WOLFSSL_ECDSA_DETERMINISTIC_K`, `WOLFSSL_HAVE_PRF`, `HAVE_HKDF`,
`HAVE_PBKDF2`, `HAVE_PWDBASED`, `WOLFSSL_KEY_GEN`, `WC_RSA_PSS`,
`WOLFSSL_PSS_SALT_LEN_DISCOVER`, `WOLFSSL_RSA_OAEP`,
`HAVE_ECC_KEY_EXPORT`, `HAVE_ECC_KEY_IMPORT`. Also forces `NO_DES3`.

**Sets under `WOLFBOOT_TZ_FWTPM`:** `WOLFSSL_AES_CFB`, `WOLFSSL_SHA384`.

**Notes.** `WOLFBOOT_NEEDS_CMAC` and `WOLFBOOT_NEEDS_KDF` for
`WOLFCRYPT_TZ_PSA`/`WOLFBOOT_TZ_FWTPM` are declared in `cascade.h`,
not here.

### 5.16. `tpm.h`

**Activation.** `WOLFBOOT_TPM && !WOLFBOOT_TZ_FWTPM`. (FWTPM uses a
firmware-TPM in the secure world and has its own configuration in
`trustzone.h`.) `WOLFBOOT_TPM_PARMENC` is derived in `cascade.h`.

**Sets:**
- `WOLFTPM2_NO_HEAP`
- Small-stack budget when `WOLFTPM_SMALL_STACK`:
  `MAX_COMMAND_SIZE 1024`, `MAX_RESPONSE_SIZE 1350`,
  `WOLFTPM2_MAX_BUFFER 1500`, `MAX_SESSION_NUM 2`,
  `MAX_DIGEST_BUFFER 973`
- `WOLFBOOT_TPM_PARMENC` branch: `WOLFSSL_AES_CFB`, `WOLFSSL_PUBLIC_MP`,
  `CUSTOM_RAND_GENERATE_SEED` stub, `WC_RNG_SEED_CB`
- `WOLFTPM_MMIO` branch: `WOLFTPM_ADV_IO`, `XTPM_WAIT()`
- `HASH_COUNT 3`
- TPM `printf` redirect when `DEBUG_WOLFTPM` and not `ARCH_SIM`

### 5.17. `wolfhsm.h`

**Activation.** `WOLFBOOT_ENABLE_WOLFHSM_CLIENT ||
WOLFBOOT_ENABLE_WOLFHSM_SERVER`.

**Sets:**
- `WOLF_CRYPTO_CB`
- `HAVE_ANONYMOUS_INLINE_AGGREGATES 1` (overrides the `0` default in
  `base.h`)
- `WOLFSSL_KEY_GEN`

### 5.18. `cert_chain.h`

**Activation.** `WOLFBOOT_ENABLE_WOLFHSM_SERVER &&
WOLFBOOT_CERT_CHAIN_VERIFY`. This is the only build mode that links
the wolfSSL TLS-layer cert manager (server side).

**Sets:** `NO_TLS`, `NO_OLD_TLS`, `WOLFSSL_NO_TLS12`,
`WOLFSSL_USER_IO`, `WOLFSSL_SP_MUL_D`, `WOLFSSL_PEM_TO_DER`,
`WOLFSSL_ALLOW_NO_SUITES`.

**Notes.** The companion `WOLFCRYPT_ONLY` carve-out (which un-defines
`WOLFCRYPT_ONLY` in this mode) still lives in `base.h` as part of the
foundation.

### 5.19. `renesas.h`

**Activation.** `WOLFBOOT_RENESAS_TSIP || WOLFBOOT_RENESAS_RSIP ||
WOLFBOOT_RENESAS_SCEPROTECT`.

**Sets:** `WOLFBOOT_SMALL_STACK`, `WOLF_CRYPTO_CB`,
`WOLF_CRYPTO_CB_ONLY_ECC`, `WOLF_CRYPTO_CB_ONLY_RSA`,
`WOLFSSL_NO_SW_MATH`, `MAX_CRYPTO_DEVID_CALLBACKS 2`,
`WC_NO_DEFAULT_DEVID`, `WOLFSSL_AES_SMALL_TABLES`. Per-flavor
(`TSIP` / `RSIP` / `SCEPROTECT`): adds the corresponding wolfCrypt
Renesas port flags and key-storage addresses.

### 5.20. `platform.h`

**Activation.** Multiple internal gates.

**Sets:**
- `WOLFSSL_HAVE_SP_ECC || WOLFSSL_HAVE_SP_RSA` (set by SIGN
  fragments): SP math word-size selection per arch
  (`WOLFSSL_SP_ARM64_ASM`, `WOLFSSL_SP_X86_64_ASM`, `SP_WORD_SIZE`,
  `HAVE___UINT128_T`, `ULLONG_MAX`)
- `__QNX__`: `WOLFSSL_HAVE_MIN`, `WOLFSSL_HAVE_MAX`
- `WOLFSSL_STM32_PKA`: `HAVE_UINTPTR_T`

### 5.21. `test_bench.h`

**Activation.** Bodies gate on `WOLFCRYPT_TEST` and
`WOLFCRYPT_BENCHMARK`.

**Sets when `WOLFCRYPT_TEST`:** `NO_CRYPT_TEST_EXTENDED`,
`USE_CERT_BUFFERS_256`, undefines `NO_CRYPT_TEST`. `NO_CRYPT_TEST` is
defined when test mode is *not* active.

**Sets when `WOLFCRYPT_BENCHMARK`:** `BENCH_EMBEDDED`, undefines
`NO_CRYPT_BENCHMARK`. `NO_CRYPT_BENCHMARK` is defined when bench mode
is *not* active.

**Sets when either is active:** `NO_WRITE_TEMP_FILES`,
`WOLFSSL_LOG_PRINTF`, static memory pool, `WOLFSSL_SP_MUL_D`,
`WOLFSSL_USER_CURRTIME`, `XTIME my_time`, `HAVE_AESGCM`, `GCM_TABLE`,
plus the LPC55S69 vs custom-RNG split (LPC55S69 path enables
`HAVE_AES_ECB`/`AES_OFB`/`AES_CFB`/`AES_COUNTER` and SHA-256/384/512;
non-LPC path defines `WC_NO_HASHDRBG` and a `my_rng_seed_gen` custom
RNG).

### 5.22. `finalize.h`

**Purpose.** Single reconciliation point. See [Section 7](#7-finalizeh-detail).

---

## 6. `cascade.h` Detail

`cascade.h` runs first and consists of three sections:

### 6.1. Feature-flag cascades

Lift Make-side feature implications into header-side cascades so an
IDE-only build (which sets only the high-level flags) sees the same
derived flags that `options.mk` would emit.

```c
/* Any TPM-using feature implies WOLFBOOT_TPM. */
#if defined(WOLFBOOT_TPM_VERIFY) || defined(WOLFBOOT_MEASURED_BOOT) || \
    defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
#  ifndef WOLFBOOT_TPM
#    define WOLFBOOT_TPM
#  endif
#endif

/* TPM keystore and seal both require TPM session parameter encryption. */
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
#  ifndef WOLFBOOT_TPM_PARMENC
#    define WOLFBOOT_TPM_PARMENC
#  endif
#endif
```

### 6.2. `WOLFBOOT_NEEDS_RSA`

Tested by `cascade.h`'s own `WOLFBOOT_NEEDS_ASN` block, set on any
RSA SIGN flag (primary or secondary, RSA or RSAPSS) or
`WOLFCRYPT_SECURE_MODE && !PKCS11_SMALL`. Mirrored in
`sign_dispatch.h`'s own gate for including `sign_rsa.h`'s positive
configuration.

### 6.3. `WOLFBOOT_NEEDS_*` declarations

The full positive-intent vocabulary, declared from feature-flag tests.
See the [Marker Reference](#4-marker-reference) for the conditions per
marker.

The cascade.h structure is intentionally one-pass: every block tests
already-known flags (high-level `WOLFBOOT_*` or `WOLFCRYPT_*` flags
plus the derived `WOLFBOOT_TPM` and `WOLFBOOT_TPM_PARMENC`) and emits
markers. No fragment downstream needs to read another fragment's
internal state.

---

## 7. `finalize.h` Detail

`finalize.h` runs last and contains four sections:

### 7.1. NEEDS_* reconciliation

Each negative wolfCrypt flag is gated by the absence of the matching
`WOLFBOOT_NEEDS_*` marker. Two markers are bipolar:

```c
/* HASHDRBG: positive when needed, WC_NO_HASHDRBG otherwise. */
#ifdef WOLFBOOT_NEEDS_HASHDRBG
#  ifndef HAVE_HASHDRBG
#    define HAVE_HASHDRBG
#  endif
#else
#  ifndef WC_NO_HASHDRBG
#    define WC_NO_HASHDRBG
#  endif
#endif

/* BASE64 / NO_CODING. */
#ifdef WOLFBOOT_NEEDS_BASE64
#  define WOLFSSL_BASE64_ENCODE
#else
#  define NO_CODING
#endif
```

The rest are unipolar:

```c
#ifndef WOLFBOOT_NEEDS_RNG
#  define WC_NO_RNG
#endif
#ifndef WOLFBOOT_NEEDS_AES
#  define NO_AES
#endif
/* ... */
```

`HAVE_PWDBASED` does not have a NEEDS marker because no fragment outside
`encrypt.h` / `trustzone.h` opts in. `finalize.h` simply checks
`#ifndef HAVE_PWDBASED: #define NO_PWDBASED`.

### 7.2. Always-on disables

A flat list of wolfCrypt features that wolfBoot never uses, regardless
of configuration:

```
NO_DH, WOLFSSL_NO_PEM, NO_ASN_TIME, NO_RC4, NO_SHA, NO_DSA, NO_MD4,
NO_RABBIT, NO_MD5, NO_SIG_WRAPPER, NO_CERT, NO_SESSION_CACHE, NO_HC128,
NO_DES3 (if not already set), NO_WRITEV, NO_FILESYSTEM (unless
WOLFBOOT_PARTITION_FILENAME), NO_MAIN_DRIVER, NO_OLD_RNGNAME,
NO_WOLFSSL_DIR, WOLFSSL_NO_SOCK, WOLFSSL_IGNORE_FILE_WARN,
NO_ERROR_STRINGS, NO_PKCS12, NO_PKCS8, NO_CHECK_PRIVATE_KEY
```

If a future feature ever needs one of these to be skipped (e.g. some
new TLS-using mode wants `NO_PEM` skipped), the entry should move
under a new NEEDS marker.

### 7.3. `BENCH_EMBEDDED` default

Outside of explicit `WOLFCRYPT_TEST`/`WOLFCRYPT_BENCHMARK` mode,
`BENCH_EMBEDDED` is set as a small-target sizing hint. Test/bench mode
sets it itself in `test_bench.h` and overrides this default.

### 7.4. Memory model

Two-branch policy:

- **`WOLFBOOT_SMALL_STACK`** sets `WOLFSSL_SMALL_STACK`. Errors out if
  combined with `WOLFBOOT_HUGE_STACK`.
- **else:** when SP math is in use, sets `WOLFSSL_SP_NO_MALLOC` and
  `WOLFSSL_SP_NO_DYN_STACK`. Then, **unless `WOLFBOOT_NEEDS_MALLOC`**
  is set, defines `NO_WOLFSSL_MEMORY` and `WOLFSSL_NO_MALLOC` for a
  pure-stack wolfCrypt with no heap. `WOLFBOOT_NEEDS_MALLOC` is
  declared by `cascade.h` for `SECURE_PKCS11`, `WOLFCRYPT_TZ_PSA`,
  `WOLFBOOT_ENABLE_WOLFHSM_SERVER`, and test/bench.

---

## 8. Adding a New Feature

This cookbook walks through adding a hypothetical 
`WOLFBOOT_NEW_FEAT=1` feature that needs RNG, AES and ASN.1 parsing. Replace the
names with your actual feature throughout.

### Step 1: Add the high-level feature flag

In `options.mk`, add:

```make
ifeq ($(WOLFBOOT_NEW_FEAT),1)
  CFLAGS+=-D"WOLFBOOT_ENABLE_NEW_FEAT"
  # Add any object files / library paths the feature needs
  WOLFCRYPT_OBJS+=...
endif
```

### Step 2: Create a fragment

Create `include/user_settings/new_feat.h`:

```c
#ifndef _WOLFBOOT_USER_SETTINGS_NEW_FEAT_H_
#define _WOLFBOOT_USER_SETTINGS_NEW_FEAT_H_

#ifdef WOLFBOOT_ENABLE_NEW_FEAT
   /* Positive flags this feature needs directly. */
#  define WOLF_CRYPTO_CB
#  define WOLFSSL_KEY_GEN
   /* anything else specific to this feature... */
#endif

#endif
```

### Step 3: Wire the fragment into the shim

In `include/user_settings.h`, add it among the feature fragments:

```c
#include "user_settings/wolfhsm.h"
#include "user_settings/new_feat.h"  /* <-- add */
#include "user_settings/cert_chain.h"
```

### Step 4: Declare NEEDS_* markers

If the feature needs RNG, AES, and ASN.1, add to `cascade.h`'s
declarations:

```c
/* NEEDS_RNG: ... */
#if defined(WOLFBOOT_TPM_PARMENC) || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(WOLFCRYPT_TEST) || \
    defined(WOLFCRYPT_BENCHMARK) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) || \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
     defined(WOLFBOOT_SIGN_ML_DSA)) || \
    defined(WOLFBOOT_ENABLE_NEW_FEAT)        /* <-- add */
#  ifndef WOLFBOOT_NEEDS_RNG
#    define WOLFBOOT_NEEDS_RNG
#  endif
#endif
```

…and the same for `WOLFBOOT_NEEDS_AES` and `WOLFBOOT_NEEDS_ASN`.

You do **not** need to touch `finalize.h` here — its `#ifndef
WOLFBOOT_NEEDS_*` blocks already do the right thing once the markers
are declared.

### Step 5: Introducing a new NEEDS_* marker (negative-polarity wolfCrypt feature)

Skip this step if the wolfCrypt capabilities your feature needs are
already covered by an existing marker (Step 4 above). This step
applies only when **your feature needs a wolfCrypt capability that is
currently disabled by a negative flag without a NEEDS_* marker** — a
case that requires extending the marker vocabulary.

#### When this case applies

Recall the polarity rules from [Section 3.1](#31-why-it-exists):

- **Positive flags** (`HAVE_*`, `WOLFSSL_*`) compose trivially. If your
  feature wants `HAVE_PKCS7` or `WOLFSSL_AES_OFB`, just `#define` it
  in your fragment header. Done. No NEEDS marker needed.

- **Negative flags** (`NO_*`, `WC_NO_*`) don't compose. If your
  feature wants Diffie-Hellman, but `NO_DH` is currently defined
  unconditionally in `finalize.h`'s always-on disables block, then
  `#define HAVE_DH` from your fragment is *not enough* — `NO_DH` will
  still be defined later (since `finalize.h` runs last) and wolfCrypt
  will strip DH regardless of `HAVE_DH`. You need to make `NO_DH`
  conditional, which means adding a `WOLFBOOT_NEEDS_DH` marker to the
  vocabulary.

The tell that you're in this case: you `#define HAVE_X` in your
fragment, build, and find that the wolfCrypt feature is still
stripped from the binary. Look at `finalize.h` — there will be a
`#define NO_X` (or `#define WC_NO_X`) in the always-on disables block.

#### Worked example: adding `WOLFBOOT_NEEDS_DH`

Imagine the new feature `WOLFBOOT_NEW_FEAT` needs Diffie-Hellman.
`NO_DH` lives in `finalize.h`'s always-on block today.

**1. Identify and confirm the polarity.** Grep for the disable:

```bash
grep -nE 'NO_DH|HAVE_DH' include/user_settings/*.h
```

Verify it's set unconditionally in `finalize.h`'s always-on block,
not in a fragment. (If a fragment sets it, the rules from Section 2.3
say fragments shouldn't `#undef`, so the right fix is still to lift
it into `finalize.h` under a NEEDS marker.)

**2. Declare the marker in `cascade.h`.** Add a new `WOLFBOOT_NEEDS_DH`
block alongside the existing markers:

```c
/* NEEDS_DH: features that use Diffie-Hellman key exchange. */
#if defined(WOLFBOOT_ENABLE_NEW_FEAT)
#  ifndef WOLFBOOT_NEEDS_DH
#    define WOLFBOOT_NEEDS_DH
#  endif
#endif
```

If your feature can't be expressed in cascade.h alone (for example,
it has an internal sub-condition that depends on other fragment
state), declare the marker from your fragment header instead. Either
location works; cascade.h is preferred when the trigger is a clean
test of high-level feature flags.

**3. Lift the disable in `finalize.h`.** Move the `#define NO_DH`
line out of the always-on disables block and into the NEEDS-gated
section:

```c
/* In finalize.h's NEEDS_* reconciliation section, alongside the
 * existing #ifndef WOLFBOOT_NEEDS_* blocks: */
#ifndef WOLFBOOT_NEEDS_DH
#  define NO_DH
#endif
```

Delete the now-redundant `#define NO_DH` from `finalize.h`'s always-on
list.

**4. Set the matching positive flag in your fragment.** Some wolfCrypt
features only get linked in when both the negative is *not* set and a
positive flag is set. For DH:

```c
/* In include/user_settings/new_feat.h */
#ifdef WOLFBOOT_ENABLE_NEW_FEAT
#  define HAVE_DH
   /* and any DH-specific tuning your feature needs... */
#endif
```

You'll know whether a positive flag is needed by checking wolfCrypt's
`wolfssl/wolfcrypt/settings.h` for how the feature is gated. Some
features (like `NO_AES`) gate purely on the negative flag — clearing
the negative is sufficient. Others (like DH) require both: clearing
`NO_DH` *and* setting `HAVE_DH`.

**5. Update [Section 4](#4-marker-reference) of this document.** Add
a row to the marker reference table for the new marker, listing the
disable it gates, the conditions that declare it, and where it's
declared. Also extend any other relevant sections (the cascade.h
walkthrough in Section 6, the fragment reference in Section 5 if
applicable).

**6. Verify byte-identity for unaffected configs.** This is critical
when extending the vocabulary. Build a representative set of canary
configs *without* `WOLFBOOT_NEW_FEAT` set (e.g. `sim.config`,
`sim-tpm-seal.config`, `stm32h5-tz-psa.config`) before and after your
changes. The binaries should be byte-identical when debug info and
build-id are stripped — none of them declare `WOLFBOOT_NEEDS_DH`, so
`NO_DH` still gets defined, and the resulting wolfCrypt is the same
as before. If a canary config diverges, you've either set the marker
too aggressively in cascade.h or moved more than just the one
`#define NO_DH` line out of the always-on block.

#### Anti-patterns

- **Don't `#undef NO_X` from a fragment.** This breaks the
  fragment-author rule from Section 2.3 (fragments only `#define`).
  It also makes the disable harder to find when reading the code:
  someone grepping for `NO_DH` in `finalize.h` would be misled. Use a
  marker instead.

- **Don't gate the marker on a flag that only your fragment knows
  about** unless the flag is unique to your feature. If `WOLFBOOT_TPM`
  needs DH too (hypothetically), the marker should mention TPM in its
  trigger list — declared in cascade.h — not be set indirectly through
  your fragment's internal logic.

- **Don't add the marker only to fragments without also lifting the
  disable in `finalize.h`.** The marker is inert on its own; it
  becomes effective when `finalize.h` reads it. Both halves of the
  edit must land together.

### Step 6: Verify

Build a config that exercises the new feature. Confirm the build
links and the binary size is sensible.


## 9. Reading an Existing Configuration

To understand what wolfCrypt config a given `.config` produces:

### 9.1. Full preprocessor dump

The most exhaustive view:

```bash
cp config/examples/<name>.config .config
make clean
make CFLAGS_EXTRA='-E -dM' wolfboot.elf 2>&1 | grep -E '^#define (WOLFSSL_|HAVE_|NO_|WC_NO_|WOLFBOOT_)' | sort
```

This shows every `#define` that wolfCrypt sees, including everything
emitted by `options.mk`, `cascade.h`, the active fragments, and
`finalize.h`.

### 9.2. Marker-level view

For a more focused view of just the `WOLFBOOT_NEEDS_*` markers (which
features are opting in) and the resulting `NO_*`/`WC_NO_*` flags
(which features wolfCrypt strips):

```bash
make CFLAGS_EXTRA='-E -dM' wolfboot.elf 2>&1 \
  | grep -E '^#define (WOLFBOOT_NEEDS_|NO_|WC_NO_)' | sort
```

### 9.3. Tracing why a flag is set

For a specific flag like `NO_AES`:

1. Search `include/user_settings/` for the flag. Anything in
   `finalize.h` is gated by a NEEDS marker; anything in a fragment is
   set by that fragment unconditionally (within its own activation
   gate).
2. If it's gated by `WOLFBOOT_NEEDS_X`, find where the marker is
   declared (almost always `cascade.h`). The condition list shows
   exactly which features opt in.
3. If the flag is set in `options.mk` (rare for purely-wolfCrypt
   flags after the refactor), grep `options.mk` for the flag.

---

## 10. Relationship to options.mk

After the refactor, `options.mk` and `include/user_settings/` have a
clear division of labor.

### 10.1. `options.mk` owns

- **WOLFCRYPT_OBJS selection.** The linker input set. This is the only
  surface the preprocessor cannot generate, because `make` chooses
  which `.o` files end up on the link line.
- **High-level `WOLFBOOT_*` flag emission.** Any flag a user sets in
  `.config` becomes a `-D` flag here.
- **Tool invocation.** `KEYGEN_OPTIONS`, `SIGN_OPTIONS`, keytool
  arguments, `whnvmtool` setup, etc.
- **Mutual-exclusion errors.** `$(error WOLFCRYPT_TZ_PKCS11 and TZ_PSA
  cannot both be set)`-style checks at config time.
- **Math backend selection.** SPMATH vs SPMATHALL vs USE_FAST_MATH —
  this affects both `MATH_OBJS` and `-D` flags. Today still in
  `arch.mk:4-17`.
- **Architecture-specific assembly objects.** `arch.mk` selects
  `sp_arm64.o` vs `sp_c64.o` etc.
- **Parameter values.** `LMS_LEVELS`, `LMS_HEIGHT`, `XMSS_PARAMS`,
  `IMAGE_SIGNATURE_SIZE`, `ML_DSA_LEVEL`, `MEASURED_PCR_A` — these
  come from the user's `.config` and reach the headers as `-D`s.

### 10.2. `include/user_settings/` owns

- **Every wolfCrypt-side `#define`** that's purely a configuration
  choice rather than an *input*. Anything a wolfBoot maintainer would
  set "because TPM+PARMENC needs `WOLFSSL_AES_CFB`" is here.
- **Every `WOLFBOOT_NEEDS_*` marker.**
- **Every `WOLFCRYPT_*` and `NO_*` / `WC_NO_*` decision.**
- **Cascades** (`WOLFBOOT_TPM_VERIFY → WOLFBOOT_TPM`, etc.) so an
  IDE/CMake/Zephyr build does not need a separate Make pass.

### 10.3. What `options.mk` no longer emits

The Phase 5 trim removed wolfCrypt-side `-D` flags from
`LMS_EXTRA` / `XMSS_EXTRA` (the `WOLFSSL_HAVE_LMS`,
`WOLFSSL_LMS_MAX_LEVELS`, etc. now live in `sign_lms.h`/`sign_xmss.h`).
The user-set parameter values (`LMS_LEVELS`, etc.) stay.

Other CFLAGS in `options.mk` (`-DWOLFTPM_SMALL_STACK`,
`-DWOLF_CRYPTO_CB`, `-DWOLFHSM_CFG_ENABLE_CLIENT`, etc.) could in
principle be moved into the matching fragments. They are left in
`options.mk` for now because: (a) they are mostly side-effect free
(positive-flag duplication is harmless under `#ifndef`), and (b) some
of them are also consumed by *non-wolfCrypt* code (wolfBoot's own
sources include the wolfHSM client, for example), where the Make-side
emission keeps them visible to those compilation units.

---

## 11. Alternative Build Paths (CMake, IDE, Zephyr)

The header-only configuration scheme means alternative build paths
need only:

1. Set the high-level `WOLFBOOT_*` flags (e.g. `WOLFBOOT_SIGN_ECC256`,
   `WOLFBOOT_HASH_SHA256`, `WOLFBOOT_TPM_KEYSTORE`).
2. Pass `-DWOLFSSL_USER_SETTINGS` to the compiler.
3. Add `include/` to the include path.
4. Compile the right `WOLFCRYPT_OBJS` set (the linker input).

Steps 1-3 produce the correct wolfCrypt configuration via the cascade
and fragments. Step 4 is the only one that still needs build-system
involvement, because it's a linker concern.

### 11.1. CMake

`CMakeLists.txt` already sets `-DWOLFSSL_USER_SETTINGS` and adds
`include/` to the include path. `cmake/config_defaults.cmake` and
`lib/CMakeLists.txt` contain a copy of the `WOLFCRYPT_OBJS` selection
logic; that piece is independent of this refactor and could be
trimmed in a future cleanup.

### 11.2. IDE projects (Renesas, MPLAB, Keil, IAR, etc.)

The Renesas IDE projects under `IDE/Renesas/e2studio/` ship their own
hand-maintained copy of `user_settings.h` today. The refactor does
not touch those — they continue to work as before. New IDE projects
should adopt the shim pattern: have the IDE define the high-level
`WOLFBOOT_*` flags in its preprocessor settings and let the project
include `include/user_settings.h` (the shim) directly. The IDE then
gets the same configuration the Make build does.

### 11.3. Zephyr

`zephyr/Kconfig` maps user choices in the Zephyr menuconfig to `-D`
flags via `CONFIG_WOLFBOOT_SIGN_ALG` / `CONFIG_WOLFBOOT_SIGN_HASH`.
Those `-D`s feed the cascade and fragments unchanged. No Zephyr-side
modifications are needed for this refactor.

---

## 12. Compatibility Notes

### 12.1. For users of `.config` files

No change. The `.config` flags (`SIGN`, `HASH`, `WOLFBOOT_TPM_*`,
`WOLFHSM_*`, etc.) work the same way they always have. `options.mk`
translates them into the same `-D` flags as before; the headers now
do more of the work that `options.mk` used to do, but the user-facing
flags are unchanged.

### 12.2. For users of the `WOLFBOOT_*` defines

Backwards-compatible. Every public flag (`WOLFBOOT_SIGN_*`,
`WOLFBOOT_HASH_*`, `WOLFBOOT_TPM`, `WOLFBOOT_ENABLE_WOLFHSM_*`, etc.)
still works as before. The `WOLFBOOT_NEEDS_*` namespace is internal
and should not be defined by users; it is reserved for cascade /
fragment internals.

### 12.3. For users with a custom `user_settings.h`

If you maintain your own `user_settings.h` (an IDE or downstream
fork), the file you copied previously was 781 lines. The new shim is
73 lines, but it produces the same wolfCrypt configuration when the
same `WOLFBOOT_*` flags are set. Two options:

- **Adopt the shim.** Replace your copy with the shim and pull the
  fragment headers from `include/user_settings/`. Keep your own
  `target.h` for board-specific addresses. Future maintenance benefits
  from any fragment improvements.
- **Keep your copy.** The old monolithic file still produces the same
  binary if it sees the same input flags. There is no upgrade
  pressure on existing IDE projects until they need a feature added
  after the refactor landed.

---

## Appendix A: File Layout

```
include/
├── user_settings.h               # 73-line shim
└── user_settings/
    ├── base.h                    # foundation defines
    ├── cascade.h                 # feature-flag cascades + NEEDS markers
    │
    ├── sign_dispatch.h           # SIGN family dispatch
    ├── sign_ecc.h
    ├── sign_rsa.h
    ├── sign_ed25519.h
    ├── sign_ed448.h
    ├── sign_ml_dsa.h
    ├── sign_lms.h
    ├── sign_xmss.h
    │
    ├── hash_dispatch.h           # HASH family dispatch
    ├── hash_sha384.h
    ├── hash_sha3.h
    │
    ├── encrypt.h                 # EXT_ENCRYPTED, SECURE_PKCS11
    ├── trustzone.h               # SECURE_MODE, TZ_PSA, TZ_FWTPM
    ├── tpm.h                     # WOLFBOOT_TPM
    ├── wolfhsm.h                 # WOLFHSM client/server
    ├── cert_chain.h              # CERT_CHAIN_VERIFY (server side)
    ├── renesas.h                 # TSIP / RSIP / SCEPROTECT
    ├── platform.h                # SP-math word size, QNX, STM32 PKA
    ├── test_bench.h              # WOLFCRYPT_TEST / BENCHMARK
    │
    └── finalize.h                # NEEDS_* reconciliation + global "off"
```

## Appendix B: Glossary

- **Fragment.** A header file in `include/user_settings/` that
  contributes wolfCrypt configuration for one wolfBoot concern (a SIGN
  family, a feature block, etc.).
- **Shim.** `include/user_settings.h`. The file wolfSSL loads via
  `-DWOLFSSL_USER_SETTINGS`. Contains only `#include`s.
- **Cascade.** A preprocessor implication that derives one flag from
  another (e.g. `WOLFBOOT_TPM_VERIFY → WOLFBOOT_TPM`). Lives in
  `cascade.h`.
- **NEEDS marker.** A `WOLFBOOT_NEEDS_*` macro declared by a fragment
  or by `cascade.h` to express positive intent. `finalize.h` reads
  the absence of a marker and applies the corresponding wolfCrypt
  negative flag.
- **Positive flag.** A wolfCrypt feature flag that is off by default
  and turned on by `#define`-ing it (`HAVE_*`, `WOLFSSL_*`).
- **Negative flag.** A wolfCrypt feature flag that is on by default
  and turned off by `#define`-ing it (`NO_*`, `WC_NO_*`).
- **Reconciliation.** The process by which `finalize.h` translates the
  set of declared NEEDS markers into the corresponding negative
  flags.
