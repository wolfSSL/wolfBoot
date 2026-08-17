#include <check.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void ForceZero(void *mem, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)mem;

    while (len-- > 0) {
        *p++ = 0;
    }
}

void wc_ForceZero(void *mem, size_t len)
{
    ForceZero(mem, len);
}

/* Simulated Secure SRAM: any pointer landing in here is rejected by the CMSE
 * stub below, exactly like a real Secure address fails CMSE_NONSECURE. */
static uint8_t secure_mem[64];

void *cmse_check_address_range(void *ptr, size_t size, int flags)
{
    uint8_t *start = (uint8_t *)ptr;
    uint8_t *end;

    (void)flags;
    if (size == 0) {
        size = 1;
    }
    end = start + size;
    if (end > secure_mem && start < secure_mem + sizeof(secure_mem)) {
        return NULL;
    }
    return ptr;
}

#include "../../src/arm_tee_psa_ipc.c"

/* Backend stubs: the tests below only exercise the IPC argument validation,
 * never the crypto/attestation back ends. */
psa_status_t psa_crypto_init(void) { return PSA_SUCCESS; }
psa_status_t psa_generate_random(uint8_t *o, size_t s)
{ (void)o; (void)s; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_get_key_attributes(psa_key_id_t k, psa_key_attributes_t *a)
{ (void)k; (void)a; return PSA_ERROR_NOT_SUPPORTED; }
void psa_reset_key_attributes(psa_key_attributes_t *a) { (void)a; }
psa_status_t psa_destroy_key(psa_key_id_t k)
{ (void)k; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_import_key(const psa_key_attributes_t *a, const uint8_t *d,
    size_t dl, psa_key_id_t *k)
{ (void)a; (void)d; (void)dl; (void)k; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_generate_key(const psa_key_attributes_t *a, psa_key_id_t *k)
{ (void)a; (void)k; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_export_key(psa_key_id_t k, uint8_t *d, size_t ds, size_t *dl)
{ (void)k; (void)d; (void)ds; (void)dl; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_export_public_key(psa_key_id_t k, uint8_t *d, size_t ds,
    size_t *dl)
{ (void)k; (void)d; (void)ds; (void)dl; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_compute(psa_algorithm_t alg, const uint8_t *i, size_t il,
    uint8_t *h, size_t hs, size_t *hl)
{ (void)alg; (void)i; (void)il; (void)h; (void)hs; (void)hl;
  return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_setup(psa_hash_operation_t *op, psa_algorithm_t alg)
{ (void)op; (void)alg; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_update(psa_hash_operation_t *op, const uint8_t *i,
    size_t il)
{ (void)op; (void)i; (void)il; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_finish(psa_hash_operation_t *op, uint8_t *h, size_t hs,
    size_t *hl)
{ (void)op; (void)h; (void)hs; (void)hl; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_clone(const psa_hash_operation_t *s,
    psa_hash_operation_t *t)
{ (void)s; (void)t; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_hash_abort(psa_hash_operation_t *op)
{ (void)op; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_encrypt_setup(psa_cipher_operation_t *op,
    psa_key_id_t k, psa_algorithm_t alg)
{ (void)op; (void)k; (void)alg; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_decrypt_setup(psa_cipher_operation_t *op,
    psa_key_id_t k, psa_algorithm_t alg)
{ (void)op; (void)k; (void)alg; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_set_iv(psa_cipher_operation_t *op, const uint8_t *iv,
    size_t ivl)
{ (void)op; (void)iv; (void)ivl; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_update(psa_cipher_operation_t *op, const uint8_t *i,
    size_t il, uint8_t *o, size_t os, size_t *ol)
{ (void)op; (void)i; (void)il; (void)o; (void)os; (void)ol;
  return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_finish(psa_cipher_operation_t *op, uint8_t *o,
    size_t os, size_t *ol)
{ (void)op; (void)o; (void)os; (void)ol; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_cipher_abort(psa_cipher_operation_t *op)
{ (void)op; return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_sign_hash(psa_key_id_t k, psa_algorithm_t alg,
    const uint8_t *h, size_t hl, uint8_t *s, size_t ss, size_t *sl)
{ (void)k; (void)alg; (void)h; (void)hl; (void)s; (void)ss; (void)sl;
  return PSA_ERROR_NOT_SUPPORTED; }
psa_status_t psa_verify_hash(psa_key_id_t k, psa_algorithm_t alg,
    const uint8_t *h, size_t hl, const uint8_t *s, size_t sl)
{ (void)k; (void)alg; (void)h; (void)hl; (void)s; (void)sl;
  return PSA_ERROR_NOT_SUPPORTED; }
int wolfBoot_dice_get_token(const uint8_t *c, size_t cs, uint8_t *t, size_t ts,
    size_t *tl)
{ (void)c; (void)cs; (void)t; (void)ts; (void)tl; return -1; }
int wolfBoot_dice_get_token_size(size_t cs, size_t *ts)
{ (void)cs; (void)ts; return -1; }
int wolfBoot_dice_get_attest_pubkey(uint8_t *b, size_t *l)
{ (void)b; (void)l; return -1; }

static void reset_ps_state(void)
{
    memset(g_ps_entries, 0, sizeof(g_ps_entries));
}

START_TEST(test_ps_set_rejects_short_uid_vector)
{
    psa_storage_uid_t uid = 0x1122334455667788ULL;
    psa_storage_create_flags_t flags = 0;
    uint8_t data[4] = {1, 2, 3, 4};
    psa_invec in_vec[3];

    reset_ps_state();
    in_vec[0].base = &uid;
    in_vec[0].len = sizeof(uid) - 1;
    in_vec[1].base = data;
    in_vec[1].len = sizeof(data);
    in_vec[2].base = &flags;
    in_vec[2].len = sizeof(flags);

    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_SET, in_vec, 3, NULL, 0),
        PSA_ERROR_INVALID_ARGUMENT);
}
END_TEST

START_TEST(test_ps_get_rejects_short_offset_vector)
{
    psa_storage_uid_t uid = 7;
    rot_size_t offset = 0;
    uint8_t out[8];
    psa_invec in_vec[2];
    psa_outvec out_vec[1];

    reset_ps_state();
    g_ps_entries[0].uid = uid;
    g_ps_entries[0].size = 4;
    g_ps_entries[0].in_use = 1;

    in_vec[0].base = &uid;
    in_vec[0].len = sizeof(uid);
    in_vec[1].base = &offset;
    in_vec[1].len = sizeof(offset) - 1;
    out_vec[0].base = out;
    out_vec[0].len = sizeof(out);

    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_GET, in_vec, 2, out_vec, 1),
        PSA_ERROR_INVALID_ARGUMENT);
}
END_TEST

START_TEST(test_ps_get_info_rejects_short_uid_vector)
{
    psa_storage_uid_t uid = 9;
    struct psa_storage_info_t info;
    psa_invec in_vec[1];
    psa_outvec out_vec[1];

    reset_ps_state();
    in_vec[0].base = &uid;
    in_vec[0].len = sizeof(uid) - 1;
    out_vec[0].base = &info;
    out_vec[0].len = sizeof(info);

    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_GET_INFO, in_vec, 1, out_vec, 1),
        PSA_ERROR_INVALID_ARGUMENT);
}
END_TEST

START_TEST(test_ps_remove_rejects_short_uid_vector)
{
    psa_storage_uid_t uid = 11;
    psa_invec in_vec[1];

    reset_ps_state();
    in_vec[0].base = &uid;
    in_vec[0].len = sizeof(uid) - 1;

    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_REMOVE, in_vec, 1, NULL, 0),
        PSA_ERROR_INVALID_ARGUMENT);
}
END_TEST

START_TEST(test_ps_set_get_info_remove_success_path)
{
    psa_storage_uid_t uid = 0xA5A5A5A5U;
    psa_storage_create_flags_t flags = 0;
    rot_size_t offset = 1;
    uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t read_buf[4] = {0};
    struct psa_storage_info_t info;
    psa_invec set_in[3];
    psa_invec get_in[2];
    psa_invec info_in[1];
    psa_invec remove_in[1];
    psa_outvec get_out[1];
    psa_outvec info_out[1];

    reset_ps_state();

    set_in[0].base = &uid;
    set_in[0].len = sizeof(uid);
    set_in[1].base = data;
    set_in[1].len = sizeof(data);
    set_in[2].base = &flags;
    set_in[2].len = sizeof(flags);
    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_SET, set_in, 3, NULL, 0),
        PSA_SUCCESS);

    get_in[0].base = &uid;
    get_in[0].len = sizeof(uid);
    get_in[1].base = &offset;
    get_in[1].len = sizeof(offset);
    get_out[0].base = read_buf;
    get_out[0].len = sizeof(read_buf);
    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_GET, get_in, 2, get_out, 1),
        PSA_SUCCESS);
    ck_assert_uint_eq(get_out[0].len, sizeof(data) - offset);
    ck_assert_mem_eq(read_buf, data + offset, sizeof(data) - offset);

    info_in[0].base = &uid;
    info_in[0].len = sizeof(uid);
    info_out[0].base = &info;
    info_out[0].len = sizeof(info);
    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_GET_INFO, info_in, 1, info_out, 1),
        PSA_SUCCESS);
    ck_assert_uint_eq(info.size, sizeof(data));
    ck_assert_uint_eq(info.flags, flags);

    remove_in[0].base = &uid;
    remove_in[0].len = sizeof(uid);
    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_REMOVE, remove_in, 1, NULL, 0),
        PSA_SUCCESS);
    ck_assert_int_eq(g_ps_entries[0].in_use, 0);
    ck_assert_uint_eq(g_ps_entries[0].size, 0);
}
END_TEST

START_TEST(test_psa_call_rejects_secure_zero_len_outvec)
{
    psa_outvec out_vec[1];
    size_t i;

    reset_ps_state();
    memset(secure_mem, 0xA5, sizeof(secure_mem));

    /* A zero-length descriptor pointing at Secure memory must not pass
     * validation: ARM_TEE_PS_GET_SUPPORT writes through .base regardless of
     * the declared length. */
    out_vec[0].base = secure_mem;
    out_vec[0].len = 0;

    ck_assert_int_eq(
        arm_tee_psa_call((psa_handle_t)ARM_TEE_PROTECTED_STORAGE_HANDLE,
                         ARM_TEE_PS_GET_SUPPORT, NULL, 0, out_vec, 1),
        PSA_ERROR_INVALID_ARGUMENT);

    for (i = 0; i < sizeof(secure_mem); i++) {
        ck_assert_uint_eq(secure_mem[i], 0xA5);
    }
}
END_TEST

START_TEST(test_ps_get_support_rejects_short_outvec)
{
    uint8_t buf[sizeof(uint32_t)];
    psa_outvec out_vec[1];

    reset_ps_state();
    memset(buf, 0xA5, sizeof(buf));

    out_vec[0].base = buf;
    out_vec[0].len = sizeof(uint32_t) - 1;

    ck_assert_int_eq(
        arm_tee_psa_test_ps_dispatch(ARM_TEE_PS_GET_SUPPORT, NULL, 0,
                                     out_vec, 1),
        PSA_ERROR_INVALID_ARGUMENT);
    ck_assert_uint_eq(buf[0], 0xA5);
}
END_TEST

START_TEST(test_ps_get_support_success_path)
{
    uint32_t support = 0xFFFFFFFFU;
    psa_outvec out_vec[1];

    reset_ps_state();
    out_vec[0].base = &support;
    out_vec[0].len = sizeof(support);

    ck_assert_int_eq(
        arm_tee_psa_call((psa_handle_t)ARM_TEE_PROTECTED_STORAGE_HANDLE,
                         ARM_TEE_PS_GET_SUPPORT, NULL, 0, out_vec, 1),
        PSA_SUCCESS);
    ck_assert_uint_eq(support, 0);
    ck_assert_uint_eq(out_vec[0].len, sizeof(support));
}
END_TEST

Suite *arm_tee_psa_ipc_suite(void)
{
    Suite *s = suite_create("arm-tee-psa-ipc");
    TCase *tc = tcase_create("protected-storage");

    tcase_add_test(tc, test_ps_set_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_get_rejects_short_offset_vector);
    tcase_add_test(tc, test_ps_get_info_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_remove_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_set_get_info_remove_success_path);
    tcase_add_test(tc, test_psa_call_rejects_secure_zero_len_outvec);
    tcase_add_test(tc, test_ps_get_support_rejects_short_outvec);
    tcase_add_test(tc, test_ps_get_support_success_path);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = arm_tee_psa_ipc_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
