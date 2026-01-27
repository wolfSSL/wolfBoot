## OTP keystore primer application

This application is used to provision the public keys into a dedicated FLASH OTP
area. For more information about its usage, please refer to [/docs/flash-OTP.md](/docs/flash-OTP.md).

## Attestation UDS storage

For targets that support it (for example STM32H5), wolfBoot can store a random
UDS for DICE attestation in OTP using the primer app. This is the default
approach when OBKeys secure storage is not available or not provisioned.

If you have access to STM32H5 OBKeys secure storage, prefer that for production
iRoT key material. Enable `WOLFBOOT_UDS_OBKEYS=1` and provision OBKeys via
STM32TrustedPackageCreator/STM32CubeProgrammer (see `docs/DICE.md`). OTP should
be used for development or as a fallback when OBKeys is unavailable.

