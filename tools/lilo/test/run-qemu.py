#!/usr/bin/env python3
# run-qemu.py
#
# End-to-end QEMU test for the x86_64_lilo target: builds a legacy MBR disk
# with a FAT32 boot partition, installs wolfBoot on it with tools/lilo from a
# lilo.conf, and boots it under SeaBIOS through the "boot:" prompt.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
#
# Prerequisites: wolfboot.bin built from config/examples/x86_64_lilo.config,
# tools/keytools/sign, gcc, cpio, sfdisk, mkfs.fat and qemu-system-x86_64.
#
# Usage: run-qemu.py [-k bzImage] [-o workdir] [-a kvm|tcg]

import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PART1_START = 2048
PART1_SECTORS = 200000
PART2_SECTORS = 2048
DISK_MB = 128
CHAIN_TYPE = "da"

TAG = "WOLFBOOT-LILO-TEST:"
APPEND = "console=ttyS0,115200 panic=-1"


def tool(name):
    for d in (os.environ.get("PATH", "") + ":/sbin:/usr/sbin").split(":"):
        p = os.path.join(d, name)
        if d and os.access(p, os.X_OK):
            return p
    raise SystemExit("missing tool: " + name)


def run(cmd, **kw):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True, **kw)


def build_initrd(out):
    rootfs = os.path.join(out, "rootfs")
    shutil.rmtree(rootfs, ignore_errors=True)
    os.makedirs(rootfs)
    run(["gcc", "-static", "-nostdlib", "-ffreestanding", "-fno-stack-protector",
         "-fno-pie", "-no-pie", "-O2", "-o", os.path.join(rootfs, "init"),
         os.path.join(HERE, "init.c")])
    initrd = os.path.join(out, "initrd.img")
    with open(initrd, "wb") as f:
        subprocess.run([tool("cpio"), "--quiet", "-o", "-H", "newc"],
                       input=b"init\n", stdout=f, cwd=rootfs, check=True)
    return initrd


def build_chain(out):
    obj = os.path.join(out, "chain.o")
    binf = os.path.join(out, "chain.bin")
    run(["gcc", "-m32", "-c", "-o", obj, os.path.join(HERE, "chain.S")])
    run(["ld", "-m", "elf_i386", "-Ttext=0x7c00", "-e", "_start",
         "--oformat=binary", "-o", binf, obj])
    return binf


def sign(out, kernel):
    dst = os.path.join(out, "vmlinuz")
    shutil.copyfile(kernel, dst)
    run([os.path.join(ROOT, "tools", "keytools", "sign"), "--no-sign",
         "--sha256", dst, "1"])
    signed = os.path.join(out, "vmlinuz_v1_signed.bin")
    broken = os.path.join(out, "vmlinuz_broken.bin")
    data = bytearray(open(signed, "rb").read())
    data[256 + 0x10000] ^= 0xFF
    open(broken, "wb").write(data)
    return signed, broken


