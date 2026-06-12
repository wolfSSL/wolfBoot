/* riscv_sbi.c
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

/* Minimal RISC-V SBI (Supervisor Binary Interface) runtime for wolfBoot.
 *
 * When wolfBoot replaces a full firmware (e.g. HSS+OpenSBI on PolarFire
 * SoC) and boots an S-mode OS (Linux), the OS issues SBI ecalls that must
 * be serviced in M-mode.  This provides the minimal set: BASE, TIME, IPI,
 * RFENCE, HSM, DBCN + the legacy console/timer calls, enough to bring a
 * RISC-V Linux kernel up to a console and timer tick.
 *
 * It is driven from handle_trap_ex() in src/boot_riscv.c on:
 *   - ecall-from-S (mcause = 9)      -> sbi_handle_ecall()
 *   - M-timer interrupt (mcause INT|7) -> sbi_timer_irq()
 *   - M-soft  interrupt (mcause INT|3) -> sbi_ipi_irq()
 */

#include <stdint.h>
#include "hal/riscv.h"
#include "printf.h"

#if defined(WOLFBOOT_RISCV_MMODE) && defined(WOLFBOOT_MMODE_SMODE_BOOT)

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

/* ---- platform glue (MPFS250) ------------------------------------------- */
#ifndef CLINT_BASE
#define CLINT_BASE          0x02000000UL
#endif
/* The platform header may already provide CLINT helpers; undef to avoid a
 * redefinition error, then define the exact register forms used here. */
#ifdef CLINT_MSIP
#undef CLINT_MSIP
#endif
#ifdef CLINT_MTIMECMP
#undef CLINT_MTIMECMP
#endif
#define CLINT_MSIP(h)       (*(volatile uint32_t *)(CLINT_BASE + (h) * 4UL))
#define CLINT_MTIMECMP(h)   (*(volatile uint64_t *)(CLINT_BASE + 0x4000UL + (h) * 8UL))

#ifndef SBI_CONSOLE_UART_BASE
#ifdef DEBUG_UART_BASE
#define SBI_CONSOLE_UART_BASE  DEBUG_UART_BASE
#else
#define SBI_CONSOLE_UART_BASE  0x20000000UL  /* MPFS MMUART0 */
#endif
#endif
/* MPFS MMUART: THR at +0x100, LSR at +0x14, THRE = bit 5. */
#define SBI_UART_THR  (*(volatile uint8_t *)(SBI_CONSOLE_UART_BASE + 0x100UL))
#define SBI_UART_LSR  (*(volatile uint8_t *)(SBI_CONSOLE_UART_BASE + 0x14UL))
#define SBI_UART_THRE 0x20U

