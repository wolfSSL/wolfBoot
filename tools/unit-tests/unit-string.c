/* unit-string.c
 *
 * Unit tests for string.c.
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#define FAST_MEMCPY

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "string.c"

static char uart_buf[256];
static size_t uart_len;

void uart_write(const char* buf, unsigned int sz)
{
    size_t i;
    for (i = 0; i < sz && uart_len + 1 < sizeof(uart_buf); i++) {
        uart_buf[uart_len++] = buf[i];
    }
    uart_buf[uart_len] = '\0';
}

static void reset_uart_buf(void)
{
    memset(uart_buf, 0, sizeof(uart_buf));
    uart_len = 0;
}

START_TEST(test_strncasecmp_n_zero)
{
    ck_assert_int_eq(strncasecmp("ABC", "abc", 0), 0);
    ck_assert_int_eq(strncasecmp("A", "B", 0), 0);
}
END_TEST

START_TEST(test_strncasecmp_n_one)
{
    ck_assert_int_eq(strncasecmp("A", "a", 1), 0);
    ck_assert_int_lt(strncasecmp("A", "b", 1), 0);
    ck_assert_int_gt(strncasecmp("b", "A", 1), 0);
}
END_TEST

START_TEST(test_strncasecmp_n_exact)
{
    ck_assert_int_eq(strncasecmp("AbC", "aBc", 3), 0);
    ck_assert_int_eq(strncasecmp("AbCd", "aBcE", 3), 0);
}
END_TEST

START_TEST(test_strncasecmp_diff_before_n)
{
    ck_assert_int_lt(strncasecmp("abc", "abd", 3), 0);
    ck_assert_int_gt(strncasecmp("abd", "abc", 3), 0);
    ck_assert_int_lt(strncasecmp("", "a", 1), 0);
    ck_assert_int_eq(strncasecmp("", "a", 0), 0);
}
END_TEST

START_TEST(test_case_insensitive_alpha_only)
{
    ck_assert_int_ne(strcasecmp("@", "`"), 0);
    ck_assert_int_ne(strcasecmp("[", "{"), 0);
    ck_assert_int_ne(strcasecmp("]", "}"), 0);
    ck_assert_int_ne(strcasecmp("!", "A"), 0);
    ck_assert_int_ne(strncasecmp("@", "`", 1), 0);
    ck_assert_int_ne(strncasecmp("!", "A", 1), 0);
    ck_assert_int_ne(strcasecmp("a@", "A`"), 0);
    ck_assert_int_ne(strncasecmp("a@", "A`", 2), 0);
}
END_TEST

START_TEST(test_strcasecmp_mixed_alnum_punct)
{
    ck_assert_int_eq(strcasecmp("Boot-123_OK!", "bOot-123_ok!"), 0);
    ck_assert_int_eq(strcasecmp("v1.2.3-rc1", "V1.2.3-RC1"), 0);
    ck_assert_int_eq(strcasecmp("A_B-C.D", "a_b-c.d"), 0);
}
END_TEST

START_TEST(test_strcasecmp_non_alpha_ordering)
{
    ck_assert_int_lt(strcasecmp("abc-1", "abc_1"), 0);
    ck_assert_int_gt(strcasecmp("abc_1", "abc-1"), 0);
    ck_assert_int_lt(strcasecmp("abc1", "abc2"), 0);
    ck_assert_int_gt(strcasecmp("abc2", "abc1"), 0);
}
END_TEST

START_TEST(test_strncasecmp_non_alpha_n_boundaries)
{
    ck_assert_int_eq(strncasecmp("Boot-123_OK!", "bOot-123_ok?", 11), 0);
    ck_assert_int_lt(strncasecmp("Boot-123_OK!", "bOot-123_ok?", 12), 0);
    ck_assert_int_gt(strncasecmp("bOot-123_ok?", "Boot-123_OK!", 12), 0);
    ck_assert_int_eq(strncasecmp("A1.B2-C3", "a1.b2-c3", 8), 0);
}
END_TEST

START_TEST(test_strcasecmp_prefix_regression)
{
    ck_assert_int_lt(strcasecmp("a", "ab"), 0);
    ck_assert_int_gt(strcasecmp("ab", "a"), 0);
    ck_assert_int_lt(strcasecmp("", "a"), 0);
    ck_assert_int_gt(strcasecmp("a", ""), 0);
}
END_TEST

START_TEST(test_strncasecmp_n_limit_regression)
{
    ck_assert_int_eq(strncasecmp("ABC", "abc", 0), 0);
    ck_assert_int_eq(strncasecmp("", "a", 0), 0);
    ck_assert_int_eq(strncasecmp("AbCd", "aBcE", 3), 0);
    ck_assert_int_lt(strncasecmp("AbCd", "aBcE", 4), 0);
}
END_TEST

START_TEST(test_strncasecmp_stop_at_null_regression)
{
    const char s1[] = { 'A', '\0', 'x', '\0' };
    const char s2[] = { 'a', '\0', 'Y', '\0' };

    ck_assert_int_eq(strncasecmp(s1, s2, 2), 0);
    ck_assert_int_eq(strncasecmp(s1, s2, 3), 0);
    ck_assert_int_eq(strncasecmp(s1, s2, 8), 0);
}
END_TEST

START_TEST(test_strncasecmp_prefix_large_n_regression)
{
    ck_assert_int_lt(strncasecmp("a", "ab", 8), 0);
    ck_assert_int_gt(strncasecmp("ab", "a", 8), 0);
    ck_assert_int_lt(strncasecmp("A", "aB", 8), 0);
}
END_TEST

START_TEST(test_strncasecmp_no_read_past_null_when_n_remaining)
{
    const char s1[] = { '\0' };
    const char s2[] = { '\0' };

    /*
     * Regression target: if implementation does not stop on '\0' when n > 1,
     * the next loop iteration reads past both 1-byte buffers.
     */
    ck_assert_int_eq(strncasecmp(s1, s2, 2), 0);
    ck_assert_int_eq(strncasecmp(s1, s2, 8), 0);
}
END_TEST

