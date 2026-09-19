# Secure Application Handoff

`WOLFBOOT_SECURE_APP` tells wolfBoot that the image it boots is a signed
**Secure-state** runtime (for example a TrustZone-M Secure application such as
wolfTrust) rather than a Non-secure application. wolfBoot authenticates and
measures the image, publishes a measured-boot record at a fixed secure-RAM
address, and then branches to the image's reset vector **without leaving Secure
state**.

This is the wolfBoot analog of MCUboot's shared-data area consumed by TF-M: the
first stage measures what it launched and hands the measurement to the runtime.

## Default TrustZone boot vs. the secure-app handoff

| | Default TZ boot | `WOLFBOOT_SECURE_APP` |
| --- | --- | --- |
| Target state | Non-secure | stays Secure |
| Stack / entry | `msp_ns`, `bxns` to the NS vector | `msp`, branch to the Secure reset vector |
| Record | none | measured-boot record written before the jump |

The Secure jump lives in `src/boot_arm.c` under `WOLFBOOT_SECURE_APP`: it turns
the wolfBoot MPU off, sets `MSP`, re-enables interrupts, and branches to the
image entry, leaving the runtime to install its own memory map from its
`Reset_Handler`.

## The measured-boot record

`include/wolfboot/secure_handoff.h` defines the record and a single builder,
`wolfBoot_secure_handoff_build()`. wolfBoot writes it from
`wolfBoot_prepare_secure_handoff()` in `src/update_flash.c` immediately before
`do_boot()`, and panics if the record cannot be built.

| Field | Type | Meaning |
| --- | --- | --- |
| `magic` | `uint32_t` | `0x5742484F`, written **last** as the valid flag |
| `magic_inverse` | `uint32_t` | `~magic`, torn-write guard |
| `version` | `uint16_t` | record layout version (`1`) |
| `size` | `uint16_t` | `sizeof(record)` (56) |
| `lifecycle` | `uint32_t` | PSA lifecycle from `hal_attestation_get_lifecycle()` |
| `image_version` | `uint32_t` | version of the booted image |
| `hash_algorithm` | `uint16_t` | `1` = SHA-256 |
| `measurement_size` | `uint16_t` | digest length (32) |
| `measurement[32]` | `uint8_t` | SHA-256 of the booted image |

The record lives at `WOLFBOOT_SECURE_HANDOFF_ADDRESS`, a secure-RAM address the
port reserves and the config supplies. The builder writes `magic_inverse` and
then `magic` last, fenced with `dmb`/`dsb`, so a consumer that observes `magic`
sees a complete record.

A consumer validates `magic` and `magic_inverse`, checks `version`/`size`, uses
the fields, and then clears the record.

## Enabling it

```
WOLFBOOT_SECURE_APP=1
WOLFBOOT_SECURE_HANDOFF_ADDRESS=0x30020000   # secure-RAM address of the record
```

The handoff requires authenticated boot: `SIGN=NONE`/`WOLFBOOT_NO_SIGN` and
`WOLFBOOT_SKIP_BOOT_VERIFY` are rejected at configure time, and the record
currently requires `HASH=SHA256`. Setting `WOLFBOOT_SECURE_APP` without
`WOLFBOOT_SECURE_HANDOFF_ADDRESS` is a compile-time error.

## Porting the handoff to a new target

The handoff mechanism (record, measurement, Secure jump, build knob) is
target-independent. A new port becomes a clean secure-runtime loader by
supplying three things:

1. **Config only** — `WOLFBOOT_SECURE_APP=1`, `WOLFBOOT_SECURE_HANDOFF_ADDRESS`,
   and the usual `TZEN`/partition layout. No code.
2. **`hal_attestation_get_lifecycle()`** *(optional)* — returns the chip's PSA
   lifecycle. A weak default in `hal/hal.c` reports "unknown", so a port links
   and hands off without it; implement it for a real lifecycle value.
3. **TrustZone setup that keeps the secure app Secure across the jump** — the
   port's own SAU / security-controller code must keep the secure app's flash
   and RAM Secure and must not unsecure peripherals when `WOLFBOOT_SECURE_APP`
   is set. Every SoC's security fabric differs (GTZC, AHBSC, TRDC, …), so this
   is the one piece that is inherently per-port; it is the TrustZone init a port
   already writes, guarded on `WOLFBOOT_SECURE_APP`.

## STM32H5 reference

`config/examples/stm32h5-tz-wolftrust.config` is the reference consumer. On the
STM32H5 the record lives at `0x30020000`, `hal_attestation_get_lifecycle()`
derives the PSA lifecycle from the flash product state and the debug
authentication status (`hal/stm32h5_lifecycle.h`), and `hal/stm32_tz.c` keeps
the whole SRAM1 window and the secure app flash Secure across the handoff.

## Tests

- `tools/unit-tests/unit-secure-handoff.c` — record layout, the builder, and the
  STM32H5 lifecycle mapping.
- `tools/unit-tests/test-secure-handoff-config.sh` — the configure-time guards
  (rejects `SIGN=NONE` and `WOLFBOOT_SKIP_BOOT_VERIFY`) and the STM32H5 stack
  floors.