/* ---- CSR helpers -------------------------------------------------------- */
#define csr_set_bits(csr, bits) \
    __asm__ volatile("csrs " #csr ", %0" :: "r"(bits) : "memory")
#define csr_clr_bits(csr, bits) \
    __asm__ volatile("csrc " #csr ", %0" :: "r"(bits) : "memory")

#define MIP_STIP   (1UL << IRQ_S_TIMER)   /* bit 5 */
#define MIP_SSIP   (1UL << IRQ_S_SOFT)    /* bit 1 */
#define MIE_MTIP   (1UL << IRQ_M_TIMER)   /* bit 7 */

/* ---- SBI constants ------------------------------------------------------ */
#define SBI_SUCCESS              0L
#define SBI_ERR_FAILED         (-1L)
#define SBI_ERR_NOT_SUPPORTED  (-2L)
#define SBI_ERR_INVALID_PARAM  (-3L)
#define SBI_ERR_ALREADY_AVAIL  (-6L)

/* Extension IDs (EIDs) */
#define SBI_EXT_0_1_SET_TIMER       0x00
#define SBI_EXT_0_1_CONSOLE_PUTCHAR 0x01
#define SBI_EXT_0_1_CONSOLE_GETCHAR 0x02
#define SBI_EXT_0_1_CLEAR_IPI       0x03
#define SBI_EXT_0_1_SEND_IPI        0x04
#define SBI_EXT_0_1_REMOTE_FENCE_I  0x05
#define SBI_EXT_0_1_REMOTE_SFENCE   0x06
#define SBI_EXT_0_1_REMOTE_SFENCE_ASID 0x07
#define SBI_EXT_0_1_SHUTDOWN        0x08
#define SBI_EXT_BASE                0x10
#define SBI_EXT_TIME                0x54494D45UL
#define SBI_EXT_IPI                 0x00735049UL
#define SBI_EXT_RFENCE              0x52464E43UL
#define SBI_EXT_HSM                 0x0048534DUL
#define SBI_EXT_SRST                0x53525354UL
#define SBI_EXT_DBCN                0x4442434EUL

/* BASE FIDs */
#define SBI_BASE_GET_SPEC_VERSION   0
#define SBI_BASE_GET_IMPL_ID        1
#define SBI_BASE_GET_IMPL_VERSION   2
#define SBI_BASE_PROBE_EXT          3
#define SBI_BASE_GET_MVENDORID      4
#define SBI_BASE_GET_MARCHID        5
#define SBI_BASE_GET_MIMPID         6

#define SBI_SPEC_VERSION  ((0UL << 24) | 2UL)   /* v0.2 */
#define SBI_IMPL_ID       3UL                    /* "wolfSBI" (custom) */
#define SBI_IMPL_VERSION  1UL

/* HSM hart states (SBI spec) */
#define SBI_HSM_STARTED        0
#define SBI_HSM_STOPPED        1
#define SBI_HSM_START_PENDING  2

/* Register-frame indices (regs[N] == xN), see src/vector_riscv.S trap_entry */
#define A0 10
#define A1 11
#define A2 12
#define A6 16
#define A7 17

#ifndef MPFS_NUM_HARTS
#define MPFS_NUM_HARTS 5
#endif

/* Cross-hart SBI state must NOT live in L2-scratch BSS: cacheable stores
 * to the scratchpad (Zero Device) can be silently lost when the dirty
 * cache line is eventually evicted, so values written by one hart vanish
 * before another hart (or a later cache-cold read) sees them.  Keep this
 * state in the E51 DTIM instead: a small always-uncached RAM that every
 * hart reads and writes coherently (the same role it has under HSS). */
#ifndef SBI_SHARED_DTIM_ADDR
#define SBI_SHARED_DTIM_ADDR 0x01000000UL  /* E51 DTIM */
#endif
#define SBI_SHARED_MAGIC 0x53424921UL      /* "SBI!" */

typedef struct {
    volatile uint32_t init_magic;
    volatile int      hart_state[MPFS_NUM_HARTS];
    volatile uint32_t ipi_ops[MPFS_NUM_HARTS];
} sbi_shared_state_t;
#define SBI_SHARED ((sbi_shared_state_t *)SBI_SHARED_DTIM_ADDR)
#define sbi_hart_state (SBI_SHARED->hart_state)
#define sbi_ipi_ops    (SBI_SHARED->ipi_ops)

/* Per-hart IPI work flags, set by a requesting hart and consumed in the
 * target hart's M-mode software-interrupt handler. */
#define SBI_IPI_OP_SSIP    1U  /* inject a supervisor software interrupt */
#define SBI_IPI_OP_FENCE_I 2U  /* remote instruction-stream sync */
#define SBI_IPI_OP_SFENCE  4U  /* remote sfence.vma (full flush) */

/* Platform hook: release a parked hart into S-mode at saddr with a1=opaque
 * (SBI HSM hart_start backend).  Weak default: unsupported. */
int __attribute__((weak)) sbi_hal_hart_start(unsigned long hartid,
    unsigned long saddr, unsigned long opaque)
{
    (void)hartid;
    (void)saddr;
    (void)opaque;
    return -1;
}

/* Called from the M->S release path on the hart entering S-mode.  The
 * first call (the boot hart, released before any other) also initializes
 * the table: zeroed BSS would otherwise read as STARTED for every hart. */
void sbi_hart_mark_started(unsigned long hartid)
{
    unsigned int k;
    if (SBI_SHARED->init_magic != SBI_SHARED_MAGIC) {
        for (k = 0; k < (unsigned int)MPFS_NUM_HARTS; k++) {
            sbi_hart_state[k] = SBI_HSM_STOPPED;
            sbi_ipi_ops[k] = 0;
        }
        __asm__ volatile("fence rw, rw" ::: "memory");
        SBI_SHARED->init_magic = SBI_SHARED_MAGIC;
    }
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        sbi_hart_state[hartid] = SBI_HSM_STARTED;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }
}

#define CLINT_MTIME (*(volatile uint64_t *)(CLINT_BASE + 0xBFF8UL))
#define MSTATUS_MPRV_BIT (1UL << 17)

/* Per-hart M-mode trap stacks.  The trap entry (src/vector_riscv.S)
 * switches to the stack armed in mscratch because the trapped S-mode
 * context's sp is a virtual address once the OS enables paging.
 *
 * The stacks must NOT live in L2-scratch BSS: under SMP cache pressure a
 * dirty frame line can be evicted mid-trap and the writeback to the
 * scratchpad is lost, so the restore reads zeros (observed: the kernel
 * resumed from an ecall with sp=0).  Use the OS-invisible reserved DDR
 * region instead (hss-buffer@103fc00000, nomap in the stock dtb -- the
 * monitor carve-out under HSS). */
#ifndef SBI_MSTACK_BASE
#define SBI_MSTACK_BASE 0x103FC00000UL
#endif
#define SBI_MSTACK_SIZE 4096UL

/* Arm this hart's M-mode trap stack; call just before entering S-mode. */
void sbi_mscratch_init(unsigned long hartid)
{
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        csr_write(mscratch,
            SBI_MSTACK_BASE + (hartid + 1UL) * SBI_MSTACK_SIZE);
    }
}

