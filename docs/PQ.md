# Post-Quantum Signatures

wolfBoot is continuously adding support for post-quantum (PQ) signature
algorithms as they mature. At present, support has been added for three NIST
approved PQ signature algorithms:

- ML-DSA: https://csrc.nist.gov/pubs/fips/204/final
- LMS/HSS: https://csrc.nist.gov/projects/stateful-hash-based-signatures
- XMSS/XMSS^MT: https://csrc.nist.gov/projects/stateful-hash-based-signatures

ML-DSA is a PQ lattice-based algorithm, derived from
CRYSTALS-DILITHIUM (a round three NIST finalist).

LMS/HSS and XMSS/XMSS^MT are both PQ stateful hash-based signature (HBS)
schemes, recommended in NIST SP 800-208.

In terms of relative tradeoffs:
- All three methods have fast verifying operations.
- All three methods have variable length signature sizes.
- ML-DSA key generation is much faster than LMS/HSS and XMSS/XMSS^MT.
- ML-DSA public keys are larger than LMS/HSS and XMSS/XMSS^MT, and
  variable sized.
- LMS/HSS and XMSS/XMSS^MT have stateful private keys, which requires
  more care with key generation and signing operations.

See these config files for simulated target examples:

- `config/examples/sim-ml-dsa.config`
- `config/examples/sim-lms.config`
- `config/examples/sim-xmss.config`

## Lattice Based Signature Methods

### ML-DSA

