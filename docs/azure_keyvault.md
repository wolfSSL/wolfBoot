# Azure Key Vault Integration for Firmware Signing

## Overview
wolfBoot integrates with Microsoft Azure Key Vault to provide secure firmware signing using Hardware Security Modules (HSMs). This integration enables:
- Centralized key management
- Secure key storage in HSMs
- Fleet-wide public key provisioning
- Automated firmware signing workflow

## Key Features
- Azure Key Vault integration
- HSM-backed key storage
- Support for ECC256 signatures
- ASN.1 DER format compatibility
- REST API-based signing process


## Setup and Configuration

### Keystore Preparation

#### 1. Download Public Keys
Retrieve public keys from Azure Key Vault in ASN.1 DER format:
```bash
az keyvault key download \
    --vault-name <vault-name> \
    -n test-signing-key-1 \
    -e DER \
    -f public-key-1.der
```

#### 2. Import Keys to wolfBoot
Create a keystore using wolfBoot's `keygen` tool:
```bash
# Import single key
./tools/keytools/keygen --ecc256 -i public-key-1.der

# Import multiple keys
./tools/keytools/keygen --ecc256 \
    -i public-key-1.der \
    -i public-key-2.der \
    -i public-key-3.der
```

**Note**: `keygen` supports both raw ECC keys and ASN.1 DER format

## Firmware Signing Process

The signing process with Azure Key Vault follows a three-step procedure as outlined in [Signing.md](signing.md). Below is the detailed workflow for Azure Key Vault integration.

### Step 1: Generate Image Digest

1. Create SHA256 digest using wolfBoot tools:
```bash
./tools/keytools/sign \
    --ecc256 \
    --sha-only \
    --sha256 \
    test-app/image.bin \
    public-key-1.der \
    1
```

2. Encode digest for HTTP transport:
```bash
DIGEST=$(cat test-app/image_v1_digest.bin | base64url_encode)
```

### Step 2: Sign with Azure Key Vault

1. Obtain Azure access token:
```bash
ACCESS_TOKEN=$(az account get-access-token \
    --resource "https://vault.azure.net" \
    --query "accessToken" \
    -o tsv)
```

2. Configure Key Vault endpoint:
```bash
KEY_IDENTIFIER="https://<vault-name>.vault.azure.net/keys/test-signing-key"
```

3. Request signature via REST API:
```bash
SIGNING_RESULT=$(curl -X POST \
    -s "${KEY_IDENTIFIER}/sign?api-version=7.4" \
    -H "Authorization: Bearer ${ACCESS_TOKEN}" \
    -H "Content-Type:application/json" \
    -H "Accept:application/json" \
    -d "{\"alg\":\"ES256\",\"value\":\"${DIGEST}\"}")
```

4. Extract and decode signature:
```bash
# Extract base64 signature
SIGNATURE=$(jq -jn "$SIGNING_RESULT|.value")

# Decode to binary
echo $SIGNATURE | base64url_decode > test-app/image_v1_digest.sig
```

### Step 3: Create Signed Firmware

Generate final signed firmware image:
```bash
./tools/keytools/sign \
    --ecc256 \
    --sha256 \
    --manual-sign \
    test-app/image.bin \
    test-signin-key_pub.der \
    1 \
    test-app/image_v1_digest.sig
```

The output file `image_v1_signed.bin` contains the firmware image with embedded signature, ready for deployment to wolfBoot-enabled devices.

## Related Documentation
- [Signing Process Details](signing.md)
- [Firmware Image Format](firmware_image.md)
- [Key Management](keystore.md)

