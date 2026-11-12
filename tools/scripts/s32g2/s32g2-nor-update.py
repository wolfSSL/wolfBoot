#!/usr/bin/env python3
# s32g2-nor-update.py - stage and confirm a wolfBoot update in the S32G2
# QuadSPI NOR from Linux, through the MTD character device.
#
#   s32g2-nor-update.py [options] status
#   s32g2-nor-update.py [options] stage <signed image>
#   s32g2-nor-update.py [options] confirm
#
#   --dev <path>         MTD device, or a flash image file (/dev/mtd0)
#   --boot <offset>      BOOT partition (0x100000)
#   --update <offset>    UPDATE partition (0x1100000)
#   --part-size <bytes>  partition size (0x1000000)
#   --sector <bytes>     wolfBoot sector size (0x10000)
#
# The defaults match config/examples/s32g2-goldbox-qspi.config, which keeps
# the partition state as a journal (NVM_FLASH_JOURNAL=1): 16-byte records in
# the last sector of each partition, each programmed once. See docs/Targets.md
# and docs/compile.md. BOOT and UPDATE are the same size and not encrypted.
# An MTD device is erased with flash_erase (mtd-utils).
#
#   stage    erases the sectors the image needs and the log sector of UPDATE,
#            writes the image, then the update marker at the partition end.
#   confirm  appends a "success" record to the log of the partition that is
#            in the "testing" state, as wolfBoot_success() does.
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3.

import os
import stat
import struct
import subprocess
import sys
import zlib

SLOT = 16
TAG = 0x4A01
MAGIC = b"BOOT"
ST_NEW, ST_UPDATING, ST_TESTING, ST_SUCCESS = 0xFF, 0x70, 0x10, 0x00
STATES = {ST_NEW: "new", ST_UPDATING: "updating", ST_TESTING: "testing",
          ST_SUCCESS: "success"}


