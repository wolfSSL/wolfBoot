/* wolfhsm_test.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifndef WOLFBOOT_TEST_WOLFHSM_H
#define WOLFBOOT_TEST_WOLFHSM_H

enum wolfhsm_test_result {
    WOLFHSM_TEST_FAIL            = -1,
    WOLFHSM_TEST_FIRST_BOOT_OK   = 1,
    WOLFHSM_TEST_SECOND_BOOT_OK  = 2
};

int cmd_wolfhsm_test(const char *args);

#endif /* WOLFBOOT_TEST_WOLFHSM_H */
