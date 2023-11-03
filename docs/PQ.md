# Post-Quantum Signatures

wolfBoot is adding support for post-quantum signatures. At present, support
for LMS/HSS (https://www.rfc-editor.org/rfc/rfc8554.html), and XMSS/XMSS^MT
(https://www.rfc-editor.org/rfc/rfc8391.html) has been added.

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


## LMS/HSS


### Building with LMS Support

LMS/HSS support in wolfCrypt requires the hash-sigs library ( https://github.com/cisco/hash-sigs ).
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

### LMS Config

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

## XMSS/XMSS^MT

### Building with XMSS Support

XMSS/XMSS^MT support in wolfCrypt requires a patched version of the
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

### XMSS Config
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
