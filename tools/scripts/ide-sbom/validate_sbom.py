#!/usr/bin/env python3
"""Lightweight structural validator for wolfBoot SBOM output.

Not a full schema validator - it asserts the essentials that every wolfBoot
SBOM route must satisfy, so CI (and humans) can fail fast on a broken generator:

  CycloneDX (*.cdx.json):
    * bomFormat == "CycloneDX"
    * specVersion == "1.6"
    * metadata.component.name == "wolfboot"
    * metadata.component has a non-empty version
    * at least one component or source recorded

  SPDX (*.spdx.json):
    * spdxVersion starts with "SPDX-2"
    * has a name and at least one package

The file kind is detected by content, so argument order does not matter.

Usage:
  validate_sbom.py FILE [FILE ...]
"""

import json
import sys

# Every wolfBoot artifact SBOM names its component in the wolfboot* family:
# wolfboot, wolfboot-hal-<target>, wolfboot-zephyr, ...
EXPECTED_NAME_PREFIX = "wolfboot"


def fail(path, msg):
    print(f"FAIL [{path}]: {msg}", file=sys.stderr)
    sys.exit(1)


def validate_cyclonedx(path, d):
    if d.get("bomFormat") != "CycloneDX":
        fail(path, f"bomFormat != CycloneDX (got {d.get('bomFormat')!r})")
    if d.get("specVersion") != "1.6":
        fail(path, f"specVersion != 1.6 (got {d.get('specVersion')!r})")
    comp = d.get("metadata", {}).get("component", {})
    name = comp.get("name", "")
    if not name.startswith(EXPECTED_NAME_PREFIX):
        fail(path, f"metadata.component.name does not start with "
                   f"{EXPECTED_NAME_PREFIX!r} (got {name!r})")
    if not comp.get("version"):
        fail(path, "metadata.component.version is empty")
    # Sources are recorded as sub-components and/or properties; require some.
    if not d.get("components") and not comp.get("properties"):
        fail(path, "no components or component properties recorded")
    print(f"OK   [{path}]: CycloneDX 1.6, component "
          f"{comp['name']} {comp['version']}")


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
    if len(argv) < 2:
        print(__doc__)
        sys.exit(2)
    for path in argv[1:]:
        try:
            with open(path) as f:
                d = json.load(f)
        except FileNotFoundError:
            fail(path, "file not found")
        except json.JSONDecodeError as e:
            fail(path, f"invalid JSON: {e}")
        if "bomFormat" in d or path.endswith(".cdx.json"):
            validate_cyclonedx(path, d)
        elif "spdxVersion" in d or path.endswith(".spdx.json"):
            validate_spdx(path, d)
        else:
            fail(path, "unrecognized SBOM format (neither CycloneDX nor SPDX)")
    print("All SBOMs valid.")


if __name__ == "__main__":
    main(sys.argv)
