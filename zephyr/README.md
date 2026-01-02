# wolfBoot Zephyr Integration (ARM TEE Replacement)

This guide explains how to replace the default TEE provider with wolfBoot as the PSA/TEE provider in a vanilla Zephyr workspace.

It assumes:
- You have this wolfBoot repo on disk (this workspace).
- You want Zephyr to build and run the PSA crypto sample using wolfBoot as the TEE.

## 1) Create a fresh Zephyr workspace

From the workspace root:

```sh
mkdir -p zephyrproject
cd zephyrproject
python3 -m venv .venv
./.venv/bin/pip install --upgrade pip west jsonschema pyelftools

./.venv/bin/west init -m https://github.com/zephyrproject-rtos/zephyr
./.venv/bin/west update
```

You now have a vanilla Zephyr tree under `zephyrproject/zephyr`.

## 2) Patch Zephyr for wolfBoot integration

From the Zephyr base (`zephyrproject/zephyr`), apply the patches in order:

```sh
cd /path/to/your/workspace/zephyrproject/zephyr

git apply /path/to/your/workspace/wolfboot/zephyr/patches/0001-wolfboot-tee-driver.patch
git apply /path/to/your/workspace/wolfboot/zephyr/patches/0002-wolfboot-tee-dt-binding.patch
git apply /path/to/your/workspace/wolfboot/zephyr/patches/0003-wolfboot-sample.patch
git apply /path/to/your/workspace/wolfboot/zephyr/patches/0004-stm32h5-ns-board-support.patch
git apply /path/to/your/workspace/wolfboot/zephyr/patches/0005-wolfboot-psa-kconfig.patch
```

These patches add:
- wolfBoot TEE driver hooks (`drivers/tee/wolfboot` + Kconfig/CMake wiring).
- Device-tree binding for the wolfBoot TEE and the `wolfssl` vendor prefix.
- `samples/wolfboot_integration/psa_crypto`.
- STM32H5 NS board variants used by the sample (Nucleo H563ZI and H573I-DK).
- Kconfig tweaks so PSA crypto client is enabled with `WOLFBOOT_TEE`, and the legacy TEE dependency isn’t forced when not configured.

## 3) Build wolfBoot (secure side) first

wolfBoot provides the secure-side PSA service and CMSE veneers. Build it before the Zephyr app:

```sh
cd /path/to/your/workspace/wolfboot
cp config/examples/stm32h5-tz-psa.config .config
make clean wolfboot.bin
```

This also produces `src/wc_secure_calls.o`, which Zephyr links for CMSE veneers.

## 4) Build the PSA crypto sample with wolfBoot as an extra module

Use the Zephyr sample in-tree, and point `ZEPHYR_EXTRA_MODULES` to the wolfBoot repo:

```sh
cd /path/to/your/workspace/zephyrproject/zephyr

./../.venv/bin/west build -p auto \
  -b nucleo_h563zi/stm32h563xx/ns \
  -d ./build \
  ./samples/wolfboot_integration/psa_crypto \
  -- -DZEPHYR_EXTRA_MODULES=/path/to/your/workspace/wolfboot
```

Notes:
- The wolfBoot module provides the PSA client veneers, PSA IPC glue, and minimal PSA API wrappers.
- The sample overlay uses `compatible = "wolfssl,tee"` and the vendor prefix is registered.

## 5) Flash on STM32H5 Nucleo (TrustZone enabled)

Follow the STM32H5 target guide in `wolfboot/docs/Targets.md` and program the board with
STM32_Programmer_CLI. The key steps are:

```sh
# Enable TrustZone
STM32_Programmer_CLI -c port=swd -ob TZEN=0xB4

# Secure window (first 384KB) + remainder non-secure
STM32_Programmer_CLI -c port=swd -ob SECWM1_STRT=0x0 SECWM1_END=0x2F SECWM2_STRT=0x0 SECWM2_END=0x7F

# Secure wolfBoot image
STM32_Programmer_CLI -c port=swd -d wolfboot.bin 0x0C000000

# Non-secure Zephyr payload
STM32_Programmer_CLI -c port=swd -d zephyrproject/zephyr/build/zephyr/zephyr.payload_v1_signed.bin 0x08060000
```

For the full option-byte list and related notes, see `wolfboot/docs/STM32-TZ.md`.

## Troubleshooting

### Missing syscall stubs when building wolfBoot
If you see link errors for `_read/_write/_close/_lseek/_fstat/_isatty`, wolfBoot includes weak stubs in:

```
wolfboot/src/arm_tee_psa_ipc.c
```

### PSA symbols missing during Zephyr build
Make sure:
- You built wolfBoot first (for `wc_secure_calls.o`).
- You passed `-DZEPHYR_EXTRA_MODULES=/path/to/wolfboot`.

### Kconfig warnings about WOLFBOOT_* symbols
The wolfBoot module supplies its own Kconfig (`wolfboot/zephyr/Kconfig`) and will generate `wolfboot.conf` during the sample build when `ZEPHYR_EXTRA_MODULES` is set correctly.
