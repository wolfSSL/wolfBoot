# wolfTPM PCR Policy pubkey sealing

## NOTE: Currently only ecc256 keys are supported for policy sealing

The pubkey that wolfBoot uses to verify its images can be stored in the TPM and tied to the boot partition digest. The key will be set to only be unsealed if the image digest is untampered, and then when signature verification is done the untampered signature proves an untampered image.
To use this feature you need to define `WOLFBOOT_TPM_KEYSTORE?=1` and set `WOLFBOOT_TPM_KEYSTORE_NV_INDEX` and `WOLFBOOT_TPM_POLICY_NV_INDEX` to the NVM indices supported by your TPM. You can also override the default PCR index by defining `WOLFBOOT_TPM_PCR_INDEX`
Then you need to generate two keys, our policy signing key and the regular image verification key. We also need to sign a Policy Signed signature from aHash, a TPM2 element. This can be done automatically with the tools/preseal/keygen command:

```
$ tools/preseal/keygen
Generating keys and signed aHash for public key sealing...
Policy Signature: policy-signed.raw
Policy Public Key: policy-public-key.raw
Verification Public Key: public-key.raw
Verification Private Key: private-key.raw
```

Since we are doing manual signing we also need to import the policy-public-key.raw as the key included with the image:

```
tools/keytools/keygen --ecc256 -i policy-public-key.raw
make wolfboot.bin
```

Next we need to manually make the image signature, note that the header contains the policy pubkey instead of the real pubkey which will be sealed to the TPM:

```
tools/keytools/sign --ecc256 --sha256 --sha-only MY_IMAGE policy-public-key.raw 1
tools/preseal/sign private-key.raw test-app/image_v1_digest.bin
```

Next you need to create the image using the sign keytool with the --manual-sign option and the --policy-sign option:

```
tools/keytools/sign --ecc256 --sha256 --manual-sign --policy-signed MY_IMAGE policy-public-key.raw 1 image-signature.raw policy-signed.raw
```

Lastly, the pubkey needs to be sealed to the TPM. Note that the previous commands could be run from a separate system, this one must be run on a system connected to the TPM:

```
tools/preseal/preseal public-key.raw policy-public-key.raw policy-signed.raw test-app/image_v1_digest.bin 25166336 25166337 16
```

If you need to seal a pubkey to a system with no filesystem or command line you can compile preseal with the following environment variables and run it without arguments:

```
NO_FILESYSTEM=1 PUBKEY="c46f95fab07b0ad2412f4b18ba14c37314feb058f106a0c21728985cd1636db9f5b73a477da4f552c1470f8c83769981f33e23ec772a2582f82ea765b221d417" POLICY_PUBKEY="925a8a35dbe4bd419a35fbf9bd30ce1440380f6d3bcd9bc5558c1fa8adb88d92c88b797dfca39af80ca9729c61508813df8254575cef48674071cf75c30e6aa8" POLICY_SIGNED="4BDAC51C517C0F3D8EDBB632B514262C256E289565A2F1CD8605A4F775302C0CD7BBFE0242CAA536A30C87A37756C390DB9A2B06037B15476A509CA06B857B6D" IMAGE_DIGEST="5b09b05afaf98e43fd59c0dc286fca8337604ec0815caad09fc0784c8a5e692b" SEAL_NV_INDEX=25166336 POLICY_DIGEST_NV_INDEX=25166337 PCR_INDEX=16 make
# Then on the target system running the resulting binary
./preseal
```

## NOTE: the PolicySigned key is used in place of the real signing key and acts as an intermediate key to unseal the actual signing key form the TPM
