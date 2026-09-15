/* library_fs.c
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

/* 64-bit file offsets, so the trailer seeks below stay correct past 2 GiB on
 * hosts where long is 32-bit. Must precede the first system header. */
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "image.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "disk_trailer.h"


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

/* Set by "--dev <path>" when that path is a disk boot slot. NULL means the
 * tool is addressing the flat image it was built against, and every command
 * uses the ordinary trailer accessors. */
static const char* disk_dev = NULL;

/* Boot confirmation on a disk slot is not the compile-time trailer.
 *
 * wolfBoot_get/set_partition_state() locate the trailer from
 * WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE and write
 * IMG_STATE_* values, which flip with WOLFBOOT_FLAGS_INVERT. Neither is
 * right here: the loader locates a slot's trailer from the size of the media
 * it actually found, and pins the values so the format does not depend on
 * how either program was built. So for "--dev" the three state commands go
 * straight at the tail of the device, through include/disk_trailer.h - the
 * same definitions src/update_disk.c uses.
 */
static int disk_trailer_open(const char* mode, FILE** fp, uint64_t* off)
{
    off_t end;

    *fp = fopen(disk_dev, mode);
    if (*fp == NULL) {
        wolfBoot_printf("Failed to open %s\n", disk_dev);
        return -1;
    }
    if (fseeko(*fp, 0, SEEK_END) != 0 || (end = ftello(*fp)) < 0) {
        wolfBoot_printf("Cannot determine the size of %s\n", disk_dev);
        fclose(*fp);
        return -1;
    }
    if (disk_trailer_offset((uint64_t)end, off) != 0) {
        wolfBoot_printf("%s is too small to carry a boot state\n", disk_dev);
        fclose(*fp);
        return -1;
    }
    if (fseeko(*fp, (off_t)*off, SEEK_SET) != 0) {
        wolfBoot_printf("Cannot seek to the tail of %s\n", disk_dev);
        fclose(*fp);
        return -1;
    }
    return 0;
}

