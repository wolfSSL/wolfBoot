#!/bin/bash
# Certificate Chain Generation Script (ECC P-256/P-384 or RSA)
# Creates a certificate chain with root, intermediate, and leaf
# Outputs DER format files plus C arrays for embedding
# Optional: Use existing leaf private key with --leaf <file> argument
# Optional: Use distinct algorithms for the CA chain vs the leaf identity
# (e.g. ECC256 root/intermediate signing an RSA4096 leaf cert)

set -e  # Exit on any error

# Default output directory and algorithms
OUTPUT_DIR="test-dummy-ca"
CA_ALGO="ecc256"      # Default to ECC P-256 keys for root + intermediate
LEAF_ALGO=""          # Defaults to CA_ALGO if not explicitly set
CA_HASH="sha256"      # Hash used for cert signatures throughout the chain

# Whitelists
SUPPORTED_ALGOS="ecc256 ecc384 rsa2048 rsa3072 rsa4096"
SUPPORTED_HASHES="sha256 sha384 sha512"

is_supported_algo() {
    local algo=$1
    for a in $SUPPORTED_ALGOS; do
        if [[ "$a" == "$algo" ]]; then return 0; fi
    done
    return 1
}

is_supported_hash() {
    local h=$1
    for a in $SUPPORTED_HASHES; do
        if [[ "$a" == "$h" ]]; then return 0; fi
    done
    return 1
}

# Helper functions for key operations. Each takes the target algo as $1.
generate_private_key() {
    local algo=$1
    local output_file=$2

    case "$algo" in
        ecc256)  openssl ecparam -genkey -name prime256v1 -noout -out "$output_file" ;;
        ecc384)  openssl ecparam -genkey -name secp384r1  -noout -out "$output_file" ;;
        rsa2048) openssl genrsa -out "$output_file" 2048 ;;
        rsa3072) openssl genrsa -out "$output_file" 3072 ;;
        rsa4096) openssl genrsa -out "$output_file" 4096 ;;
        *) echo "Unsupported algo: $algo" >&2; exit 1 ;;
    esac
}

convert_key_to_der() {
    local algo=$1
    local input_file=$2
    local output_file=$3

    case "$algo" in
        ecc256|ecc384)
            openssl ec  -in "$input_file" -outform DER -out "$output_file"
            ;;
        rsa2048|rsa3072|rsa4096)
            openssl rsa -in "$input_file" -outform DER -out "$output_file"
            ;;
        *) echo "Unsupported algo: $algo" >&2; exit 1 ;;
    esac
}

extract_public_key() {
    local algo=$1
    local cert_file=$2
    local pubkey_pem=$3
    local pubkey_der=$4

    # Extract public key from certificate (same command for all supported algos)
    openssl x509 -in "$cert_file" -pubkey -noout > "$pubkey_pem"

    # Convert public key to DER format
    case "$algo" in
        ecc256|ecc384)
            openssl ec  -pubin -in "$pubkey_pem" -outform DER -out "$pubkey_der"
            ;;
        rsa2048|rsa3072|rsa4096)
            openssl rsa -pubin -in "$pubkey_pem" -outform DER -out "$pubkey_der"
            ;;
        *) echo "Unsupported algo: $algo" >&2; exit 1 ;;
    esac
}

validate_key_format() {
    local algo=$1
    local key_file=$2

    case "$algo" in
        ecc256|ecc384)
            openssl ec  -in "$key_file" -noout
            ;;
        rsa2048|rsa3072|rsa4096)
            openssl rsa -in "$key_file" -noout
            ;;
        *) echo "Unsupported algo: $algo" >&2; exit 1 ;;
    esac
}

usage() {
    cat <<EOT
Usage: $0 [options]

Options:
  --algo <algo>          Set both CA and leaf algos (legacy single-algo shortcut).
  --ca-algo <algo>       Algo for root + intermediate keys.
  --leaf-algo <algo>     Algo for the leaf key (defaults to --ca-algo).
  --ca-hash <hash>       Hash used for cert signatures (default: sha256).
  --leaf <key_file>      Use existing leaf private key (must match --leaf-algo).
  --outdir <dir>         Output directory (default: test-dummy-ca).

Supported algos: $SUPPORTED_ALGOS
Supported hashes: $SUPPORTED_HASHES
EOT
}

