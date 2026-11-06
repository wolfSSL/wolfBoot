## wolfBoot Docs and Platform-Specific Details

See also: [wolfBoot Product Overview](https://www.wolfssl.com/products/wolfboot/) and [wolfBoot Manual](https://www.wolfssl.com/documentation/manuals/wolfboot/).


- [**Agilex5.md**](./Agilex5.md) - Altera Agilex 5 port: Cortex-A TF-A model with wolfBoot as BL33 booting a signed Linux FIT.
- [**API.md**](./API.md) - Overview of wolfBoot public APIs and how to use them.
- [**ata_security.md**](./ata_security.md) - ATA security features (lock/unlock, passwords) and wolfBoot integration.
- [**azure_keyvault.md**](./azure_keyvault.md) - Using Azure Key Vault for key management and signing with wolfBoot.
- [**CMake.md**](./CMake.md) - CMake-based build setup, presets, toolchains, and tips for building wolfBoot.
- [**compile.md**](./compile.md) - How to build/compile wolfBoot (toolchains, options, typical steps).
- [**Compression.md**](./Compression.md) - Compressed firmware updates: a gzip-compressed update partition installed into a boot partition that executes in place.
- [**DICE.md**](./DICE.md) - DICE-based PSA attestation service: token format, keying options, and access from non-secure code.
- [**encrypted_partitions.md**](./encrypted_partitions.md) - Creating and managing encrypted firmware/data partitions.
- [**firmware_image.md**](./firmware_image.md) - wolfBoot firmware image format, layout, and metadata.
- [**firmware_update.md**](./firmware_update.md) - Update flow: slots, verification, rollback, and recovery.
- [**FIPS.md**](./FIPS.md) - Building wolfBoot with the wolfCrypt FIPS 140-3 module: approved algorithms, DRBG entropy, and in-core integrity sealing.
- [**flash-OTP.md**](./flash-OTP.md) - Using One-Time Programmable (OTP) regions in flash for secure data.
- [**flash_partitions.md**](./flash_partitions.md) - Flash partitioning schemes and configuration guidance.
- [**fwTPM.md**](./fwTPM.md) - Hosting wolfTPM's firmware TPM 2.0 in the secure TrustZone image on STM32H5, exposed via callable services.
- [**HAL.md**](./HAL.md) - Hardware Abstraction Layer notes and porting considerations.
- [**hooks.md**](./hooks.md) - User-defined hooks for injecting custom logic into the wolfBoot boot process.
- [**keystore.md**](./keystore.md) - Keystore design, key storage, and access strategies.
- [**lib.md**](./lib.md) - Using wolfBoot as a library and linking/integration guidance.
- [**Loader.md**](./Loader.md) - Loader/secondary stage behavior and handoff to application.
- [**MCXN947-DICE.md**](./MCXN947-DICE.md) - DICE / PSA initial attestation sample on NXP MCXN947 using the EdgeLock Secure Subsystem.
- [**measured_boot.md**](./measured_boot.md) - Measured boot concepts and recording measurements (e.g., PCRs).
- [**png/**](./png) - Folder of images/diagrams referenced by the documentation.
- [**PolarFire.md**](./PolarFire.md) - Microchip PolarFire SoC: quick start, boot configurations, sNVM/PUF key storage, SBI vendor extension, boot mode 2.
- [**PQ.md**](./PQ.md) - Post-quantum algorithms and PQC support in wolfBoot.
- [**README.md**](./README.md) - Overview and index of the documentation set.
- [**remote_flash.md**](./remote_flash.md) - Working with external/remote flash (SPI/QSPI, mapping, access).
- [**Renesas.md**](./Renesas.md) - Notes and specifics for Renesas platforms/ports.
- [**SBOM.md**](./SBOM.md) - Generating CycloneDX 1.6 and SPDX 2.3 SBOMs for every configuration and build system.
- [**secure_app_handoff.md**](./secure_app_handoff.md) - WOLFBOOT_SECURE_APP: booting a signed Secure-state runtime (e.g. wolfTrust) with measured-boot handoff.
- [**Signing.md**](./Signing.md) - Keys, signatures, and the image signing workflow.
- [**STM32-TZ.md**](./STM32-TZ.md) - STM32 TrustZone (Armv8-M) setup and usage with wolfBoot.
- [**Targets.md**](./Targets.md) - Supported targets and platform-specific configuration.
- [**TPM.md**](./TPM.md) - TPM integration, measured boot, and attestation flows.
- [**Windows.md**](./Windows.md) - Barebones Windows setup without Visual Studio: LLVM/clang host compiler for VS Code or command-prompt builds.
- [**wolfHAL.md**](./wolfHAL.md) - Using wolfHAL as an alternative portable HAL backend for clock, flash, GPIO, UART, and SPI.
- [**wolfHSM.md**](./wolfHSM.md) - Integrating wolfHSM with wolfBoot for secure key operations.


