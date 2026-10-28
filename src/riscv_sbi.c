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

/* Minimal RISC-V SBI runtime: BASE, TIME, IPI, RFENCE, HSM, DBCN and the legacy
 * calls, driven from handle_trap_ex() in src/boot_riscv.c. */

#include <stdint.h>
#include "hal/riscv.h"
#include "printf.h"

#if defined(WOLFBOOT_RISCV_MMODE) && defined(WOLFBOOT_MMODE_SMODE_BOOT)

/* The SBI error codes and the vendor-extension IDs are generic. */
#include "riscv_sbi.h"

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

/* ---- SBI constants (error codes in riscv_sbi.h) ------------------------- */
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

/* v0.3: the OS only uses SRST (reboot and power-off) from 0.3 up. */
#define SBI_SPEC_VERSION  ((0UL << 24) | 3UL)
/* wolfBoot has no registry ID; 0x776F6C66 ("wolf") cannot collide with the
 * small spec-registry values. */
#define SBI_IMPL_ID       0x776F6C66UL           /* "wolf" (custom, unregistered) */
#define SBI_IMPL_VERSION  1UL

/* HSM hart states (SBI spec) */
#define SBI_HSM_STARTED        0
#define SBI_HSM_STOPPED        1
#define SBI_HSM_START_PENDING  2

/* regs[N] == xN for the a* registers used here.  Slots 0 and 2 are repurposed
 * by the trap entry, so they are NOT x0/x2; nothing below indexes them. */
#define A0 10
#define A1 11
#define A2 12
#define A6 16
#define A7 17

#ifndef MPFS_NUM_HARTS
#define MPFS_NUM_HARTS 5
#endif

/* Cross-hart state cannot live in L2-scratch BSS: a cacheable store there is
 * lost on eviction.  The E51 DTIM is uncached and coherent, as under HSS. */
#ifndef SBI_SHARED_DTIM_ADDR
#define SBI_SHARED_DTIM_ADDR 0x01000000UL  /* E51 DTIM */
#endif
#define SBI_SHARED_MAGIC 0x53424921UL      /* "SBI!" */

typedef struct {
    volatile uint32_t init_magic;
    volatile int      hart_state[MPFS_NUM_HARTS];
    volatile uint32_t ipi_ops[MPFS_NUM_HARTS];
    /* Fence completion: a requester ORs its op in, then takes a ticket; the
     * target publishes the ticket it saw once the fences have run. */
    volatile uint32_t ipi_posted[MPFS_NUM_HARTS];
    volatile uint32_t ipi_done[MPFS_NUM_HARTS];
} sbi_shared_state_t;
#define SBI_SHARED ((sbi_shared_state_t *)SBI_SHARED_DTIM_ADDR)
#define sbi_hart_state   (SBI_SHARED->hart_state)
#define sbi_ipi_ops      (SBI_SHARED->ipi_ops)
#define sbi_ipi_posted   (SBI_SHARED->ipi_posted)
#define sbi_ipi_done     (SBI_SHARED->ipi_done)

/* Per-hart IPI work flags, set by a requesting hart and consumed in the
 * target hart's M-mode software-interrupt handler. */
#define SBI_IPI_OP_SSIP    1U  /* inject a supervisor software interrupt */
#define SBI_IPI_OP_FENCE_I 2U  /* remote instruction-stream sync */
#define SBI_IPI_OP_SFENCE  4U  /* remote sfence.vma (full flush) */
#define SBI_IPI_OP_HALT    8U  /* park the hart in M-mode (system shutdown) */

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

/* Called on the hart entering S-mode.  The first call also initializes the
 * table: zeroed BSS would otherwise read as STARTED for every hart. */
