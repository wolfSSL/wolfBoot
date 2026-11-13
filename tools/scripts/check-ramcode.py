#!/usr/bin/env python3
# check-ramcode.py: fail when code reachable from an entry function, or data
# it reads, is outside RAM, such as a RAM_CODE self-update calling into or
# reading from the flash it erases.
#
# usage: check-ramcode.py <elf> <entry> <ram_start> <ram_end>
#        check-ramcode.py --selftest

import bisect
import os
import re
import subprocess
import sys
import tempfile


def tool(*args):
    return subprocess.run(list(args), capture_output=True, text=True,
                          check=True).stdout


def check(elf, entry, ram_lo, ram_hi):
    # RAM_CODE puts code and data in one section, so nm cannot tell them apart
    objects = set()
    marks = []
    for line in tool("arm-none-eabi-readelf", "-sW", elf).splitlines():
        f = line.split()
        if len(f) == 8 and f[3] == "OBJECT":
            objects.add(f[7])
        # Mapping symbols mark the literal pools inside functions
        elif len(f) == 8 and f[7][:2] in ("$d", "$t", "$a"):
            marks.append((int(f[1], 16), f[7][1]))
    marks.sort()
    syms = []
    objs = []
    for line in tool("arm-none-eabi-nm", "-S", "-n", elf).splitlines():
        f = line.split()
        if len(f) == 4 and f[3] in objects:
            objs.append((int(f[0], 16), int(f[1], 16), "data " + f[3]))
        # Assembly functions without .size print no size field
        elif len(f) == 4 and f[2] in "TtWw":
            syms.append([int(f[0], 16) & ~1, int(f[1], 16), f[3]])
        elif len(f) == 3 and f[1] in "TtWw":
            syms.append([int(f[0], 16) & ~1, 0, f[2]])
    syms.sort()
    objs.sort()
    sections = []
    image = []
    sec_row = re.compile(r"^\s*\[\s*\d+\]\s+\S+\s+(\S+)\s+([0-9a-f]+)\s+"
                         r"[0-9a-f]+\s+([0-9a-f]+)")
    for line in tool("arm-none-eabi-readelf", "-SW", elf).splitlines():
        m = sec_row.match(line)
        if m and int(m.group(2), 16) != 0:
            sec = (int(m.group(2), 16), int(m.group(2), 16) +
                   int(m.group(3), 16))
            sections.append(sec)
            if m.group(1) != "NOBITS" and not ram_lo <= sec[0] < ram_hi:
                image.append(sec)

    def section_end(addr):
        return min([e for b, e in sections if b <= addr < e] or [addr + 2])

    # An unsized function ends at the next symbol, within its own section
    for i, f in enumerate(syms):
        if f[1] == 0:
            end = syms[i + 1][0] if i + 1 < len(syms) else f[0] + 2
            f[1] = max(min(end, section_end(f[0])) - f[0], 2)
    funcs = [tuple(f) for f in syms]
    starts = [f[0] for f in funcs]
    obj_starts = [o[0] for o in objs]
    names = {f[2]: f for f in funcs}
    places = dict(names)
    places.update((o[2], o) for o in objs)
    # Section contents, for the words of data objects
    mem = {}
    row = re.compile(r"^ ([0-9a-f]+) ((?:[0-9a-f]{2,8} ?){1,4})")
    alloc = True
    for line in tool("arm-none-eabi-objdump", "-s", elf).splitlines():
        if line.startswith("Contents of section "):
            alloc = not line.split()[3].startswith((".debug", ".comment",
                                                    ".ARM.attributes"))
            continue
        m = row.match(line)
        if m and alloc:
            data = bytes.fromhex(m.group(2).replace(" ", ""))
            base = int(m.group(1), 16)
            for i, byte in enumerate(data):
                mem[base + i] = byte
    # The self-update writes the new image at the image base
    image_base = min([a for a in mem if not ram_lo <= a < ram_hi] or [-1])
    if entry not in names:
        print("%s: no function %s" % (elf, entry))
        return 1

    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and addr < funcs[i][0] + max(funcs[i][1], 2):
            return funcs[i]
        return None

    def obj_owner(addr):
        i = bisect.bisect_right(obj_starts, addr) - 1
        if i >= 0 and addr < objs[i][0] + objs[i][1]:
            return objs[i]
        return None

    edges = {}

    # A word is a Thumb function pointer, a pointer to data, or a constant;
    # data in the flash image counts even without a symbol
    def link(src, value, branch=False):
        dst = owner(value & ~1) if value & 1 else None
        if dst is None or dst[0] != value & ~1:
            dst = None
            # The entry's own image base literal is its write target
            if not branch and (value != image_base or src[2] != entry):
                dst = obj_owner(value)
                if dst is None and any(b <= value < e for b, e in image):
                    dst = (value, 1, "data at 0x%08x" % value)
                    places[dst[2]] = dst
        if dst is not None and dst != src:
            edges.setdefault(src[2], set()).add(dst[2])

    def word(addr):
        if all(a in mem for a in range(addr, addr + 4)):
            return int.from_bytes(bytes(mem[a] for a in range(addr, addr + 4)),
                                  "little")
        return None

    # Literal words, read from the bytes: objdump may decode a pool that
    # starts on a halfword boundary at the wrong offset
    pools = []
    for i, (addr, kind) in enumerate(marks):
        func = owner(addr)
        if kind != "d" or func is None:
            continue
        end = func[0] + func[1]
        if i + 1 < len(marks):
            end = min(end, marks[i + 1][0])
        pools.append((addr, end))
        for at in range((addr + 3) & ~3, end - 3, 4):
            value = word(at)
            if value is not None:
                link(func, value)
    pool_starts = [p[0] for p in pools]

    def in_pool(addr):
        i = bisect.bisect_right(pool_starts, addr) - 1
        return i >= 0 and addr < pools[i][1]

    insn = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8} )+\s*(\S*)\s*(.*)$")
    pc_load = re.compile(r"(r\d+), \[pc, #-?\d+\]\s*@ \(([0-9a-f]+)")
    imm = r"#(-?(?:0x[0-9a-f]+|\d+))"
    # Register constants built in code count like literals. The entry may not
    # read through the image base; callees get it as their write destination,
    # and a flash driver may read that back to skip unchanged rows
    regs = {}
    func = None
    base_read = False
    for line in tool("arm-none-eabi-objdump", "-D", elf).splitlines():
        m = insn.match(line)
        if not m:
            continue
        here = int(m.group(1), 16)
        src = owner(here)
        if src is None or in_pool(here):
            continue
        if src is not func:
            func, regs = src, {}
        op, args = m.group(2), m.group(3)
        t = re.match(r"([0-9a-f]+) <", args)
        if op.startswith("b") and t:
            link(src, int(t.group(1), 16) | 1, branch=True)
        if re.fullmatch(r"blx?(?:\.[a-z0-9]+)?", op):
            for r in ("r0", "r1", "r2", "r3", "r12"):
                regs.pop(r, None)
            continue
        if op.startswith(("ldr", "ldm")):
            base = re.search(r"\[(r\d+)[,\]]", args) if op.startswith("ldr") \
                else re.match(r"(r\d+)", args)
            if (src[2] == entry) and base and \
                    (regs.get(base.group(1)) == image_base):
                base_read = True
        dst = re.match(r"(r\d+)(?:,|$)", args)
        if dst is None:
            continue
        reg = dst.group(1)
        value = None
        t = pc_load.match(args) if op.startswith("ldr") else None
        u = re.match(r"r\d+, " + imm + "$", args)
        v = re.match(r"r\d+, (r\d+), " + imm + "$", args) or \
            re.match(r"r\d+, " + imm + "$", args)
        w = re.match(r"r\d+, (r\d+)$", args)
        if t:
            value = word(int(t.group(2), 16))
        elif op.startswith(("mov", "cpy")) and w:
            value = regs.get(w.group(1))
        elif op.startswith(("mov", "movw")) and not op.startswith("movt") and u:
            value = int(u.group(1), 0) & 0xFFFFFFFF
        elif op.startswith("movt") and u and reg in regs:
            value = (regs[reg] & 0xFFFF) | ((int(u.group(1), 0) & 0xFFFF) << 16)
        elif op.startswith(("lsl", "add", "sub")) and v:
            srcreg = v.group(1) if v.re.groups == 2 else reg
            amount = int(v.group(v.re.groups), 0)
            if srcreg in regs:
                if op.startswith("lsl"):
                    value = (regs[srcreg] << amount) & 0xFFFFFFFF
                elif op.startswith("add"):
                    value = (regs[srcreg] + amount) & 0xFFFFFFFF
                else:
                    value = (regs[srcreg] - amount) & 0xFFFFFFFF
        if value is None:
            regs.pop(reg, None)
            continue
        regs[reg] = value
        # A pool placed after the function's .size has no owner to scan it
        link(src, value)

    # Data objects may hold callbacks or further pointers
    for obj in objs:
        for addr in range(obj[0], obj[0] + obj[1] - 3, 4):
            value = word(addr)
            if value is not None:
                link(obj, value)

    seen, todo, bad = set(), [entry], []
    if base_read:
        bad.append("0x%08x read of the image base by %s" % (image_base, entry))
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        if not ram_lo <= places[name][0] < ram_hi:
            bad.append("0x%08x %s" % (places[name][0], name))
        todo.extend(edges.get(name, ()))
    if bad:
        print("%s: reachable from %s but outside RAM:" % (elf, entry))
        print("\n".join(sorted(bad)))
        return 1
    print("%s: %d functions reachable from %s, all in RAM" % (
        elf, len([n for n in seen if n in names]), entry))
    return 0


