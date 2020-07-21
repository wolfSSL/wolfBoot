## Encrypted external partitions

wolfBoot offers the possibility to encrypt the content of the entire UPDATE partition,
by using a pre-shared symmetric key which can be temporarily stored in a safer non-volatile memory area.

SWAP partition is also temporarily encrypted using the same key, so a dump of the external flash won't reveal
any content of the firmware update packages.

### Rationale

Encryption of external partition works at the level of the external flash interface.

All write calls to external partitions from the bootloader perform an additional encryption step
to hide the actual content of the external non-volatile memory.

Viceversa, all read operations will decrypt the data stored when the feature is enabled.

An extra option is provided to the `sign.py` sign tool to encrypt the firmware update after signing it, so
that it can be stored as is in the external memory by the application, and will be decrypted by the bootloader
in order to verify the update and begin the installation.


### Temporary key storage

By default, wolfBoot will store the pre-shared symmetric key used for encryption in a temporary area
on the internal flash. This allows read-out protections to be used to hide the temporary key.

Alternatively, more secure mechanisms are available to store the temporary key in a different key storage
(e.g. using a hardware security module or a TPM device).

The temporary key can be set at run time by the application, and will be used exactly once by the bootloader
to verify and install the next update. The key can be for example received from a back-end during the update
process using secure communication, and set by the application, using `libwolfboot` API, to be used by
wolfBoot upon next boot.

Aside from setting the temporary key, the update mechanism remains the same for distributing, uploading and
installing firmware updates through wolfBoot.

### Libwolfboot API

The API to communicate with the bootloader from the application is expanded when this feature is enabled,
to allow setting a temporary key to process the next update.

The functions

```
int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce);
int wolfBoot_erase_encrypt_key(void);
```

can be used to set a temporary encryption key for the external partition, or erase a key previously set, respectively.

Moreover, using `libwolfboot` to access the external flash with wolfboot hal from the application will not
use encryption. This way the received update, already encrypted at origin, can be stored in the external
memory unchanged, and retrieved in its encrypted format, e.g. to verify that the transfer has been successful before
reboot.

### Symmetric encryption algorithm

The algorithm currently used to encrypt and decrypt data in external partitions
is Chacha20-256.

 - The `key` provided to `wolfBoot_set_encrypt_key()` must be exactly 32 Bytes long.
 - The `nonce` argument must be a 96-bit (12 Bytes) randomly generated buffer, to be used as IV for encryption and decryption.

## Example usage

### Signing and encrypting the update bundle

The `sign.py` tool can sign and encrypt the image with a single command.
The encryption secret is provided in a binary file that should contain a concatenation of
a 32B ChaCha-256 key and a 12B nonce.

In the examples provided, the test application uses the following parameters:

```
key = "0123456789abcdef0123456789abcdef"
nonce = "0123456789ab"
```

So it is easy to prepare the encryption secret in the test scripts or from the command line using:

```
echo -n "0123456789abcdef0123456789abcdef0123456789ab" > enc_key.der
```

The `sign.py` script can now be invoked to produce a signed+encrypted image, by using the extra argument `--encrypt` followed by the
secret file:

```
./tools/keytools/sign.py --encrypt enc_key.der test-app/image.bin ecc256.der 24

```

which will produce as output the file `test-app/image_v24_signed_and_encrypted.bin`, that can be transferred to the target's external device.


### API usage in the application

When transferring the image, the application can still use the libwolfboot API functions to store the encrypted firmware. When called from the application,
the function `ext_flash_write` will store the payload unencrypted.

In order to trigger an update, before calling `wolfBoot_update_trigger` it is necessary to set the temporary key used by the bootloader by calling `wolfBoot_set_encrypt_key`.

An example of encrypted update trigger can be found in the [stm32wb test application source code](../test-app/app_stm32wb.c).




