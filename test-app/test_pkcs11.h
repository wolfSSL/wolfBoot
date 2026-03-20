#ifndef WOLFBOOT_TEST_PKCS11_H
#define WOLFBOOT_TEST_PKCS11_H

enum test_pkcs11_result {
    PKCS11_TEST_FAIL = -1,
    PKCS11_TEST_FIRST_BOOT_OK = 1,
    PKCS11_TEST_SECOND_BOOT_OK = 2
};

int test_pkcs11_start(void);

#endif /* WOLFBOOT_TEST_PKCS11_H */