/* Copy bytes from an S-mode pointer (virtual once paging is on) into an
 * M-mode buffer using the MPRV trick: with mstatus.MPRV set and MPP=S
 * (true inside a trap taken from S-mode), M-mode loads are translated
 * exactly like S-mode accesses. */
static void sbi_copy_from_smode(uint8_t *dst, unsigned long src,
    unsigned long len)
{
    unsigned long i;
    csr_set_bits(mstatus, MSTATUS_MPRV_BIT);
    for (i = 0; i < len; i++) {
        dst[i] = ((const volatile uint8_t *)src)[i];
    }
    csr_clr_bits(mstatus, MSTATUS_MPRV_BIT);
}

#define MSTATUS_MXR_BIT (1UL << 19)

/* Guest-context byte accessors: with mstatus.MPRV set, M-mode memory
 * accesses use the trapped (S/U) context's translation and permissions;
 * in bare mode addresses pass through.  MXR additionally permits reading
 * execute-only pages (instruction fetch for emulation). */
static uint8_t sbi_guest_lb(unsigned long a)
{
    uint8_t v;
    csr_set_bits(mstatus, MSTATUS_MPRV_BIT);
    v = *(const volatile uint8_t *)a;
    csr_clr_bits(mstatus, MSTATUS_MPRV_BIT);
    return v;
}

static void sbi_guest_sb(unsigned long a, uint8_t v)
{
    csr_set_bits(mstatus, MSTATUS_MPRV_BIT);
    *(volatile uint8_t *)a = v;
    csr_clr_bits(mstatus, MSTATUS_MPRV_BIT);
}

static uint16_t sbi_guest_ifetch16(unsigned long a)
{
    uint16_t v;
    csr_set_bits(mstatus, MSTATUS_MPRV_BIT | MSTATUS_MXR_BIT);
    v = *(const volatile uint16_t *)a;
    csr_clr_bits(mstatus, MSTATUS_MPRV_BIT | MSTATUS_MXR_BIT);
    return v;
}

/* Emulate a misaligned load (cause 4) or store (cause 6) from S/U mode.
 * These harts take misaligned accesses to M-mode and expect the firmware
 * to emulate them byte-wise (OpenSBI behavior).  Handles the integer
 * I/S-type forms and the common compressed forms; AMO and floating-point
 * forms are not emulated (returns 0 -> fatal dump).  Returns the
 * advanced epc, or 0 if the instruction is not handled. */
