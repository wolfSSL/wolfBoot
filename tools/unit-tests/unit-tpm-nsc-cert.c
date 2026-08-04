/* unit-tpm-nsc-cert.c
 *
 * Unit tests for the wolfBoot_tpm2_read_cert() non-secure entry veneer.
 *
 * The veneer validates the caller-supplied output buffer against the length
 * word the non-secure caller points at. That length must be snapshotted into
 * Secure memory before validation, otherwise a racing non-secure agent (a
 * second NS thread, an NS interrupt, or NS-programmed DMA) can enlarge it
 * between the veneer's cmse_check_address_range() and wolfTPM's own capacity
 * check, and the secure world writes NV data past the validated range.
 */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif

#include "tpm.h"

/* Simulated non-secure region followed by the Secure SRAM adjacent to it. Any
 * range touching 'secure' is rejected by the CMSE stub below, exactly like a
 * real Secure address fails CMSE_NONSECURE. */
#define NS_CERT_CAP 16
static struct {
    uint8_t cert[NS_CERT_CAP];
    uint8_t secure[64];
} ns_edge;

/* Non-secure length word handed to the veneer, and the value a racing
 * non-secure agent stores into it once the veneer has validated it. */
static uint32_t ns_cert_sz;
static uint32_t ns_race_value;

/* Size of the certificate the TPM reports for the requested NV index. */
static uint32_t nv_cert_size;

void *cmse_check_address_range(void *ptr, size_t size, int flags)
{
    uint8_t *start = (uint8_t *)ptr;
    uint8_t *end;

    (void)flags;
    if (start == NULL) {
        return NULL;
    }
    if (size == 0) {
        size = 1;
    }
    end = start + size;
    if (end > ns_edge.secure &&
            start < ns_edge.secure + sizeof(ns_edge.secure)) {
        return NULL;
    }
    return ptr;
}

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

const char* TPM2_GetAlgName(TPM_ALG_ID alg)
{
    (void)alg;
    return NULL;
}

const char* TPM2_GetRCString(int rc)
{
    (void)rc;
    return NULL;
}

int wolfTPM2_SetAuthPassword(WOLFTPM2_DEV* dev, int index,
    const TPM2B_AUTH* auth)
{
    (void)dev;
    (void)index;
    (void)auth;
    return 0;
}

/* Faithful stand-in for wolfTPM2_NVReadCert() (lib/wolfTPM/src/tpm2_wrap.c):
 * '*len' is an input capacity, re-read after the veneer's own validation, and
 * the NV data is copied into 'buffer' only if it fits. The racing non-secure
 * write is modelled here because that is exactly the window it occupies. */
int wolfTPM2_NVReadCert(WOLFTPM2_DEV* dev, TPM_HANDLE handle,
    uint8_t* buffer, uint32_t* len)
{
    (void)dev;
    (void)handle;

    if (len == NULL) {
        return BAD_FUNC_ARG;
    }
    if (ns_race_value != 0) {
        ns_cert_sz = ns_race_value; /* NS agent enlarges the length word */
    }
    if (nv_cert_size > *len) {
        return BUFFER_E;
    }
    *len = nv_cert_size;
    memset(buffer, 0xA5, nv_cert_size);
    return 0;
}

#include "../../src/tpm.c"

static void setup_ns_edge(uint32_t certSz, uint32_t race, uint32_t nvSize)
{
    memset(&ns_edge, 0xEE, sizeof(ns_edge));
    ns_cert_sz = certSz;
    ns_race_value = race;
    nv_cert_size = nvSize;
}

static int secure_untouched(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(ns_edge.secure); i++) {
        if (ns_edge.secure[i] != 0xEE) {
            return 0;
        }
    }
    return 1;
}

/* A non-secure caller presents a 16-byte capacity, so only 16 bytes of NS
 * memory are validated, then enlarges the length word. The 48-byte NV
 * certificate must not be written, because 32 of those bytes land in the
 * Secure SRAM following the validated range. */
START_TEST(test_read_cert_ns_length_race)
{
    int rc;

    setup_ns_edge(NS_CERT_CAP, sizeof(ns_edge), 48);

    rc = wolfBoot_tpm2_read_cert(0x01C00002, ns_edge.cert, &ns_cert_sz);

    ck_assert_int_eq(secure_untouched(), 1);
    ck_assert_int_ne(rc, 0);
}
END_TEST

/* Without a race the veneer must still behave as documented: the certificate
 * is copied and the non-secure length word receives the actual size. */
START_TEST(test_read_cert_normal)
{
    uint8_t cert[128];
    uint32_t certSz = (uint32_t)sizeof(cert);
    int rc;

    setup_ns_edge(0, 0, 48);
    memset(cert, 0xEE, sizeof(cert));

    rc = wolfBoot_tpm2_read_cert(0x01C00002, cert, &certSz);

    ck_assert_int_eq(rc, 0);
    ck_assert_uint_eq(certSz, 48);
    ck_assert_int_eq(cert[0], 0xA5);
    ck_assert_int_eq(cert[47], 0xA5);
    ck_assert_int_eq(cert[48], 0xEE);
}
END_TEST

/* A length word pointing into Secure memory must be rejected outright. */
START_TEST(test_read_cert_secure_length_pointer)
{
    int rc;

    setup_ns_edge(NS_CERT_CAP, 0, 48);

    rc = wolfBoot_tpm2_read_cert(0x01C00002, ns_edge.cert,
        (uint32_t*)ns_edge.secure);

    ck_assert_int_eq(rc, BAD_FUNC_ARG);
    ck_assert_int_eq(secure_untouched(), 1);
}
END_TEST

static Suite* tpm_nsc_cert_suite(void)
{
    Suite* s;
    TCase* tc;

    s = suite_create("TPM NSC read cert");
    tc = tcase_create("ns_bounds");
    tcase_add_test(tc, test_read_cert_ns_length_race);
    tcase_add_test(tc, test_read_cert_normal);
    tcase_add_test(tc, test_read_cert_secure_length_pointer);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s;
    SRunner* sr;
    int failed;

    s = tpm_nsc_cert_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
