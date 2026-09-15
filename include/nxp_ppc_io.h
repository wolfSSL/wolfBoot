/* nxp_ppc_io.h
 *
 * MMIO accessors for the generic NS16550 driver on NXP QorIQ PowerPC.
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

#ifndef WOLFBOOT_NXP_PPC_IO_H
#define WOLFBOOT_NXP_PPC_IO_H

/* PowerPC device access needs the enforced-ordering sequences; a plain
 * volatile access is not enough. These match get8()/set8() in hal/nxp_ppc.h
 * byte for byte, repeated rather than included so the driver builds the same
 * from the stage1 and test-app sub-makes, which do not share the top-level
 * include path. */

static inline uint8_t ns16550_ppc_rd8(uintptr_t addr)
{
    int ret;
    __asm__ __volatile__(
        "sync;\n"
        "lbz%U1%X1 %0,%1;\n"
        "twi 0,%0,0;\n"
        "isync"
            : "=r" (ret) : "m" (*(const volatile unsigned char*)addr)
    );
    return (uint8_t)ret;
}

static inline void ns16550_ppc_wr8(uintptr_t addr, uint8_t val)
{
    __asm__ __volatile__(
        "stb%U0%X0 %1,%0;\n"
        "eieio"
            : "=m" (*(volatile unsigned char*)addr) : "r" ((int)val)
    );
}

#define NS16550_RD8(a)     ns16550_ppc_rd8((uintptr_t)(a))
#define NS16550_WR8(a, v)  ns16550_ppc_wr8((uintptr_t)(a), (uint8_t)(v))

#endif /* WOLFBOOT_NXP_PPC_IO_H */
