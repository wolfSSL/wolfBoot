/* library_fs.c
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "image.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"


/* Helper function to convert partition ID to string */
static const char* partition_name(uint8_t part)
{
    switch (part) {
        case PART_BOOT:
            return "BOOT";
        case PART_UPDATE:
            return "UPDATE";
        default:
            return "UNKNOWN";
    }
}

/* Helper function to convert state value to string */
static const char* partition_state_name(uint8_t state)
{
    switch (state) {
        case IMG_STATE_NEW:
            return "NEW";
        case IMG_STATE_UPDATING:
            return "UPDATING";
        case IMG_STATE_FINAL_FLAGS:
            return "FFLAGS";
        case IMG_STATE_TESTING:
            return "TESTING";
        case IMG_STATE_SUCCESS:
            return "SUCCESS";
        default:
            return "UNKNOWN";
    }
}

/* Print all partition states */
static int cmd_get_all_states(void)
{
    uint32_t cur_fw_version, update_fw_version;
    uint16_t hdrSz;
    uint8_t boot_part_state = IMG_STATE_NEW, update_part_state = IMG_STATE_NEW;

    cur_fw_version = wolfBoot_current_firmware_version();
    update_fw_version = wolfBoot_update_firmware_version();

    wolfBoot_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_get_partition_state(PART_UPDATE, &update_part_state);

    wolfBoot_printf("\n");
    wolfBoot_printf("System information\n");
    wolfBoot_printf("====================================\n");
    wolfBoot_printf("Firmware version : 0x%lx\n",
        (unsigned long)wolfBoot_current_firmware_version());
    wolfBoot_printf("Current firmware state: %s\n",
        partition_state_name(boot_part_state));
    if (update_fw_version != 0) {
        if (update_part_state == IMG_STATE_UPDATING) {
            wolfBoot_printf("Candidate firmware version : 0x%lx\n",
                (unsigned long)update_fw_version);
        } else {
            wolfBoot_printf("Backup firmware version : 0x%lx\n",
                (unsigned long)update_fw_version);
        }
        wolfBoot_printf("Update state: %s\n",
            partition_state_name(update_part_state));
        if (update_fw_version > cur_fw_version) {
            wolfBoot_printf("'reboot' to initiate update.\n");
        } else {
            wolfBoot_printf("Update image older than current.\n");
        }
    } else {
        wolfBoot_printf("No image in update partition.\n");
    }

    return 0;
}

static int cmd_get_keystore(void)
{
    int i, j;
    uint32_t n_keys;

    wolfBoot_printf("\n");
    wolfBoot_printf("Bootloader keystore information\n");
    wolfBoot_printf("====================================\n");
    n_keys = keystore_num_pubkeys();
    wolfBoot_printf("Number of public keys: %lu\n", (unsigned long)n_keys);
    for (i = 0; i < (int)n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint32_t mask = keystore_get_mask(i);
        uint8_t *keybuf = keystore_get_buffer(i);

        wolfBoot_printf("\n");
        wolfBoot_printf("  Public Key #%d: size %lu, type %lx, mask %08lx\n", i,
                (unsigned long)size, (unsigned long)type, (unsigned long)mask);
        wolfBoot_printf("  ====================================\n  ");
        for (j = 0; j < (int)size; j++) {
            wolfBoot_printf("%02X ", keybuf[j]);
            if (j % 16 == 15) {
                wolfBoot_printf("\n  ");
            }
        }
        wolfBoot_printf("\n");
    }
    return 0;
}

/* Trigger an update */
static int cmd_update_trigger(void)
{
    wolfBoot_printf("Triggering update...\n");
    wolfBoot_update_trigger();
    wolfBoot_printf("Update triggered successfully. UPDATE partition set to UPDATING state.\n");
    return 0;
}

/* Mark current boot as successful */
static int cmd_success(void)
{
    wolfBoot_printf("Marking BOOT partition as SUCCESS...\n");
    wolfBoot_success();
    wolfBoot_printf("BOOT partition marked as SUCCESS.\n");
    return 0;
}

/* Print usage information */
static void print_usage(const char* prog_name)
{
    wolfBoot_printf("wolfBoot Partition Manager CLI\n");
    wolfBoot_printf("\nUsage: %s <command> [options]\n\n", prog_name);
    wolfBoot_printf("Commands:\n");
    wolfBoot_printf("  status              - Show state of all partitions\n");
    wolfBoot_printf("  keystore            - Show keystore information\n");
    wolfBoot_printf("  update-trigger      - Trigger an update (sets UPDATE partition to UPDATING)\n");
    wolfBoot_printf("  success             - Mark BOOT partition as SUCCESS\n");
    wolfBoot_printf("  verify-boot         - Verify integrity and authenticity of BOOT partition\n");
    wolfBoot_printf("  verify-update       - Verify integrity and authenticity of UPDATE partition\n");
    wolfBoot_printf("  help                - Show this help message\n");
    wolfBoot_printf("\nPartitions:\n");
    wolfBoot_printf("  BOOT                - Currently running firmware partition\n");
    wolfBoot_printf("  UPDATE              - Staging partition for new firmware\n");
    wolfBoot_printf("\nExamples:\n");
    wolfBoot_printf("  %s status           - Display all partition states\n", prog_name);
    wolfBoot_printf("  %s update-trigger   - Stage an update for next boot\n", prog_name);
    wolfBoot_printf("  %s success          - Confirm current firmware is working\n", prog_name);
    wolfBoot_printf("\n");
}

/* Verify integrity and authenticity of a partition */
static int cmd_verify(uint8_t part)
{
    struct wolfBoot_image img;
    int ret;
    size_t img_size = 0;

    ret = wolfBoot_open_image(&img, part);
    if (ret < 0) {
        wolfBoot_printf("Failed to open image header for %s partition (error: %d)\n",
                        partition_name(part), ret);
        return -1;
    }

    ret = wolfBoot_verify_integrity(&img);
    if (ret < 0) {
        wolfBoot_printf("Integrity check failed for %s partition\n",
            partition_name(part));
        return -1;
    }

    ret = wolfBoot_verify_authenticity(&img);
    if (ret < 0) {
        wolfBoot_printf("Authenticity check failed for %s partition\n",
            partition_name(part));
        return -1;
    }

    wolfBoot_printf("%s partition: Integrity and authenticity verified.\n",
        partition_name(part));
    return ret;
}

int main(int argc, const char* argv[])
{
    int ret = 0;
    const char* prog_name = "lib-fs";
    const char* command;

    if (argc >= 1) {
        prog_name = argv[0];
    }

    /* Check for argument count */
    if (argc != 2) {
        print_usage(prog_name);
        return 1;
    }

    command = argv[1];

    /* Process commands */
    if (strcmp(command, "status") == 0) {
        ret = cmd_get_all_states();
    }
    else if (strcmp(command, "keystore") == 0) {
        ret = cmd_get_keystore();
    }
    else if (strcmp(command, "update-trigger") == 0) {
        ret = cmd_update_trigger();
    }
    else if (strcmp(command, "success") == 0) {
        ret = cmd_success();
    }
    else if (strcmp(command, "verify-boot") == 0) {
        ret = cmd_verify(PART_BOOT);
    }
    else if (strcmp(command, "verify-update") == 0) {
        ret = cmd_verify(PART_UPDATE);
    }
    else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 ||
             strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        ret = 0;
    }
    else {
        wolfBoot_printf("Error: Unknown command '%s'\n\n", command);
        print_usage(argv[0]);
        ret = 1;
    }

    return ret;
}