# Parse command line arguments
LEAF_KEY_FILE=""
ALGO_LEGACY_SET=0
ALGO_PER_LEVEL_SET=0
while [[ $# -gt 0 ]]; do
  case $1 in
    --leaf)
      LEAF_KEY_FILE="$2"
      shift 2
      ;;
    --outdir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --algo)
      if ! is_supported_algo "$2"; then
        echo "Invalid algorithm: $2. Supported: $SUPPORTED_ALGOS" >&2
        exit 1
      fi
      CA_ALGO="$2"
      LEAF_ALGO="$2"
      ALGO_LEGACY_SET=1
      shift 2
      ;;
    --ca-algo)
      if ! is_supported_algo "$2"; then
        echo "Invalid CA algorithm: $2. Supported: $SUPPORTED_ALGOS" >&2
        exit 1
      fi
      CA_ALGO="$2"
      ALGO_PER_LEVEL_SET=1
      shift 2
      ;;
    --leaf-algo)
      if ! is_supported_algo "$2"; then
        echo "Invalid leaf algorithm: $2. Supported: $SUPPORTED_ALGOS" >&2
        exit 1
      fi
      LEAF_ALGO="$2"
      ALGO_PER_LEVEL_SET=1
      shift 2
      ;;
    --ca-hash)
      if ! is_supported_hash "$2"; then
        echo "Invalid CA hash: $2. Supported: $SUPPORTED_HASHES" >&2
        exit 1
      fi
      CA_HASH="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

if [ "$ALGO_LEGACY_SET" -eq 1 ] && [ "$ALGO_PER_LEVEL_SET" -eq 1 ]; then
    echo "Error: --algo cannot be combined with --ca-algo or --leaf-algo." >&2
    echo "  Use --algo as a shortcut to set both, or use the per-level flags." >&2
    exit 1
fi

# Default leaf algo to CA algo when not explicitly set
if [[ -z "$LEAF_ALGO" ]]; then
    LEAF_ALGO="$CA_ALGO"
fi

# Configuration
ROOT_SUBJECT="/C=US/ST=California/L=San Francisco/O=MyOrganization/OU=Root CA/CN=My Root CA"
INTERMEDIATE_SUBJECT="/C=US/ST=California/L=San Francisco/O=MyOrganization/OU=Intermediate CA/CN=My Intermediate CA"
LEAF_SUBJECT="/C=US/ST=California/L=San Francisco/O=MyOrganization/OU=Services/CN=service.example.com"

# Create directory structure
echo "Creating directory structure..."
mkdir -p ${OUTPUT_DIR}/temp

##################
# GENERATE CHAIN
##################
echo "Generating Certificate Chain (ca-algo=$CA_ALGO, leaf-algo=$LEAF_ALGO, ca-hash=$CA_HASH)..."

# Step 1: Generate Root key and certificate (CA algo)
echo "Generating Root CA..."
generate_private_key "$CA_ALGO" "${OUTPUT_DIR}/temp/root.key.pem"

# Create PEM format root certificate (temporary)
openssl req -new -x509 -days 3650 -$CA_HASH \
    -key ${OUTPUT_DIR}/temp/root.key.pem \
    -out ${OUTPUT_DIR}/temp/root.crt.pem \
    -subj "$ROOT_SUBJECT" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign,digitalSignature"

# Convert root key and certificate to DER format
convert_key_to_der "$CA_ALGO" "${OUTPUT_DIR}/temp/root.key.pem" "${OUTPUT_DIR}/root-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/root.crt.pem -outform DER -out ${OUTPUT_DIR}/root-cert.der

# Step 2: Generate Intermediate key and CSR (CA algo)
echo "Generating Intermediate CA..."
generate_private_key "$CA_ALGO" "${OUTPUT_DIR}/temp/intermediate.key.pem"