unsigned long sbi_misaligned_ldst(unsigned long *regs, unsigned long epc,
    unsigned long tval, unsigned long cause)
{
    uint32_t insn;
    unsigned long ilen;
    unsigned long addr = tval;
    unsigned long val = 0;
    unsigned long n = 0;
    unsigned long i;
    uint32_t rd = 0;
    uint32_t rs2 = 0;
    uint32_t f3;
    int sign = 0;
    int store;

    store = (cause == 6UL) ? 1 : 0;
    insn = (uint32_t)sbi_guest_ifetch16(epc);
    if ((insn & 3U) == 3U) {
        insn |= ((uint32_t)sbi_guest_ifetch16(epc + 2U)) << 16;
        ilen = 4U;
    }
    else {
        ilen = 2U;
    }

    if (ilen == 4U) {
        f3 = (insn >> 12) & 7U;
        if ((store == 0) && ((insn & 0x7FU) == 0x03U)) {
            /* LH/LW/LD/LHU/LWU */
            rd = (insn >> 7) & 0x1FU;
            switch (f3) {
            case 1: n = 2; sign = 1; break;
            case 2: n = 4; sign = 1; break;
            case 3: n = 8; break;
            case 5: n = 2; break;
            case 6: n = 4; break;
            default: return 0;
            }
        }
        else if ((store != 0) && ((insn & 0x7FU) == 0x23U)) {
            /* SH/SW/SD */
            rs2 = (insn >> 20) & 0x1FU;
            switch (f3) {
            case 1: n = 2; break;
            case 2: n = 4; break;
            case 3: n = 8; break;
            default: return 0;
            }
        }
        else {
            return 0;
        }
    }
    else {
        /* Compressed: C.LW/C.LD/C.SW/C.SD (quadrant 0, reg-reg') and the
         * stack-pointer forms C.LWSP/C.LDSP/C.SWSP/C.SDSP (quadrant 2). */
        uint32_t q = insn & 3U;
        f3 = (insn >> 13) & 7U;
        if (q == 0U) {
            if ((store == 0) && (f3 == 2U || f3 == 3U)) {
                rd = 8U + ((insn >> 2) & 7U);
                n = (f3 == 2U) ? 4U : 8U;
                sign = (f3 == 2U) ? 1 : 0;
            }
            else if ((store != 0) && (f3 == 6U || f3 == 7U)) {
                rs2 = 8U + ((insn >> 2) & 7U);
                n = (f3 == 6U) ? 4U : 8U;
            }
            else {
                return 0;
            }
        }
        else if (q == 2U) {
            if ((store == 0) && (f3 == 2U || f3 == 3U)) {
                rd = (insn >> 7) & 0x1FU;
                n = (f3 == 2U) ? 4U : 8U;
                sign = (f3 == 2U) ? 1 : 0;
            }
            else if ((store != 0) && (f3 == 6U || f3 == 7U)) {
                rs2 = (insn >> 2) & 0x1FU;
                n = (f3 == 6U) ? 4U : 8U;
            }
            else {
                return 0;
            }
        }
        else {
            return 0;
        }
    }

    if (store != 0) {
        val = (rs2 != 0U) ? regs[rs2] : 0UL;
        for (i = 0; i < n; i++) {
            sbi_guest_sb(addr + i, (uint8_t)(val >> (8U * i)));
        }
    }
    else {
        for (i = 0; i < n; i++) {
            val |= ((unsigned long)sbi_guest_lb(addr + i)) << (8U * i);
        }
        if (sign != 0) {
            if (n == 2U) {
                val = (unsigned long)(long)(short)val;
            }
            else if (n == 4U) {
                val = (unsigned long)(long)(int)val;
            }
        }
        if (rd != 0U) {
            regs[rd] = val;
        }
    }
    return epc + ilen;
}

/* Emulate instructions that trap as illegal from S-mode.  These harts
 * have no time CSR (CLINT MTIME is memory-mapped only), so the kernel's
 * rdtime traps here.  Returns the updated epc, or 0 if not handled. */
unsigned long sbi_illegal_insn(unsigned long *regs, unsigned long epc,
    unsigned long tval)
{
    unsigned long rd;
    /* rdtime rd  ==  csrrs rd, time(0xC01), x0 */
    if ((tval & 0xFFFFF07FUL) == 0xC0102073UL) {
        rd = (tval >> 7) & 0x1FUL;
        if (rd != 0UL) {
            regs[rd] = (unsigned long)CLINT_MTIME;
        }
        return epc + 4UL;
    }
    return 0UL;
}

