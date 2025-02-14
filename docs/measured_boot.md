# Measured Boot with wolfBoot

## Overview
wolfBoot provides a secure measured boot implementation using TPM2.0 technology for system state verification and tamper detection.

### Key Features
- System state tracking
- TPM2.0 integration
- Tamper-proof logging
- Multi-platform support
- Runtime verification

### System Components
```
+-------------+     +---------+     +----------+
|  wolfBoot   | --> | wolfTPM | --> |  TPM2.0 |
+-------------+     +---------+     +----------+
       ↓                                 ↓
    Measures         Interfaces     Stores PCRs
```

### Platform Support
| Platform | Support Level |
|----------|--------------|
| Windows | Native |
| Linux | Native |
| RTOS | Full |
| Bare Metal | Full |

## Technical Overview

### Secure vs. Measured Boot
| Feature | Secure Boot | Measured Boot |
|---------|-------------|---------------|
| Purpose | Verify firmware signature | Track system state |
| Timing | Boot-time only | Continuous |
| Storage | None after boot | Persistent in TPM |
| Access | Boot process only | Runtime accessible |

### Measurement Process
1. Component verification
2. PCR extension (TPM measurement)
3. State recording
4. Runtime verification

### TPM Integration
- PCR (Platform Configuration Register)
  - Tamper-proof storage
  - Power-cycle reset only
  - Cryptographic extension

### wolfBoot Implementation
- Single component focus
  - Main firmware image
  - Extensible design
  - Additional PCR support
- Runtime verification
  - OS/firmware access
  - State validation

## Configuration Guide

### Basic Setup
```make
# Enable measured boot
MEASURED_BOOT=1

# Select PCR index
MEASURED_PCR_A=16  # Example for development
```

### PCR Index Selection

#### PCR Register Map
| Index | Purpose | Platform Support |
|-------|---------|-----------------|
| 0 | Core Root of Trust/BIOS | Bare-metal, RTOS |
| 1 | Platform Config Data | Bare-metal, RTOS |
| 2-3 | Option ROM Code | Bare-metal, RTOS |
| 4-5 | Master Boot Record | Bare-metal, RTOS |
| 6 | State Transitions | Bare-metal, RTOS |
| 7 | Vendor Specific | Bare-metal, RTOS |
| 8-9 | Partition Data | Bare-metal, RTOS |
| 10 | Boot Manager | Bare-metal, RTOS |
| 11 | BitLocker Reserved | Windows Only |
| 12-15 | General Purpose | All Platforms |
| 16 | Debug/Development | Testing Only |
| 17 | DRTM | Trusted Boot |
| 18-22 | Trusted OS | TEE Only |
| 23 | Application | Temporary Use |

#### Selection Guidelines
1. **Development**
   - Use PCR16 (Debug)
   - Unrestricted access
   - No conflicts

2. **Production - Bare Metal/RTOS**
   - PCR0-15 available
   - Avoid PCR17-23
   - Follow platform conventions

3. **Production - Linux/Windows**
   - Use PCR12-15
   - Avoid system PCRs
   - Consider OS requirements

### Example Configuration
Development setup in `.config`:
```make
# Enable measured boot
MEASURED_BOOT?=1

# Use debug PCR
MEASURED_PCR_A?=16
```

## Implementation Details

### Architecture
```
+----------------+     +---------------+
| Firmware Image |     | TPM2.0 Device |
+----------------+     +---------------+
        ↓                     ↓
   measure_boot() ----→ PCR Extension
        ↓                     ↓
    Verification     State Preservation
```

### Key Components
- **src/image.c**: Core implementation
- **measure_boot()**: Main measurement function
- **wolfTPM API**: TPM2.0 interface

### Features
- Zero-touch integration
- Automatic measurement
- TPM2.0 native calls
- Runtime verification

## Related Documentation
- [wolfTPM Integration](https://github.com/wolfSSL/wolfTPM)
- [TPM Security](TPM.md)
- [Secure Boot](Signing.md)