class Flash:
    def __init__(self, dev, boot, update, part_size, sector):
        self.dev = dev
        self.parts = (("BOOT", boot), ("UPDATE", update))
        self.part_size = part_size
        self.sector = sector
        # The magic, the state and one flag byte for each two sectors.
        self.trailer = (4 + 1 + (part_size // sector + 1) // 2 + 3) & ~3
        self.state_off = self.trailer - 5
        self.slots = (sector - self.trailer) // SLOT
        self.mark_slot = self.slots - 1

    def read(self, off, n):
        fd = os.open(self.dev, os.O_RDONLY)
        try:
            return os.pread(fd, n, off)
        finally:
            os.close(fd)

    def write(self, off, data):
        fd = os.open(self.dev, os.O_RDWR)
        try:
            if os.pwrite(fd, data, off) != len(data):
                sys.exit("short write at 0x%x" % off)
        finally:
            os.close(fd)
        if self.read(off, len(data)) != data:
            sys.exit("read back mismatch at 0x%x" % off)

    def erase(self, off, n):
        if stat.S_ISCHR(os.stat(self.dev).st_mode):
            # flash_erase counts in the device's erase blocks.
            with open("/sys/class/mtd/%s/erasesize" %
                      os.path.basename(os.path.realpath(self.dev))) as f:
                block = int(f.read())
            if off % block or n % block:
                sys.exit("%s erase block is 0x%x: not a divisor of the "
                         "sector size" % (self.dev, block))
            subprocess.check_call(["flash_erase", "-q", self.dev,
                                   "0x%x" % off, str(n // block)])
            return
        fd = os.open(self.dev, os.O_RDWR)
        try:
            os.pwrite(fd, b"\xff" * n, off)
        finally:
            os.close(fd)

    def log_base(self, base):
        return base + self.part_size - self.sector

    def version(self, base):
        h = self.read(base, 16)
        if h[:4] != b"WOLF" or h[8:12] != b"\x01\x00\x04\x00":
            return None
        return struct.unpack("<I", h[12:16])[0]

    def journal(self, base):
        """Replay the log sector the way wolfBoot does.

        Returns (state, slot of the last record or -1, next seq, notes);
        state is None when the partition has no trailer.
        """
        sec = self.read(self.log_base(base), self.sector)
        blank = b"\xff" * SLOT
        notes = []
        trailer = bytearray(b"\xff" * self.trailer)
        last, seq, junk, gap = -1, 0, 0, False

        if sec[-4:] == MAGIC:
            # A trailer in the in-place format, not yet rewritten as records.
            trailer[:] = sec[-self.trailer:]
            notes.append("in-place trailer")
            return trailer[self.state_off], None, 0, notes
        for s in range(self.mark_slot):
            r = sec[s * SLOT:(s + 1) * SLOT]
            if r == blank:
                continue
            tag, rseq, off, ln = struct.unpack("<HHHH", r[:8])
            crc = struct.unpack("<I", r[12:16])[0]
            if (tag != TAG or not 1 <= ln <= 4 or off + ln > self.trailer or
                    crc != zlib.crc32(r[:12]) & 0xFFFFFFFF):
                junk += 1
                continue
            if rseq != seq and rseq + 1 != seq:
                gap = True
                break
            trailer[off:off + ln] = r[8:8 + ln]
            if rseq == seq:
                seq += 1
            last = s
            junk = 0
            notes.append("slot %d: seq %d, offset %d, data %s" % (
                s, rseq, off, r[8:8 + ln].hex()))
        marked = sec[self.mark_slot * SLOT:(self.mark_slot + 1) * SLOT] != blank
        if marked or gap or (last < 0 and junk > 0):
            notes.append("log is not valid: wolfBoot reads it as erased")
            return None, -2, 0, notes
        if bytes(trailer[self.state_off + 1:]) != MAGIC:
            return None, last, seq, notes
        return trailer[self.state_off], last, seq, notes


def status(fl):
    for name, base in fl.parts:
        st, last, seq, notes = fl.journal(base)
        ver = fl.version(base)
        print("%s 0x%x: %s, state %s" % (
            name, base,
            "no image" if ver is None else "version %d" % ver,
            "none" if st is None else STATES.get(st, "0x%02x" % st)))
        for n in notes:
            print("    " + n)


def stage(fl, path):
    base = fl.parts[1][1]
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"WOLF":
        sys.exit("%s is not a signed wolfBoot image" % path)
    data += b"\xff" * (-len(data) % SLOT)
    if len(data) > fl.part_size - fl.sector:
        sys.exit("%s does not fit in the partition" % path)
    fl.erase(base, (len(data) + fl.sector - 1) // fl.sector * fl.sector)
    fl.erase(fl.log_base(base), fl.sector)
    for off in range(0, len(data), fl.sector):
        fl.write(base + off, data[off:off + fl.sector])
    # The marker shares its 16-byte unit with nothing else.
    fl.write(base + fl.part_size - SLOT,
             b"\xff" * (SLOT - 5) + bytes([ST_UPDATING]) + MAGIC)
    print("staged version %s, update marker set" % fl.version(base))


def confirm(fl):
    for name, base in fl.parts:
        st, last, seq, _ = fl.journal(base)
        if st != ST_TESTING:
            continue
        if last is None or last < 0:
            sys.exit("%s is testing but has no log to append to" % name)
        # wolfBoot leaves one slot free after the last record it finds.
        slot = last + 2
        while (slot < fl.mark_slot and fl.read(
                fl.log_base(base) + slot * SLOT, SLOT) != b"\xff" * SLOT):
            slot += 1
        if slot >= fl.mark_slot:
            sys.exit("%s log is full" % name)
        addr = fl.log_base(base) + slot * SLOT
        rec = struct.pack("<HHHH", TAG, seq, fl.state_off, 1)
        rec += bytes([ST_SUCCESS]) + b"\xff\xff\xff"
        rec += struct.pack("<I", zlib.crc32(rec) & 0xFFFFFFFF)
        fl.write(addr, rec)
        print("%s confirmed" % name)
        return 0
    print("no partition is in the testing state")
    return 1


def main(argv):
    opts = {"--dev": "/dev/mtd0", "--boot": "0x100000",
            "--update": "0x1100000", "--part-size": "0x1000000",
            "--sector": "0x10000"}
    args = argv[1:]
    while len(args) >= 2 and args[0] in opts:
        opts[args[0]] = args[1]
        args = args[2:]
    try:
        fl = Flash(opts["--dev"], int(opts["--boot"], 0),
                   int(opts["--update"], 0), int(opts["--part-size"], 0),
                   int(opts["--sector"], 0))
    except ValueError:
        args = []
    if args == ["status"]:
        status(fl)
    elif len(args) == 2 and args[0] == "stage":
        stage(fl, args[1])
    elif args == ["confirm"]:
        return confirm(fl)
    else:
        print("usage: %s [--dev <path>] [--boot <offset>] [--update <offset>]"
              " [--part-size <bytes>] [--sector <bytes>]"
              " status | stage <signed image> | confirm" % argv[0],
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
