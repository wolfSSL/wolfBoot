#!/usr/bin/env python3
"""Generate a wolfBoot SBOM from a Clang compilation database (compile_commands.json).

This is the universal IDE/toolchain fallback.  Any build that can emit a
compilation database gives us an exact, ground-truth list of the files that were
compiled and the -D configuration they were compiled with - independent of the
build system or compiler.  That covers cases with no Make/CMake SBOM path:

  * CMake builds            (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
  * TI Code Composer Studio (CCS / armcl, via `bear -- make ...`)
  * Microchip MPLAB X       (xc32, via `bear -- make ...` on the nbproject make)
  * Renesas e2studio / CCRX (via a build wrapper that records commands)
  * Xilinx SDK / Vitis      (via `bear -- make ...`)

The extracted sources + defines are handed to the shared driver
(tools/scripts/wolfboot-sbom.sh), so the resulting CycloneDX 1.6 / SPDX 2.3
SBOM is identical in shape to the Make, CMake and IAR paths.

Usage:
  tools/scripts/ide-sbom/compdb_sbom.py build/compile_commands.json [options]

Options:
  --include REGEX   Only include source files whose absolute path matches REGEX
                    (repeatable). Default: all C/asm sources.
  --exclude REGEX   Exclude source files whose absolute path matches REGEX
                    (repeatable, applied after --include). Handy to drop
                    test-app/ or unit-test sources.
  --gen-sbom PATH   Path to wolfSSL scripts/gen-sbom (passed through).
  --version VER     Package version (passed through).
  --cdx-out / --spdx-out PATH   Output paths (passed through).
  --srcs-out PATH   Where to write the extracted source list (default: temp).
  --print-only      Print the extracted sources + defines and exit.
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

SRC_EXTS = ('.c', '.cc', '.cpp', '.cxx', '.s', '.asm')

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..'))
DRIVER = os.path.join(ROOT, 'tools', 'scripts', 'wolfboot-sbom.sh')


def entry_tokens(entry):
    """Return the compiler argument tokens for a compdb entry."""
    if 'arguments' in entry and entry['arguments']:
        return list(entry['arguments'])
    if 'command' in entry and entry['command']:
        try:
            return shlex.split(entry['command'])
        except ValueError:
            return entry['command'].split()
    return []


def extract_defines(tokens):
    """Return -D define tokens (normalized to bare NAME[=VAL]) from arg tokens."""
    defs = []
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t == '-D' and i + 1 < len(tokens):
            defs.append(tokens[i + 1])
            i += 2
            continue
        if t.startswith('-D'):
            defs.append(t[2:])
        i += 1
    return defs


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('compdb', help='Path to compile_commands.json')
    ap.add_argument('--include', action='append', default=[])
    ap.add_argument('--exclude', action='append', default=[])
    ap.add_argument('--gen-sbom', default=None)
    ap.add_argument('--version', default=None)
    ap.add_argument('--name', default='wolfboot')
    ap.add_argument('--cdx-out', default=None)
    ap.add_argument('--spdx-out', default=None)
    ap.add_argument('--srcs-out', default=None)
    ap.add_argument('--print-only', action='store_true')
    args = ap.parse_args()

    if not os.path.isfile(args.compdb):
        sys.exit(f"ERROR: compilation database not found: {args.compdb}")
    with open(args.compdb) as f:
        try:
            db = json.load(f)
        except json.JSONDecodeError as e:
            sys.exit(f"ERROR: cannot parse {args.compdb}: {e}")

    inc = [re.compile(p) for p in args.include]
    exc = [re.compile(p) for p in args.exclude]

    srcs = []
    defines = set()
    seen = set()
    for entry in db:
        fpath = entry.get('file')
        if not fpath:
            continue
        directory = entry.get('directory', os.getcwd())
        if not os.path.isabs(fpath):
            fpath = os.path.join(directory, fpath)
        fpath = os.path.normpath(fpath)
        if not fpath.lower().endswith(SRC_EXTS):
            continue
        if inc and not any(r.search(fpath) for r in inc):
            continue
        if exc and any(r.search(fpath) for r in exc):
            continue
        tokens = entry_tokens(entry)
        for d in extract_defines(tokens):
            defines.add(d)
        if fpath not in seen and os.path.isfile(fpath):
            seen.add(fpath)
            srcs.append(fpath)

    if not srcs:
        sys.exit("ERROR: no matching source files found in compilation database")

    defines = sorted(defines)
    cflags = ' '.join(f'-D{d}' for d in defines)

    if args.print_only:
        print(f"# {len(srcs)} sources, {len(defines)} defines")
        print("\n[defines]")
        for d in defines:
            print(f"  -D{d}")
        print("\n[sources]")
        for s in srcs:
            print(f"  {s}")
        return

    srcs_out = args.srcs_out
    tmp = None
    if not srcs_out:
        fd, srcs_out = tempfile.mkstemp(prefix='wolfboot-compdb-srcs-', suffix='.txt')
        os.close(fd)
        tmp = srcs_out
    with open(srcs_out, 'w') as f:
        f.write('\n'.join(srcs) + '\n')

    cmd = [DRIVER, '--srcs-file', srcs_out, '--cflags', cflags,
           '--name', args.name, '--root', ROOT]
    if args.version:
        cmd += ['--version', args.version]
    if args.gen_sbom:
        cmd += ['--gen-sbom', args.gen_sbom]
    if args.cdx_out:
        cmd += ['--cdx-out', args.cdx_out]
    if args.spdx_out:
        cmd += ['--spdx-out', args.spdx_out]

    print(f"compdb SBOM: {len(srcs)} sources, {len(defines)} defines")
    try:
        rc = subprocess.call(cmd)
    finally:
        if tmp and os.path.exists(tmp):
            os.remove(tmp)
    sys.exit(rc)


if __name__ == '__main__':
    main()
