/* acpi.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#ifndef WOLFBOOT_ACPI_H
#define WOLFBOOT_ACPI_H

#include <stdint.h>

/* Base and size of the region the generated ACPI tables are laid into: high
 * usable RAM (below tolum), where real firmware places ACPI and clear of the
 * payload, kernel and initrd. The loader adds a matching e820 reservation.
 * Overridable so a host unit test can retarget it at a heap buffer. */
#ifndef ACPI_TABLE_BASE
#define ACPI_TABLE_BASE 0x60000000UL
#endif
#define ACPI_TABLE_SIZE 0x00010000UL /* 64 KiB */

/* Build RSDP -> XSDT -> { FADT, MADT, MCFG, DSDT } at ACPI_TABLE_BASE and
 * return the RSDP physical address for boot_params.acpi_rsdp_addr. */
uint64_t acpi_setup(void);

#endif /* WOLFBOOT_ACPI_H */
