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

#include "../../src/arm_tee_psa_ipc.c"

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

Suite *arm_tee_psa_ipc_suite(void)
{
    Suite *s = suite_create("arm-tee-psa-ipc");
    TCase *tc = tcase_create("protected-storage");

    tcase_add_test(tc, test_ps_set_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_get_rejects_short_offset_vector);
    tcase_add_test(tc, test_ps_get_info_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_remove_rejects_short_uid_vector);
    tcase_add_test(tc, test_ps_set_get_info_remove_success_path);
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
