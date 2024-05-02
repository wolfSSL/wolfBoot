# ATA Security

## Introduction
This document provides an overview of how wolfBoot can leaverage the ATA security features to lock or unlock ATA drive.
The ATA drive may be locked either by using a hardcoded password or by using a secret that is sealed in the TPM.

## Table of Contents
- [ATA Security](#ata-security)
  - [Introduction](#introduction)
  - [Table of Contents](#table-of-contents)
  - [Unlocking the Disk with a Hardcoded Password](#unlocking-the-disk-with-a-hardcoded-password)
  - [Unlocking the Disk with a TPM-Sealed Secret](#unlocking-the-disk-with-a-tpm-sealed-secret)
  - [Disabling the password](#disabling-the-password)

## Unlocking the Disk with a Hardcoded Password
To unlock the disk using a hardcoded password, use the following options in your .config file:
```
DISK_LOCK=1
DISK_LOCK_PASSWORD=hardcoded_password
```
If the ATA disk has no password set, the disk will be locked with the password provided at the first boot.

## Unlocking the Disk with a TPM-Sealed Secret
wolfBoot allows to seal secret safely in the TPM in a way that it can be unsealed only under specific conditions. Please refer to files TPM.md and measured_boot.md for more information. If the option `WOLFBOOT_TPM_SEAL` is enabled and `DISK_LOCK` is enabled, wolfBoot will use a TPM sealed secret as the password to unlock the disk. The following options controls the sealing and unsealing of the secret:

| Option | Description |
|--------|-------------|
| WOLFBOOT_TPM_SEAL_KEY_ID| The key ID to use for sign the policy |
| ATA_UNLOCK_DISK_KEY_NV_INDEX | The NV index to store the sealed secret. |
| WOLFBOOT_DEBUG_REMOVE_SEALED_ON_ERROR| In case of error, delete the secret and panic() |

In case there are no secret sealed at `ATA_UNLOCK_DISK_KEY_NV_INDEX`, a new random secret will be created and sealed at that index. 
In case the ATA drive is not locked, it will be locked at the first boot with the secret sealed in the TPM.

## Disabling the password

If you need to disable the password, a master password should be already set on the device. Then you can use the following option to compile wolfBoot so that it will disable the password from the drive and panic:

```
WOLFBOOT_ATA_DISABLE_USER_PASSWORD=1
ATA_MASTER_PASSWORD=the_master_password
``` 

