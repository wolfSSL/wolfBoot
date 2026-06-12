#!/usr/bin/env python3
# Independent cross-check of a wolfCOSE-authored SUIT envelope, using
# implementations other than wolfCOSE: cbor2 (CBOR), hashlib (SHA-256), and
# cryptography (ECDSA / RFC 9052 Sig_structure). This is the interop step that
# turns "follows the SUIT/COSE format" into "verifiable by independent tools".
#
#   ./tests/suit_host_test.sh                       # writes /tmp/suit_envelope.cbor
#   python3 tests/suit_cross_check.py [envelope]    # default: frozen vector
import sys
import hashlib
import cbor2
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives import hashes

# Fixed P-256 test public key (matches tests/suit_test.c TEST_QX/TEST_QY).
QX = bytes.fromhex(
    "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6")
QY = bytes.fromhex(
    "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299")

DEFAULT = "tests/vectors/suit_envelope.cbor"
path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
data = open(path, "rb").read()

# 1. Envelope is valid CBOR with the SUIT envelope members.
env = cbor2.loads(data)
assert isinstance(env, dict) and 2 in env and 3 in env, "envelope map keys 2,3"
print("OK  envelope: valid CBOR map with auth-wrapper(2) + manifest(3)")

auth = cbor2.loads(env[2])
assert isinstance(auth, list) and len(auth) >= 2, "SUIT_Authentication array"
suit_digest_bytes = auth[0]
cose = cbor2.loads(auth[1])
assert getattr(cose, "tag", None) == 18, "COSE_Sign1_Tagged (#6.18)"
cose = cose.value
manifest_bytes = env[3]

# 2. SUIT_Digest binds hash(manifest) [independent SHA-256].
sd = cbor2.loads(suit_digest_bytes)
assert sd[0] == -16, "digest alg SHA-256 (COSE -16)"
assert hashlib.sha256(manifest_bytes).digest() == sd[1], "digest binds manifest"
print("OK  digest:   SUIT_Digest == SHA-256(manifest) [independent hashlib]")

# 3. COSE_Sign1 ES256 signature over the RFC 9052 Sig_structure (detached
#    payload = the bstr-wrapped SUIT_Digest) [independent cryptography].
prot, sig = cose[0], cose[3]
sig_structure = cbor2.dumps(["Signature1", prot, b"", suit_digest_bytes])
r = int.from_bytes(sig[:32], "big")
s = int.from_bytes(sig[32:], "big")
der = utils.encode_dss_signature(r, s)
pub = ec.EllipticCurvePublicNumbers(
    int.from_bytes(QX, "big"), int.from_bytes(QY, "big"),
    ec.SECP256R1()).public_key()
pub.verify(der, sig_structure, ec.ECDSA(hashes.SHA256()))
print("OK  cose:     COSE_Sign1 ES256 verifies [independent cryptography]")

# 4. Manifest decodes and carries the expected members.
man = cbor2.loads(manifest_bytes)
assert 3 in man, "manifest has suit-common(3)"
print("OK  manifest: decodes; keys =", sorted(man.keys()), "[independent cbor2]")
print("CROSS-CHECK PASSED: wolfCOSE SUIT envelope verified by independent tools")