START_TEST(test_isalpha_helpers)
{
    ck_assert_int_eq(islower('a'), 1);
    ck_assert_int_eq(islower('Z'), 0);
    ck_assert_int_eq(isupper('Z'), 1);
    ck_assert_int_eq(isupper('a'), 0);
    ck_assert_int_eq(isalpha('Q'), 1);
    ck_assert_int_eq(isalpha('q'), 1);
    ck_assert_int_eq(isalpha('1'), 0);
    ck_assert_int_eq(tolower('A'), 'a');
    ck_assert_int_eq(tolower('a'), 'a');
    ck_assert_int_eq(toupper('a'), 'A');
    ck_assert_int_eq(toupper('A'), 'A');
    ck_assert_int_eq(tolower('1'), '1');
    ck_assert_int_eq(toupper('1'), '1');
}
END_TEST

START_TEST(test_memset_memcmp_memchr)
{
    uint8_t buf[8];
    uint8_t other[8];

    memset(buf, 0xAA, sizeof(buf));
    memset(other, 0xAA, sizeof(other));
    ck_assert_int_eq(memcmp(buf, other, sizeof(buf)), 0);

    other[3] = 0xAB;
    ck_assert_int_lt(memcmp(buf, other, sizeof(buf)), 0);
    ck_assert_int_eq(memcmp(buf, other, 0), 0);

    ck_assert_ptr_eq(memchr(buf, 0xAA, sizeof(buf)), &buf[0]);
    ck_assert_ptr_eq(memchr(buf, 0xAB, sizeof(buf)), NULL);
    buf[6] = 0xAA;
    ck_assert_ptr_eq(memchr(buf, 0xAA, 8), &buf[0]);
    ck_assert_ptr_eq(memchr(buf, 0xAA, 1), &buf[0]);
    ck_assert_ptr_eq(memchr(buf, 0xAA, 0), NULL);
    ck_assert_ptr_eq(memchr(buf, 0xAA, 7), &buf[0]);
}
END_TEST

START_TEST(test_strlen_strcmp)
{
    ck_assert_uint_eq(strlen(""), 0);
    ck_assert_uint_eq(strlen("abc"), 3);
    ck_assert_int_eq(strcmp("abc", "abc"), 0);
    ck_assert_int_lt(strcmp("abc", "abd"), 0);
    ck_assert_int_gt(strcmp("abe", "abd"), 0);
    ck_assert_int_lt(strcmp("", "a"), 0);
    ck_assert_int_gt(strcmp("a", ""), 0);
}
END_TEST