static int disk_state_get(uint8_t* state)
{
    uint8_t buf[DISK_TRAILER_SZ];
    uint64_t off = 0;
    FILE* fp = NULL;

    if (disk_trailer_open("rb", &fp, &off) != 0) {
        return -1;
    }
    if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        wolfBoot_printf("Failed to read the tail of %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *state = disk_trailer_decode(buf);
    return 0;
}

static int disk_state_set(uint8_t state)
{
    uint8_t buf[DISK_TRAILER_SZ];
    uint8_t hdr[8];
    uint32_t magic;
    uint32_t fw_size;
    uint64_t off = 0;
    FILE* fp = NULL;

    if (disk_trailer_open("r+b", &fp, &off) != 0) {
        return -1;
    }
    /* Refuse if the trailer would land inside the image, mirroring the loader's
     * slot_state_write() guard, so a "stage"/"success" cannot corrupt a staged
     * image whose tail reaches the trailer. A device with no wolfBoot image at
     * offset 0 (a raw partition) has nothing to overlap and is written as before. */
    if (fseeko(fp, 0, SEEK_SET) != 0) {
        wolfBoot_printf("Cannot seek %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    if (fread(hdr, 1, sizeof(hdr), fp) == sizeof(hdr)) {
        magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        fw_size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                  ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        if ((magic == WOLFBOOT_MAGIC) &&
                (off < (uint64_t)IMAGE_HEADER_SIZE + fw_size)) {
            wolfBoot_printf("Boot state would overlap the image on %s\n",
                disk_dev);
            fclose(fp);
            return -1;
        }
    }
    if (fseeko(fp, (off_t)off, SEEK_SET) != 0) {
        wolfBoot_printf("Cannot seek to the tail of %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    disk_trailer_encode(buf, state);
    if (fwrite(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        wolfBoot_printf("Failed to write the tail of %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    if (fflush(fp) != 0) {
        wolfBoot_printf("Failed to flush %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    /* fflush only reaches the OS; fsync gets the boot state onto the device so
     * it survives the power cycle it exists to be read across. */
    if (fsync(fileno(fp)) != 0) {
        wolfBoot_printf("Failed to sync %s\n", disk_dev);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* Print a disk slot's boot state. */
static int cmd_disk_status(void)
{
    uint8_t state = DISK_STATE_NEW;

    if (disk_state_get(&state) != 0) {
        return -1;
    }
    wolfBoot_printf("\n");
    wolfBoot_printf("Disk boot slot: %s\n", disk_dev);
    wolfBoot_printf("====================================\n");
    wolfBoot_printf("Boot state     : %s (0x%02x)\n",
        disk_trailer_state_name(state), (unsigned int)state);
    wolfBoot_printf("\n");
    return 0;
}

/* Mark current boot as successful */
static int cmd_success(void)
{
    if (disk_dev != NULL) {
        wolfBoot_printf("Confirming this boot slot...\n");
        if (disk_state_set(DISK_STATE_SUCCESS) != 0) {
            return -1;
        }
        wolfBoot_printf("Slot marked SUCCESS.\n");
        return 0;
    }
    wolfBoot_printf("Marking BOOT partition as SUCCESS...\n");
    wolfBoot_success();
    wolfBoot_printf("BOOT partition marked as SUCCESS.\n");
    return 0;
}

/* Mark the current backing store as carrying a staged update.
 *
 * On a flash target the application calls wolfBoot_update_trigger(), which
 * marks the separate UPDATE partition. A disk slot is self-describing: the
 * state lives in its own tail, so staging means marking the slot that was
 * just written. wolfBoot promotes that to TESTING when it boots it, and
 * "success" below clears it. */
static int cmd_stage(void)
{
    if (disk_dev == NULL) {
        wolfBoot_printf("\"stage\" applies to a disk boot slot; pass "
            "--dev <path>.\n");
        return -1;
    }
    wolfBoot_printf("Marking this slot as carrying a staged update...\n");
    if (disk_state_set(DISK_STATE_UPDATING) != 0) {
        return -1;
    }
    wolfBoot_printf("Slot marked UPDATING; it will be put on probation at "
        "the next boot.\n");
    return 0;
}

/* Print usage information */
static void print_usage(const char* prog_name)
{
    wolfBoot_printf("wolfBoot Partition Manager CLI\n");
    wolfBoot_printf("\nUsage: %s [--dev <path>] <command>\n\n", prog_name);
    wolfBoot_printf("Commands:\n");
    wolfBoot_printf("  status              - Show state of all partitions\n");
    wolfBoot_printf("  keystore            - Show keystore information\n");
    wolfBoot_printf("  update-trigger      - Trigger an update (sets UPDATE partition to UPDATING)\n");
    wolfBoot_printf("  stage               - Mark this slot as carrying a staged update\n");
    wolfBoot_printf("  success             - Mark BOOT partition as SUCCESS\n");
    wolfBoot_printf("  verify-boot         - Verify integrity and authenticity of BOOT partition\n");
    wolfBoot_printf("  verify-update       - Verify integrity and authenticity of UPDATE partition\n");
    wolfBoot_printf("  help                - Show this help message\n");
    wolfBoot_printf("\nOptions:\n");
    wolfBoot_printf("  --dev <path>        - Operate on this backing store instead of the\n");
    wolfBoot_printf("                        image built in. With a disk boot slot's\n");
    wolfBoot_printf("                        partition device, \"status\", \"stage\" and\n");
    wolfBoot_printf("                        \"success\" act on that slot's own tail.\n");
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
    int argi = 1;

    if (argc >= 1) {
        prog_name = argv[0];
    }

    /* Optional "--dev <path>": operate on that backing store instead of the
     * image the binary was built against. Pointed at a disk boot slot's
     * partition device, the three state commands locate the trailer from the
     * size of that device, so one binary addresses any slot in turn with no
     * rebuild and nothing to keep in sync with the loader's layout. */
    if (argc >= 3 && strcmp(argv[1], "--dev") == 0) {
        disk_dev = argv[2];
        hal_filesystem_set_target(disk_dev);
        argi = 3;
    }

    /* Check for argument count */
    if (argc != argi + 1) {
        print_usage(prog_name);
        return 1;
    }

    command = argv[argi];

    /* Process commands */
    if (strcmp(command, "status") == 0) {
        ret = (disk_dev != NULL) ? cmd_disk_status() : cmd_get_all_states();
    }
    else if (strcmp(command, "keystore") == 0) {
        ret = cmd_get_keystore();
    }
    else if (strcmp(command, "update-trigger") == 0) {
        /* Refused with --dev, and not merely as a nicety. update-trigger
         * marks the separate UPDATE partition, which it locates from the
         * compile-time WOLFBOOT_PARTITION_UPDATE offset. Against a raw slot
         * device that offset is just somewhere in the middle of the slot, so
         * the write would land on whatever lives there. A disk slot is
         * self-describing and "stage" is its analogue. */
        if (disk_dev != NULL) {
            wolfBoot_printf("\"update-trigger\" does not apply to a disk "
                "boot slot; use \"stage\" instead.\n");
            ret = -1;
        }
        else {
            ret = cmd_update_trigger();
        }
    }
    else if (strcmp(command, "stage") == 0) {
        ret = cmd_stage();
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