SELFTEST_SRC = """
    .syntax unified
    .thumb
    .section .flashbase, "a"
    .type vectors, %%object
vectors:
    .word flash_helper
    .size vectors, . - vectors
    .section .flashdata, "a"
    .byte 0
    .type bytes, %%object
bytes:
    .byte 1, 2, 3
    .size bytes, . - bytes
.Lflash_string:
    .asciz "flash"
    .section .ramdata, "aw"
.Lram_string:
    .asciz "ram"
    .section .ramtext, "ax"
    .global entry
    .thumb_func
entry:
    bl ram_helper
    bl %(callee)s
    ldr r0, =callbacks
    ldr r0, [r0]
    blx r0
    ldr r1, =vectors
    ldr r2, =%(read)s
    ldrb r2, [r2]
    movs r3, #1
    lsls r3, r3, #28
    %(extra)s
    bx lr
    .ltorg
    .size entry, . - entry
    .thumb_func
ram_helper:
    bx lr
    .thumb_func
ram_base_reader:
    ldr r0, =vectors
    bx lr
    .ltorg
    .thumb_func
ram_built_reader:
    movs r0, #1
    lsls r0, r0, #28
    ldr r0, [r0]
    bx lr
    .thumb_func
late_call:
    ldr r3, =%(late)s
    blx r3
    bx lr
    .size late_call, . - late_call
    .ltorg
    .thumb_func
late_read:
    ldr r3, =.Lflash_string
    ldrb r3, [r3]
    bx lr
    .size late_read, . - late_read
    .ltorg
    .section .flashtext, "ax"
    .thumb_func
flash_helper:
    bx lr
    .section %(table)s
    .type callbacks, %%object
callbacks:
    .word %(callback)s
    .size callbacks, . - callbacks
"""


