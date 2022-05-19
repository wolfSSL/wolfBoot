# Build wolfBoot as Library

## Example Build steps

```
ln -s config/examples/library.config .config
```

```
cat > include/target.h << EOF
#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#define WOLFBOOT_NO_PARTITIONS

#define WOLFBOOT_SECTOR_SIZE                 0x20000
#define WOLFBOOT_PARTITION_SIZE              0x20000

#endif /* !H_TARGETS_TARGET_ */

EOF
```

```
touch empty
make keytools
./tools/keytools/keygen --ed25519 src/ed25519_pub_key.c
./tools/keytools/sign --ed25519 --sha256 empty ed25519.der 1
```

```
make test-lib
./test-lib empty_v1_signed.bin

Firmware Valid
booting 0x5609e3526590(actually exiting)
```
