#!/usr/bin/env python3
"""Generate an SBOM for the wolfBoot Zephyr module (TEE / PSA client).

The `zephyr/` directory is not the bootloader: it is a Zephyr module that
compiles a small TEE/PSA non-secure client shim *into a Zephyr application*
(`zephyr_library_sources(...)`, gated on CONFIG_WOLFBOOT_TEE). Its source set is
fixed and lives in the module's CMakeLists, but it is built by Zephyr/west - not
by wolfBoot's Makefile or CMakeLists - so neither the Make nor the CMake SBOM
target sees it.

This extractor reads the module's source list straight out of
`zephyr/CMakeLists.txt` (so it stays in sync automatically) and hands it to the
shared driver as a separate component, `wolfboot-zephyr`.

The module's configuration is Kconfig-driven (CONFIG_* symbols), not a `-D`
macro set, so by default this produces a source-inventory SBOM (no build-config
macros). If you have a real Zephyr build and want the exact compiled config,
generate the SBOM from that build's compilation database with compdb_sbom.py
instead.

Usage:
  tools/scripts/ide-sbom/zephyr_sbom.py [--cmakelists zephyr/CMakeLists.txt] [options]

Options:
  --cmakelists PATH  Module CMakeLists (default: <root>/zephyr/CMakeLists.txt).
  --gen-sbom PATH    Path to wolfSSL scripts/gen-sbom (passed through).
  --version VER      Package version (passed through).
  --cdx-out / --spdx-out PATH   Output paths
                     (default: wolfboot-zephyr-<version>.{cdx,spdx}.json).
  --srcs-out PATH    Where to write the extracted source list (default: temp).
  --print-only       Print the extracted sources and exit.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..'))
DRIVER = os.path.join(ROOT, 'tools', 'scripts', 'wolfboot-sbom.sh')

SRC_EXTS = ('.c', '.cc', '.cpp', '.cxx', '.s', '.asm')


def parse_library_sources(cmakelists):
    """Extract the file list from the zephyr_library_sources(...) block."""
    with open(cmakelists) as f:
        text = f.read()
    module_dir = os.path.dirname(os.path.abspath(cmakelists))

    srcs = []
    for m in re.finditer(r'zephyr_library_sources\s*\((.*?)\)', text, re.DOTALL):
        for raw in m.group(1).split():
            raw = raw.strip()
            if not raw or not raw.lower().endswith(SRC_EXTS):
                continue
            # Resolve the CMake variables Zephyr uses for the module directory.
            p = raw.replace('${CMAKE_CURRENT_LIST_DIR}', module_dir)
            p = p.replace('${CMAKE_CURRENT_SOURCE_DIR}', module_dir)
            p = p.replace('${WOLFBOOT_MODULE_DIR}', os.path.dirname(module_dir))
            if '${' in p:
                # Unknown variable - skip rather than emit a bogus path.
                sys.stderr.write(f"WARNING: skipping unresolved source: {raw}\n")
                continue
            if not os.path.isabs(p):
                p = os.path.join(module_dir, p)
            srcs.append(os.path.normpath(p))

    seen = set()
    present, missing = [], []
    for s in srcs:
        if s in seen:
            continue
        seen.add(s)
        (present if os.path.isfile(s) else missing).append(s)
    return present, missing


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cmakelists', default=os.path.join(ROOT, 'zephyr', 'CMakeLists.txt'))
    ap.add_argument('--gen-sbom', default=None)
    ap.add_argument('--version', default=None)
    ap.add_argument('--cdx-out', default=None)
    ap.add_argument('--spdx-out', default=None)
    ap.add_argument('--srcs-out', default=None)
    ap.add_argument('--print-only', action='store_true')
    args = ap.parse_args()

    if not os.path.isfile(args.cmakelists):
        sys.exit(f"ERROR: CMakeLists not found: {args.cmakelists}")

    srcs, missing = parse_library_sources(args.cmakelists)
    if missing:
        sys.stderr.write(
            "WARNING: %d module source(s) not found on disk (excluded):\n"
            % len(missing))
        for m in missing:
            sys.stderr.write("  - %s\n" % m)
    if not srcs:
        sys.exit("ERROR: no wolfBoot Zephyr module sources found in "
                 + args.cmakelists)

    if args.print_only:
        print(f"# wolfboot-zephyr: {len(srcs)} sources")
        for s in srcs:
            print(f"  {s}")
        return

    srcs_out = args.srcs_out
    tmp = None
    if not srcs_out:
        fd, srcs_out = tempfile.mkstemp(prefix='wolfboot-zephyr-srcs-', suffix='.txt')
        os.close(fd)
        tmp = srcs_out
    with open(srcs_out, 'w') as f:
        f.write('\n'.join(srcs) + '\n')

    cmd = [DRIVER, '--srcs-file', srcs_out, '--source-only',
           '--name', 'wolfboot-zephyr', '--root', ROOT]
    if args.version:
        cmd += ['--version', args.version]
    if args.gen_sbom:
        cmd += ['--gen-sbom', args.gen_sbom]
    if args.cdx_out:
        cmd += ['--cdx-out', args.cdx_out]
    if args.spdx_out:
        cmd += ['--spdx-out', args.spdx_out]

    print(f"Zephyr module SBOM: {len(srcs)} sources (source-inventory)")
    try:
        rc = subprocess.call(cmd)
    finally:
        if tmp and os.path.exists(tmp):
            os.remove(tmp)
    sys.exit(rc)


if __name__ == '__main__':
    main()
