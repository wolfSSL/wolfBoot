/* boot_c2000.c
 *
 * Architecture boot handoff for the TI C2000 C28x DSP (TMS320F28P550SJ).
 *
 * The reset/startup path is provided by the C2000Ware codestart
 * (f28p55x_codestartbranch.asm -> _c_int00 -> main); wolfBoot's main()
 * (src/loader.c) then runs hal_init() and the verify state machine.  This file
 * provides the two arch hooks wolfBoot requires: do_boot(), which branches to
 * the verified application resident in the BOOT partition (execute-in-place),
 * and arch_reboot().
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

#include <stdint.h>
#include "hal.h"

#include "driverlib.h"
#include "device.h"

/*
 * Branch to the verified application.
 *
 * app_offset is the firmware base (BOOT partition address + IMAGE_HEADER_SIZE),
 * i.e. the application's own codestart, linked to execute in place.  There is
 * no vector table to reload on the C28x: interrupts are masked here and the
 * application's codestart re-establishes its stack pointer and re-runs the
 * C-runtime init before calling its main().  This never returns.
 */
void do_boot(const uint32_t *app_offset)
{
    void (*app_entry)(void);

    DINT;   /* mask maskable interrupts across the handoff */

    app_entry = (void (*)(void))(uintptr_t)app_offset;
    app_entry();

    /* Not reached. */
    while (1)
        ;
}

void arch_reboot(void)
{
    SysCtl_resetDevice();
    while (1)
        ;
}
