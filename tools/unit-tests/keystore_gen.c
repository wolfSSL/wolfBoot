/* keystore_gen.c
 *
 * Test helper for unit-keygen-keystore (F-13624). Emits a self-contained
 * keystore.c to stdout: the real accessors from keygen's Keystore_API
 * template (extracted into keystore_api_extract.h by the Makefile) wrapped
 * in the same #ifdef context the emitted file has (WOLFBOOT_NO_SIGN + the
 * KEYSTORE_ANY size check), with a one-key PubKeys array. The test compiles
 * the emitted file and asserts the out-of-range accessor contract.
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

#include <stdio.h>

/* The real Keystore_API template, extracted from tools/keytools/keygen.c. */
#include "keystore_api_extract.h"

int main(void)
{
    fputs("#include <stdint.h>\n", stdout);
    fputs("#define KEYSTORE_PUBKEY_SIZE 260\n", stdout);
    fputs("struct keystore_slot {\n"
          "    uint32_t slot_id;\n"
          "    uint32_t key_type;\n"
          "    uint32_t part_id_mask;\n"
          "    uint32_t pubkey_size;\n"
          "    uint8_t  pubkey[KEYSTORE_PUBKEY_SIZE];\n"
          "};\n", stdout);
    /* Match the emitted file's #ifdef nesting so the Keystore_API's trailing
     * #endifs (size check + WOLFBOOT_NO_SIGN) close the right guards. The
     * inner check is forced false (the test's KEYSTORE_PUBKEY_SIZE matches). */
    fputs("#ifdef WOLFBOOT_NO_SIGN\n"
          "#define NUM_PUBKEYS 0\n"
          "#else\n"
          "#if 0\n"
          "#error Key algorithm mismatch\n"
          "#else\n", stdout);
    fputs("#define NUM_PUBKEYS 1\n"
          "const struct keystore_slot PubKeys[NUM_PUBKEYS] = {\n"
          "    { 0, 1, 0x1, 32, { 0 } }\n"
          "};\n", stdout);
    fputs(Keystore_API, stdout);
    return 0;
}
