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
been recommended by both NIST SP 800-208, and the NSA’s CNSA 2.0 suite.

See these links for more info on stateful HBS support and wolfSSL/wolfCrypt:
- https://www.wolfssl.com/documentation/manuals/wolfssl/appendix07.html#post-quantum-stateful-hash-based-signatures
- https://github.com/wolfSSL/wolfssl-examples/tree/master/pq/stateful_hash_sig

### Supported PQ HBS Options

These four PQ signature options are supported:
- LMS: uses wolfcrypt implementation from `wc_lms.c`, and `wc_lms_impl.c`.
- XMSS: uses wolfcrypt implementation from `wc_xmss.c`, and `wc_xmss_impl.c`.
- ext_LMS: uses external integration from `ext_lms.c`.
- ext_XMSS: uses external integration from `ext_xmss.c`.

The wolfcrypt implementations are more performant and are recommended.
The external integrations are experimental and for testing interoperability.

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

## Building the external PQ Integrations

### ext_LMS Support

The external LMS/HSS support in wolfCrypt requires the hash-sigs library ( https://github.com/cisco/hash-sigs ).
Use the following procedure to prepare hash-sigs for building with wolfBoot:

```
$ cd lib
$ mkdir hash-sigs
$ls
 CMakeLists.txt  hash-sigs  wolfssl  wolfTPM
$ cd hash-sigs
$ mkdir lib
$ git clone https://github.com/cisco/hash-sigs.git src
$ cd src
$ git checkout b0631b8891295bf2929e68761205337b7c031726
$ git apply ../../../tools/lms/0001-Patch-to-support-wolfBoot-LMS-build.patch
```

Nothing more is needed, as wolfBoot will automatically produce the required
hash-sigs build artifacts.

Note: the hash-sigs project only builds static libraries:
- hss_verify.a: a single-threaded verify-only static lib.
- hss_lib.a: a single-threaded static lib.
- hss_lib_thread.a: a multi-threaded static lib.

The keytools utility links against `hss_lib.a`, as it needs full
keygen, signing, and verifying functionality. However wolfBoot
links directly with the subset of objects in the `hss_verify.a`
build rule, as it only requires verify functionality.


### ext_XMSS Support

The external XMSS/XMSS^MT support in wolfCrypt requires a patched version of the
xmss-reference library ( https://github.com/XMSS/xmss-reference.git ).
Use the following procedure to prepare xmss-reference for building with
wolfBoot:

```
$ cd lib
$ git clone https://github.com/XMSS/xmss-reference.git xmss
$ ls
CMakeLists.txt  wolfPKCS11  wolfTPM  wolfssl  xmss
$ cd xmss
$ git checkout 171ccbd26f098542a67eb5d2b128281c80bd71a6
$ git apply ../../tools/xmss/0001-Patch-to-support-wolfSSL-xmss-reference-integration.patch
```

The patch creates an addendum readme, `patch_readme.md`, with further comments.

Nothing more is needed beyond the patch step, as wolfBoot will handle building
the xmss build artifacts it requires.
