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

void usage(void) {
    printf("Sign an image digest with an ecc private key:\n");
    printf("./sign private-key digest");
}

int main(int argc, char** argv)
{
    int rc;
    int fd;
    uint8_t eccBuff[ECC_KEY_SIZE * 3];
    uint32_t eccBuffSz = 0;
    uint8_t hash[WC_SHA256_DIGEST_SIZE];
    uint8_t sig[ECC_KEY_SIZE * 2];
    ecc_key privateKey[1];
    mp_int r, s;
    WC_RNG rng[1];
    struct stat st[1];

    if (argc != 3) {
        usage();
        return 0;
    }

    printf("Signing the digest\n");

    rc = wc_InitRng(rng);
    if (rc != 0) {
        printf("wc_InitRng failed\n");
        goto exit;
    }

    /* read the key */
    rc = stat(argv[1], st);
    if (rc != 0) {
        printf("stat %s failed\n", argv[1]);
        goto exit;
    }

    eccBuffSz = st->st_size;

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("open %s failed\n", argv[1]);
        goto exit;
    }

    rc = read(fd, eccBuff, eccBuffSz);
    if (rc != (int)eccBuffSz) {
        printf("read %s failed\n", argv[1]);
        goto exit;
    }

    close(fd);

    /* read the digest */
    fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        printf("open %s failed\n", argv[2]);
        goto exit;
    }

    rc = read(fd, hash, WC_SHA256_DIGEST_SIZE);
    if (rc != WC_SHA256_DIGEST_SIZE) {
        printf("read %s failed\n", argv[2]);
        goto exit;
    }

    close(fd);

    /* import the ecc key */
    rc = wc_ecc_init(privateKey);
    if (rc != 0) {
        printf("wc_ecc_init failed\n");
        goto exit;
    }

    rc = wc_ecc_import_unsigned(privateKey, eccBuff, eccBuff + ECC_KEY_SIZE,
        eccBuff + ECC_KEY_SIZE * 2, ECC_SECP256R1);
    if (rc != 0) {
        printf("wc_ecc_import_raw failed %d\n", rc);
        goto exit;
    }

    /* sign the hash with the private key */
    mp_init(&r);
    mp_init(&s);

    rc = wc_ecc_sign_hash_ex(hash, sizeof(hash), rng, privateKey, &r, &s);

    mp_to_unsigned_bin(&r, sig);
    mp_to_unsigned_bin(&s, sig + ECC_KEY_SIZE);

    mp_clear(&r);
    mp_clear(&s);

    if (rc != 0) {
        printf("wc_ecc_sign_hash_ex failed %d\n", rc);
        goto exit;
    }

    fd = open("image-signature.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("open image-signature.raw failed\n");
        goto exit;
    }

    rc = write(fd, sig, ECC_KEY_SIZE * 2);
    if (rc != ECC_KEY_SIZE * 2) {
        printf("write image-signature.raw failed\n");
        goto exit;
    }

    close(fd);

    printf("Image Signature: image-signature.raw\n");

exit:
    wc_ecc_free(privateKey);
    wc_FreeRng(rng);

    return 0;
}
