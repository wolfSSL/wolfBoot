# SUIT Manifest Support (experimental)

wolfBoot can verify and process **SUIT manifests**
(`draft-ietf-suit-manifest-34`) as an alternative to its native TLV-signed image
header. SUIT is an IETF-standard, CBOR-encoded, COSE-signed manifest that
describes a firmware update: the image digest, the target device identity
(vendor/class), and the install command sequence.

This is **off by default** and gated behind `WOLFBOOT_SUIT`. The native TLV path
is unchanged when it is not enabled. SUIT is intended for richer, networked
secure update (e.g. wolfUpdate); the lean TLV path remains the default for plain
single-image secure boot.

## What it provides

- **Authenticity + integrity**: the manifest is signed with COSE_Sign1
  (`wc_CoseSign1_Verify`, via the `lib/wolfCOSE` submodule), and the signed
  `SUIT_Digest` is bound to `hash(manifest)`.
- **Image binding**: `suit-condition-image-match` hashes the staged component and
  compares it to the digest the manifest authorizes.
- **Identity checks**: `suit-condition-vendor-identifier` /
  `-class-identifier` against this device's configured identity.

## Build

```sh
make WOLFBOOT_SUIT=1 SIGN=ECC256 ...
```

The `WOLFBOOT_SUIT` block in `options.mk` adds the SUIT sources plus the wolfCOSE
verify objects (lean, `WOLFCOSE_LEAN_VERIFY`). Fine-tuning macros:

| Macro | Effect |
| --- | --- |
| `SUIT_INSTALL_HANDOFF` (default) | verify then drive the existing A/B swap |
| `SUIT_INSTALL_DIRECTIVES` | SUIT copy/write directives drive flash directly |
| `SUIT_HAVE_ENCRYPTION` | decrypt COSE_Encrypt0 payloads on install (AES-GCM); enables AES-GCM in the build |
| `SUIT_DEVICE_VENDOR_ID` / `SUIT_DEVICE_CLASS_ID` | this device's identity (brace initializers) for the vendor/class conditions |
| `SUIT_KEY_SLOT` | fallback trust-anchor slot when the COSE_Sign1 carries no key id |
| `SUIT_HAVE_FETCH` | enable directive-fetch; the host supplies an `ops->fetch` callback that retrieves the payload by uri (e.g. wolfUpdate transport) |
| `SUIT_HAVE_REPORT` | build `suit_report_encode()`, a compact `{ result, sequence }` status record an update server reads to learn the outcome |
| `SUIT_HAVE_TRY_EACH` / `SUIT_HAVE_RUN_SEQUENCE` | optional commands |

## Architecture

The processor is host-agnostic: it never touches flash directly. Storage access
is via a pluggable `struct suit_component_ops` (hash/write/copy) the host
supplies. In wolfBoot those wrap the flash HAL; the host unit test wraps a RAM
buffer. This keeps the SUIT code reusable outside wolfBoot.

- `suit_open()`: parse the envelope + manifest (zero-copy offsets).
- `suit_verify_auth()`: COSE_Sign1 + digest binding.
- `suit_process()`: command-sequence interpreter (conditions/directives).

## Boot-time dispatch

When `WOLFBOOT_SUIT` is enabled, `wolfBoot_verify_authenticity` (and the open /
integrity path) detect whether a partition holds a wolfBoot TLV image
(`WOLFBOOT_MAGIC`) or a SUIT envelope (CBOR), and route the SUIT case to the SUIT
path automatically; the TLV path is untouched. The layout is concatenated
`[envelope][image]`: the manifest is authenticated, `image-match` hashes the
image that follows the envelope, and on success the image is exposed as
`fw_base`/`fw_size` for the existing A/B swap. The TLV path remains the default
when the macro is off.

## Test

Three layers (A self, B interop, C frozen):

```sh
# A: author a full signed SUIT envelope with wolfCOSE and run the whole chain
#    (parse, verify, identity-validate, install via directive-write, image-match)
#    plus tamper cases. Also writes /tmp/suit_envelope.cbor.
WOLFSSL_DIR=/usr/local ./tests/suit_host_test.sh

# B: cross-check that envelope with implementations other than wolfCOSE
#    (cbor2 + hashlib + cryptography), the interop step.
# C: by default it validates the committed frozen vector tests/vectors/.
python3 tests/suit_cross_check.py            # pip install cbor2 cryptography
```

A needs a host wolfSSL with ECC (sign+verify) and the `lib/wolfCOSE` submodule
(`git submodule update --init lib/wolfCOSE`).

## Compliance

The CBOR/COSE structures and all integer codes follow draft-34 (cross-checked
against the IANA SUIT registry). This implements the minimal "trusted
invocation" profile: the codes/conditions/directives listed above. It is
**format-compliant for that profile and interop-verified** against independent
CBOR/COSE tooling (test B); it is not a full draft-34 implementation.

- Unrecognized (or known-but-unsupported) commands are **default-denied** (the
  sequence fails), as a SUIT processor must, rather than silently skipped.
- Optional, built only when their macro is set: directive-fetch
  (`SUIT_HAVE_FETCH`, via a host callback) and a compact status report
  (`SUIT_HAVE_REPORT`, not the full draft-suit-report COSE attestation).
- Not implemented (and rejected if present): severable members,
  try-each / run-sequence / swap, dependencies/trust-domains.

## Status

Implemented + host-tested + interop cross-checked: parse, COSE_Sign1 verify +
digest binding, the command interpreter (identity + image-match conditions,
set-component-index / override-parameters / write / copy directives), default
deny, **payload decryption on install (COSE_Encrypt0 / AES-GCM) for
confidentiality** (`SUIT_HAVE_ENCRYPTION`), the `wolfBoot_suit_verify()` entry
point, and **boot-time auto-dispatch** (format detection from
`wolfBoot_verify_authenticity`, concatenated `[envelope][image]` layout, handoff
to the A/B swap).

Security hardening: **anti-rollback** (rejects a manifest whose
`sequence-number` is below the running version), **image bounds** (rejects an
image or content larger than the partition space, and an out-of-range component
index), and **key-id selection** (the COSE_Sign1 `kid` picks the trust anchor
via the keystore, like the TLV path's pubkey hint).

Networked update support: `directive-fetch` (`SUIT_HAVE_FETCH`) retrieves the
payload by uri through a host callback, and `suit_report_encode()`
(`SUIT_HAVE_REPORT`) emits a compact status record, so a server (e.g. wolfUpdate)
can pull images and learn outcomes. Remaining optional commands: `try-each` /
`run-sequence` / `swap`.

Production readiness: this feature is experimental and off by default. Before
enabling it in a shipping product the gate is, at minimum: fuzz the manifest
parser and complete a security review (the manifest is attacker-controlled),
hardware-test the boot/swap path, and provision the content-encryption key by
key-wrap rather than handing it in raw. Encryption (`SUIT_HAVE_ENCRYPTION`) is
not production-ready until the key-wrap step exists.

This PR is gated on the wolfCOSE fixes in wolfSSL/wolfCOSE PR #53; the submodule
is pinned to that work and should be repinned to the wolfCOSE v1.0 tag before
merge.
