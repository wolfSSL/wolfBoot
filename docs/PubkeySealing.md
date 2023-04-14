# wolfTPM PCR Policy pubkey sealing

## NOTE: Currently only ecc256 keys are supported for policy sealing

The pubkey that wolfBoot uses to verify its images can be stored in the TPM and will be set to only be unsealed if the image signature is untampered, and then when signature verification is done the untampered signature proves an untampered image.
To use this feature you need to define `WOLFBOOT_TPM_KEYSTORE?=1` and set `WOLFBOOT_TPM_KEYSTORE_NV_INDEX` and `WOLFBOOT_TPM_POLICY_NV_INDEX` to the NVM indicies supported by your TPM. You can also override the default PCR index by defining `WOLFBOOT_TPM_PCR_INDEX`
Next you need to use external signing to sign the image you want to include and then program the TPM with the public portion of the key, keeping the private part separate. Next you need to hash an expiration date and then sign it to create a PolicySigned signature. There are additional elements as documented in the TPM2 manuals but they aren't used in this case. You can create this signature with no expiration date with the following commands:

```
echo -n -e '\x00\x00\x00\x00' > zeroExpiry
openssl dgst -sha256 -sign policy_signed_ecc.pem -out policySigned zeroExpiry
```

Next you need to create the image using the sign keytool with the --manual-sign option and the --policy-sign option:

```
tools/keytools/sign --ecc256 --sha256 --manual-sign --policy-signed my_image.bin policy_signed_ecc.raw 1 my_image_sig policySigned
```

## NOTE: the PolicySigned key is used in place of the real signing key and acts as an intermediate key to unseal the actual signing key form the TPM