ML-DSA (Module-Lattice Digital Signature Algorithm) was standardized in
FIPS 204 (https://csrc.nist.gov/pubs/fips/204/final), based on its
round 3 predecessor CRYSTALS-DILITHIUM.

ML-DSA has three standardized parameter sets:

- `ML-DSA-44`
- `ML-DSA-65`
- `ML-DSA-87`

The numerical suffix (44, 65, 87) denotes the dimension of the matrix used
in the underlying lattice construction.

The private key, public key, signature size, and overall security strength
all depend on the parameter set:

```
#
#               Private Key   Public Key   Signature Size   Security Strength
#   ML-DSA-44      2560          1312         2420            Category 2
#   ML-DSA-65      4032          1952         3309            Category 3
#   ML-DSA-87      4896          2592         4627            Category 5
#
```

### ML-DSA Config

A new ML-DSA sim example has been added here:

```
config/examples/sim-ml-dsa.config
```

The security category level is configured with `ML_DSA_LEVEL=<num>`, where
num = 2, 3, 5. Here is an example from the `sim-ml-dsa.config` for category
2:

```
# ML-DSA config examples:
#
# Category 2:
ML_DSA_LEVEL=2
IMAGE_SIGNATURE_SIZE=2420
IMAGE_HEADER_SIZE?=4840
```

Note: The wolfcrypt implementation of ML-DSA (dilithium) builds to the
FIPS 204 final standard by default. If you wish to conform to the older
FIPS 204 draft standard, then build with `WOLFSSL_DILITHIUM_FIPS204_DRAFT`
instead.

## Stateful Hash-Based Signature Methods

LMS/HSS and XMSS/XMSS^MT are both post-quantum stateful hash-based signature
(HBS) schemes. They are known for having small public keys, relatively fast
signing and verifying operations, but larger signatures. Their signature sizes
however are tunable via their different parameters, which affords a space-time
tradeoff.

Stateful HBS schemes are based on the security of their underlying hash
functions and Merkle trees, which are not expected to be broken by the advent
of cryptographically relevant quantum computers. For this reason they have
been recommended by both NIST SP 800-208, and the NSA's CNSA 2.0 suite.

See these links for more info on stateful HBS support and wolfSSL/wolfCrypt:
- https://www.wolfssl.com/documentation/manuals/wolfssl/appendix07.html#post-quantum-stateful-hash-based-signatures
- https://github.com/wolfSSL/wolfssl-examples/tree/master/pq/stateful_hash_sig

### Supported PQ HBS Options

These two hash-based PQ signature options are supported:
- LMS: uses wolfcrypt implementation from `wc_lms.c`, and `wc_lms_impl.c`.
- XMSS: uses wolfcrypt implementation from `wc_xmss.c`, and `wc_xmss_impl.c`.

### LMS/HSS Config

A new LMS sim example has been added here:
```
config/examples/sim-lms.config
```

The `LMS_LEVELS`, `LMS_HEIGHT`, and `LMS_WINTERNITZ`, `IMAGE_SIGNATURE_SIZE`,
and (optionally) `IMAGE_HEADER_SIZE` must be set:

```
SIGN?=LMS
...
LMS_LEVELS=2
LMS_HEIGHT=5
LMS_WINTERNITZ=8
...
IMAGE_SIGNATURE_SIZE=2644
IMAGE_HEADER_SIZE?=5288
```

In LMS the signature size is a function of the parameters. Use the added helper
script `tools/lms/lms_siglen.sh` to calculate your signature length given your
LMS parameters:
```
$ ./tools/lms/lms_siglen.sh  2 5 8
levels:     2
height:     5
winternitz: 8
signature length: 2644
```

### XMSS/XMSS^MT Config

A new XMSS sim example has been added here:
```
config/examples/sim-xmss.config
```

The `XMSS_PARAMS`, `IMAGE_SIGNATURE_SIZE`, and (optionally) `IMAGE_HEADER_SIZE`
must be set:

```
SIGN?=XMSS
...
XMSS_PARAMS='XMSS-SHA2_10_256'
...
IMAGE_SIGNATURE_SIZE=2500
IMAGE_HEADER_SIZE?=5000
```

The `XMSS_PARAMS` may be any SHA256 parameter set string from Tables 10 and 11
from NIST SP 800-208.  Use the helper script `tools/xmss/xmss_siglen.sh` to
calculate your signature length given your XMSS/XMSS^MT parameter string, e.g.:
```
$ ./tools/xmss/xmss_siglen.sh XMSS-SHA2_10_256
parameter set:    XMSS-SHA2_10_256
signature length: 2500
```

```
$ ./tools/xmss/xmss_siglen.sh XMSSMT-SHA2_20/2_256
parameter set:    XMSSMT-SHA2_20/2_256
signature length: 4963
```

## Hybrid mode (classic + PQ)

wolfBoot supports a hybrid mode where both classic and PQ signatures are verified,
sequentially. This allows for a gradual transition from classic to PQ signatures,
and protects the secure boot mechanism from potential vulnerabilities in either of
the two ciphers in use.

The hybrid mode is enabled by setting a `SECONDARY_SIGN` option in the config file,
which specifies the secondary signature algorithm to be used. The secondary signature
option requires the option `WOLFBOOT_UNIVERSAL_KEYSTORE=1`, to ensure that the
keystore can handle both classic and PQ keys.

The secondary signature option can be set to any of the supported PQ signature options.

The example configuration provided in `config/examples/sim-ml-dsa-ecc-hybrid.config`
demonstrates the use of hybrid mode with both ML-DSA-65 and ECC-384.

### Hybrid signature

The `sign` tool supports hybrid signatures. It is sufficient to specify two
ciphers argument from command line. When you do that, the tool expects two private
key files path passed as arguments, instead of one.

The two public keys must be added to the same keystore. For this reason, the two
keypairs must be generated either at the same time, or in two subsequent steps.

### Generating keypairs for hybrid signatures

#### Generate both keys at the same time:

```
./tools/keytools/keygen --ml_dsa -g wolfboot_signing_private_key.der --ecc384 -g wolfboot_signing_second_private_key.der
```

both keys are automatically added to the same keystore.


#### Generate the two keys in separate steps

If you want to generate the keys in two steps, you will have to import the first
key in the new keystore. The first public key is stored in `keystore.der` during
the keypair generation:
```
./tools/keytools/keygen --ml_dsa -g wolfboot_signing_private_key.der
```

The first 16 bytes contain the keystore header, and must be skipped:

```
dd if=keystore.der of=ml_dsa-pubkey.der bs=1 skip=16
```

The new keypair can be generated (`-g`) while importing (`-i`)the old public key:

```
./tools/keytools/keygen --ml_dsa -i ml_dsa-pubkey.der -g --ecc384 wolfboot_signing_second_private_key.der
```

The keystore generated is now ready to be used by wolfBoot for hybrid signature verification.

### Hybrid signature of the firmware

In both examples above, the two private keys are now available in separate .der files.
The `sign` tool can now be used to sign the firmware with both keys:

```
./tools/sign/sign --ml_dsa --ecc384 --sha256 test-app/image.elf wolfboot_signing_private_key.der wolfboot_signing_second_private_key.der 1
```

The command should confirm that both keys are loaded, and that the image is signed and includes the hybrid signature:
```
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Parsing arguments in hybrid mode
Secondary private key: wolfboot_signing_second_private_key.der
Secondary cipher: ECC384
Version: 1
Update type:          Firmware
Input image:          test-app/image.elf
Selected cipher:      ML-DSA
Selected hash  :      SHA256
Private key:           wolfboot_signing_private_key.der
Secondary cipher:     ECC384
Secondary private key: wolfboot_signing_second_private_key.der
Output  image:        test-app/image_v1_signed.bin
Target partition id : 1
info: using ML-DSA parameters: 3
info: ML-DSA signature size: 3309
Key buffer size: 5984
info: ml-dsa priv len: 4032
info: ml-dsa pub len: 1952
Found ml-dsa key
image header size overridden by config value (8192 bytes)
Loading secondary key
Key buffer size: 144
Secondary ECC key, size: 96
image header size overridden by config value (8192 bytes)
Creating hybrid signature
[...]
```

The resulting image `image_v1_signed.bin` contains both signatures, and can be verified using a wolfBoot with hybrid signature support.

