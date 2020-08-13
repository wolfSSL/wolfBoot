/* unit-parser.c
 *
 * Unit test for parser functions in libwolfboot.c
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define UNIT_TEST
#include <stdio.h>
#include "libwolfboot.c"
#include <check.h>
static int locked = 0;

/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    return 0;
}
void hal_flash_unlock(void)
{
    fail_unless(locked, "Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    fail_if(locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}
/* End Mocks */

Suite *wolfboot_suite(void);

static uint8_t test_buffer[512] = {
    'W',  'O',  'L',  'F',  0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x04, 0x00, 0x0d, 0x0c, 0x0b, 0x0a,
    0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x08, 0x00,
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x20, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /*<-- end of options */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        /* End HDR */
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
};

START_TEST (test_parser_sunny)
{
    uint8_t *p;
    int i;

    /* Check version */
    fail_if(wolfBoot_find_header(test_buffer + 8, HDR_VERSION, &p) != 4, "Parser error: cannot locate version");
    fail_if((p[0] != 0x0d) || (p[1] != 0x0c) || (p[2] != 0x0b) || (p[3] != 0x0a), "Parser error: version doesn't match");
    
    /* Check timestamp */
    fail_if(wolfBoot_find_header(test_buffer + 8, HDR_TIMESTAMP, &p) != 8, "Parser error: cannot locate timestamp");
    fail_if((p[0] != 0x07) || (p[1] != 0x06) || (p[2] != 0x05) || (p[3] != 0x04), "Parser error: timestamp doesn't match");
    fail_if((p[4] != 0x03) || (p[5] != 0x02) || (p[6] != 0x01) || (p[7] != 0x00), "Parser error: timestamp doesn't match");

    /* Check sha256 field */
    fail_if(wolfBoot_find_header(test_buffer + 8, HDR_SHA256, &p) != 32, "Parser error: cannot locate hash");
    for (i = 0; i < 32; i++) 
        fail_unless(p[i] == i, "Parser error: hash does not match");

    /* Check non-existing field */
    fail_if(wolfBoot_find_header(test_buffer + 8, HDR_SHA3_384, &p) != 0, "Parser error: found a non-existing field");
}
END_TEST

START_TEST (test_parser_borders)
{
    uint8_t *p;
    int i;
    uint8_t bad_buff[512];
    memset(bad_buff, 0xFF, 256);

    /* Field out of bounds */
    bad_buff[256] = 0x02;
    bad_buff[257] = 0x00;
    bad_buff[258] = 0x04;
    bad_buff[259] = 0x00;
    fail_if(wolfBoot_find_header(bad_buff + 8, HDR_VERSION, &p) != 0, "Parser error: accessing version field out of bounds");

    /* Single field too large */
    bad_buff[8]  = 0x02;
    bad_buff[9]  = 0x00;
    bad_buff[10]  = 0xF8;
    bad_buff[11]  = 0x00;
    fail_if(wolfBoot_find_header(bad_buff + 8, HDR_VERSION, &p) != 0, "Parser error: accessing version field out of bounds");
    
    /* Second field too large */
    bad_buff[8]  = 0x01;
    bad_buff[9]  = 0x00;
    bad_buff[10]  = 0x04;
    bad_buff[11]  = 0x00;
    bad_buff[12]  = 0x05;
    bad_buff[13]  = 0x05;
    bad_buff[14]  = 0x05;
    bad_buff[15]  = 0x05;
    bad_buff[16]  = 0x02;
    bad_buff[17]  = 0x00;
    bad_buff[18]  = 0xf0; /** Timestamp field too large **/
    bad_buff[19]  = 0x00;
    fail_if(wolfBoot_find_header(bad_buff + 8, HDR_TIMESTAMP, &p) != 0, "Parser error: accessing version field out of bounds");

    /* High memory access */
    fail_if(wolfBoot_find_header(((void *)(0 - 0xF8)), HDR_VERSION, &p) != 0);
    fail_if(wolfBoot_find_header(((void *)(0 - 0x10)), HDR_VERSION, &p) != 0);

}
END_TEST

Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    /* Test cases */
    TCase *parser_sunny  = tcase_create("Parser Sunny-day case");
    TCase *parser_borders  = tcase_create("Parser test buffer borders");

    /* Test function <-> Test case */
    tcase_add_test(parser_sunny, test_parser_sunny);
    tcase_add_test(parser_borders, test_parser_borders);

    /* Set parameters + add to suite */
    tcase_set_timeout(parser_sunny, 20);
    suite_add_tcase(s, parser_sunny);
    suite_add_tcase(s, parser_borders);

    return s;
}


int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
