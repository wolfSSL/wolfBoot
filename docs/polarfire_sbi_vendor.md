# PolarFire SoC: wolfBoot SBI vendor extension

The M-mode wolfBoot stays resident as the SBI for the OS. With `MPFS_SBI_VENDOR=1` it also serves the Microchip vendor extension, EID `0x09000029` (`SBI_EXT_VENDOR_START | mvendorid 0x029`), the same number the Hart Software Services register, so an OS probes one extension for either firmware and tells them apart with `GET_VERSION`. The HSS function IDs for inter-hart communication, remoteproc, crypto and IPC are answered with `SBI_ERR_NOT_SUPPORTED`; `HSS_REBOOT` (0x10) performs a cold MSS reset.

The definitions live in `include/riscv_sbi.h`; the handler is `sbi_hal_vendor_ecall()` in `hal/mpfs250.c`. Arguments travel in `a0..a2`, the SBI error returns in `a0` and the value in `a1`. Buffers are addresses in the caller's address space: M-mode reads and writes them through the caller's translation, byte by byte, with a trap vector that turns a bad address into `SBI_ERR_INVALID_ADDRESS` instead of a stopped hart. A buffer is at most 4096 bytes and, while the caller runs with paging off, must lie in DDR.

| FID | Call | Arguments | Result |
|-----|------|-----------|--------|
| 0x40 | `GET_VERSION` | - | `a1` = 1 |
| 0x41 | `GET_SERIAL` | `a0` buffer, `a1` length >= 16 | the 16-byte device serial number |
| 0x42 | `GET_KEY` | `a0` purpose, `a1` buffer, `a2` length == 32 | a PUF-derived key; each purpose is served once per boot, a second call returns `SBI_ERR_DENIED` |
| 0x43 | `DIGEST_CHECK` | `a0` options word | starts the System Controller digest check |
| 0x44 | `IAP_AUTHENTICATE` | `a0` SPI directory index | starts IAP image authentication |
| 0x45 | `IAP_VERIFY` | `a0` SPI directory index | starts IAP verify |
| 0x46 | `IAP_PROGRAM` | `a0` SPI directory index | the device restarts on success; a return is the failure status in `a1` |
| 0x47 | `AUTO_UPDATE` | - | the device restarts on success; a return is the failure status in `a1` |
| 0x48 | `SC_FLASH_ERASE` | `a0` flash offset, `a1` length == one erase block (64 KB), both block aligned | erases that block of the System Controller's SPI flash; `MPFS_SBI_SC_FLASH=1` builds only |
| 0x49 | `SC_FLASH_WRITE` | `a0` flash offset, `a1` buffer, `a2` length <= 4096 | programs the bytes; `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4A | `SC_FLASH_READ` | `a0` flash offset, `a1` buffer, `a2` length <= 4096 | reads the bytes (any offset, the directory included); `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4B | `SC_FLASH_SET_IMAGE` | `a0` directory index 1 (upgrade) or 2 (blank), `a1` image offset | rewrites the SPI directory block with that entry changed (entry 1 also zeroes entry 2); `MPFS_SBI_SC_FLASH=1` builds only |
| 0x4F | `SC_STATUS` | - | `SBI_ERR_ALREADY_STARTED` while the started service runs, else `a1` = 16-bit status with mailbox word 1 (the digest-check `DIGESTERR` bits) in the upper half |

`GET_KEY` purposes name a fixed label; the caller never supplies a challenge, so the OS cannot ask the PUF for wolfBoot's own key-encryption key. Purpose 1 is the MRAM log key (`SHA-384("wolfBoot-mram-log-v1" || PUF(SHA-384("wolfBoot-mram-log-v1")[0..15]))[0..31]`); purpose 2 reproduces the earlier user-space `varlog` derivation. The key is device-bound: the same whether the golden or an operational image booted.

The System Controller services (0x43-0x4F) are served only in a build with `MPFS_SCB_SMODE_DENY=1`, where the OS cannot reach the mailbox itself; in an unfenced build they return `SBI_ERR_DENIED` and the OS uses its own mailbox drivers, which the fence would otherwise race. The long-running ones are started inside the ecall and polled through `SC_STATUS`, because an ecall holds its hart in M-mode with interrupts off. The mailbox is one resource for all harts: a call that finds it taken returns `SBI_ERR_FAILED`.

The fence also covers the System Controller's QSPI flash controller, so a fenced OS cannot stage a bitstream the way the mainline `mpfs-auto-update` driver does (through `mtd` on that controller). `MPFS_SBI_SC_FLASH=1` adds the flash calls (0x48-0x4B) for that build: the OS erases and writes the image region directly, reads back what it wants verified, and points the SPI directory's upgrade entry at the image with `SC_FLASH_SET_IMAGE` before `IAP_AUTHENTICATE` / `IAP_PROGRAM` or `AUTO_UPDATE`. The first erase block (the directory and the start of the design-info area) is never erased or written through `SC_FLASH_ERASE` / `SC_FLASH_WRITE`; `SET_IMAGE` reads it, changes the one entry (the golden entry 0 stays what it was) and writes it back, the same read-erase-write the OS driver performs. Each erase call takes one block and each transfer at most 4 KB, so no ecall holds its hart for more than the erase time of one block; the OS side loops. The flash layout the mainline driver uses (directory in the first 1 KB, design info at 1 KB, bitstream slots from 1 MB + 1 KB) is the OS's business; wolfBoot only protects the directory block.

The Linux side is the `wolfboot-sbi` module in `tools/linux/wolfboot-sbi/`: `/dev/wolfboot` (root only) with one ioctl per call, see `wolfboot_sbi.h`. It probes the extension and `GET_VERSION`, so it unloads cleanly under the HSS, and it never touches the mailbox registers, so the mainline `mpfs-sys-controller`, `mpfs-auto-update` and `mpfs-rng` drivers keep working in an unfenced build. A caller of `IAP_PROGRAM` or `AUTO_UPDATE` syncs its filesystems first: the device restarts inside the call.

Build: `MPFS_SBI_VENDOR=1` on the M-mode target (`RISCV_MMODE=1`); the key derivation is SHA-384 whatever the image hash.