openssl req -new -$CA_HASH \
    -key ${OUTPUT_DIR}/temp/intermediate.key.pem \
    -out ${OUTPUT_DIR}/temp/intermediate.csr \
    -subj "$INTERMEDIATE_SUBJECT"

# Step 3: Sign Intermediate certificate with Root
openssl x509 -req -days 1825 -$CA_HASH \
    -in ${OUTPUT_DIR}/temp/intermediate.csr \
    -out ${OUTPUT_DIR}/temp/intermediate.crt.pem \
    -CA ${OUTPUT_DIR}/temp/root.crt.pem \
    -CAkey ${OUTPUT_DIR}/temp/root.key.pem \
    -CAcreateserial \
    -extfile <(printf "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign,digitalSignature")

# Convert intermediate key and certificate to DER format
convert_key_to_der "$CA_ALGO" "${OUTPUT_DIR}/temp/intermediate.key.pem" "${OUTPUT_DIR}/intermediate-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/intermediate.crt.pem -outform DER -out ${OUTPUT_DIR}/intermediate-cert.der

# Step 4: Handle Leaf key (generate or use existing) - LEAF algo
echo "Handling Leaf Certificate..."
if [ -z "$LEAF_KEY_FILE" ]; then
    echo "Generating new leaf private key (algo=$LEAF_ALGO)..."
    generate_private_key "$LEAF_ALGO" "${OUTPUT_DIR}/temp/leaf.key.pem"
else
    echo "Using provided leaf private key: $LEAF_KEY_FILE (algo=$LEAF_ALGO)"
    cp "$LEAF_KEY_FILE" ${OUTPUT_DIR}/temp/leaf.key.pem
    # Ensure the key file is in the right format for the declared leaf algo
    validate_key_format "$LEAF_ALGO" "${OUTPUT_DIR}/temp/leaf.key.pem"
fi

# Create CSR for leaf certificate (CSR is signed by leaf key, but the
# resulting cert signature is set by the CA when signing - so the CSR
# self-signature uses CA_HASH for consistency).
openssl req -new -$CA_HASH \
    -key ${OUTPUT_DIR}/temp/leaf.key.pem \
    -out ${OUTPUT_DIR}/temp/leaf.csr \
    -subj "$LEAF_SUBJECT"

# Step 5: Sign Leaf certificate with Intermediate (uses CA_HASH)
openssl x509 -req -days 365 -$CA_HASH \
    -in ${OUTPUT_DIR}/temp/leaf.csr \
    -out ${OUTPUT_DIR}/temp/leaf.crt.pem \
    -CA ${OUTPUT_DIR}/temp/intermediate.crt.pem \
    -CAkey ${OUTPUT_DIR}/temp/intermediate.key.pem \
    -CAcreateserial \
    -extfile <(printf "basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth")

# Convert leaf key and certificate to DER format
convert_key_to_der "$LEAF_ALGO" "${OUTPUT_DIR}/temp/leaf.key.pem" "${OUTPUT_DIR}/leaf-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/leaf.crt.pem -outform DER -out ${OUTPUT_DIR}/leaf-cert.der

# Extract the public key from leaf certificate in DER format
echo "Extracting public key from leaf certificate..."
extract_public_key "$LEAF_ALGO" "${OUTPUT_DIR}/temp/leaf.crt.pem" "${OUTPUT_DIR}/temp/leaf_pubkey.pem" "${OUTPUT_DIR}/leaf-pubkey.der"

# Create raw DER format certificate chain
cat ${OUTPUT_DIR}/intermediate-cert.der ${OUTPUT_DIR}/leaf-cert.der > ${OUTPUT_DIR}/raw-chain.der

##################################
# GENERATE C ARRAYS FOR EMBEDDING
##################################
echo "Generating C arrays for embedding in programs..."

# Create a header file for certificates
HEADER_FILE="${OUTPUT_DIR}/gen_certificates.h"