def build_disk(out, signed, broken, initrd, chain):
    disk = os.path.join(out, "disk.img")
    with open(disk, "wb") as f:
        f.truncate(DISK_MB * 1024 * 1024)
    layout = ("label: dos\n"
              "start=%d, size=%d, type=c, bootable\n"
              "size=%d, type=%s\n" % (PART1_START, PART1_SECTORS,
                                       PART2_SECTORS, CHAIN_TYPE))
    run([tool("sfdisk"), "-q", disk], input=layout.encode())
    run([tool("mkfs.fat"), "-F", "32", "-n", "WOLFBOOT",
         "--offset=%d" % PART1_START, disk, str(PART1_SECTORS // 2)],
        stdout=subprocess.DEVNULL)
    run([sys.executable, os.path.join(HERE, "fatput.py"), disk,
         str(PART1_START * 512),
         "/vmlinuz-wolfboot-v1.bin=" + signed,
         "/initrd-wolfboot.img=" + initrd,
         "/kernels/vmlinuz-rescue.bin=" + signed,
         "/vmlinuz-broken.bin=" + broken])
    with open(disk, "r+b") as f:
        f.seek(0x1BE + 16 + 8)
        part2 = struct.unpack("<I", f.read(4))[0]
        f.seek(part2 * 512)
        f.write(open(chain, "rb").read())
    return disk


def write_conf(out, disk):
    conf = os.path.join(out, "lilo.conf")
    with open(conf, "w") as f:
        f.write("""# wolfBoot x86_64_lilo QEMU test
boot=%s
install=%s
map=/boot/map
lba32
prompt
timeout=30
serial=0,115200n8
default=linux
read-only
append="%s"

image=/boot/vmlinuz-wolfboot-v1.bin
    label=linux
    initrd=/boot/initrd-wolfboot.img

image=/boot/kernels/vmlinuz-rescue.bin
    label=rescue
    alias=r
    initrd=/boot/initrd-wolfboot.img
    addappend="single"

image=/boot/vmlinuz-broken.bin
    label=broken
    initrd=/boot/initrd-wolfboot.img

image=/boot/vmlinuz-missing.bin
    label=missing
    optional

other=/dev/sda2
    label=chain
""" % (disk, os.path.join(ROOT, "wolfboot.bin"), APPEND))
    return conf


class Qemu:
    accel = None

    def __init__(self, disk, log):
        accel = Qemu.accel or ("kvm" if os.access("/dev/kvm", os.R_OK | os.W_OK)
                               else "tcg")
        cmd = [tool("qemu-system-x86_64"), "-accel", accel, "-m", "512",
               "-display", "none", "-serial", "stdio", "-monitor", "none",
               "-no-reboot", "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
               "-drive", "file=%s,format=raw,if=ide" % disk]
        print("+ " + " ".join(cmd))
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        self.buf = b""
        self.pos = 0
        self.log = open(log, "wb")
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            b = self.p.stdout.read1(4096)
            if not b:
                return
            with self.lock:
                self.buf += b
            self.log.write(b)
            self.log.flush()
            sys.stdout.write(b.decode("latin-1"))
            sys.stdout.flush()

    def expect(self, text, timeout=120):
        end = time.time() + timeout
        needle = text.encode()
        while time.time() < end:
            with self.lock:
                i = self.buf.find(needle, self.pos)
                if i >= 0:
                    self.pos = i + len(needle)
                    return
            if self.p.poll() is not None:
                time.sleep(0.2)
                with self.lock:
                    if self.buf.find(needle, self.pos) >= 0:
                        continue
                raise AssertionError("QEMU exited waiting for: " + text)
            time.sleep(0.05)
        raise AssertionError("timeout waiting for: " + text)

    def send(self, text):
        time.sleep(0.2)
        for ch in text.encode():
            self.p.stdin.write(bytes([ch]))
            self.p.stdin.flush()
            time.sleep(0.02)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()
        if not self.log.closed:
            self.log.close()

    def finish(self, timeout=60):
        try:
            return self.p.wait(timeout)
        finally:
            if self.p.poll() is None:
                self.p.kill()
            self.log.close()


def scenario_default(disk, out):
    with Qemu(disk, os.path.join(out, "default.log")) as q:
        q.expect("LILO")
        q.expect("boot: ")
        q.expect("Loading linux")
        q.expect("Verified /vmlinuz-wolfboot-v1.bin, version 1")
        q.expect("%s initrd running, cmdline: BOOT_IMAGE=linux ro %s\r\n" %
                 (TAG, APPEND))
        q.finish()


def scenario_typed(disk, out):
    with Qemu(disk, os.path.join(out, "typed.log")) as q:
        q.expect("boot: ")
        q.send("r  init_test=1\r")
        q.expect("Loading rescue")
        q.expect("%s initrd running, cmdline: BOOT_IMAGE=rescue ro %s single "
                 "init_test=1\r\n" % (TAG, APPEND))
        q.finish()


def scenario_recover(disk, out):
    with Qemu(disk, os.path.join(out, "recover.log")) as q:
        q.expect("boot: ")
        q.send("nosuch\r")
        q.expect("No such image. [Tab] shows a list.")
        q.expect("boot: ")
        q.send("\t")
        q.expect("linux*")
        q.expect("chain")
        q.expect("boot: ")
        q.send("broken\r")
        q.expect("integrity check failed")
        q.expect("boot: ")
        q.send("LINUX\r")
        q.expect("%s initrd running, cmdline: BOOT_IMAGE=linux ro" % TAG)
        q.finish()


def scenario_chain(disk, out):
    with Qemu(disk, os.path.join(out, "chain.log")) as q:
        q.expect("boot: ")
        q.send("chain\r")
        q.expect("%s CHAIN-OK" % TAG)
        rc = q.finish()
        if rc != 33:
            raise AssertionError("unexpected QEMU exit code %d" % rc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", "--kernel", help="x86_64 bzImage to boot")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "out"))
    ap.add_argument("-a", "--accel", help="QEMU accelerator (kvm, tcg)")
    args = ap.parse_args()
    Qemu.accel = args.accel

    kernel = args.kernel
    if kernel is None:
        found = sorted(glob.glob("/boot/vmlinuz-*"), key=os.path.getmtime)
        if not found:
            raise SystemExit("no kernel found, use -k")
        kernel = found[-1]
    if not os.path.exists(os.path.join(ROOT, "wolfboot.bin")):
        raise SystemExit("build wolfboot.bin first "
                         "(config/examples/x86_64_lilo.config)")
    os.makedirs(args.out, exist_ok=True)

    run(["make", "-C", os.path.join(ROOT, "tools", "lilo")])
    lilo = os.path.join(ROOT, "tools", "lilo", "lilo")
    initrd = build_initrd(args.out)
    chain = build_chain(args.out)
    signed, broken = sign(args.out, kernel)
    disk = build_disk(args.out, signed, broken, initrd, chain)
    conf = write_conf(args.out, disk)
    run([lilo, "-C", conf, "-v", "-s", os.path.join(args.out, "boot.save")])
    run([lilo, "-C", conf, "-q", "-v"])

    for name, fn in (("default boot after timeout", scenario_default),
                     ("label typed at the prompt", scenario_typed),
                     ("bad label and corrupt image", scenario_recover),
                     ("chain loading other=", scenario_chain)):
        print("\n=== %s ===" % name)
        fn(disk, args.out)
        print("\n=== PASS: %s ===" % name)
    print("\nAll x86_64_lilo QEMU tests passed")


if __name__ == "__main__":
    main()
