#!/usr/bin/env python3
"""Check that no MSS watchdog refresh can land in the forbidden window.

A write to MSS_WDT_REFRESH while the counter is above MVRP is the forbidden
window, and the hardware answers it with a chip reset rather than an error.
Only two kinds of site may write it: the gate helpers, which test
MSS_WDT_STAT_FORBIDDEN first, and code that only ever runs in M-mode, where
wolfBoot programs MVRP itself so no forbidden window exists.

This has already been reintroduced in three separate places, so the rule is
checked rather than remembered.

  usage: check-mpfs-wdt-gate.py [hal dir]
"""

import re
import sys

GATES = ("mpfs_wdt_pet_all", "ubl_wdt_pet_all")
MMODE = "WOLFBOOT_RISCV_MMODE"

FUNC = re.compile(r"^(?:static\s+)?(?:void|int|long|unsigned\s+\w+)\s+"
                  r"(?:RAMFUNCTION\s+)?(\w+)\s*\(")


def mmode_guard(line):
    """True when this #if directive selects M-mode-only code."""
    return MMODE in line and not re.search(r"!\s*defined\(%s" % MMODE, line) \
        and not line.startswith("#ifndef %s" % MMODE)


def check(path):
    bad = []
    stack = []          # one entry per open #if: True when it selects M-mode
    func = "(file scope)"

    with open(path) as f:
        for n, line in enumerate(f, 1):
            s = line.strip()

            if s.startswith("#if"):
                stack.append(mmode_guard(s))
            elif s.startswith("#elif"):
                if stack:
                    stack[-1] = mmode_guard(s)
            elif s.startswith("#else"):
                if stack:
                    stack[-1] = not stack[-1] if stack[-1] else False
            elif s.startswith("#endif"):
                if stack:
                    stack.pop()

            m = FUNC.match(line)
            if m:
                func = m.group(1)

            if "MSS_WDT_REFRESH(" in line and not s.startswith("#define"):
                if func in GATES or any(stack):
                    continue
                bad.append("%s:%d: ungated MSS_WDT_REFRESH in %s()"
                           % (path, n, func))
    return bad


def main():
    hal = sys.argv[1] if len(sys.argv) > 1 else "hal"
    bad = []
    for name in ("mpfs250.c", "mpfs250_ubl.c"):
        bad += check("%s/%s" % (hal, name))

    if bad:
        for b in bad:
            print(b)
        print("A refresh outside a gate helper and outside M-mode resets the "
              "chip.")
        print("Call mpfs_wdt_pet_all(), or ubl_wdt_pet_all() in the UBL stub.")
        return 1

    print("mpfs wdt gate: every MSS_WDT_REFRESH is gated or M-mode only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
