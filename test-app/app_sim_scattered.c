/* app_sim_scattered.c
 *
 * Test bare-metal boot-led-on application
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

#include "target.h"

#include "wolfboot/wolfboot.h"

#ifdef TARGET_sim


__attribute__((section(".r3text")))
int do_cmd(const char *cmd)
{
    /* Do nothing */
    return 0;
}

__attribute__((section(".r2text")))
int do_exec_cmd(char *name)
{
    return do_cmd(name);
}

__attribute__((section(".r1text")))
int main(int argc, char *argv[]) {
    do_exec_cmd((void *)0);
    return 0;
}
#endif /** TARGET_sim **/
