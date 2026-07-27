#!/usr/bin/env python3
"""wolfGlass SBOM driver - the product-neutral SBOM engine.

This is the generalized form of wolfBoot's tools/scripts/wolfboot-sbom.sh. It is
product-neutral: the name, version, root, generator, and license are all
arguments. Nothing product-specific is hard-coded. Any product front end
(autotools, CMake, plain Make, IAR, or a compilation database) produces the
inputs and hands them to this driver.

The driver covers every product tier:

  * Library (tier R/L/S): hash the built library with --lib.
  * Source-embedded (tier E, e.g. wolfBoot): hash a source set with --srcs-file.
  * As-built / FIPS / kernel module: --lib with --no-artifact-hash on the
    as-built artifact. Never substitute a source list for a certified artifact.
  * Linker / binding: record a declared dependency with --dep-wolfssl and
    friends (feature-detected against the generator).

Config (choose one):
  --cflags "..."     Raw CFLAGS. -D tokens are expanded through the host
                     compiler and scrubbed of absolute paths.
  --options-h PATH   A pre-expanded flat #define header, used verbatim (scrubbed).
  --user-settings P  A user_settings.h. Passed to the generator, which captures it.
  --source-only      No build-config macros (e.g. a Kconfig-driven build).

The macro capture runs the HOST compiler (never the cross compiler), so the SBOM
is byte-reproducible across toolchains: gcc, clang, IAR, armcl, ccrx, and xc32
all converge to the same document for the same config.

Reproducibility: captured macros are scrubbed of absolute host paths before they
reach gen-sbom (for example -DPICO_SDK_PATH=/home/you/pico-sdk from arch.mk). Use
--no-scrub to disable (debug only).
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

_ABS_PATH_POSIX = re.compile(r'^/[^ ]+')
_ABS_PATH_WINDOWS_DRIVE = re.compile(r'^[A-Za-z]:[\\/]')
_ABS_PATH_WINDOWS_UNC = re.compile(r'^\\\\')
_DEFINE = re.compile(r'^#define\s+(\S+)\s*(.*)$')


def is_absolute_path_token(token):
    """Return True when a macro token looks like an absolute host path."""
    return bool(
        _ABS_PATH_POSIX.match(token) or
        _ABS_PATH_WINDOWS_DRIVE.match(token) or
        _ABS_PATH_WINDOWS_UNC.match(token)
    )


def scrub_defines(text):
    """Redact absolute-path tokens from a flat #define header text.

    A macro value that is (or contains) an absolute path would otherwise make
    the SBOM machine-specific and leak the local file system. The macro name is
    kept, so the configuration record stays complete.
    """
    out = []
    for line in text.splitlines():
        m = _DEFINE.match(line)
        if not m:
            out.append(line)
            continue
        name, val = m.group(1), m.group(2)
        if val == "":
            out.append(line)
            continue
        toks = []
        for t in val.split(" "):
            core = t.replace('"', '')
            toks.append("<redacted-path>" if is_absolute_path_token(core) else t)
        out.append("#define " + name + " " + " ".join(toks))
    return "\n".join(out) + "\n"


def read_version(version_file, version_macro):
    """Read a version string from a header, e.g. LIBWOLFBOOT_VERSION_STRING."""
    if not version_file or not os.path.isfile(version_file):
        return ""
    pat = re.compile(re.escape(version_macro) + r'\s*"([^"]*)"')
    with open(version_file, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                return m.group(1)
    return ""


def find_gen_sbom(explicit):
    """Locate gen-sbom: explicit arg, a sibling vendored copy, then WOLFSSL_DIR."""
    if explicit:
        return explicit
    sibling = os.path.join(SCRIPT_DIR, "gen-sbom")
    if os.path.isfile(sibling):
        return sibling
    wolfssl_dir = os.environ.get("WOLFSSL_DIR")
    if wolfssl_dir:
        cand = os.path.join(wolfssl_dir, "scripts", "gen-sbom")
        if os.path.isfile(cand):
            return cand
    return sibling  # report the sibling path in the error message


def gen_sbom_supports(python, gen_sbom, flag):
    """Return True if the generator advertises a flag in its --help."""
    try:
        res = subprocess.run([python, gen_sbom, "--help"],
                             capture_output=True, text=True, check=False)
    except OSError:
        return False
    return flag in (res.stdout + res.stderr)


def capture_macros(hostcc, cflags):
    """Expand the -D tokens of CFLAGS through the host compiler's -dM -E.

    Only tokens that start with ``-D`` are kept. ``-I``, ``-include``, and
    every other flag are dropped. Products whose configuration is expressed
    by ``-include``'ing a settings header must capture that header themselves
    (``hostcc -dM -E ... -include ...``) and pass the dump via ``--options-h``.
    """
    defs = [t for t in cflags.split() if t.startswith("-D")]
    cmd = [hostcc, "-dM", "-E", "-DWOLFSSL_USER_SETTINGS", *defs,
           "-x", "c", os.devnull]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        sys.exit(f"ERROR: '{hostcc} -dM -E' failed; install a host C compiler "
                 f"or set --hostcc.")
    return res.stdout


def load_srcs(srcs_file, skip_missing):
    """Read the source list; optionally drop paths that do not exist."""
    kept, dropped = [], 0
    with open(srcs_file, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if skip_missing and not os.path.isfile(s):
                print(f"WARNING: skipping missing source: {s}", file=sys.stderr)
                dropped += 1
                continue
            kept.append(s)
    if not kept:
        sys.exit("ERROR: no source files remain for the SBOM.")
    if dropped:
        print(f"  (--skip-missing dropped {dropped} file(s))", file=sys.stderr)
    return kept


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    # Composition (at least one of --lib / --srcs-file; --source-only needs srcs).
    ap.add_argument("--lib", default="",
                    help="Path to the built library to hash (tier R/L/S).")
    ap.add_argument("--srcs-file", default="",
                    help="File listing compiled-in sources (tier E).")
    ap.add_argument("--no-artifact-hash", action="store_true",
                    help="Do not recompute the artifact hash. For as-built FIPS "
                         "or kernel modules.")

    # Config (choose one).
    grp = ap.add_mutually_exclusive_group()
    grp.add_argument("--cflags", default="",
                     help="Build CFLAGS; -D tokens expanded through the host cc.")
    grp.add_argument("--options-h", default="",
                     help="A pre-expanded flat #define header, used verbatim.")
    grp.add_argument("--user-settings", default="",
                     help="A user_settings.h; passed to the generator.")
    grp.add_argument("--source-only", action="store_true",
                     help="Source-inventory SBOM with no build-config macros.")

    # Identity.
    ap.add_argument("--name", required=True, help="Package name in the SBOM.")
    ap.add_argument("--version", default="")
    ap.add_argument("--version-file", default="")
    ap.add_argument("--version-macro", default="")
    ap.add_argument("--supplier", default="wolfSSL Inc.")
    ap.add_argument("--license-file", default="")
    ap.add_argument("--license-override", default="")
    ap.add_argument("--license-text", default="")

    # Dependencies (feature-detected against the generator).
    ap.add_argument("--dep-wolfssl", default="")
    ap.add_argument("--dep-openssl", default="")
    ap.add_argument("--dep-version", action="append", default=[],
                    help="Dependency version, e.g. wolfssl=5.9.2 (repeatable).")

    # Outputs and environment.
    ap.add_argument("--cdx-out", default="")
    ap.add_argument("--spdx-out", default="")
    ap.add_argument("--gen-sbom", default="")
    ap.add_argument("--python", default=sys.executable)
    ap.add_argument("--hostcc", default=os.environ.get("HOSTCC", "cc"))
    ap.add_argument("--root", default="")
    ap.add_argument("--skip-missing", action="store_true")
    ap.add_argument("--no-scrub", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.root) if args.root else os.getcwd()
    license_file = args.license_file or os.path.join(root, "LICENSE")

    version = args.version or read_version(args.version_file, args.version_macro)
    if not version:
        sys.exit("ERROR: could not determine version; pass --version or "
                 "--version-file with --version-macro.")

    if not args.lib and not args.srcs_file:
        sys.exit("ERROR: pass at least one of --lib or --srcs-file.")
    if args.source_only and not args.srcs_file:
        sys.exit("ERROR: --source-only needs --srcs-file.")
    if args.srcs_file and not os.path.isfile(args.srcs_file):
        sys.exit(f"ERROR: --srcs-file '{args.srcs_file}' does not exist.")
    if args.lib and not os.path.isfile(args.lib):
        sys.exit(f"ERROR: --lib '{args.lib}' does not exist.")

    gen_sbom = find_gen_sbom(args.gen_sbom)
    if not os.path.isfile(gen_sbom):
        sys.exit(f"ERROR: gen-sbom not found at '{gen_sbom}'. Pass --gen-sbom "
                 f"/path/to/wolfssl/scripts/gen-sbom or set WOLFSSL_DIR.")

    cdx_out = args.cdx_out or f"{args.name}-{version}.cdx.json"
    spdx_out = args.spdx_out or f"{args.name}-{version}.spdx.json"

    tmp_files = []
    try:
        cmd = [args.python, gen_sbom,
               "--name", args.name,
               "--version", version,
               "--supplier", args.supplier,
               "--license-file", license_file,
               "--cdx-out", cdx_out,
               "--spdx-out", spdx_out]

        # Composition.
        if args.srcs_file:
            srcs = load_srcs(args.srcs_file, args.skip_missing)
            fd, srcs_path = tempfile.mkstemp(prefix="wolfglass-srcs-", suffix=".txt")
            os.close(fd)
            tmp_files.append(srcs_path)
            with open(srcs_path, "w", encoding="utf-8") as f:
                f.write("\n".join(srcs) + "\n")
            cmd += ["--srcs-file", srcs_path]
            src_count = len(srcs)
        else:
            src_count = 0
        if args.lib:
            cmd += ["--lib", args.lib]
        if args.no_artifact_hash:
            cmd += ["--no-artifact-hash"]

        # Config.
        if args.user_settings:
            if not os.path.isfile(args.user_settings):
                sys.exit(f"ERROR: --user-settings '{args.user_settings}' "
                         f"does not exist.")
            cmd += ["--user-settings", args.user_settings]
        else:
            if args.source_only:
                defines_text = ""
            elif args.options_h:
                if not os.path.isfile(args.options_h):
                    sys.exit(f"ERROR: --options-h '{args.options_h}' does not exist.")
                with open(args.options_h, encoding="utf-8", errors="replace") as f:
                    defines_text = f.read()
            elif args.cflags:
                defines_text = capture_macros(args.hostcc, args.cflags)
            else:
                sys.exit("ERROR: pass one of --cflags, --options-h, "
                         "--user-settings, or --source-only.")
            if not args.no_scrub and not args.source_only:
                defines_text = scrub_defines(defines_text)
            fd, defines_path = tempfile.mkstemp(prefix="wolfglass-defs-", suffix=".h")
            os.close(fd)
            tmp_files.append(defines_path)
            with open(defines_path, "w", encoding="utf-8") as f:
                f.write(defines_text)
            cmd += ["--options-h", defines_path]

        # License overrides.
        if args.license_override:
            cmd += ["--license-override", args.license_override]
        if args.license_text:
            cmd += ["--license-text", args.license_text]

        # Dependencies, only if the generator supports them.
        if args.dep_wolfssl and gen_sbom_supports(args.python, gen_sbom, "--dep-wolfssl"):
            cmd += ["--dep-wolfssl", args.dep_wolfssl]
        if args.dep_openssl and gen_sbom_supports(args.python, gen_sbom, "--dep-openssl"):
            cmd += ["--dep-openssl", args.dep_openssl]
        if args.dep_version and gen_sbom_supports(args.python, gen_sbom, "--dep-version"):
            for dv in args.dep_version:
                cmd += ["--dep-version", dv]

        print(f"SBOM: name={args.name} version={version}")
        if args.lib:
            print(f"  library: {args.lib}"
                  + (" (as-built, no re-hash)" if args.no_artifact_hash else ""))
        if src_count:
            print(f"  sources: {src_count} file(s)")
        print(f"  outputs: {cdx_out}  {spdx_out}")

        rc = subprocess.call(cmd)
    finally:
        for f in tmp_files:
            try:
                os.remove(f)
            except OSError:
                pass

    if rc == 0:
        print(f"SBOM written: {cdx_out}  {spdx_out}")
    sys.exit(rc)


if __name__ == "__main__":
    main()
