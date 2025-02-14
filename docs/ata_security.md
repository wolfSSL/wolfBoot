# ATA Security in wolfBoot

## Overview
wolfBoot provides secure ATA drive locking and unlocking capabilities through two mechanisms:
1. Hardcoded password authentication
2. TPM-sealed secret authentication

This integration enables secure storage protection while maintaining compatibility with standard ATA security features.

## Key Features
- ATA drive locking/unlocking support
- TPM integration for secure secret storage
- First-boot password initialization
- Master password support for administrative control
- Configurable security policies

## Configuration Methods

### Hardcoded Password Authentication
Uses a static password defined at compile time for drive locking/unlocking.

#### Configuration Options
```make
DISK_LOCK=1                           # Enable ATA security features
DISK_LOCK_PASSWORD=hardcoded_password # Set static password
```

#### Behavior
- First boot: If drive is unlocked, sets configured password
- Subsequent boots: Uses configured password to unlock drive

### TPM-Sealed Secret Authentication
Leverages TPM capabilities to securely store and manage drive unlock secrets. For detailed TPM integration information, see [TPM.md](TPM.md) and [measured_boot.md](measured_boot.md).

#### Configuration Options
| Option | Description | Usage |
|--------|-------------|--------|
| `WOLFBOOT_TPM_SEAL` | Enable TPM sealing support | Required with `DISK_LOCK=1` |
| `WOLFBOOT_TPM_SEAL_KEY_ID` | Policy signing key identifier | Used for TPM policy binding |
| `ATA_UNLOCK_DISK_KEY_NV_INDEX` | TPM NV storage index | Location for sealed secret |
| `WOLFBOOT_DEBUG_REMOVE_SEALED_ON_ERROR` | Error handling behavior | Deletes secret and panics on error |

#### Behavior
- First boot with no sealed secret:
  1. Generates random secret
  2. Seals secret to TPM
  3. Locks drive with sealed secret
- Subsequent boots:
  1. Unseals secret from TPM
  2. Unlocks drive using unsealed secret

### Administrative Control

#### Disabling User Password
Requires existing master password configuration.

```make
# Configuration for password disable
WOLFBOOT_ATA_DISABLE_USER_PASSWORD=1   # Enable password disable
ATA_MASTER_PASSWORD=master_password    # Set master password
```

#### Operation Flow
1. Verifies master password
2. Disables user password
3. Executes panic sequence

For more information about TPM integration, see:
- [TPM Security](TPM.md)
- [Measured Boot](measured_boot.md) 

