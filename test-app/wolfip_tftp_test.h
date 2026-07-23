/* wolfip_tftp_test.h
 *
 * Optional wolfIP network test entry for the wolfBoot test-app.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#ifndef WOLFIP_TFTP_TEST_H
#define WOLFIP_TFTP_TEST_H

/* Run the wolfIP network test. Returns 0 on success, negative on failure.
 * Without WOLFBOOT_TEST_TFTP this is a link/PHY bring-up smoke test;
 * with WOLFBOOT_TEST_TFTP it performs a full TFTP fetch and verify. */
int wolfip_tftp_test_run(void);

/* Run the test and print the "Starting..." banner and PASS/FAIL result.
 * Convenience wrapper so each target app's main() is a single call. */
void wolfip_tftp_test_report(void);

#endif /* WOLFIP_TFTP_TEST_H */
