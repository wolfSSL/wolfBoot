#!/bin/bash
# Certificate Chain Generation Script (ECC P256 or RSA)
# Creates a certificate chain with root, intermediate, and leaf
# Outputs DER format files plus C arrays for embedding
# Optional: Use existing leaf private key with --leaf <file> argument

set -e  # Exit on any error

# Default output directory and algorithm
OUTPUT_DIR="test-dummy-ca"
ALGO="ecc256"  # Default to ECC P-256 keys

# Helper functions for key operations
generate_private_key() {
    local output_file=$1

    if [[ "$ALGO" == "ecc256" ]]; then
        openssl ecparam -genkey -name prime256v1 -noout -out "$output_file"
    elif [[ "$ALGO" == "rsa2048" ]]; then
        openssl genrsa -out "$output_file" 2048
    elif [[ "$ALGO" == "rsa4096" ]]; then
        openssl genrsa -out "$output_file" 4096
    fi
}

convert_key_to_der() {
    local input_file=$1
    local output_file=$2

    if [[ "$ALGO" == "ecc256" ]]; then
        openssl ec -in "$input_file" -outform DER -out "$output_file"
    elif [[ "$ALGO" == "rsa2048" || "$ALGO" == "rsa4096" ]]; then
        openssl rsa -in "$input_file" -outform DER -out "$output_file"
    fi
}

extract_public_key() {
    local cert_file=$1
    local pubkey_pem=$2
    local pubkey_der=$3

    # Extract public key from certificate (same for both algos)
    openssl x509 -in "$cert_file" -pubkey -noout > "$pubkey_pem"

    # Convert public key to DER format
    if [[ "$ALGO" == "ecc256" ]]; then
        openssl ec -pubin -in "$pubkey_pem" -outform DER -out "$pubkey_der"
    elif [[ "$ALGO" == "rsa2048" || "$ALGO" == "rsa4096" ]]; then
        openssl rsa -pubin -in "$pubkey_pem" -outform DER -out "$pubkey_der"
    fi
}

validate_key_format() {
    local key_file=$1

    if [[ "$ALGO" == "ecc256" ]]; then
        openssl ec -in "$key_file" -noout
    elif [[ "$ALGO" == "rsa2048" || "$ALGO" == "rsa4096" ]]; then
        openssl rsa -in "$key_file" -noout
    fi
}

# Parse command line arguments
LEAF_KEY_FILE=""
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
      ALGO="$2"
      if [[ "$ALGO" != "ecc256" && "$ALGO" != "rsa2048" && "$ALGO" != "rsa4096" ]]; then
        echo "Invalid algorithm: $ALGO. Use 'ecc256', 'rsa2048', or 'rsa4096'"
        exit 1
      fi
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--leaf <private_key_file>] [--outdir <output_directory>] [--algo <ecc256|rsa2048|rsa4096>]"
      exit 1
      ;;
  esac
done

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
echo "Generating Certificate Chain using $ALGO..."

# Step 1: Generate Root key and certificate
echo "Generating Root CA..."
generate_private_key "${OUTPUT_DIR}/temp/root.key.pem"

# Create PEM format root certificate (temporary)
openssl req -new -x509 -days 3650 -sha256 \
    -key ${OUTPUT_DIR}/temp/root.key.pem \
    -out ${OUTPUT_DIR}/temp/root.crt.pem \
    -subj "$ROOT_SUBJECT" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign,digitalSignature"

# Convert root key and certificate to DER format
convert_key_to_der "${OUTPUT_DIR}/temp/root.key.pem" "${OUTPUT_DIR}/root-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/root.crt.pem -outform DER -out ${OUTPUT_DIR}/root-cert.der

# Step 2: Generate Intermediate key and CSR
echo "Generating Intermediate CA..."
generate_private_key "${OUTPUT_DIR}/temp/intermediate.key.pem"

openssl req -new -sha256 \
    -key ${OUTPUT_DIR}/temp/intermediate.key.pem \
    -out ${OUTPUT_DIR}/temp/intermediate.csr \
    -subj "$INTERMEDIATE_SUBJECT"

