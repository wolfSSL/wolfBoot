# Post-Quantum Signatures

wolfBoot is adding support for post-quantum signatures. At present, support
for LMS/HSS signatures has been added.

## LMS/HSS

LMS/HSS is a post-quantum stateful hash-based signature scheme (HBS). It
is known for having small public and private keys, but larger signatures.
The signature size is tunable via the different LMS parameters.

Stateful HBS schemes are based on the security of their underlying hash
functions and Merkle trees, which are not expected to be broken by the advent
of cryptographically relevant quantum computers.

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

### Config

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
$./tools/lms/lms_siglen.sh
levels:      3
height:      5
winternitz:  8
#
total_len:   3992
```

### More Info

See these links for more info on LMS and wolfSSL/wolfCrypt:
- https://www.wolfssl.com/documentation/manuals/wolfssl/appendix07.html#post-quantum-stateful-hash-based-signatures
- https://github.com/wolfSSL/wolfssl-examples/tree/master/pq/stateful_hash_sig