void sbi_hart_mark_started(unsigned long hartid)
{
    unsigned int k;
    if (SBI_SHARED->init_magic != SBI_SHARED_MAGIC) {
        for (k = 0; k < (unsigned int)MPFS_NUM_HARTS; k++) {
            sbi_hart_state[k] = SBI_HSM_STOPPED;
            sbi_ipi_ops[k] = 0;
            sbi_ipi_posted[k] = 0;
            sbi_ipi_done[k] = 0;
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

/* Per-hart M-mode trap stacks, in the OS-invisible reserved DDR region: in
 * L2-scratch BSS a dirty frame line can be evicted mid-trap. */
#ifndef SBI_MSTACK_BASE
#define SBI_MSTACK_BASE 0x103FC00000UL
#endif
#ifndef SBI_MSTACK_SHIFT
#define SBI_MSTACK_SHIFT 12
#endif
#define SBI_MSTACK_SIZE (1UL << SBI_MSTACK_SHIFT)

/* Remote-fence completion wait, in CLINT ticks (1 MHz on PolarFire). */
#ifndef RTC_CLOCK_FREQ
#define RTC_CLOCK_FREQ 1000000UL
#endif
#define SBI_IPI_WAIT_TICKS ((uint64_t)RTC_CLOCK_FREQ)

/* Arm this hart's M-mode trap stack; call just before entering S-mode. */
void sbi_mscratch_init(unsigned long hartid)
{
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        csr_write(mscratch,
            SBI_MSTACK_BASE + (hartid + 1UL) * SBI_MSTACK_SIZE);
    }
}

/* With mstatus.MPRV and MPP=S, M-mode accesses translate as S-mode ones.  A
 * bad address faults, so mtvec points at sbi_mprv_trap to step over it. */
volatile unsigned long sbi_mprv_fault[MPFS_NUM_HARTS];

__asm__(
    ".pushsection .text\n"
    ".align 2\n"
    ".type sbi_mprv_trap, @function\n"
    "sbi_mprv_trap:\n"
    "    csrr  t0, mepc\n"
    "    addi  t0, t0, 4\n"
    "    csrw  mepc, t0\n"
    "    li    t0, 0x20000\n"
    "    csrc  mstatus, t0\n"
    "    la    t0, sbi_mprv_fault\n"
    "    csrr  t1, mhartid\n"
    "    slli  t1, t1, 3\n"
    "    add   t0, t0, t1\n"
    "    li    t1, 1\n"
    "    sd    t1, 0(t0)\n"
    "    mret\n"
    ".size sbi_mprv_trap, .-sbi_mprv_trap\n"
    ".popsection\n"
);

static unsigned long sbi_hartid(void)
{
    unsigned long id;
    __asm__ volatile("csrr %0, mhartid" : "=r"(id));
    return id;
}

static int sbi_mprv_lb(unsigned long a, uint8_t *v)
{
    unsigned long mtvec_save, mstatus_save, tmp, val;
    unsigned long hart = sbi_hartid();

    sbi_mprv_fault[hart] = 0;
    __asm__ volatile(
        "la    %[tmp], sbi_mprv_trap\n"
        "csrrw %[mtvec], mtvec, %[tmp]\n"
        "csrrs %[mstatus], mstatus, %[mprv]\n"
        ".option push\n"
        ".option norvc\n"
        "lbu   %[val], 0(%[addr])\n"
        ".option pop\n"
        "csrw  mstatus, %[mstatus]\n"
        "csrw  mtvec, %[mtvec]\n"
        : [mtvec] "=&r"(mtvec_save), [mstatus] "=&r"(mstatus_save),
          [tmp] "=&r"(tmp), [val] "=&r"(val)
        : [addr] "r"(a), [mprv] "r"(MSTATUS_MPRV_BIT)
        : "memory", "t0", "t1");
    *v = (uint8_t)val;
    return (sbi_mprv_fault[hart] != 0UL) ? -1 : 0;
}

static int sbi_mprv_sb(unsigned long a, uint8_t v)
{
    unsigned long mtvec_save, mstatus_save, tmp;
    unsigned long hart = sbi_hartid();

    sbi_mprv_fault[hart] = 0;
    __asm__ volatile(
        "la    %[tmp], sbi_mprv_trap\n"
        "csrrw %[mtvec], mtvec, %[tmp]\n"
        "csrrs %[mstatus], mstatus, %[mprv]\n"
        ".option push\n"
        ".option norvc\n"
        "sb    %[val], 0(%[addr])\n"
        ".option pop\n"
        "csrw  mstatus, %[mstatus]\n"
        "csrw  mtvec, %[mtvec]\n"
        : [mtvec] "=&r"(mtvec_save), [mstatus] "=&r"(mstatus_save),
          [tmp] "=&r"(tmp)
        : [addr] "r"(a), [val] "r"((unsigned long)v),
          [mprv] "r"(MSTATUS_MPRV_BIT)
        : "memory", "t0", "t1");
    return (sbi_mprv_fault[hart] != 0UL) ? -1 : 0;
}

/* A caller's buffer must be bounded, and DDR with paging off: an M-mode access
 * to a device window would not fault the way a bad translation does. */
#if !defined(DDR_BASE_CACHED) || !defined(DDR_SIZE)
#error "sbi_smode_range_ok needs DDR_BASE_CACHED and DDR_SIZE"
#endif
int sbi_smode_range_ok(unsigned long addr, unsigned long len)
{
    unsigned long satp;

    if ((len == 0UL) || (len > SBI_SMODE_BUF_MAX) || (addr + len < addr)) {
        return 0;
    }
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    if ((satp >> 60) == 0UL) {
        if ((addr >= DDR_BASE_CACHED) &&
                (addr + len <= DDR_BASE_CACHED + 0x40000000UL)) {
            return 1;
        }
        if ((addr >= 0x1000000000UL) &&
                (addr + len <= 0x1000000000UL + DDR_SIZE)) {
            return 1;
        }
        return 0;
    }
    return 1;
}

int sbi_copy_from_smode(uint8_t *dst, unsigned long src, unsigned long len)
{
    unsigned long i;

    for (i = 0; i < len; i++) {
        if (sbi_mprv_lb(src + i, &dst[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int sbi_copy_to_smode(unsigned long dst, const uint8_t *src, unsigned long len)
{
    unsigned long i;

    for (i = 0; i < len; i++) {
        if (sbi_mprv_sb(dst + i, src[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Platform hook for the vendor extension; weak default declines. */
long __attribute__((weak)) sbi_hal_vendor_ecall(unsigned long fid,
    unsigned long *regs, unsigned long *val)
{
    (void)fid;
    (void)regs;
    (void)val;
    return SBI_ERR_NOT_SUPPORTED;
}

#define MSTATUS_MXR_BIT (1UL << 19)

/* Guest-context byte accessors: MPRV borrows the trapped context's
 * translation, and MXR additionally permits reading execute-only pages. */
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

/* Emulate a misaligned load or store from S/U mode byte-wise, as OpenSBI does.
 * Integer and common compressed forms only; AMO and FP return 0 (fatal). */
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

/* Emulate instructions that trap as illegal from S-mode: these harts have no
 * time CSR, so the kernel's rdtime arrives here. */
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

/* The OS owns the UART and may have stopped it, so never wait long on it with
 * M-mode interrupts masked.  Returns 1 when the byte was written. */
#define SBI_UART_SPIN 200000U

static int sbi_uart_ready(void)
{
    uint32_t n = SBI_UART_SPIN;

    while ((n > 0U) && ((SBI_UART_LSR & SBI_UART_THRE) == 0U)) {
        n--;
    }
    return (n > 0U) ? 1 : 0;
}

static int sbi_putc(char c)
{
    if (c == '\n') {
        if (!sbi_uart_ready()) {
            return 0;
        }
        SBI_UART_THR = (uint8_t)'\r';
    }
    if (!sbi_uart_ready()) {
        return 0;
    }
    SBI_UART_THR = (uint8_t)c;
    return 1;
}

/* Park this hart in M-mode for good. */
static void __attribute__((noreturn)) sbi_system_halt(void)
{
    csr_clr_bits(mie, MIE_MTIP);
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* Set a hart's S-mode timer: program CLINT mtimecmp, drop any injected
 * S-timer interrupt and re-enable the M-timer. */
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

/* IPI on a running hart: clear the MSIP, run any requested fences, inject SSIP
 * for an OS IPI.  A bare MSIP with no op flags counts as an OS IPI. */
void sbi_ipi_irq(unsigned long hartid)
{
    uint32_t ops = 0;
    uint32_t posted = 0;

    CLINT_MSIP(hartid) = 0U;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        /* Read the ticket counter before consuming the ops: every request
         * posted under a ticket acknowledged below is then already in it. */
        posted = __atomic_load_n(&sbi_ipi_posted[hartid], __ATOMIC_ACQUIRE);
        /* amoswap.w: a remote hart may be OR-ing a new op in concurrently, so
         * the read and clear must be one operation or a post is lost. */
        ops = __atomic_exchange_n(&sbi_ipi_ops[hartid], 0U, __ATOMIC_ACQ_REL);
    }
    if ((ops & SBI_IPI_OP_HALT) != 0U) {
        sbi_system_halt();
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
    /* Publish completion only after the requested fences have executed. */
    if (hartid < (unsigned long)MPFS_NUM_HARTS) {
        __atomic_store_n(&sbi_ipi_done[hartid], posted, __ATOMIC_RELEASE);
    }
}

/* Post an op to every hart in (mask << base) and ring its MSIP.  A self-IPI
 * raises our own SSIP; a self-RFENCE is skipped, already done locally. */
static void sbi_post_ipi(unsigned long mask, unsigned long base,
    uint32_t op, unsigned long self, uint32_t *ticket)
{
    unsigned long i;
    unsigned long h;

    for (i = 0; i < (unsigned long)MPFS_NUM_HARTS; i++) {
        ticket[i] = 0;
    }
    /* SBI v0.2: base == -1 selects all harts and the mask is ignored.
     * Normalize it, or the loop computes h = (-1)+i and skips every target. */
    if (base == (unsigned long)-1) {
        base = 0;
        mask = (1UL << MPFS_NUM_HARTS) - 1UL;
    }
    for (i = 0; i < (unsigned long)MPFS_NUM_HARTS; i++) {
        if ((mask & (1UL << i)) == 0UL) {
            continue;
        }
        h = base + i;
        if (h >= (unsigned long)MPFS_NUM_HARTS) {
            continue;
        }
        if (h == self) {
            if ((op & SBI_IPI_OP_SSIP) != 0U) {
                csr_set_bits(mip, MIP_SSIP);
            }
            continue;
        }
        if (sbi_hart_state[h] != SBI_HSM_STARTED) {
            continue; /* parked harts consume MSIP in their wake loop */
        }
        /* amoor.w: two harts may post to one target while it consumes, and a
         * plain |= read-modify-write could drop an op. */
        (void)__atomic_fetch_or(&sbi_ipi_ops[h], op, __ATOMIC_ACQ_REL);
        /* Take the ticket after the op is in: the target acknowledges a
         * ticket only with an ops word that already held this request. */
        if ((op & (SBI_IPI_OP_FENCE_I | SBI_IPI_OP_SFENCE)) != 0U) {
            ticket[h] = __atomic_add_fetch(&sbi_ipi_posted[h], 1U,
                                           __ATOMIC_ACQ_REL);
            if (ticket[h] == 0U) {  /* 0 means "nothing posted" below */
                ticket[h] = __atomic_add_fetch(&sbi_ipi_posted[h], 1U,
                                               __ATOMIC_ACQ_REL);
            }
        }
        __asm__ volatile("fence rw, rw" ::: "memory");
        CLINT_MSIP(h) = 1U;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* Shutdown has nowhere to go: park every started hart.  The stopped ones still
 * take timer interrupts and would keep the stall detectors running. */
static void __attribute__((noreturn)) sbi_system_shutdown(unsigned long self)
{
    uint32_t ticket[MPFS_NUM_HARTS];

    sbi_post_ipi((1UL << MPFS_NUM_HARTS) - 1UL, 0, SBI_IPI_OP_HALT, self,
                 ticket);
    sbi_system_halt();
}

/* Wait, bounded, for every posted fence ticket to be reached.  The SBI calls
 * are synchronous; the bound stops a wedged target wedging the caller. */
static long sbi_wait_ipi_done(unsigned long mask, unsigned long base,
    unsigned long self, const uint32_t *ticket)
{
    unsigned long i;
    unsigned long h;
    uint64_t deadline;
    long err = SBI_SUCCESS;
    /* SBI v0.2: hart_mask_base == -1 selects all harts (see sbi_post_ipi). */
    if (base == (unsigned long)-1) {
        base = 0;
        mask = (1UL << MPFS_NUM_HARTS) - 1UL;
    }
    for (i = 0; i < (unsigned long)MPFS_NUM_HARTS; i++) {
        if ((mask & (1UL << i)) == 0UL) {
            continue;
        }
        h = base + i;
        if (h == self || h >= (unsigned long)MPFS_NUM_HARTS ||
            ticket[h] == 0U) {
            continue;
        }
        /* 1 s of CLINT time: giving up early leaves stale TLBs.  Interrupts
         * are masked, so service fences posted to us or two harts deadlock. */
        deadline = CLINT_MTIME + SBI_IPI_WAIT_TICKS;
        while ((int32_t)(__atomic_load_n(&sbi_ipi_done[h], __ATOMIC_ACQUIRE) -
                ticket[h]) < 0 && (int64_t)(CLINT_MTIME - deadline) < 0) {
            if (sbi_ipi_ops[self] != 0U) {
                sbi_ipi_irq(self);
            }
        }
        if ((int32_t)(__atomic_load_n(&sbi_ipi_done[h], __ATOMIC_ACQUIRE) -
                ticket[h]) < 0) {
            err = SBI_ERR_FAILED;
        }
    }
    return err;
}

/* Deliver an M-mode trap from S/U to the supervisor's vector as delegation
 * would: sepc/scause/stval/sstatus describe it and mret lands on stvec. */
unsigned long sbi_redirect_trap(unsigned long cause, unsigned long epc,
    unsigned long tval)
{
    unsigned long mpp = (csr_read(mstatus) & MSTATUS_MPP_MASK) >>
        MSTATUS_MPP_SHIFT;
    unsigned long stvec = csr_read(stvec) & ~3UL;
    unsigned long sstatus;

    if ((mpp == (unsigned long)PRV_M) || (stvec == 0UL)) {
        return 0UL;
    }
    csr_write(sepc, epc);
    csr_write(scause, cause);
    csr_write(stval, tval);
    sstatus = csr_read(sstatus) & ~(MSTATUS_SPP | MSTATUS_SPIE);
    if (mpp == (unsigned long)PRV_S) {
        sstatus |= MSTATUS_SPP;
    }
    if ((sstatus & MSTATUS_SIE) != 0UL) {
        sstatus |= MSTATUS_SPIE;
    }
    sstatus &= ~MSTATUS_SIE;
    csr_write(sstatus, sstatus);
    csr_clr_bits(mstatus, MSTATUS_MPP_MASK);
    csr_set_bits(mstatus, MSTATUS_MPP_S);
    return stvec;
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
    uint32_t ticket[MPFS_NUM_HARTS];
#ifdef DEBUG_SBI
    volatile uint32_t spin;  /* UART-drain delay before SRST/SHUTDOWN reset */
    static uint32_t sbi_dbg_calls = 0;
#endif

    __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));

#ifdef DEBUG_SBI
    /* Trace the first few ecalls to tell a silent kernel from a broken console.
     * Console and timer are exempt: a traced tick serializes on the UART. */
    if (sbi_dbg_calls < 40U && eid != SBI_EXT_0_1_CONSOLE_PUTCHAR &&
        eid != SBI_EXT_DBCN && eid != SBI_EXT_TIME &&
        eid != SBI_EXT_0_1_SET_TIMER) {
        sbi_dbg_calls++;
        wolfBoot_printf("[SBI#%u h%lu] eid=0x%lx fid=%lu a0=0x%lx\n",
            (unsigned)sbi_dbg_calls, hartid, eid, fid, regs[A0]);
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
#ifdef MPFS_SBI_VENDOR
            case SBI_EXT_VENDOR_MCHP:
#endif
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
            sbi_post_ipi(regs[A0], regs[A1], SBI_IPI_OP_SSIP, hartid, ticket);
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
        }
        break;

    case SBI_EXT_RFENCE: {
        /* remote_fence_i / remote_sfence_vma / ..._asid: over-flush with full
         * fences, which the spec permits, and wait for the targets. */
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
        sbi_post_ipi(regs[A0], regs[A1], op, hartid, ticket);
        err = sbi_wait_ipi_done(regs[A0], regs[A1], hartid, ticket);
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
            else {
                /* Publish START_PENDING before the MSIP: the woken target
                 * marks itself STARTED, which a later write would clobber. */
                sbi_hart_state[regs[A0]] = SBI_HSM_START_PENDING;
                if (sbi_hal_hart_start(regs[A0], regs[A1],
                                       regs[A2]) != 0) {
                    sbi_hart_state[regs[A0]] = SBI_HSM_STOPPED;
                    err = SBI_ERR_FAILED;
                }
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
            /* Bound one call; the kernel loops on val.  Interrupts are off, so
             * a long write holds up fence IPIs: 256 B is ~25 ms at 115200. */
            if (n > 256UL) {
                n = 256UL;
            }
            val = 0;
            for (k = 0; k < n; k += c) {
                c = n - k;
                if (c > sizeof(cbuf)) {
                    c = sizeof(cbuf);
                }
                if (sbi_copy_from_smode(cbuf, gp + k, c) != 0) {
                    err = SBI_ERR_INVALID_ADDRESS;
                    break;
                }
                for (j = 0; j < c; j++) {
                    if (!sbi_putc((char)cbuf[j])) {
                        k = n;
                        break;
                    }
                    val++;
                }
            }
            break;
        }
        case 2: /* console_write_byte(byte) */
            (void)sbi_putc((char)regs[A0]);
            break;
        case 1: /* console_read -- no input wired yet */
            val = 0;
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

#ifdef MPFS_SBI_VENDOR
    case SBI_EXT_VENDOR_MCHP:
        err = sbi_hal_vendor_ecall(fid, regs, &val);
        break;
#endif

    case SBI_EXT_SRST:
        /* system_reset(type, reason).  No console output: the OS owns the UARTs
         * and a blocked printf strands the hart with the reset never issued. */
#ifdef DEBUG_SBI
        wolfBoot_printf("[SBI] SYSTEM RESET requested: type=0x%lx "
            "reason=0x%lx\n", regs[A0], regs[A1]);
        for (spin = 0; spin < 20000000UL; spin++) { }
#endif
        if (regs[A0] == 0UL) {
            sbi_system_shutdown(hartid);
        }
        if (regs[A0] > 2UL) {
            err = SBI_ERR_INVALID_PARAM;
            break;
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
        (void)sbi_putc((char)regs[A0]);
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
            (void)sbi_copy_from_smode((uint8_t *)&lmask, regs[A0],
                sizeof(unsigned long));
            sbi_post_ipi(lmask, 0, SBI_IPI_OP_SSIP, hartid, ticket);
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
            (void)sbi_copy_from_smode((uint8_t *)&fmask, regs[A0],
                sizeof(unsigned long));
            sbi_post_ipi(fmask, 0,
                (eid == SBI_EXT_0_1_REMOTE_FENCE_I) ?
                    SBI_IPI_OP_FENCE_I : SBI_IPI_OP_SFENCE, hartid, ticket);
            err = sbi_wait_ipi_done(fmask, 0, hartid, ticket);
        }
        regs[A0] = (unsigned long)err;
        return epc + 4;
    case SBI_EXT_0_1_SHUTDOWN:
#ifdef DEBUG_SBI
        wolfBoot_printf("[SBI] legacy SHUTDOWN requested\n");
        for (spin = 0; spin < 20000000UL; spin++) { }
#endif
        sbi_system_shutdown(hartid);

    default:
        err = SBI_ERR_NOT_SUPPORTED;
        break;
    }

    regs[A0] = (unsigned long)err;
    regs[A1] = val;
    return epc + 4;
}

#endif /* WOLFBOOT_RISCV_MMODE && WOLFBOOT_MMODE_SMODE_BOOT */
