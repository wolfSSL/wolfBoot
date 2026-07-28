/* disk_app.S - minimal CM4 disk-boot test payload.
 *
 * Prints a banner over the BCM2711 mini-UART (AUX @ 0xFE215040 - the console
 * wired to GPIO14/15 on the CM4, i.e. Linux ttyS0; NOT the PL011) and spins.
 * Linked at 0x10000000 (WOLFBOOT_LOAD_ADDRESS for cm4_emmc.config). Signed and
 * written raw into the eMMC A/B image partitions to prove wolfBoot's disk-boot
 * path (read from eMMC -> verify -> ELF load -> handoff).
 *
 * Build:  aarch64-none-elf-gcc -nostdlib -nostartfiles \
 *             -Wl,-Ttext=0x10000000 -o disk_app.elf disk_app.S
 *
 * Copyright (C) 2026 wolfSSL Inc.  GPLv3 (see wolfBoot COPYING).
 */
.section .text
.global _start
_start:
    movz    x1, #0xFE21, lsl #16
    movk    x1, #0x5000            /* x1 = 0xFE215000 (AUX mini-UART base) */
    adr     x0, msg
1:  ldrb    w2, [x0], #1
    cbz     w2, 2f
3:  ldr     w3, [x1, #0x54]        /* AUX_MU_LSR: bit5 = TX can accept */
    tbz     w3, #5, 3b
    str     w2, [x1, #0x40]        /* AUX_MU_IO (data) */
    b       1b
2:  wfi
    b       2b
.align 3
msg:
    .asciz "\r\n>>> CM4 DISK APP OK: wolfBoot read + verified + booted from eMMC <<<\r\n"