# Step 3: Sign Intermediate certificate with Root
openssl x509 -req -days 1825 -sha256 \
    -in ${OUTPUT_DIR}/temp/intermediate.csr \
    -out ${OUTPUT_DIR}/temp/intermediate.crt.pem \
    -CA ${OUTPUT_DIR}/temp/root.crt.pem \
    -CAkey ${OUTPUT_DIR}/temp/root.key.pem \
    -CAcreateserial \
    -extfile <(printf "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign,digitalSignature")

# Convert intermediate key and certificate to DER format
convert_key_to_der "${OUTPUT_DIR}/temp/intermediate.key.pem" "${OUTPUT_DIR}/intermediate-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/intermediate.crt.pem -outform DER -out ${OUTPUT_DIR}/intermediate-cert.der

# Step 4: Handle Leaf key (generate or use existing)
echo "Handling Leaf Certificate..."
if [ -z "$LEAF_KEY_FILE" ]; then
    echo "Generating new leaf private key..."
    generate_private_key "${OUTPUT_DIR}/temp/leaf.key.pem"
else
    echo "Using provided leaf private key: $LEAF_KEY_FILE"
    cp "$LEAF_KEY_FILE" ${OUTPUT_DIR}/temp/leaf.key.pem
    # Ensure the key file is in the right format
    validate_key_format "${OUTPUT_DIR}/temp/leaf.key.pem"
fi

# Create CSR for leaf certificate
openssl req -new -sha256 \
    -key ${OUTPUT_DIR}/temp/leaf.key.pem \
    -out ${OUTPUT_DIR}/temp/leaf.csr \
    -subj "$LEAF_SUBJECT"

# Step 5: Sign Leaf certificate with Intermediate
openssl x509 -req -days 365 -sha256 \
    -in ${OUTPUT_DIR}/temp/leaf.csr \
    -out ${OUTPUT_DIR}/temp/leaf.crt.pem \
    -CA ${OUTPUT_DIR}/temp/intermediate.crt.pem \
    -CAkey ${OUTPUT_DIR}/temp/intermediate.key.pem \
    -CAcreateserial \
    -extfile <(printf "basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth")

# Convert leaf key and certificate to DER format
convert_key_to_der "${OUTPUT_DIR}/temp/leaf.key.pem" "${OUTPUT_DIR}/leaf-prvkey.der"
openssl x509 -in ${OUTPUT_DIR}/temp/leaf.crt.pem -outform DER -out ${OUTPUT_DIR}/leaf-cert.der

# Extract the public key from leaf certificate in DER format
echo "Extracting public key from leaf certificate..."
extract_public_key "${OUTPUT_DIR}/temp/leaf.crt.pem" "${OUTPUT_DIR}/temp/leaf_pubkey.pem" "${OUTPUT_DIR}/leaf-pubkey.der"

# Create raw DER format certificate chain
cat ${OUTPUT_DIR}/intermediate-cert.der ${OUTPUT_DIR}/leaf-cert.der > ${OUTPUT_DIR}/raw_chain.der

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
append_cert_array "${OUTPUT_DIR}/raw_chain.der" "RAW_CERT_CHAIN" "Raw Certificate Chain (Intermediate+Leaf) (DER format)"
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
echo "DER Format (Algorithm: $ALGO):"
echo "  Root CA certificate:        ${OUTPUT_DIR}/root-cert.der"
echo "  Root CA key:                ${OUTPUT_DIR}/root-prvkey.der"
echo "  Intermediate certificate:   ${OUTPUT_DIR}/intermediate-cert.der"
echo "  Intermediate key:           ${OUTPUT_DIR}/intermediate-prvkey.der"
echo "  Leaf certificate:           ${OUTPUT_DIR}/leaf-cert.der"
echo "  Leaf key:                   ${OUTPUT_DIR}/leaf-prvkey.der"
echo "  Raw chain:                  ${OUTPUT_DIR}/raw_chain.der"
echo "  Leaf public key:            ${OUTPUT_DIR}/leaf-pubkey.der"
echo ""
echo "C Header file:"
echo "  Certificate arrays:         ${OUTPUT_DIR}/gen_certificates.h"

# Clean up temporary files
rm -rf ${OUTPUT_DIR}/temp ${OUTPUT_DIR}/root.srl ${OUTPUT_DIR}/intermediate.srl
