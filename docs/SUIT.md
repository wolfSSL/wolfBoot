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
| `SUIT_HAVE_FETCH` / `SUIT_HAVE_TRY_EACH` / `SUIT_HAVE_RUN_SEQUENCE` | optional commands |

## Architecture

The processor is host-agnostic: it never touches flash directly. Storage access
is via a pluggable `struct suit_component_ops` (hash/write/copy) the host
supplies. In wolfBoot those wrap the flash HAL; the host unit test wraps a RAM
buffer. This keeps the SUIT code reusable outside wolfBoot.

- `suit_open()` — parse the envelope + manifest (zero-copy offsets).
- `suit_verify_auth()` — COSE_Sign1 + digest binding.
- `suit_process()` — command-sequence interpreter (conditions/directives).

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
- Not implemented (and rejected if present): directive-fetch, severable members,
  try-each / run-sequence / swap, dependencies/trust-domains, SUIT Reports.

## Status

Implemented + host-tested + interop cross-checked: parse, COSE_Sign1 verify +
digest binding, the command interpreter (identity + image-match conditions,
set-component-index / override-parameters / write / copy directives), default
deny, and the `wolfBoot_suit_verify()` entry point.

Follow-ups: A/B-swap handoff wiring from `wolfBoot_verify_authenticity`, and
payload encryption (COSE_Encrypt0) for confidentiality.

This PR is gated on the wolfCOSE fixes in wolfSSL/wolfCOSE PR #53; the submodule
is pinned to that work and should be repinned to the wolfCOSE v1.0 tag before
merge.
