
# wolfHSM Integration Guide

## Overview
wolfBoot provides secure cryptographic offloading through wolfHSM integration, enabling hardware-backed security features through Remote Procedure Calls (RPCs).

### Architecture
```
+-------------+     +------------+     +-----------+
|  wolfBoot   | --> |  wolfHSM   | --> |  Hardware |
|  (Client)   |     |  (Server)  |     |   HSM     |
+-------------+     +------------+     +-----------+
      ↓                   ↓                 ↓
    Requests         Operations         Secure Keys
```

### Key Features
| Feature | Description |
|---------|-------------|
| Key Storage | Secure HSM-backed storage |
| Crypto Ops | Remote operation execution |
| Key Rotation | Runtime key management |
| Isolation | Hardware security boundary |

## Platform Support

### Supported Targets
| Platform | Transport | Status |
|----------|-----------|---------|
| Simulator | POSIX TCP | ✓ Full |
| AURIX TC3xx | Shared Memory | ✓ Full |

### Documentation
- [wolfHSM Manual](https://wolfSSL.com/https://www.wolfssl.com/documentation/manuals/wolfhsm/)
- [GitHub Repository](https://github.com/wolfssl/wolfHSM.git)
- Platform-specific guides in respective docs

## Cryptographic Support

### Supported Algorithms

#### Signature Verification
| Algorithm | Key Sizes | Notes |
|-----------|-----------|-------|
| RSA | 2048/3072/4096 | Image signatures |
| ECDSA | P-256/384/521 | Image signatures |
| ML-DSA | Level 2/3/5 | Platform dependent |
| SHA256 | N/A | Image integrity |

**Note**: Encrypted image support pending

### Configuration

#### Basic Setup
```make
# Enable HSM client
WOLFBOOT_ENABLE_WOLFHSM_CLIENT=1

# Use HSM key storage
WOLFBOOT_USE_WOLFHSM_PUBKEY_ID=1
```

#### Key Management Modes

1. **HSM-Stored Keys** (Recommended)
```bash
# Generate keys without local storage
keygen --nolocalkeys ...

# Configuration
WOLFBOOT_USE_WOLFHSM_PUBKEY_ID=1
```

2. **Local Keys** (Debug/Testing)
```bash
# Generate standard keys
keygen ...

# Configuration
# WOLFBOOT_USE_WOLFHSM_PUBKEY_ID not set
```

#### Operation Flow
```
HSM-Stored Keys:
wolfBoot → HSM KeyId → Direct Operations

Local Keys:
wolfBoot → Local Key → HSM Cache → Operations → Evict
```

## HAL Integration

### Required Components

#### Global Variables
| Variable | Type | Purpose |
|----------|------|---------|
| `hsmClientCtx` | Context | Client state |
| `hsmClientDevIdHash` | ID | Hash operations |
| `hsmClientDevIdPubKey` | ID | Public key ops |
| `hsmClientKeyIdPubKey` | ID | Key selection |

#### Function Interface
```c
/* Initialize HSM connection */
int hal_hsm_init_connect(void);

/* Cleanup HSM connection */
void hal_hsm_disconnect(void);
```

### Implementation Flow
```
Initialization:
+----------------+     +------------------+
| Boot Sequence  | --> | hal_hsm_init    |
+----------------+     +------------------+
                              ↓
                      +------------------+
                      | Context Setup    |
                      +------------------+
                              ↓
                      +------------------+
                      | API Init         |
                      +------------------+

Shutdown:
+----------------+     +------------------+
| System Cleanup | --> | hal_hsm_disc    |
+----------------+     +------------------+
                              ↓
                      +------------------+
                      | Context Cleanup  |
                      +------------------+
                              ↓
                      +------------------+
                      | Resource Free    |
                      +------------------+
```

## Simulator Integration

### Overview
The wolfBoot simulator provides a complete wolfHSM testing environment using POSIX TCP transport.

### Network Configuration
```
+----------------+     +----------------+
| wolfBoot Sim   | <-> | wolfHSM Server |
| 127.0.0.1     |     | :1234         |
+----------------+     +----------------+
```

### Build Instructions

#### 1. Configuration
```bash
# Setup simulator config
cp config/examples/sim-wolfHSM.config .config

# Enable HSM client
make WOLFHSM_CLIENT=1
```

#### 2. Test Application
```bash
# Build and sign test apps
make test-sim-internal-flash-with-update

# Key location:
# wolfboot_signing_private_key_pub.der
```

#### Key Features
| Feature | Description |
|---------|-------------|
| Transport | POSIX TCP |
| Algorithms | Full support |
| Key Storage | HSM-backed |
| Testing | Automated |

## Testing Guide

### Server Setup

#### 1. Build Server
```bash
# Get wolfHSM examples
cd wolfHSM-examples/posix/tcp/wh_server_tcp

# Build server
make WOLFHSM_DIR=/path/to/wolfHSM/install
```

#### 2. Start Server
```bash
# Run with wolfBoot key
./Build/wh_server_tcp.elf \
    --key wolfboot_signing_private_key_pub.der \
    --id 0xFF  # Match hsmClientKeyIdPubKey
```

### Testing Process

#### 1. Stage Update
```bash
# Trigger update
./wolfboot.elf update_trigger get_version

# Expected Output:
# Boot partition: v1
# Update partition: v2
# Status: Update staged
```

#### 2. Verify Update
```bash
# Restart server first
./Build/wh_server_tcp.elf \
    --key wolfboot_signing_private_key_pub.der \
    --id 0xFF

# Complete update
./wolfboot.elf success get_version

# Verification Points
- Boot partition: v2
- Update success
- Version increment
```

### Update Flow
```
Initial State:
Boot(v1) | Update(v2)

After Stage:
Boot(v1) | Update(v2*)

After Success:
Boot(v2) | Update(v1)
```

## Related Documentation
- [wolfHSM Examples](https://github.com/wolfSSL/wolfHSM-examples)
- [Firmware Updates](firmware_update.md)
- [Key Management](keystore.md)

