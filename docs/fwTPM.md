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

`WOLFCRYPT_TZ_FWTPM` is mutually exclusive with `WOLFCRYPT_TZ_PKCS11` and
`WOLFCRYPT_TZ_PSA` because each option selects a different TrustZone secure
service surface for the test application.
