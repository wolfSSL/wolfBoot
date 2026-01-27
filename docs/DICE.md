# DICE Attestation

This document describes the DICE-based PSA attestation service in wolfBoot,
including the token format, keying options, and how to access the service from
non-secure code.

## Protocol overview

wolfBoot implements the PSA Certified Attestation API and emits a
COSE_Sign1-wrapped EAT token following the PSA Attestation Token profile.
The minimum claim set includes:

- Nonce binding (challenge).
- Device identity (UEID).
- Implementation ID and lifecycle when available.
- Measured boot components for wolfBoot and the boot image.

The attestation token is produced by the secure world service and signed with
an attestation key derived by DICE or supplied as a provisioned IAK.

## Implementation summary

The implementation lives under `src/dice/` and is shared across targets. The
service is invoked through the PSA Initial Attestation API and builds the
COSE_Sign1 token using a minimal CBOR encoder.

- Claim construction and COSE_Sign1 encoding: `src/dice/dice.c`.
- PSA Initial Attestation service dispatch: `src/arm_tee_psa_ipc.c`.
- NSC wrappers for the PSA Initial Attestation API: `zephyr/src/arm_tee_attest_api.c`.

Measured boot claims reuse the image hashing pipeline already used by
wolfBoot to validate images. Component claims include a measurement type,
measurement value, and a description string.

## Keying model

wolfBoot supports two keying modes selected at build time.

### DICE derived key (default for no provisioning)

- UDS/HUK-derived secret is fetched via `hal_uds_derive_key()`.
- CDI and signing key material are derived deterministically using HKDF.
- The attestation keypair is derived deterministically and used to sign the
  COSE_Sign1 payload.

This path requires no external provisioning and binds the attestation key to
UDS plus measured boot material.

### Provisioned IAK

If a platform already provisions an Initial Attestation Key (IAK), wolfBoot
can use it directly to sign the token.

The attestation service calls `hal_attestation_get_iak_private_key()` to
retrieve the private key material from secure storage (or a manufacturer
injection flow). The IAK is used instead of the DICE derived key.

## HAL integration (per-target)

These HAL hooks are optional and have weak stubs for non-TZ boards. Target
families must implement the appropriate subset based on hardware support.

- `hal_uds_derive_key(uint8_t *out, size_t out_len)`
  - Returns a device-unique secret (UDS/HUK-derived) for DICE key derivation.
  - Test-only fallback: when `WOLFBOOT_UDS_UID_FALLBACK_FORTEST=1`, targets
    may derive UDS from the device UID for demo purposes. This should not be
    used in production builds.
  - HKDF hash selection follows the configured measurement hash; for
    `WOLFBOOT_HASH_SHA3_384`, HKDF uses SHA3-384 as well.
- `hal_attestation_get_ueid(uint8_t *buf, size_t *len)`
  - Returns a stable UEID. If unavailable, the UEID is derived from UDS.
- `hal_attestation_get_implementation_id(uint8_t *buf, size_t *len)`
  - Optional implementation ID for the token.
- `hal_attestation_get_lifecycle(uint32_t *lifecycle)`
  - Optional lifecycle state for the token.
- `hal_attestation_get_iak_private_key(uint8_t *buf, size_t *len)`
  - Optional provisioned IAK private key (used in IAK mode only).

## STM32H5 OBKeys UDS (optional)

STM32H5 devices provide OBKeys secure storage areas tied to temporal isolation
levels (HDPL). The HDPL1 area is intended for iRoT keys and is the recommended
location for a device-unique UDS when `WOLFBOOT_UDS_OBKEYS=1` is enabled. OBKeys
secure storage is only available on STM32H5 lines except STM32H503.

Provisioning uses STs secure data provisioning flow:

1. Create an OBKeys provisioning file (`.obk`) using STM32TrustedPackageCreator
   (CLI supports `-obk <xml>` input).
2. Program the `.obk` file using STM32CubeProgrammer CLI with the `-sdp` option.

3. After provisioning, move the device to the CLOSED state as appropriate for
   production.

When `WOLFBOOT_UDS_OBKEYS=1`, the STM32H5 HAL first attempts to read UDS from
OBKeys using a platform hook (`stm32h5_obkeys_read_uds`). Integrate this hook
with your RSSe/RSSLib provisioning flow (DataProvisioning API) as described in
ST documentation.

## NSC access (non-secure API)

The non-secure application calls the PSA Initial Attestation API wrappers:

- `psa_initial_attest_get_token_size()`
- `psa_initial_attest_get_token()`

These are provided in `zephyr/include/psa/initial_attestation.h` and are
implemented as NSC calls in `zephyr/src/arm_tee_attest_api.c`.

When `WOLFCRYPT_TZ_PSA=1`, the NS application can also use PSA Crypto through
`zephyr/include/psa/crypto.h` via the NSC dispatch path
(`zephyr/src/arm_tee_crypto_api.c`). PSA Protected Storage uses
`zephyr/include/psa/protected_storage.h` in the same fashion.

## Test application

The STM32H5 TrustZone test application in `test-app/` exercises PSA crypto,
attestation, and store access. It requests a token at boot and can perform
PSA crypto operations from the non-secure side.

See `docs/Targets.md` for the STM32H5 TrustZone scenarios and how to enable
PSA mode.
