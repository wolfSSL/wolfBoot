#include <stdio.h>
#include "args.h"

#include "wolfssl/wolfcrypt/rsa.h"

#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/asn.h"
#include "wolfssl/wolfcrypt/random.h"

static int rsa2raw(FILE *rsaDer, FILE *out, int keySz, int pub)
{

    enum
    {
        RSA_1024 = 1024,
        RSA_2048 = 2048,
    } rsaSize;

    #define DERSIZE 1024
    unsigned char der[DERSIZE];
    int derSz;
    int ret;
    #define RSASIZE (2048/8)

    unsigned char n[RSASIZE];
    unsigned char e[RSASIZE];
    unsigned char d[RSASIZE];
    unsigned char p[RSASIZE];
    unsigned char q[RSASIZE];

    unsigned int nSz = sizeof(n), eSz = sizeof(e);
    unsigned int dSz = sizeof(d), pSz = sizeof(p), qSz = sizeof(q);

    RsaKey rsa;
    unsigned int inOutIdx = 0;
    int i;

    switch (keySz) {
    case 0:
        keySz = RSA_2048;
        break;
    case RSA_1024:
    case RSA_2048:
        break;
    default:
        fprintf(stderr, "ERROR: Key Size(%d)\n", keySz);
        return -1;
    }

    if((derSz = fread(der, 1, sizeof(der), rsaDer)) <= 0){
        fprintf(stderr, "ERROR: Read DER file(%d)\n", ret);
        return -1;
    }

    if((ret = wc_InitRsaKey(&rsa, NULL)) != 0) {
        fprintf(stderr, "ERROR: wc_InitRsaKey(%d)\n", ret);
        return -1;
    }

    if(pub) {
        if ((ret = wc_RsaPublicKeyDecode(der, &inOutIdx, &rsa, derSz)) != 0) {
            fprintf(stderr, "ERROR: wc_RsaPublicKeyDecode(%d)\n", ret);
            return -1;
        }
    } else {
        if ((ret = wc_RsaPrivateKeyDecode(der, &inOutIdx, &rsa, derSz)) != 0) {
            fprintf(stderr, "ERROR: wc_RsaPublicKeyDecode(%d)\n", ret);
            return -1;
        }
    }

    memset(n, 0, sizeof(n));
    memset(e, 0, sizeof(e));

    if (pub) {
        if ((ret = wc_RsaFlattenPublicKey(&rsa, e, &eSz, n, &nSz)) != 0) {
            fprintf(stderr, "ERROR: wc_RsaFlattenPublicKey(%d)\n", ret);
            return -1;
        }
    } else {
        memset(d, 0, sizeof(d));
        if ((ret = wc_RsaExportKey(&rsa, 
                    e, &eSz, n, &nSz, d, &dSz, p, &pSz, q, &qSz)) != 0) {
            fprintf(stderr, "ERROR: wc_RsaExportKey(%d)\n", ret);
            return -1;
        }
    }

    for (i = 0; i < keySz / 8; i++) {
        fprintf(out, "%02x", n[i]);
    }

    for (i = 0; i < 4; i++) {
        fprintf(out, "%02x", e[i]);
    }

    if (!pub) {
        for (i = 0; i < keySz / 8; i++) {
            fprintf(out, "%02x", d[i]);
        }
    }

    return 0;
}

int ecc2raw(FILE *eccDer, FILE *out, int keySz, int pub)
{

    enum
    {
        ECC_192 = 192,
        ECC_224 = 224,
        ECC_256 = 256,
        ECC_384 = 364,
    } eccSize;

    #define ECC_DER 256
    byte der[ECC_DER];
    int derSz;
    unsigned int inOutIdx = 0;
    struct ecc_key key;
    byte qx[MAX_ECC_BYTES];
    byte qy[MAX_ECC_BYTES];
    byte d [MAX_ECC_BYTES];
    word32 qxSz = sizeof(qx);
    word32 qySz = sizeof(qy);
    word32 dSz  = sizeof(d);
    WC_RNG rng;

    int i;
    int ret;
   
    switch(keySz) {
    case 0:
        keySz = ECC_256;
        break;
    case ECC_192:
    case ECC_224:
    case ECC_256:
    case ECC_384:
        break;
    default:
        fprintf(stderr, "ERROR: Key Size(%d)\n", keySz);
        return -1;
    }

    if ((derSz = fread(der, 1, sizeof(der), eccDer)) < 0) {
        fprintf(stderr, "ERROR: Read DER file(%d)\n", ret);
        return -1;
    }

    if((ret = wc_ecc_init(&key)) != 0) {
        fprintf(stderr, "ERROR: wc_ecc_init(%d)\n", ret);
        return -1;
    }

    if (pub) {
        if ((ret = wc_EccPublicKeyDecode(der, &inOutIdx, &key, derSz)) != 0) {
            fprintf(stderr, "ERROR: wc_EccPublicKeyDecode(%d)\n", ret);
            return -1;
        }
    } else {
        if ((ret = wc_EccPrivateKeyDecode(der, &inOutIdx, &key, derSz)) != 0) {
            fprintf(stderr, "ERROR: wc_EccPrivateKeyDecode(%d)\n", ret);
            return -1;
        }
    }

    memset(qx, 0, sizeof(qx));
    memset(qy, 0, sizeof(qy));


    if (pub) {
        if ((ret = wc_ecc_export_public_raw(&key, qx, &qxSz, qy, &qySz)) != 0) {
            fprintf(stderr, "ERROR: wc_ecc_export_public_raw(%d)\n", ret);
            return -1;
        }
    } else {
        memset(d, 0, sizeof(d));
        if((ret = wc_ecc_export_private_raw(&key, qx, &qxSz, qy, &qySz, d, &dSz)) != 0) {
            fprintf(stderr, "ERROR: wc_ecc_export_private_raw(%d)\n", ret);
            return -1;
        }
    }

    for (i = 0; i < keySz / 8; i++) {
        fprintf(out, "%02x", qx[i]);
    }

    for (i = 0; i < keySz / 8; i++) {
        fprintf(out, "%02x", qy[i]);
    }

    if (!pub) {
        for (i = 0; i < keySz / 8; i++) {
            fprintf(out, "%02x", d[i]);
        }
    }

    wc_ecc_free(&key);

    return 0;
}

static void usage(void)
{
    char desc[] =
        "\n"
        "$ command[-e][-pub][-s <size>] in_file [out_file]\n"
        "\n"
        "in_file is mandate. If no out_file is specified, output to stdout\n"
        "-s <size>:   Key size bits in decimal (Default: 2049 bit/RSA, 256 bit/ECC)\n"
        "-e:          Input is a ECC key (Default: RSA)\n"
        "-pub:        Input is a public key (Default: private)\n"
        "-? or -help: Display this help message\n";

    printf("\nUsage:\n%s", desc);
}

int main(int ac, char** av)
{
    FILE *in, *out;
    int ret;
    int keySz = 0;
    int ecc;
    int pub;
    int help = 0;

    Args_open(ac, av);

    help |= Args_option("?");
    help |= Args_option("help");
    if (help) {
        usage();
        return 0;
    }

    ret = Args_optDec("s", &keySz);
    ecc = Args_option("e");
    pub = Args_option("pub");
    in  = Args_infile("rb", 0);
    out = Args_outfile("w+", ARGS_STDOUT);

    if (Args_error()) {
        Args_close(in, out);
        return -1;
    }

    if (ecc)
        ret = ecc2raw(in, out, keySz, pub);
    else
        ret = rsa2raw(in, out, keySz, pub);

    Args_close(in, out);

    return ret;
}