static void sbi_putc(char c)
{
    if (c == '\n') {
        while ((SBI_UART_LSR & SBI_UART_THRE) == 0U) { }
        SBI_UART_THR = (uint8_t)'\r';
    }
    while ((SBI_UART_LSR & SBI_UART_THRE) == 0U) { }
    SBI_UART_THR = (uint8_t)c;
}

/* Set a hart's S-mode timer.  M-mode owns the timer; we program the CLINT
 * mtimecmp, drop any already-injected S-timer interrupt, and (re)enable the
 * M-timer so it fires when mtime reaches the comparator. */
static void sbi_set_timer(unsigned long hartid, uint64_t stime)
{
    CLINT_MTIMECMP(hartid) = stime;
    csr_clr_bits(mip, MIP_STIP);
    csr_set_bits(mie, MIE_MTIP);
}

/* M-timer interrupt: deliver to S-mode by setting STIP, and mask the M-timer
 * until the OS reschedules via set_timer (otherwise it would re-fire). */
void sbi_timer_irq(void)
{
    csr_clr_bits(mie, MIE_MTIP);
    csr_set_bits(mip, MIP_STIP);
}

/* M-soft (IPI) interrupt on a RUNNING (S-mode) hart: clear the CLINT MSIP
 * that woke us, perform any requested remote-fence work, and inject a
 * supervisor software interrupt when an OS IPI was requested.  A bare
 * MSIP with no op flags is treated as an OS IPI for compatibility. */
void sbi_ipi_irq(unsigned long hartid)
{
    uint32_t ops = 0;

    CLINT_MSIP(hartid) = 0U;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        ops = sbi_ipi_ops[hartid];
        sbi_ipi_ops[hartid] = 0;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }
    if ((ops & SBI_IPI_OP_FENCE_I) != 0U) {
        __asm__ volatile("fence.i" ::: "memory");
    }
    if ((ops & SBI_IPI_OP_SFENCE) != 0U) {
        __asm__ volatile("sfence.vma" ::: "memory");
    }
    if ((ops & SBI_IPI_OP_SSIP) != 0U || ops == 0U) {
        csr_set_bits(mip, MIP_SSIP);
    }
}

/* Post an IPI op to every hart in (mask << base) and ring its MSIP.
 * The caller hart is skipped (callers handle self locally). */
