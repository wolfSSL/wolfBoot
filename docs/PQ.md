# Post-Quantum Signatures in wolfBoot

wolfBoot provides support for NIST-approved post-quantum (PQ) signature algorithms to ensure future-proof secure boot capabilities.

## Supported Algorithms

### 1. ML-DSA (Lattice-Based)
- FIPS 204 standardized algorithm
- Derived from CRYSTALS-DILITHIUM
- Reference: [FIPS 204 Final](https://csrc.nist.gov/pubs/fips/204/final)

### 2. LMS/HSS (Hash-Based)
- Stateful hash-based signature scheme
- NIST SP 800-208 recommended
- Reference: [NIST Stateful HBS](https://csrc.nist.gov/projects/stateful-hash-based-signatures)

### 3. XMSS/XMSS^MT (Hash-Based)
- Stateful hash-based signature scheme
- NIST SP 800-208 recommended
- Reference: [NIST Stateful HBS](https://csrc.nist.gov/projects/stateful-hash-based-signatures)

## Algorithm Comparison

| Feature | ML-DSA | LMS/HSS & XMSS/XMSS^MT |
|---------|--------|------------------------|
| Verification Speed | Fast | Fast |
| Signature Size | Variable | Variable |
| Key Generation | Fast | Slower |
| Public Key Size | Larger, Variable | Smaller |
| Private Key | Stateless | Stateful* |

*Stateful private keys require careful management during key generation and signing.

## Example Configurations

Find example configurations in:
```
config/examples/sim-ml-dsa.config  # ML-DSA example
config/examples/sim-lms.config     # LMS/HSS example
config/examples/sim-xmss.config    # XMSS/XMSS^MT example
```

## ML-DSA Implementation

### Overview
ML-DSA (Module-Lattice Digital Signature Algorithm) is a FIPS 204 standardized algorithm based on lattice cryptography, derived from CRYSTALS-DILITHIUM.

### Parameter Sets

ML-DSA offers three security levels, with numerical suffixes indicating matrix dimensions:

| Parameter Set | Security Category | Description |
|--------------|-------------------|-------------|
| ML-DSA-44 | Category 2 | Lower security, smaller sizes |
| ML-DSA-65 | Category 3 | Medium security, balanced |
| ML-DSA-87 | Category 5 | Highest security, larger sizes |

### Key and Signature Sizes

| Parameter Set | Private Key (bytes) | Public Key (bytes) | Signature (bytes) | Security Category |
|--------------|---------------------|-------------------|-------------------|-------------------|
| ML-DSA-44 | 2,560 | 1,312 | 2,420 | Category 2 |
| ML-DSA-65 | 4,032 | 1,952 | 3,309 | Category 3 |
| ML-DSA-87 | 4,896 | 2,592 | 4,627 | Category 5 |

### Configuration

#### Basic Setup
```make
# In config file (e.g., config/examples/sim-ml-dsa.config)
ML_DSA_LEVEL=2                # Security category (2, 3, or 5)
IMAGE_SIGNATURE_SIZE=2420     # Must match parameter set
IMAGE_HEADER_SIZE?=4840       # Typically 2x signature size
```

#### Standards Compliance
- Default: FIPS 204 Final Standard
- Legacy Support: Use `WOLFSSL_DILITHIUM_FIPS204_DRAFT` for draft standard

## Stateful Hash-Based Signatures (HBS)

### Overview
LMS/HSS and XMSS/XMSS^MT are quantum-resistant signature schemes based on hash functions and Merkle trees.

**Key Features:**
- Small public key sizes
- Fast signature operations
- Configurable signature sizes
- Strong security foundations
  - Based on hash function security
  - Quantum-resistant by design
  - Recommended by NIST SP 800-208
  - Included in NSA's CNSA 2.0 suite

**Implementation Details:**
- LMS/HSS Implementation:
  - Core files: `wc_lms.c`, `wc_lms_impl.c`
  - Provides HSS (Hierarchical Signature System)
  
- XMSS/XMSS^MT Implementation:
  - Core files: `wc_xmss.c`, `wc_xmss_impl.c`
  - Supports both single-tree and multi-tree variants

**Additional Resources:**
- [wolfSSL HBS Documentation](https://www.wolfssl.com/documentation/manuals/wolfssl/appendix07.html#post-quantum-stateful-hash-based-signatures)
- [wolfSSL HBS Examples](https://github.com/wolfSSL/wolfssl-examples/tree/master/pq/stateful_hash_sig)

### LMS/HSS Configuration

#### Basic Setup
```make
# In config/examples/sim-lms.config
SIGN?=LMS
LMS_LEVELS=2                  # Number of HSS levels
LMS_HEIGHT=5                  # Tree height
LMS_WINTERNITZ=8             # Winternitz parameter
IMAGE_SIGNATURE_SIZE=2644     # From lms_siglen.sh
IMAGE_HEADER_SIZE?=5288       # Typically 2x signature size
```

#### Signature Size Calculator
Use the provided helper script to calculate required signature size:
```bash
$ ./tools/lms/lms_siglen.sh 2 5 8
levels:     2
height:     5
winternitz: 8
signature length: 2644
```

### XMSS/XMSS^MT Configuration

#### Basic Setup
```make
# In config/examples/sim-xmss.config
SIGN?=XMSS
XMSS_PARAMS='XMSS-SHA2_10_256'    # From NIST SP 800-208
IMAGE_SIGNATURE_SIZE=2500          # From xmss_siglen.sh
IMAGE_HEADER_SIZE?=5000            # Typically 2x signature size
```

#### Parameter Sets
Supports all SHA256 parameter sets from NIST SP 800-208 Tables 10 and 11.

#### Signature Size Calculator
```bash
# Single-tree XMSS
$ ./tools/xmss/xmss_siglen.sh XMSS-SHA2_10_256
parameter set:    XMSS-SHA2_10_256
signature length: 2500

# Multi-tree XMSS^MT
$ ./tools/xmss/xmss_siglen.sh XMSSMT-SHA2_20/2_256
parameter set:    XMSSMT-SHA2_20/2_256
signature length: 4963
```

## Hybrid Mode (Classic + Post-Quantum)

### Overview
wolfBoot supports hybrid signatures combining classic and post-quantum algorithms for:
- Gradual transition to post-quantum security
- Protection against vulnerabilities in either algorithm
- Maintaining backwards compatibility

### Configuration

#### Required Settings
```make
SECONDARY_SIGN=<algorithm>           # Enable hybrid mode
WOLFBOOT_UNIVERSAL_KEYSTORE=1        # Required for hybrid keys
```

#### Example Configuration
See `config/examples/sim-ml-dsa-ecc-hybrid.config` for ML-DSA-65 + ECC-384 hybrid setup.

### Hybrid Signature Support

#### Key Generation Options

1. **Simultaneous Key Generation** (Recommended)
   - Generates both keys at once
   - Automatically adds both to keystore
   - Ensures proper key compatibility

2. **Sequential Key Generation**
   - Allows adding PQ keys to existing classic setup
   - Requires manual keystore management
   - Must follow specific import procedure

### Key Generation Procedures

#### Method 1: Single Command Generation
```bash
# Generate both ML-DSA and ECC384 keys
./tools/keytools/keygen --ml_dsa -g wolfboot_signing_private_key.der \
                       --ecc384 -g wolfboot_signing_second_private_key.der
```
**Result:** Both keys are automatically added to keystore

#### Method 2: Sequential Generation

1. Generate First Key:
   ```bash
   # Generate ML-DSA key
   ./tools/keytools/keygen --ml_dsa -g wolfboot_signing_private_key.der
   ```

2. Extract Public Key:
   ```bash
   # Skip 16-byte keystore header
   dd if=keystore.der of=ml_dsa-pubkey.der bs=1 skip=16
   ```

3. Generate Second Key with Import:
   ```bash
   # Generate ECC384 key while importing ML-DSA key
   ./tools/keytools/keygen --ml_dsa -i ml_dsa-pubkey.der \
                          --ecc384 -g wolfboot_signing_second_private_key.der
   ```

**Result:** Complete keystore with both keys ready for hybrid verification

### Firmware Signing with Hybrid Keys

#### Signing Command
```bash
./tools/sign/sign --ml_dsa --ecc384 --sha256 test-app/image.elf \
    wolfboot_signing_private_key.der \
    wolfboot_signing_second_private_key.der 1
```

#### Command Parameters
- `--ml_dsa --ecc384`: Specify both signature algorithms
- `--sha256`: Hash algorithm
- `test-app/image.elf`: Input firmware image
- `wolfboot_signing_private_key.der`: Primary key (ML-DSA)
- `wolfboot_signing_second_private_key.der`: Secondary key (ECC384)
- `1`: Target partition ID

#### Expected Output
```
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Parsing arguments in hybrid mode
...
Creating hybrid signature
```

#### Results
- Output file: `test-app/image_v1_signed.bin`
- Contains both ML-DSA and ECC384 signatures
- Compatible with hybrid-enabled wolfBoot
- Verifiable using both algorithms during boot

**Note:** Ensure wolfBoot is compiled with hybrid signature support to verify these images.

