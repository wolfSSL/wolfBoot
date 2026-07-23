#!/usr/bin/env python3
"""Structural validator for wolfGlass SBOM output.

This is the product-neutral form of wolfBoot's validate_sbom.py. It asserts the
essentials that every SBOM route must satisfy, so CI (and humans) can fail fast
on a broken generator. It is not a full schema validator.

  CycloneDX (*.cdx.json):
    * bomFormat == "CycloneDX"
    * specVersion == "1.6"
    * metadata.component.name starts with --name-prefix (if given)
    * metadata.component has a non-empty version
    * at least one component or component property recorded

  SPDX (*.spdx.json):
    * spdxVersion starts with "SPDX-2"
    * has a name and at least one package

The file kind is detected by content, so argument order does not matter.

Usage:
  validate_sbom.py [--name-prefix PREFIX] FILE [FILE ...]
"""

import argparse
import json
import sys


def fail(path, msg):
    print(f"FAIL [{path}]: {msg}", file=sys.stderr)
    sys.exit(1)


def validate_cyclonedx(path, d, name_prefix):
    if d.get("bomFormat") != "CycloneDX":
        fail(path, f"bomFormat != CycloneDX (got {d.get('bomFormat')!r})")
    if d.get("specVersion") != "1.6":
        fail(path, f"specVersion != 1.6 (got {d.get('specVersion')!r})")
    comp = d.get("metadata", {}).get("component", {})
    name = comp.get("name", "")
    if name_prefix and not name.startswith(name_prefix):
        fail(path, f"metadata.component.name does not start with "
                   f"{name_prefix!r} (got {name!r})")
    if not comp.get("version"):
        fail(path, "metadata.component.version is empty")
    if not d.get("components") and not comp.get("properties"):
        fail(path, "no components or component properties recorded")
    print(f"OK   [{path}]: CycloneDX 1.6, component "
          f"{comp.get('name')} {comp.get('version')}")


def validate_spdx(path, d):
    ver = d.get("spdxVersion", "")
    if not ver.startswith("SPDX-2"):
        fail(path, f"spdxVersion not SPDX-2.x (got {ver!r})")
    if not d.get("name"):
        fail(path, "document name is empty")
    if not d.get("packages"):
        fail(path, "no packages recorded")
    print(f"OK   [{path}]: {ver}, {len(d['packages'])} package(s)")


def main(argv):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name-prefix", default="",
                    help="Require metadata.component.name to start with this.")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args(argv[1:])

    for path in args.files:
        try:
            with open(path) as f:
                d = json.load(f)
        except FileNotFoundError:
            fail(path, "file not found")
        except json.JSONDecodeError as e:
            fail(path, f"invalid JSON: {e}")
        if "bomFormat" in d or path.endswith(".cdx.json"):
            validate_cyclonedx(path, d, args.name_prefix)
        elif "spdxVersion" in d or path.endswith(".spdx.json"):
            validate_spdx(path, d)
        else:
            fail(path, "unrecognized SBOM format (neither CycloneDX nor SPDX)")
    print("All SBOMs valid.")


if __name__ == "__main__":
    main(sys.argv)
