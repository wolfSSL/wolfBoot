# KeyStore structure: support for multiple public keys

## What is wolfBoot KeyStore

KeyStore is the mechanism used by wolfBoot to store all the public keys used for
authenticating the signature of current firmware and updates.

wolfBoot's key generation tool can be used to generate one or more keys. By default,
when running `make` for the first time, a single key `wolfboot_signing_private_key.der`
is created, and added to the keystore module. This key should be used to sign any firmware
running on the target, as well as firmware update binaries.

Additionally, the `keygen` tool creates additional files with different representations
of the keystore
 - A .c file (src/keystore.c) which can be used to deploy public keys as part
   of the bootloader itself, by linking the keystore in `wolfboot.elf`
 - A .bin file (keystore.bin) which contains the keystore that can be hosted
   on a custom memory support. In order to access the keystore, a small driver is
   required (see section "Interface API" below).

## Default usage (built-in keystore)

By default, the keystore object in `src/keystore.c` is accessed by wolfboot by including
its symbols in the build.
Once generated, this file contains an array of structures describing each public
key that will be available to wolfBoot on the target system. Additionally, there are a few
functions that connect to the wolfBoot keystore API to access the details and the
content of the public key slots.

The public key is described by the following structure:

```
 struct keystore_slot {
     uint32_t slot_id;
     uint32_t key_type;
     uint32_t part_id_mask;
     uint32_t pubkey_size;
     uint8_t  pubkey[KEYSTORE_PUBKEY_SIZE];
 };

```

- `slot_id` is the incremental identifier for the key slot, starting from 0.

- `key_type` describes the algorithm of the key, e.g. `AUTH_KEY_ECC256` or `AUTH_KEY_RSA3072`

- `mask` describes the permissions for the key. It's a bitmap of the partition ids for which this key can be used for verification

- `pubkey_size` the size of the public key buffer

- `pubkey` the actual buffer containing the public key in its raw format

When booting, wolfBoot will automatically select the public key associated to the signed firmware image, check that it matches the permission mask for the partition id where the verification is running and then attempts to authenticate the signature of the image using the selected public key slot.

### Creating multiple keys

`keygen` accepts multiple filenames for private keys.

Two arguments:

 - `-g priv.der` generate new keypair, store the private key in priv.der, add the public key to the keystore
 - `-i pub.der` import an existing public key and add it to the keystore

Example of creation of a keystore with two ED25519 keys:

`./tools/keytools/keygen.py --ed25519 -g first.der -g second.der`

will create the following files:

 - `first.der` first private key
 - `second.der` second private key
 - `src/keystore.c` C keystore containing both public keys associated with `first.der`
     and `second.der`.

The `keystore.c` generated should look similar to this:

```
#define NUM_PUBKEYS 2
const struct keystore_slot PubKeys[NUM_PUBKEYS] = {

     /* Key associated to private key 'first.der' */
    {
        .slot_id = 0,
        .key_type = AUTH_KEY_ED25519,
        .part_id_mask = KEY_VERIFY_ALL,
        .pubkey_size = KEYSTORE_PUBKEY_SIZE_ED25519,
        .pubkey = {
            0x21, 0x7B, 0x8E, 0x64, 0x4A, 0xB7, 0xF2, 0x2F,
            0x22, 0x5E, 0x9A, 0xC9, 0x86, 0xDF, 0x42, 0x14,
            0xA0, 0x40, 0x2C, 0x52, 0x32, 0x2C, 0xF8, 0x9C,
            0x6E, 0xB8, 0xC8, 0x74, 0xFA, 0xA5, 0x24, 0x84
        },
    },

     /* Key associated to private key 'second.der' */
    {
        .slot_id = 1,
        .key_type = AUTH_KEY_ED25519,
        .part_id_mask = KEY_VERIFY_ALL,
        .pubkey_size = KEYSTORE_PUBKEY_SIZE_ED25519,
        .pubkey = {
            0x41, 0xC8, 0xB6, 0x6C, 0xB5, 0x4C, 0x8E, 0xA4,
            0xA7, 0x15, 0x40, 0x99, 0x8E, 0x6F, 0xD9, 0xCF,
            0x00, 0xD0, 0x86, 0xB0, 0x0F, 0xF4, 0xA8, 0xAB,
            0xA3, 0x35, 0x40, 0x26, 0xAB, 0xA0, 0x2A, 0xD5
        },
    },


};

```

### Permissions

By default, when a new keystore is created, the permissions mask is set
to `KEY_VERIFY_ALL`, which means that the key can be used to verify a firmware
targeting any partition id.

To restrict the permissions for single keys, it would be sufficient to change the value
of their `part_id_mask` attributes.

The `part_id_mask` value is a bitmask, where each bit represent a different partition.
The bit '0' is reserved for wolfBoot self-update, while typically the main firmware partition
is associated to id 1, so it requires a key with the bit '1' set. In other words, signing a
partition with `--id 3` would require turning on bit '3' in the mask, i.e. adding (1U << 3) to it.

Beside `KEY_VERIFY_ALL`, pre-defined mask values can also be used here:

- `KEY_VERIFY_APP_ONLY` only verifies the main application, with partition id 1
- `KEY_VERIFY_SELF_ONLY` this key can only be used to authenticate wolfBoot self-updates (id = 0)
- `KEY_VERIFY_ONLY_ID(N)` macro that can be used to restrict the usage of the key to a specific partition id `N`


### Importing public keys

Work in progress.

## Using KeyStore with external Key Vaults

It is possible to use an external NVM, a Key Vault or any generic support to
access the KeyStore. In this case, wolfBoot should not link the generated keystore.c directly,
but rather rely on an external interface, that exports the same API which
would be implemented by `keystore.c`.

The API consists of a few functions described below.

### Interface API

#### Number of keys in the keystore
`int keystore_num_pubkeys(void)`

Returns the number of slots in the keystore. At least one slot
should be populated if you want to authenticate your firmware today.
The interface assumes that the slots are numbered sequencially, from zero to 
`keystore_num_pubkeys() - 1`. Accessing those slots through this API should always
 return a valid public key.

#### Size of the public key in a slot
`int keystore_get_size(int id)`

Returns the size of the public key stored in the slot `id`.
In case of error, return a negative value.

#### Actual public key buffer (mapped/copied in memory)

`uint8_t *keystore_get_buffer(int id)`

Returns a pointer to an accessible area in memory, containing the buffer with the
public key associated to the slot `id`.

#### Permissions mask

`uint32_t keystore_get_mask(int id)`

Returns the permissions mask, as a 32-bit word, for the public key stored in the slot `id`.


