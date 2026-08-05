# Altera Agilex 5

The Agilex 5 port follows wolfBoot's established Cortex-A TF-A model. It does
not replace the platform first stage or TF-A:

```text
SDM -> U-Boot SPL -> TF-A BL31/EL3 -> wolfBoot BL33/EL2 -> signed Linux FIT
```

SPL remains responsible for DDR, clocks, resets, pinmux, the SD6HC PHY and
loading the signed U-Boot FIT. BL31 remains responsible for EL3, GICv3, PSCI,
secondary CPUs and security-controller setup. The U-Boot-proper payload in the
FIT is replaced with `wolfboot.bin` at load/entry address `0x80200000`.

## Build

Use an isolated wolfBoot checkout with initialized wolfSSL submodules:

```sh
cp config/examples/agilex5_013b_sdcard.config .config
make clean
make -j"$(nproc)"
```

The hosted build is also covered by the `Agilex 5 wolfBoot build` GitHub
Actions workflow. It installs the same freestanding AArch64 toolchain used by
the other Cortex-A ports and builds this configuration on every relevant
pull request; hardware is required only for the SD-card and Linux handoff
steps below.

The default RSA-4096/SHA3-384 key is for development only. Production builds
must supply controlled signing keys and keep them separate from both the SDM
owner key and the SPL FIT-signing key.

Create the Linux FIT from the GSRD `Image` and the exact board DTB, then sign
that FIT with wolfBoot's normal image-signing flow. Initialize both raw A/B
partitions with a valid signed image for the first boot.

The checked-in `hal/agilex5.its` is a minimal FIT example. A GSRD production
image must use the GSRD-generated kernel load address and board DTB, then be
signed with the deployment key. Do not place private keys in the source tree
or the WIC.

## GSRD integration

Keep the GSRD `u-boot.itb` structure and signature intact:

- retain the existing BL31 firmware node;
- replace only the U-Boot-proper/BL33 data with `wolfboot.bin`;
- retain load and entry `0x80200000`;
- rebuild and sign the ITB using the same GSRD/SPL trust configuration.

Use a four-partition WIC:

1. 128 MiB FAT boot partition containing the signed boot artifacts;
2. 200 MiB raw wolfBoot A partition;
3. 200 MiB raw wolfBoot B partition;
4. ext4 Linux root filesystem.

The wolfSSL FCS packages belong in partition 4. libfcs is Linux userspace
software and is deliberately not linked into bare-metal wolfBoot.

## Test order

1. Cross-build the existing ZynqMP and Versal SD-card configurations to catch
   shared AArch64 regressions, then build Agilex 5.
2. First boot a minimal signed payload that prints its exception level and
   proves the generic timer and PSCI reset path.
3. Exercise SD reads in PIO mode, then valid, corrupt-signature, truncated and
   oversized images.
4. Boot Linux and require the DTB in x0, x1-x3 zero, four online CPUs, working
   PSCI reboot, and rootfs `/dev/mmcblk0p4`.
5. Build the complete WIC, inspect all four partitions, flash the whole card,
   compare the complete image span, cold boot, and test A-to-B fallback by
   corrupting only a disposable copy of A.
6. From the booted WIC run `/usr/bin/wolfcrypttest`. Require exit zero and
   `ALTERA-FCS test passed!` to prove the Linux image still provides hardware
   FCS offload.

For the reproducible meta-wolfSSL customer flow, use
`recipes-wolfssl/wolfboot/agilex5/README.md` in the meta-wolfSSL checkout. It
covers the Kas fragment, the four-partition WIC inspection, whole-device flash
and byte comparison, virtual SDM owner-key reprovisioning after power loss,
and the target-side loader/FCS checks. wolfBoot itself does not link libfcs;
the FCS test belongs to the Linux image in WIC partition 4.

Do not patch TF-A or take over GICv3, SMMU/firewall, mailbox, or PSCI ownership
without a reproduced hardware failure showing the stock GSRD contract is
insufficient.

## CI and hardware boundary

CI proves that the Agilex 5 configuration, linker script, startup assembly,
SDHCI changes, and test application remain buildable without Agilex hardware.
It cannot prove SPL DDR training, TF-A handoff, SDM services, signed FIT
verification, or FCS offload. Those require the GSRD image and the
DK-A5E013BM16AEA. Record the WIC hash, complete-card comparison, UART boot
log, target `uname`, loader output, and `wolfcrypttest` exit status and FCS
marker for a hardware release sign-off.
