# wolfTPM PCR Policy pubkey sealing

## NOTE: Currently only ecc256 keys are supported for policy sealing

The pubkey that wolfBoot uses to verify its images can be stored in the TPM and tied to a hash of wolfBoot's text section. The key will be set to only be unsealed if wolfBoot is untampered.
To use this feature you need to define `WOLFBOOT_TPM_KEYSTORE?=1` and set `WOLFBOOT_TPM_KEYSTORE_NV_INDEX` and `WOLFBOOT_TPM_POLICY_NV_INDEX` to the NVM indicies supported by your TPM.
You can also override the default PCR index by defining `WOLFBOOT_TPM_PCR_INDEX`.
Once the config is set we need to generate the key

```
$ tools/keytools/keygen --ecc256 -g verification-key.bin
```

Then we need to isolate just the public part for later

```
$ dd if=verification-key.bin of=verification-public-key.bin bs=64 count=1
```

Now we have to fill the keystore with an empty key and make wolfBoot

```
$ dd if=/dev/zero of=empty-key.bin bs=64 count=1
$ tools/keytools/keygen --ecc256 -i empty-key.bin
$ make wolfboot.bin
```

Now that we have the wolfBoot image we can preseal the public key

```
$ tools/preseal/hash --sha256 wolfboot.bin
Digest output file: wolfBootDigest.bin
$ tools/preseal/preseal verification-public-key.bin wolfBootDigest.bin 25166336 25166337 16
```

If you need to seal a pubkey to a system with no filesystem or command line you can compile preseal with the following environment variables and run it without arguments.
You must create the hash first to add it during compilation:

```
$ tools/preseal/hash --sha256 wolfboot.bin
Digest output file: wolfBootDigest.bin
$ PUBKEY="c46f95fab07b0ad2412f4b18ba14c37314feb058f106a0c21728985cd1636db9f5b73a477da4f552c1470f8c83769981f33e23ec772a2582f82ea765b221d417" IMAGE_DIGEST="5b09b05afaf98e43fd59c0dc286fca8337604ec0815caad09fc0784c8a5e692b" SEAL_NV_INDEX=25166336 POLICY_DIGEST_NV_INDEX=25166337 PCR_INDEX=16 make
# Then on the target system running the resulting binary
$ ./preseal
```