static void sbi_post_ipi(unsigned long mask, unsigned long base,
    uint32_t op, unsigned long self)
{
    unsigned long i;
    unsigned long h;
    for (i = 0; i < (unsigned long)MPFS_NUM_HARTS; i++) {
        if ((mask & (1UL << i)) == 0UL) {
            continue;
        }
        h = base + i;
        if (h == self || h >= (unsigned long)MPFS_NUM_HARTS) {
            continue;
        }
        if (sbi_hart_state[h] != SBI_HSM_STARTED) {
            continue; /* parked harts consume MSIP in their wake loop */
        }
        sbi_ipi_ops[h] |= op;
        __asm__ volatile("fence rw, rw" ::: "memory");
        CLINT_MSIP(h) = 1U;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* Wait (bounded) for the posted fence ops to be consumed by the targets.
 * The SBI remote-fence calls are synchronous; the bound guards against a
 * wedged target turning into a wedged caller. */
static void sbi_wait_ipi_done(unsigned long mask, unsigned long base,
    unsigned long self)
{
    unsigned long i;
    unsigned long h;
    uint32_t spin;
    for (i = 0; i < (unsigned long)MPFS_NUM_HARTS; i++) {
        if ((mask & (1UL << i)) == 0UL) {
            continue;
        }
        h = base + i;
        if (h == self || h >= (unsigned long)MPFS_NUM_HARTS ||
            sbi_hart_state[h] != SBI_HSM_STARTED) {
            continue;
        }
        spin = 10000000U;
        while (sbi_ipi_ops[h] != 0U && spin > 0U) {
            spin--;
        }
    }
}

/* Returns the (possibly advanced) PC to resume at.  For ecall we skip the
 * 4-byte ecall instruction. */
unsigned long sbi_handle_ecall(unsigned long *regs, unsigned long epc)
{
    unsigned long eid = regs[A7];
    unsigned long fid = regs[A6];
    long err = SBI_SUCCESS;
    unsigned long val = 0;
    unsigned long hartid;

    __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));

#ifdef DEBUG_SBI
    /* Bring-up visibility: trace the first few ecalls so a silent kernel
     * can be distinguished from a broken console path.  Console and timer
     * ecalls are exempted: tracing the timer path serializes every tick
     * behind the UART and slows the OS dramatically. */
    {
        static uint32_t sbi_dbg_calls = 0;
        if (sbi_dbg_calls < 40U && eid != SBI_EXT_0_1_CONSOLE_PUTCHAR &&
            eid != SBI_EXT_DBCN && eid != SBI_EXT_TIME &&
            eid != SBI_EXT_0_1_SET_TIMER) {
            sbi_dbg_calls++;
            wolfBoot_printf("[SBI#%u h%lu] eid=0x%lx fid=%lu a0=0x%lx\n",
                (unsigned)sbi_dbg_calls, hartid, eid, fid, regs[A0]);
        }
    }
#endif


    switch (eid) {
    case SBI_EXT_BASE:
        switch (fid) {
        case SBI_BASE_GET_SPEC_VERSION: val = SBI_SPEC_VERSION; break;
        case SBI_BASE_GET_IMPL_ID:      val = SBI_IMPL_ID; break;
        case SBI_BASE_GET_IMPL_VERSION: val = SBI_IMPL_VERSION; break;
        case SBI_BASE_PROBE_EXT:
            /* a0 = extension id to probe; return 1 if supported. */
            switch (regs[A0]) {
            case SBI_EXT_BASE:
            case SBI_EXT_TIME:
            case SBI_EXT_IPI:
            case SBI_EXT_RFENCE:
            case SBI_EXT_HSM:
            case SBI_EXT_SRST:
            case SBI_EXT_DBCN:
                val = 1; break;
            default:
                val = 0; break;
            }
            break;
        case SBI_BASE_GET_MVENDORID:
        case SBI_BASE_GET_MARCHID:
        case SBI_BASE_GET_MIMPID:
            val = 0; break;
        default:
            err = SBI_ERR_NOT_SUPPORTED; break;
        }
        break;

    case SBI_EXT_TIME:
        if (fid == 0) {
            sbi_set_timer(hartid, (uint64_t)regs[A0]);
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
        }
        break;

    case SBI_EXT_IPI:
        if (fid == 0) {
            /* send_ipi(hart_mask, hart_mask_base) */
            sbi_post_ipi(regs[A0], regs[A1], SBI_IPI_OP_SSIP, hartid);
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
        }
        break;

    case SBI_EXT_RFENCE: {
        /* remote_fence_i(mask, base) / remote_sfence_vma(mask, base,
         * start, size) / ..._asid: over-flush with full fences (legal
         * per the spec) and wait for the targets to consume the op. */
        uint32_t op;
        if (fid == 0) {
            op = SBI_IPI_OP_FENCE_I;
            __asm__ volatile("fence.i" ::: "memory");
        } else if (fid == 1 || fid == 2) {
            op = SBI_IPI_OP_SFENCE;
            __asm__ volatile("sfence.vma" ::: "memory");
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        sbi_post_ipi(regs[A0], regs[A1], op, hartid);
        sbi_wait_ipi_done(regs[A0], regs[A1], hartid);
        break;
    }

    case SBI_EXT_HSM:
        switch (fid) {
        case 0: /* hart_start(hartid, start_addr, opaque) */
            if (regs[A0] >= (unsigned long)MPFS_NUM_HARTS) {
                err = SBI_ERR_INVALID_PARAM;
            }
            else if (sbi_hart_state[regs[A0]] == SBI_HSM_STARTED) {
                err = SBI_ERR_ALREADY_AVAIL;
            }
            else if (sbi_hal_hart_start(regs[A0], regs[A1],
                                        regs[A2]) != 0) {
                err = SBI_ERR_FAILED;
            }
            else {
                sbi_hart_state[regs[A0]] = SBI_HSM_START_PENDING;
            }
            break;
        case 1: /* hart_stop */
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        case 2: /* hart_get_status(hartid) */
            if (regs[A0] < (unsigned long)MPFS_NUM_HARTS) {
                val = (unsigned long)sbi_hart_state[regs[A0]];
            } else {
                err = SBI_ERR_INVALID_PARAM;
            }
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case SBI_EXT_DBCN:
        switch (fid) {
        case 0: { /* console_write(num_bytes, base_lo, base_hi) */
            unsigned long n = regs[A0];
            unsigned long gp = regs[A1];
            unsigned long k;
            unsigned long c;
            unsigned long j;
            uint8_t cbuf[64];
            if (n > 4096UL) {
                n = 4096UL; /* bound a single call; kernel loops on val */
            }
            for (k = 0; k < n; k += c) {
                c = n - k;
                if (c > sizeof(cbuf)) {
                    c = sizeof(cbuf);
                }
                sbi_copy_from_smode(cbuf, gp + k, c);
                for (j = 0; j < c; j++) {
                    sbi_putc((char)cbuf[j]);
                }
            }
            val = n;
            break;
        }
        case 2: /* console_write_byte(byte) */
            sbi_putc((char)regs[A0]);
            break;
        case 1: /* console_read -- no input wired yet */
            val = 0;
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case SBI_EXT_SRST:
        /* system_reset(type, reason): announce, drain UART, then reset. */
        wolfBoot_printf("[SBI] SYSTEM RESET requested: type=0x%lx "
            "reason=0x%lx\n", regs[A0], regs[A1]);
        {
            volatile uint32_t spin;
            for (spin = 0; spin < 20000000UL; spin++) { }
        }
#ifdef TARGET_mpfs250
        SYSREG_MSS_RESET_CR = 0xDEAD;
#endif
        while (1) { }

    /* ---- Legacy (v0.1) calls: return value in a0, no a1 ---- */
    case SBI_EXT_0_1_SET_TIMER:
        sbi_set_timer(hartid, (uint64_t)regs[A0]);
        regs[A0] = 0;
        return epc + 4;
    case SBI_EXT_0_1_CONSOLE_PUTCHAR:
        sbi_putc((char)regs[A0]);
        regs[A0] = 0;
        return epc + 4;
    case SBI_EXT_0_1_CONSOLE_GETCHAR:
        regs[A0] = (unsigned long)-1L;
        return epc + 4;
    case SBI_EXT_0_1_CLEAR_IPI:
        csr_clr_bits(mip, MIP_SSIP);
        regs[A0] = 0;
        return epc + 4;
    case SBI_EXT_0_1_SEND_IPI:
        /* a0 = S-mode pointer to a hart mask; fetch via MPRV. */
        if (regs[A0] != 0UL) {
            unsigned long lmask = 0;
            sbi_copy_from_smode((uint8_t *)&lmask, regs[A0],
                sizeof(unsigned long));
            sbi_post_ipi(lmask, 0, SBI_IPI_OP_SSIP, hartid);
        }
        regs[A0] = 0;
        return epc + 4;
    case SBI_EXT_0_1_REMOTE_FENCE_I:
    case SBI_EXT_0_1_REMOTE_SFENCE:
    case SBI_EXT_0_1_REMOTE_SFENCE_ASID:
        __asm__ volatile("fence.i" ::: "memory");
        __asm__ volatile("sfence.vma" ::: "memory");
        if (regs[A0] != 0UL) {
            unsigned long fmask = 0;
            sbi_copy_from_smode((uint8_t *)&fmask, regs[A0],
                sizeof(unsigned long));
            sbi_post_ipi(fmask, 0,
                (eid == SBI_EXT_0_1_REMOTE_FENCE_I) ?
                    SBI_IPI_OP_FENCE_I : SBI_IPI_OP_SFENCE, hartid);
            sbi_wait_ipi_done(fmask, 0, hartid);
        }
        regs[A0] = 0;
        return epc + 4;
    case SBI_EXT_0_1_SHUTDOWN:
        wolfBoot_printf("[SBI] legacy SHUTDOWN requested\n");
        {
            volatile uint32_t spin2;
            for (spin2 = 0; spin2 < 20000000UL; spin2++) { }
        }
#ifdef TARGET_mpfs250
        SYSREG_MSS_RESET_CR = 0xDEAD;
#endif
        while (1) { }

    default:
        err = SBI_ERR_NOT_SUPPORTED;
        break;
    }

    regs[A0] = (unsigned long)err;
    regs[A1] = val;
    return epc + 4;
}

#endif /* WOLFBOOT_RISCV_MMODE && WOLFBOOT_MMODE_SMODE_BOOT */