def selftest():
    # Unsized assembly helpers in RAM or flash, called directly or through a
    # callback table, and data read from RAM or flash, with or without a
    # symbol; the entry may only write at the image base, not read it
    ram_data = '.ramdata, "aw"'
    ram_code = '.ramtext, "ax"'
    flash_data = '.flashdata, "a"'
    ram, flash = "ram_helper", "flash_helper"
    cases = ((ram, ram_data, ram, ".Lram_string", 0),
             (flash, ram_data, ram, ".Lram_string", 1),
             (ram, ram_data, flash, ".Lram_string", 1),
             (ram, ram_code, ram, ".Lram_string", 0),
             (ram, ram_code, flash, ".Lram_string", 1),
             (ram, flash_data, ram, ".Lram_string", 1),
             (ram, ram_data, ram, ".Lflash_string", 1),
             (ram, ram_data, ram, "bytes", 1),
             (ram, ram_data, ram, "bytes + 2", 1),
             (ram, ram_data, ram, "vectors", 1),
             ("ram_base_reader", ram_data, ram, ".Lram_string", 1),
             ("ram_built_reader", ram_data, ram, ".Lram_string", 1),
             (ram, ram_data, ram, ".Lram_string", 1, "ldr r3, [r3]"),
             (ram, ram_data, ram, ".Lram_string", 1,
              "mov r2, r1\n    ldr r0, [r2]"),
             (ram, ram_data, ram, ".Lram_string", 1,
              "movs r2, r3\n    ldr r0, [r2]"),
             (ram, ram_data, ram, ".Lram_string", 0, "bl late_call"),
             (ram, ram_data, ram, ".Lram_string", 1, "bl late_call", flash),
             (ram, ram_data, ram, ".Lram_string", 1, "bl late_read"),
             # a flash-base register survives a conditional branch, not a call
             (ram, ram_data, ram, ".Lram_string", 1,
              "bls 1f\n1:\n    ldr r0, [r3]"))
    with tempfile.TemporaryDirectory() as tmp:
        for case in cases:
            callee, table, callback, read, expect = case[:5]
            extra = case[5] if len(case) > 5 else "nop"
            late = case[6] if len(case) > 6 else ram
            src = os.path.join(tmp, "t.S")
            elf = os.path.join(tmp, "t.elf")
            with open(src, "w") as out:
                out.write(SELFTEST_SRC % {"callee": callee, "table": table,
                                          "callback": callback, "read": read,
                                          "extra": extra, "late": late})
            subprocess.run(["arm-none-eabi-gcc", "-mcpu=cortex-m0plus",
                            "-mthumb", "-nostdlib", "-Wl,-e,entry",
                            "-Wl,--section-start=.ramtext=0x08000000",
                            "-Wl,--section-start=.ramdata=0x08001000",
                            "-Wl,--section-start=.flashbase=0x10000000",
                            "-Wl,--section-start=.flashtext=0x10000100",
                            "-Wl,--section-start=.flashdata=0x10001000",
                            "-o", elf, src], check=True)
            if check(elf, "entry", 0x08000000, 0x08060000) != expect:
                print("selftest: wrong result calling %s and %s from %s, "
                      "reading %s, then %s (late pool %s)" % (callee,
                                                              callback, table,
                                                              read, extra,
                                                              late))
                return 1
    print("selftest: ok")
    return 0


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    if len(sys.argv) != 5:
        sys.exit("usage: check-ramcode.py <elf> <entry> <ram_start> <ram_end>")
    sys.exit(check(sys.argv[1], sys.argv[2], int(sys.argv[3], 0),
                   int(sys.argv[4], 0)))


if __name__ == "__main__":
    main()
