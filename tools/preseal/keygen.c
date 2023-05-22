#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/ecc.h>

#define ECC_KEY_SIZE 32

int main(void)
{
    int rc;
    int fd;
    WC_RNG rng[1];
    wc_Sha256 sha[1];
    uint8_t zeroExpiry[4];
    uint8_t hash[WC_SHA256_DIGEST_SIZE];
    uint8_t sig[ECC_KEY_SIZE * 2];
    uint8_t qx[ECC_KEY_SIZE];
    uint32_t qxSz = ECC_KEY_SIZE;
    uint8_t qy[ECC_KEY_SIZE];
    uint32_t qySz = ECC_KEY_SIZE;
    uint8_t d[ECC_KEY_SIZE];
    uint32_t dSz = ECC_KEY_SIZE;
    ecc_key policyKey[1];
    mp_int r, s;

    XMEMSET(zeroExpiry, 0, sizeof(zeroExpiry));

    printf("Generating keys and signed aHash for public key sealing...\n");

    rc = wc_InitRng(rng);
    if (rc != 0) {
        printf("wc_InitRng failed\n");
        goto exit;
    }

    /* hash the zero expiry */
    rc = wc_InitSha256(sha);
    if (rc != 0) {
        printf("wc_InitSha256 failed\n");
        goto exit;
    }

    rc = wc_Sha256Update(sha, zeroExpiry, sizeof(zeroExpiry));
    if (rc != 0) {
        printf("wc_Sha256Update failed\n");
        goto exit;
    }

    rc = wc_Sha256Final(sha, hash);
    if (rc != 0) {
        printf("wc_Sha256Final failed\n");
        goto exit;
    }

    /* create the policy key */
    rc = wc_ecc_init(policyKey);
    if (rc != 0) {
        printf("wc_ecc_init failed\n");
        goto exit;
    }

    rc = wc_ecc_make_key(rng, ECC_KEY_SIZE, policyKey);
    if (rc != 0) {
        printf("wc_ecc_make_key failed\n");
        goto exit;
    }

    /* sign the expiry with the policyKey */
    mp_init(&r);
    mp_init(&s);

    rc = wc_ecc_sign_hash_ex(hash, sizeof(hash), rng, policyKey, &r, &s);

    mp_to_unsigned_bin(&r, sig);
    mp_to_unsigned_bin(&s, sig + ECC_KEY_SIZE);

    mp_clear(&r);
    mp_clear(&s);

    if (rc != 0) {
        printf("wc_ecc_sign_hash_ex failed %d\n", rc);
        goto exit;
    }

    /* write the signature */
    fd = open("policy-signed.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open policy-signed.raw failed\n");
        goto exit;
    }

    rc = write(fd, sig, ECC_KEY_SIZE * 2);
    if (rc != sizeof(sig)) {
        printf("write qx failed\n");
        goto exit;
    }

    close(fd);

    printf("Policy Signature: policy-signed.raw\n");

    /* export the public part of the key */
    rc = wc_ecc_export_public_raw(policyKey, qx, &qxSz, qy, &qySz);
    if (rc != 0) {
        printf("wc_ecc_export_public_raw failed\n");
        goto exit;
    }

    fd = open("policy-public-key.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open policy-public-key.raw failed\n");
        goto exit;
    }

    rc = write(fd, qx, qxSz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    rc = write(fd, qy, qySz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    close(fd);

    printf("Policy Public Key: policy-public-key.raw\n");

    /* export the full key */
    rc = wc_ecc_export_private_raw(policyKey, qx, &qxSz, qy, &qySz, d, &dSz);
    if (rc != 0) {
        printf("wc_ecc_export_private_raw failed\n");
        goto exit;
    }

    rc = wc_ecc_export_public_raw(policyKey, qx, &qxSz, qy, &qySz);
    if (rc != 0) {
        printf("wc_ecc_export_public_raw failed\n");
        goto exit;
    }

    fd = open("policy-private-key.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open policy-private-key.raw failed\n");
        goto exit;
    }

    rc = write(fd, qx, qxSz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    rc = write(fd, qy, qySz);
    if (rc != (int)qxSz) {
        printf("write qy failed\n");
        goto exit;
    }

    rc = write(fd, d, dSz);
    if (rc != (int)qxSz) {
        printf("write d failed\n");
        goto exit;
    }

    close(fd);

    printf("Policy Private Key: policy-private-key.raw\n");

    /* make the signing key */
    wc_ecc_free(policyKey);

    rc = wc_ecc_init(policyKey);
    if (rc != 0) {
        printf("wc_ecc_init failed\n");
        goto exit;
    }

    rc = wc_ecc_make_key(rng, ECC_KEY_SIZE, policyKey);
    if (rc != 0) {
        printf("wc_ecc_make_key failed\n");
        goto exit;
    }

    /* export the public part of the key */
    rc = wc_ecc_export_public_raw(policyKey, qx, &qxSz, qy, &qySz);
    if (rc != 0) {
        printf("wc_ecc_export_public_raw failed\n");
        goto exit;
    }

    fd = open("public-key.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open public-key.raw failed\n");
        goto exit;
    }

    rc = write(fd, qx, qxSz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    rc = write(fd, qy, qySz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    close(fd);

    printf("Verification Public Key: public-key.raw\n");

    /* export the full key */
    rc = wc_ecc_export_private_raw(policyKey, qx, &qxSz, qy, &qySz, d, &dSz);
    if (rc != 0) {
        printf("wc_ecc_export_private_raw failed\n");
        goto exit;
    }

    rc = wc_ecc_export_public_raw(policyKey, qx, &qxSz, qy, &qySz);
    if (rc != 0) {
        printf("wc_ecc_export_public_raw failed\n");
        goto exit;
    }

    fd = open("private-key.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open private-key.raw failed\n");
        goto exit;
    }

    rc = write(fd, qx, qxSz);
    if (rc != (int)qxSz) {
        printf("write qx failed\n");
        goto exit;
    }

    rc = write(fd, qy, qySz);
    if (rc != (int)qxSz) {
        printf("write qy failed\n");
        goto exit;
    }

    rc = write(fd, d, dSz);
    if (rc != (int)qxSz) {
        printf("write d failed\n");
        goto exit;
    }

    close(fd);

    printf("Verification Private Key: private-key.raw\n");

exit:
    wc_FreeRng(rng);
    wc_ecc_free(policyKey);

    return 0;
}
