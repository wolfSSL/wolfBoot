#!/usr/bin/env python3
"""Generate a wolfBoot SBOM from an IAR Embedded Workbench project (.ewp).

IAR builds happen entirely inside the IDE and never touch the Makefile or CMake,
so the compiled source set and the preprocessor configuration live in the .ewp
project file rather than in OBJS / CFLAGS.  This extractor reads them out of the
.ewp and feeds them to the shared wolfBoot SBOM driver
(tools/scripts/wolfboot-sbom.sh), so an IAR-built wolfBoot gets the same
CycloneDX 1.6 + SPDX 2.3 SBOM as a Make- or CMake-built one.

It parses:
  * the C compiler preprocessor defines (ICCARM -> CCDefines <state> entries)
  * the compiled source files (<file><name> ...), resolving $PROJ_DIR$

Usage:
  tools/scripts/ide-sbom/iar_sbom.py IDE/IAR/wolfboot.ewp [options]

Options:
  --config NAME     IAR build configuration to read (default: the configuration
                    with the most preprocessor defines, i.e. the real one).
  --gen-sbom PATH   Path to wolfSSL scripts/gen-sbom (passed through).
  --version VER     Package version (passed through; else driver reads version.h).
  --cdx-out PATH / --spdx-out PATH   Output paths (passed through).
  --srcs-out PATH   Where to write the extracted source list
                    (default: a temp file).
  --print-only      Print the extracted sources + defines and exit (no SBOM).
"""

import argparse
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

SRC_EXTS = ('.c', '.cc', '.cpp', '.cxx', '.s', '.asm')

# Repo root: tools/scripts/ide-sbom/iar_sbom.py -> up 3.
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..'))
DRIVER = os.path.join(ROOT, 'tools', 'scripts', 'wolfboot-sbom.sh')


def resolve_proj_dir(raw, proj_dir):
    """Resolve an IAR $PROJ_DIR$-relative, backslash path to an absolute path."""
    p = raw.replace('$PROJ_DIR$', proj_dir)
    p = p.replace('\\', '/')
    if not os.path.isabs(p):
        p = os.path.join(proj_dir, p)
    return os.path.normpath(p)


def parse_configs(root):
    """Return {config_name: [define, ...]} from each configuration's ICCARM
    CCDefines option."""
    configs = {}
    for cfg in root.findall('configuration'):
        name_el = cfg.find('name')
        cfg_name = name_el.text if name_el is not None else '(unnamed)'
        defines = []
        for settings in cfg.findall('settings'):
            sname = settings.find('name')
            if sname is None or sname.text != 'ICCARM':
                continue
            for data in settings.findall('data'):
                for option in data.findall('option'):
                    oname = option.find('name')
                    if oname is None or oname.text != 'CCDefines':
                        continue
                    for state in option.findall('state'):
                        if state.text:
                            defines.append(state.text.strip())
        configs[cfg_name] = defines
    return configs


def collect_sources(root, proj_dir):
    """Return (present, missing) absolute paths of compiled source files.

    Files that do not exist on disk are separated out: build-time generated
    files such as keystore.c are listed in the .ewp but only materialize during
    a build.  This mirrors the Make path, where $(wildcard) silently drops
    sources that are not present.
    """
    srcs = []
    for file_el in root.iter('file'):
        name_el = file_el.find('name')
        if name_el is None or not name_el.text:
            continue
        raw = name_el.text.strip()
        if not raw.lower().endswith(SRC_EXTS):
            continue
        srcs.append(resolve_proj_dir(raw, proj_dir))
    # De-dup while preserving order, then split on existence.
    seen = set()
    present, missing = [], []
    for s in srcs:
        if s in seen:
            continue
        seen.add(s)
        (present if os.path.isfile(s) else missing).append(s)
    return present, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ewp', help='Path to the IAR .ewp project file')
    ap.add_argument('--config', help='IAR configuration name (default: richest)')
    ap.add_argument('--gen-sbom', default=None)
    ap.add_argument('--version', default=None)
    ap.add_argument('--cdx-out', default=None)
    ap.add_argument('--spdx-out', default=None)
    ap.add_argument('--srcs-out', default=None)
    ap.add_argument('--print-only', action='store_true')
    args = ap.parse_args()

    if not os.path.isfile(args.ewp):
        sys.exit(f"ERROR: .ewp not found: {args.ewp}")
    proj_dir = os.path.dirname(os.path.abspath(args.ewp))

    try:
        tree = ET.parse(args.ewp)
    except ET.ParseError as e:
        sys.exit(f"ERROR: cannot parse {args.ewp}: {e}")
    root = tree.getroot()

    configs = parse_configs(root)
    if not configs:
        sys.exit("ERROR: no <configuration> with ICCARM CCDefines found in .ewp")

    if args.config:
        if args.config not in configs:
            sys.exit(f"ERROR: config {args.config!r} not found. "
                     f"Available: {', '.join(configs)}")
        cfg_name = args.config
    else:
        # Pick the configuration with the most defines (the real build config;
        # a Debug config often only carries NDEBUG-style noise).
        cfg_name = max(configs, key=lambda k: len(configs[k]))

    defines = configs[cfg_name]
    srcs, missing = collect_sources(root, proj_dir)
    if not srcs:
        sys.exit("ERROR: no existing source files found in .ewp")
    if missing:
        sys.stderr.write(
            "WARNING: %d source(s) listed in the .ewp were not found on disk "
            "and are excluded (e.g. build-time generated files like "
            "keystore.c):\n" % len(missing))
        for m in missing:
            sys.stderr.write("  - %s\n" % m)

    cflags = ' '.join(f'-D{d}' for d in defines)

    if args.print_only:
        print(f"# IAR configuration: {cfg_name}")
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
        fd, srcs_out = tempfile.mkstemp(prefix='wolfboot-iar-srcs-', suffix='.txt')
        os.close(fd)
        tmp = srcs_out
    with open(srcs_out, 'w') as f:
        f.write('\n'.join(srcs) + '\n')

    cmd = [DRIVER, '--srcs-file', srcs_out, '--cflags', cflags,
           '--name', 'wolfboot', '--root', ROOT]
    if args.version:
        cmd += ['--version', args.version]
    if args.gen_sbom:
        cmd += ['--gen-sbom', args.gen_sbom]
    if args.cdx_out:
        cmd += ['--cdx-out', args.cdx_out]
    if args.spdx_out:
        cmd += ['--spdx-out', args.spdx_out]

    print(f"IAR SBOM: configuration={cfg_name} "
          f"({len(srcs)} sources, {len(defines)} defines)")
    try:
        rc = subprocess.call(cmd)
    finally:
        if tmp and os.path.exists(tmp):
            os.remove(tmp)
    sys.exit(rc)


if __name__ == '__main__':
    main()
