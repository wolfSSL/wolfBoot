## Signing firmware using Microsoft Azure Keyvault

Microsoft offer secure key management and provisioning tools, using keys stored
in HSMs. This mechanisms helps to centralize key management for several purposes,
including the support for signing payloads using the managed keys, which can be
used in combination with wolfBoot for provisioning public keys in a fleet of
devices.


### Preparing the keystore

wolfBoot can import public keys in the keystore using the `keygen` command line
tool provided. `keygen` supports both raw ECC keys and ASN.1 format (.der).

Azure allows to download the public keys in ASN.1 format to provision the device.
To retrieve  each pukey to use for firmware authentication in wolfBoot, use:

```
az keyvault key download --vault-name <vault-name> -n test-signing-key-1 -e DER -f public-key-1.der
```

A keystore can now be created importing the public keys and with `keygen`'s `-i`
(import) option. The option may be repeated multiple times to add more keys to 
the keystore.

```
tools/keytools/keygen --ecc256 -i public-key-1.der [-i public-key-2.der ...] 
```

### Signing the firmware image for wolfBoot

The signing operation using any external HSM is performed through three-steps,
as described in the relevant section in [Signing.md](signing.md).
In this section we describe the procedure to sign the firmware image using Azure key vault.


#### Obtaining the SHA256 digest

Step 1 consists in calling the `./sign` tool with the extra `--sha-only` argument,
to generate the digest to sign. The public key associated to the selected signing
key in the vault needs to be provided:

```
./sign --ecc256 --sha-only --sha256 test-app/image.bin public-keyi-1.der 1
```

To fit in a https REST request, the digest obtained must be encoded using base64:

```
DIGEST=$(cat test-app/image_v1_digest.bin | base64url_encode)

```

The variable `DIGEST` now contains a printable encoding of the key, which can be
attached to the request.

#### HTTPS request for signing the digest with the Key Vault


To prepare the request, first get an access token from the vault and store it in a variable:

```
ACCESS_TOKEN=$(az account get-access-token --resource "https://vault.azure.net" --query "accessToken" -o tsv)
```

Use the URL associated to the selected key vault:

```
KEY_IDENTIFIER="https://<vault-name>.vault.azure.net/keys/test-signing-key"
```

Perform the request using cURL, and store the result in a variable:

```
SIGNING_RESULT=$(curl -X POST \
    -s "${KEY_IDENTIFIER}/sign?api-version=7.4" \
    -H "Authorization: Bearer ${ACCESS_TOKEN}" \
    -H "Content-Type:application/json" \
    -H "Accept:application/json" \
    -d "{\"alg\":\"ES256\",\"value\":\"${DIGEST}\"}")
echo $SIGNING_RESULT
```

The field `.value` in the result contains the (base64 encoded) signature.
To extract the signature from the response, you can use a JSON parser:

```
SIGNATURE=$(jq -jn "$SIGNING_RESULT|.value")
```

The signature can now be decoded from base64 into a binary, so the
`sign` tool can incorporate the signature into the manifest header.

```
echo $SIGNATURE| base64url_decode > test-app/image_v1_digest.sig
```

#### Final step: create the signed firmware image

The 'third step' in the HSM three-steps procedure requires the `--manual-sign` option and the 
signature obtained through the Azure REST API.


``` 
./sign --ecc256 --sha256 --manual-sign test-app/image.bin test-signin-key_pub.der 1 test-app/image_v1_digest.sig
```

The resulting binary file `image_v1_signed.bin` will now contain a signed firmware
image that can be authenticated and staged by wolfBoot.
    