START_TEST(test_strcmp_prefix_termination)
{
    ck_assert_int_lt(strcmp("a", "abc"), 0);
    ck_assert_int_lt(strcmp("ab", "abc"), 0);
    ck_assert_int_gt(strcmp("abc", "ab"), 0);
    ck_assert_int_gt(strcmp("abc", "a"), 0);
    ck_assert_int_eq(strcmp("", ""), 0);
}
END_TEST

START_TEST(test_strcpy_strncpy_strcat_strncat)
{
    char buf[8];
    char dest[8];
    char short_dest[4];
    char padded[8];

    strcpy(buf, "hi");
    ck_assert_str_eq(buf, "hi");

    memset(dest, 0, sizeof(dest));
    strncpy(dest, "abc", 4);
    ck_assert_str_eq(dest, "abc");

    memset(padded, 'X', sizeof(padded));
    strncpy(padded, "abc", sizeof(padded));
    ck_assert_int_eq(padded[0], 'a');
    ck_assert_int_eq(padded[1], 'b');
    ck_assert_int_eq(padded[2], 'c');
    ck_assert_int_eq(padded[3], '\0');
    ck_assert_int_eq(padded[4], '\0');
    ck_assert_int_eq(padded[5], '\0');
    ck_assert_int_eq(padded[6], '\0');
    ck_assert_int_eq(padded[7], '\0');

    memset(short_dest, 'X', sizeof(short_dest));
    strncpy(short_dest, "abcdef", 3);
    ck_assert_int_eq(short_dest[0], 'a');
    ck_assert_int_eq(short_dest[1], 'b');
    ck_assert_int_eq(short_dest[2], 'c');
    ck_assert_int_eq(short_dest[3], 'X');

    strcpy(dest, "a");
    strcat(dest, "b");
    ck_assert_str_eq(dest, "ab");

    strcpy(dest, "a");
    strncat(dest, "bc", 3);
    ck_assert_str_eq(dest, "abc");

    strcpy(dest, "a");
    strncat(dest, "bc", 1);
    ck_assert_str_eq(dest, "ab");

    strcpy(dest, "");
    strncat(dest, "x", 2);
    ck_assert_str_eq(dest, "x");
}
END_TEST

START_TEST(test_strncmp)
{
    ck_assert_int_eq(strncmp("abc", "abc", 3), 0);
    ck_assert_int_lt(strncmp("abc", "abd", 3), 0);
    ck_assert_int_eq(strncmp("abc", "abd", 2), 0);
    ck_assert_int_eq(strncmp("abc", "abc", 0), 0);
    ck_assert_int_lt(strncmp("", "a", 1), 0);
    ck_assert_int_gt(strncmp("a", "", 1), 0);
}
END_TEST

START_TEST(test_memcpy_memmove)
{
    union {
        unsigned long align;
        unsigned char buf[32];
    } storage;
    unsigned char src[16];
    unsigned char dst[16];
    unsigned char *p = (unsigned char *)&storage.align;
    int i;

    for (i = 0; i < 16; i++) {
        src[i] = (unsigned char)i;
        dst[i] = 0;
        p[i] = (unsigned char)i;
    }

    memcpy(dst, src, 16);
    ck_assert_int_eq(memcmp(src, dst, 16), 0);

    memmove(p + 2, p, 8);
    ck_assert_int_eq(p[2], 0);
    ck_assert_int_eq(p[3], 1);

    memmove(p, p + 2, 8);
    ck_assert_int_eq(p[0], 0);
    ck_assert_int_eq(p[1], 1);

    ck_assert_ptr_eq(memmove(p, p, 4), p);
}
END_TEST

START_TEST(test_memcpy_aligned_buffers)
{
    union {
        unsigned long align;
        unsigned char buf[64];
    } storage;
    unsigned char *src = (unsigned char *)&storage.align;
    unsigned char *dst = src + 16;
    int i;

    for (i = 0; i < 32; i++) {
        src[i] = (unsigned char)(i + 1);
        dst[i] = 0;
    }

    memcpy(dst, src, 32);
    ck_assert_int_eq(memcmp(src, dst, 32), 0);
}
END_TEST

