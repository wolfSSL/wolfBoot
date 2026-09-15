# wolfBoot fwTPM on STM32H5

wolfBoot can host wolfTPM's firmware TPM 2.0 implementation in the secure
TrustZone image and expose it to the non-secure application through the wolfBoot
callable service interface. This lets the non-secure application use the normal
wolfTPM client API while TPM commands are processed inside the secure world.

The feature is intended for STM32H5 TrustZone builds. The secure image contains
the fwTPM command processor and the non-secure test application uses a small TIS
shim that forwards commands through the NSC entry point.

## Configuration

Use these wolfBoot configuration options:

| Option | Effect |
| ------ | ------ |
| `TZEN=1` | Builds wolfBoot for TrustZone-enabled STM32H5 parts. |
| `WOLFCRYPT_TZ=1` | Enables the wolfCrypt secure callable service layer. |
| `WOLFCRYPT_TZ_FWTPM=1` | Enables the secure fwTPM service and non-secure fwTPM test support. |

`WOLFCRYPT_TZ_FWTPM=1` defines `WOLFBOOT_TZ_FWTPM` for the secure and
non-secure builds. It also enables wolfTPM fwTPM sources, `WOLFTPM_FWTPM`,
`FWTPM_NO_NV`, and the callable fwTPM object.

The ready-to-use STM32H5 configuration is:

```sh
cp config/examples/stm32h5-tz-fwtpm.config .config
```

## Build

Build wolfBoot and the signed STM32H5 test application from the repository root:

```sh
cp config/examples/stm32h5-tz-fwtpm.config .config
make clean
make
make test-app/image_v1_signed.bin
```

The main outputs are:

| Output | Description |
| ------ | ----------- |
| `wolfboot.bin` | Secure wolfBoot image with the fwTPM service. |
| `test-app/image_v1_signed.bin` | Signed non-secure STM32H5 test application. |
| `test-app/image.elf` | Non-secure test application ELF for debugging. |

## Flash on STM32H5

Enable TrustZone and program the secure and non-secure images with
STM32CubeProgrammer:

```sh
STM32_Programmer_CLI -c port=swd mode=hotplug -ob TZEN=0xB4
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000
STM32_Programmer_CLI -c port=swd -d test-app/image_v1_signed.bin 0x08060000
```

The addresses above match `config/examples/stm32h5-tz-fwtpm.config`:

| Region | Address |
| ------ | ------- |
| Secure wolfBoot image | `0x0C000000` |
| Non-secure boot partition | `0x08060000` |
| Non-secure update partition | `0x0C100000` |
| Swap partition | `0x0C1A0000` |
| NSC veneer region | `0x0C05C000` |

## Test

Open the board serial console and run the fwTPM test command:

```text
fwtpm
```

The test application initializes wolfTPM using the non-secure TIS callback,
queries capabilities, requests random bytes, extends PCR 0, verifies the PCR
value, and seals/unseals a PCR-bound secret. A successful run ends with:

```text
fwTPM NSC tests passed
```

The STM32H5 test app also runs the same fwTPM test automatically during startup
when built with `WOLFBOOT_TZ_FWTPM`.

## Notes

The current wolfBoot integration builds the secure fwTPM service with
`FWTPM_NO_NV`, so TPM NV state is not persistent across resets. To add persistent
NV storage, provide a flash-backed `FWTPM_NV_HAL` implementation and remove
`FWTPM_NO_NV` from the fwTPM build flags.

### Secure RAM footprint

The secure image keeps one `FWTPM_CTX` in `.bss`, and wolfTPM 4.2.0 grew it to
about 93 KB with the library defaults. The STM32H5 secure RAM region is 128 KB
and the stack grows down from the top of it, so the default context left under
12 KB for the stack. Deep call chains then overran the tail of the context,
where the auth session slots live, and `StartAuthSession` failed with
`TPM_RC_SESSION_HANDLES` on a fresh TPM.

The build trims the NV index slots, which are dead weight under `FWTPM_NO_NV`:

| Flag | wolfTPM default | wolfBoot fwTPM build |
| ---- | --------------- | -------------------- |
| `FWTPM_MAX_NV_INDICES` | 16 | 2 |
| `FWTPM_MAX_NV_DATA` | 2048 | 512 |

That brings the context to about 60 KB and leaves roughly 45 KB of stack.
`hal/stm32h5.ld` also reserves `_min_stack` (32 KB) below the top of RAM and
asserts at link time that `.bss` does not encroach on it, so an oversized
context fails the build instead of corrupting memory at run time.

If you re-enable persistent NV, raise `FWTPM_MAX_NV_INDICES` and
`FWTPM_MAX_NV_DATA` to what the deployment needs and re-check the link assert.
The other `FWTPM_MAX_*` limits in `wolftpm/fwtpm/fwtpm.h` are `#ifndef`-guarded
and can be tuned the same way on the compile line.

`WOLFCRYPT_TZ_FWTPM` is mutually exclusive with `WOLFCRYPT_TZ_PKCS11` and
`WOLFCRYPT_TZ_PSA` because each option selects a different TrustZone secure
service surface for the test application.
