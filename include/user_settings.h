/* user_settings.h : custom configuration for wolfcrypt/wolfSSL */

/* System */
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME

//#define WOLFSSL_SMALL_STACK
#define USE_SLOW_SHA512

#   define NO_SHA
#	define NO_DH
#	define NO_DSA
#	define NO_MD4
#	define NO_RABBIT
#	define NO_MD5
#	define NO_SIG_WRAPPER
#   define NO_CERT
#   define NO_SESSION_CACHE
#   define NO_HC128
#   define NO_DES3
#   define NO_WOLFSSL_DIR
#	define NO_PWDBASED

#define WOLFSSL_GENERAL_ALIGNMENT 4
#define TFM_ARM
#define SINGLE_THREADED

#define SMALL_SESSION_CACHE
#define WOLFSSL_DH_CONST
#define TFM_TIMING_RESISTANT
#define NO_RC4
#define WOLFCRYPT_ONLY
#define WOLFSSL_NO_SOCK
#   define WC_NO_RNG

#ifdef WOLFBOOT_SIGN_ED25519
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define NO_ED25519_SIGN
#   define NO_ED25519_EXPORT
#   define USE_FAST_MATH
#   define WOLFSSL_SHA512
#   define NO_ASN
#endif

#ifdef WOLFBOOT_SIGN_ECC256
#   define HAVE_ECC
#   define FP_ECC
#   define HAVE_ECC_VERIFY
#   define ECC_ALT_SIZE
#   define NO_ECC_SIGN
#   define NO_ECC_EXPORT
#   define USE_FAST_MATH
#   define WOLFSSL_SHA512
#   define WOLFSSL_SP_SMALL
#   define SP_WORD_SIZE 32
#   define WOLFSSL_HAVE_SP_ECC
#   define WOLFSSL_SP_MATH
#   define NO_ASN
//#   define NO_ECC_SIGN
#   define NO_ECC_DHE
#   define NO_ECC_KEY_EXPORT
#endif

/* AES */
#	define NO_AES
#	define NO_CMAC
#	define NO_CODING
#   define NO_BIG_INT
#	define NO_RSA