START_TEST(test_uart_writenum_basic)
{
    reset_uart_buf();
    uart_writenum(0, 10, 0, 0);
    ck_assert_str_eq(uart_buf, "0");

    reset_uart_buf();
    uart_writenum(255, 16, 0, 0);
    ck_assert_str_eq(uart_buf, "FF");

    reset_uart_buf();
    uart_writenum(-5, 10, 0, 0);
    ck_assert_str_eq(uart_buf, "-5");

    reset_uart_buf();
    uart_writenum(7, 10, 1, 4);
    ck_assert_str_eq(uart_buf, "0007");

    reset_uart_buf();
    uart_writenum(1, 10, 1, 2);
    ck_assert_str_eq(uart_buf, "01");

    reset_uart_buf();
    uart_writenum(0x1234, 16, 1, 6);
    ck_assert_str_eq(uart_buf, "001234");

    reset_uart_buf();
    uart_writenum(1, 10, 1, 64);
    ck_assert_int_eq(uart_buf[0], '0');
}
END_TEST

START_TEST(test_uart_printf_formats)
{
    reset_uart_buf();
    uart_printf("X=%u Y=%d Z=%x W=%X %% %s %c", 5U, -3, 0x2a, 0x2a, "ok", '!');
    ck_assert_str_eq(uart_buf, "X=5 Y=-3 Z=2A W=2A % ok !");

    reset_uart_buf();
    uart_printf("%04u", 12U);
    ck_assert_str_eq(uart_buf, "0012");

    reset_uart_buf();
    uart_printf("%p", (void*)(uintptr_t)0x10);
    ck_assert_str_eq(uart_buf, "0x10");

    reset_uart_buf();
    uart_printf("A%08uB", 12U);
    ck_assert_str_eq(uart_buf, "A00000012B");

    reset_uart_buf();
    uart_printf("%12u", 3U);
    ck_assert_str_eq(uart_buf, "3");

    reset_uart_buf();
    uart_printf("%lu", (unsigned long)7);
    ck_assert_str_eq(uart_buf, "7");

    reset_uart_buf();
    uart_printf("%zu", (size_t)9);
    ck_assert_str_eq(uart_buf, "9");

    reset_uart_buf();
    uart_printf("bad%qend");
    ck_assert_str_eq(uart_buf, "badend");

    reset_uart_buf();
    uart_printf("%i", -1);
    ck_assert_str_eq(uart_buf, "-1");
}
END_TEST

Suite *string_suite(void)
{
    Suite *s = suite_create("String");
    TCase *tcase_strncasecmp = tcase_create("strncasecmp");
    TCase *tcase_misc = tcase_create("misc");

    tcase_add_test(tcase_strncasecmp, test_strncasecmp_n_zero);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_n_one);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_n_exact);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_diff_before_n);
    tcase_add_test(tcase_strncasecmp, test_case_insensitive_alpha_only);
    tcase_add_test(tcase_strncasecmp, test_strcasecmp_mixed_alnum_punct);
    tcase_add_test(tcase_strncasecmp, test_strcasecmp_non_alpha_ordering);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_non_alpha_n_boundaries);
    tcase_add_test(tcase_strncasecmp, test_strcasecmp_prefix_regression);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_n_limit_regression);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_stop_at_null_regression);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_prefix_large_n_regression);
    tcase_add_test(tcase_strncasecmp, test_strncasecmp_no_read_past_null_when_n_remaining);
    tcase_add_test(tcase_misc, test_isalpha_helpers);
    tcase_add_test(tcase_misc, test_memset_memcmp_memchr);
    tcase_add_test(tcase_misc, test_strlen_strcmp);
    tcase_add_test(tcase_misc, test_strcmp_prefix_termination);
    tcase_add_test(tcase_misc, test_strcpy_strncpy_strcat_strncat);
    tcase_add_test(tcase_misc, test_strncmp);
    tcase_add_test(tcase_misc, test_memcpy_memmove);
    tcase_add_test(tcase_misc, test_memcpy_aligned_buffers);
    tcase_add_test(tcase_misc, test_uart_writenum_basic);
    tcase_add_test(tcase_misc, test_uart_printf_formats);

    suite_add_tcase(s, tcase_strncasecmp);
    suite_add_tcase(s, tcase_misc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = string_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
