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
static const char* state_name(uint8_t state)
{
    switch (state) {
        case IMG_STATE_NEW:
            return "NEW";
        case IMG_STATE_UPDATING:
            return "UPDATING";
        case IMG_STATE_SUCCESS:
            return "SUCCESS";
        default:
            return "UNKNOWN";
    }
}

/* Print partition state */
static int cmd_get_state(uint8_t part)
{
    uint8_t state;
    int ret;
    
    ret = wolfBoot_get_partition_state(part, &state);
    if (ret != 0) {
        wolfBoot_printf("Error: Failed to get state for %s partition (error: %d)\n",
               partition_name(part), ret);
        return -1;
    }
    
    wolfBoot_printf("%s partition state: %s (0x%02X)\n",
           partition_name(part), state_name(state), state);
    return 0;
}

/* Print all partition states */
static int cmd_get_all_states(void)
{
    int ret = 0;
    
    wolfBoot_printf("=== Partition States ===\n");
    
    if (cmd_get_state(PART_BOOT) != 0)
        ret = -1;
    
    if (cmd_get_state(PART_UPDATE) != 0)
        ret = -1;
    
    return ret;
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
    wolfBoot_printf("  get-boot            - Get BOOT partition state\n");
    wolfBoot_printf("  get-update          - Get UPDATE partition state\n");
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
        wolfBoot_printf("Error: Failed to open image header for %s partition (error: %d)\n",
                        partition_name(part), ret);
        return -1;
    }

    ret = wolfBoot_verify_integrity(&img);
    if (ret < 0) {
        wolfBoot_printf("Integrity check failed for %s partition\n", partition_name(part));
        return -1;
    }

    ret = wolfBoot_verify_authenticity(&img);
    if (ret < 0) {
        wolfBoot_printf("Authenticity check failed for %s partition\n", partition_name(part));
        return -1;
    }

    wolfBoot_printf("%s partition: Integrity and authenticity verified.\n", partition_name(part));
    return ret;
}

int main(int argc, const char* argv[])
{
    int ret = 0;
    
    /* Check for argument count */
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* command = argv[1];
    
    /* Process commands */
    if (strcmp(command, "status") == 0) {
        ret = cmd_get_all_states();
    }
    else if (strcmp(command, "get-boot") == 0) {
        ret = cmd_get_state(PART_BOOT);
    }
    else if (strcmp(command, "get-update") == 0) {
        ret = cmd_get_state(PART_UPDATE);
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