# Initialize the header file with header guards and includes
cat > "${HEADER_FILE}" << 'EOT'
/*
 * Certificate arrays for embedded SSL/TLS applications
 * Generated by OpenSSL certificate chain script
 */

#ifndef GEN_CERTIFICATES_H
#define GEN_CERTIFICATES_H

#include <stddef.h>

EOT

# Function to append a certificate array to the header file
append_cert_array() {
    local infile=$1
    local arrayname=$2
    local description=$3

    echo "/* ${description} */" >> "${HEADER_FILE}"
    echo "const unsigned char ${arrayname}[] = {" >> "${HEADER_FILE}"

    # Use xxd instead of hexdump for more reliable output
    xxd -i < "${infile}" | grep -v "unsigned char" | grep -v "unsigned int" | \
        sed 's/  0x/0x/g' >> "${HEADER_FILE}"

    echo "};" >> "${HEADER_FILE}"
    echo "const size_t ${arrayname}_len = sizeof(${arrayname});" >> "${HEADER_FILE}"
    echo "" >> "${HEADER_FILE}"
}

### Add certificates to the header file
echo "/* Certificates */" >> "${HEADER_FILE}"
append_cert_array "${OUTPUT_DIR}/root-cert.der" "ROOT_CERT" "Root CA Certificate (DER format)"
append_cert_array "${OUTPUT_DIR}/intermediate-cert.der" "INTERMEDIATE_CERT" "Intermediate CA Certificate (DER format)"
append_cert_array "${OUTPUT_DIR}/leaf-cert.der" "LEAF_CERT" "Leaf/Server Certificate (DER format)"
append_cert_array "${OUTPUT_DIR}/raw-chain.der" "RAW_CERT_CHAIN" "Raw Certificate Chain (Intermediate+Leaf) (DER format)"
# Add leaf certificate public key
append_cert_array "${OUTPUT_DIR}/leaf-pubkey.der" "LEAF_PUBKEY" "Leaf Certificate Public Key (DER format)"

# Close the header guard
echo "#endif /* GEN_CERTIFICATES_H */" >> "${HEADER_FILE}"

echo "Generated C header file with certificate arrays: ${HEADER_FILE}"

# Display verification information
echo ""
echo "=== Certificate Chain Generation Complete ==="
echo ""

# Verify Chain
echo "=== Verifying Certificate Chain ==="
echo "Verifying intermediate certificate against root:"
openssl verify -CAfile ${OUTPUT_DIR}/temp/root.crt.pem ${OUTPUT_DIR}/temp/intermediate.crt.pem

echo ""
echo "Verifying leaf certificate against intermediate and root:"
openssl verify -CAfile ${OUTPUT_DIR}/temp/root.crt.pem -untrusted ${OUTPUT_DIR}/temp/intermediate.crt.pem ${OUTPUT_DIR}/temp/leaf.crt.pem

# Display generated files summary
echo ""
echo "=== Generated Files Summary ==="
echo ""
echo "DER Format (CA algo: $CA_ALGO, leaf algo: $LEAF_ALGO, CA hash: $CA_HASH):"
echo "  Root CA certificate:        ${OUTPUT_DIR}/root-cert.der"
echo "  Root CA key:                ${OUTPUT_DIR}/root-prvkey.der"
echo "  Intermediate certificate:   ${OUTPUT_DIR}/intermediate-cert.der"
echo "  Intermediate key:           ${OUTPUT_DIR}/intermediate-prvkey.der"
echo "  Leaf certificate:           ${OUTPUT_DIR}/leaf-cert.der"
echo "  Leaf key:                   ${OUTPUT_DIR}/leaf-prvkey.der"
echo "  Raw chain:                  ${OUTPUT_DIR}/raw-chain.der"
echo "  Leaf public key:            ${OUTPUT_DIR}/leaf-pubkey.der"
echo ""
echo "C Header file:"
echo "  Certificate arrays:         ${OUTPUT_DIR}/gen_certificates.h"

# Clean up temporary files
rm -rf ${OUTPUT_DIR}/temp ${OUTPUT_DIR}/root.srl ${OUTPUT_DIR}/intermediate.srl
