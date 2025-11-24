#!/usr/bin/env python3

# Adds .config file to CMakePresets.json
#
# Example:
#   python3 ./tools/scripts/config2presets.py ./config/examples/stm32h7.config

import argparse
import json
import os
import re
import sys
import subprocess
from collections import OrderedDict
from pathlib import Path

COMMENT_RE = re.compile(r"\s*(#.*)?$")
LINE_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\??=\s*(.*)$')

BOOL_TRUE = {"1", "on", "true", "yes", "y"}
BOOL_FALSE = {"0", "off", "false", "no", "n"}

# Known inherited values: these are provided implicitly by presets/build
# If the .config value matches, do NOT write to cacheVariables.
# If it differs, keep it and warn that it overrides the inherited default.
KNOWN_INHERITED = {
    "WOLFBOOT_CONFIG_MODE": "preset",
    "CMAKE_TOOLCHAIN_FILE": "cmake/toolchain_arm-none-eabi.cmake",
    "SIGN": "ECC256",
    "HASH": "SHA256",
    "BUILD_TEST_APPS": "ON"
}


def normalize_bool(s: str):
    v = s.strip().lower()
    if v in BOOL_TRUE:
        return "ON"
    if v in BOOL_FALSE:
        return "OFF"
    return None


def parse_config(path: Path):
    kv = OrderedDict()
    with path.open("r", encoding="utf-8") as f:
        for ln in f:
            if COMMENT_RE.fullmatch(ln):
                continue
            m = LINE_RE.match(ln.rstrip("\n"))
            if not m:
                # skip silently; load_dot_config warns elsewhere
                continue
            key, val = m.group(1), m.group(2).strip()
            kv[key] = val
    return kv


def filter_inherited_values(kv):
    """
    Remove keys whose values match KNOWN_INHERITED defaults (do not
    emit them into cacheVariables), and collect messages about
    inherited/overridden values.
    """
    new_kv = OrderedDict()
    messages = []

    for k, v in kv.items():
        if k in KNOWN_INHERITED:
            expected = KNOWN_INHERITED[k]
            v_stripped = v.strip()
            if v_stripped == expected:
                messages.append(
                    "Note: '{}' is an inherited value with default '{}'; "
                    "omitting from preset cacheVariables.".format(k, expected)
                )
                # Do not add to new_kv; rely on inherited value
                continue
            else:
                messages.append(
                    "WARNING: '{}' in .config is '{}', overriding inherited "
                    "default '{}' in preset cacheVariables.".format(
                        k, v_stripped, expected
                    )
                )
                # Fall through to keep the override

        new_kv[k] = v

    return new_kv, messages


def choose_target(kv):
    # Prefer explicit wolfBoot var; else accept TARGET if present
    if "WOLFBOOT_TARGET" in kv and kv["WOLFBOOT_TARGET"]:
        return kv["WOLFBOOT_TARGET"]
    if "TARGET" in kv and kv["TARGET"]:
        return kv["TARGET"]
    return "custom"


def to_cache_vars(kv):
    cache = OrderedDict()
    for k, v in kv.items():
        # Map TARGET -> WOLFBOOT_TARGET if the latter is not already set
        if k == "TARGET" and "WOLFBOOT_TARGET" not in kv:
            cache["WOLFBOOT_TARGET"] = v
            continue

        # Normalize booleans to ON/OFF; keep everything else as strings
        nb = normalize_bool(v)
        cache[k] = nb if nb is not None else v
    return cache


def ensure_base_vars(cache, toolchain_value):
    # These should be inherited from [base] in the JSON presets:
    # Always ensure toolchain file is set using the literal value passed in
    # cache.setdefault("CMAKE_TOOLCHAIN_FILE", toolchain_value)
    # Typically desired
    # cache.setdefault("BUILD_TEST_APPS", "ON")
    # Force preset mode when generating from .config into presets
    # cache["WOLFBOOT_CONFIG_MODE"] = "preset"
    return cache


def make_preset_name(target):
    return f"{target}"


def make_binary_dir(source_dir, target):
    return os.path.join(source_dir, f"build-{target}")


def validate_presets_json(presets_path: Path):
    """
    If the presets file exists but is not valid JSON, print a clear message
    and exit with a non-zero status instead of raising an exception.
    """
    if not presets_path.exists():
        return

    try:
        with presets_path.open("r", encoding="utf-8") as f:
            json.load(f)
    except json.JSONDecodeError as e:
        print(
            "Error: '{}' exists but is not valid JSON (CMakePresets.json is malformed).".format(
                presets_path
            ),
            file=sys.stderr,
        )
        print("Details: {}".format(e), file=sys.stderr)
        print(
            "Please fix the JSON (for example, remove any trailing commas) and rerun this script.",
            file=sys.stderr,
        )
        sys.exit(3)


def load_existing_presets(presets_path: Path):
    try:
        with presets_path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None


def _merge_configure_preset_list(preset_list, cfg_preset):
    """
    Update or insert a configure preset.

    - If a preset with the same name exists:
      * Preserve all top-level keys (inherits, environment, binaryDir, etc.)
      * Merge cacheVariables: existing keys plus new ones, with new values winning.

    - If it does not exist:
      * Insert the new preset among the non-hidden presets in a best-fit
        alphabetical position, ignoring hidden presets when choosing where
        to insert. Existing entries (hidden or not) are not reordered.
    """
    name = cfg_preset.get("name")

    # First: update existing entry if present
    for idx, existing in enumerate(preset_list):
        if existing.get("name") == name:
            merged = existing.copy()

            existing_cache = existing.get("cacheVariables", {})
            new_cache = cfg_preset.get("cacheVariables", {})

            merged_cache = {}
            if isinstance(existing_cache, dict):
                merged_cache.update(existing_cache)
            if isinstance(new_cache, dict):
                merged_cache.update(new_cache)

            merged["cacheVariables"] = merged_cache
            merged["name"] = name
            preset_list[idx] = merged
            return preset_list

    # Only reach here if this is a new preset

    if not isinstance(name, str):
        # Fallback: no sensible name to compare; just append
        preset_list.append(cfg_preset)
        return preset_list

    # Build a list of (real_index, preset_name) for NON-hidden presets
    visible = []
    for idx, existing in enumerate(preset_list):
        # Treat missing "hidden" as visible
        if existing.get("hidden") is True:
            continue
        existing_name = existing.get("name")
        if isinstance(existing_name, str):
            visible.append((idx, existing_name))

    # Decide insertion point among visible presets
    insert_at_real_index = None
    for real_idx, existing_name in visible:
        if name < existing_name:
            insert_at_real_index = real_idx
            break

    if insert_at_real_index is None:
        if visible:
            # After the last visible preset
            last_visible_index = visible[-1][0]
            insert_at_real_index = last_visible_index + 1
        else:
            # No visible presets at all: append
            insert_at_real_index = len(preset_list)

    preset_list.insert(insert_at_real_index, cfg_preset)
    return preset_list


def _merge_build_preset_list(preset_list, bld_preset):
    """
    Update or insert a build preset, keeping existing entries in their
    current order and placing new ones in a best-fit alphabetical position.

    - If a preset with the same name exists:
      * Preserve existing jobs/verbose/targets and any other fields.
      * Only ensure configurePreset is set if it was missing.

    - If it does not exist:
      * Consider only entries whose configurePreset is NOT 'sim' when
        choosing the insertion point.
      * Insert the new preset before the first such entry whose name is
        lexicographically greater; if none, insert after the last such entry.
    """
    name = bld_preset.get("name")

    # First: update existing entry if present
    for idx, existing in enumerate(preset_list):
        if existing.get("name") == name:
            merged = existing.copy()
            if "configurePreset" not in merged and "configurePreset" in bld_preset:
                merged["configurePreset"] = bld_preset["configurePreset"]
            merged["name"] = name
            preset_list[idx] = merged
            return preset_list

    # Only reach here for a new preset
    if not isinstance(name, str):
        # No reasonable name to compare; just append
        preset_list.append(bld_preset)
        return preset_list

    # Build list of (real_index, preset_name) for entries we care about
    # Ignore any entry whose configurePreset is 'sim'.
    visible = []
    for idx, existing in enumerate(preset_list):
        if existing.get("configurePreset") == "sim":
            continue
        existing_name = existing.get("name")
        if isinstance(existing_name, str):
            visible.append((idx, existing_name))

    insert_at_real_index = None

    # Find first visible preset whose name is lexicographically greater
    for real_idx, existing_name in visible:
        if name < existing_name:
            insert_at_real_index = real_idx
            break

    if insert_at_real_index is None:
        if visible:
            # After the last visible (non-sim) preset
            last_visible_index = visible[-1][0]
            insert_at_real_index = last_visible_index + 1
        else:
            # No visible presets at all: append to the end
            insert_at_real_index = len(preset_list)

    preset_list.insert(insert_at_real_index, bld_preset)
    return preset_list


def merge_preset(doc, cfg_preset, bld_preset):
    """
    Merge a configure/build preset into an existing CMakePresets.json document.

    - If doc is None, create a fresh schema v3 doc with just these presets.
    - Otherwise:
      * Ensure configurePresets/buildPresets arrays exist.
      * Update or append the specific presets without reordering any others.
    """
    if doc is None:
        return {
            "version": 3,
            "configurePresets": [cfg_preset],
            "buildPresets": [bld_preset],
        }

    if "configurePresets" not in doc or not isinstance(doc["configurePresets"], list):
        doc["configurePresets"] = []
    if "buildPresets" not in doc or not isinstance(doc["buildPresets"], list):
        doc["buildPresets"] = []

    _merge_configure_preset_list(doc["configurePresets"], cfg_preset)
    _merge_build_preset_list(doc["buildPresets"], bld_preset)

    return doc


def extract_unused_vars(output: str):
    """
    Parse CMake output and return a list of
    'Manually-specified variables were not used by the project' names.
    """
    lines = output.splitlines()
    unused = []
    capture = False

    for line in lines:
        if "Manually-specified variables were not used by the project:" in line:
            capture = True
            continue

        if not capture:
            continue

        stripped = line.strip()

        # Skip leading blank lines after the header
        if stripped == "":
            # If we have already collected some variables, a blank line
            # means the list is over.
            if unused:
                break
            continue

        # Only take reasonably-indented lines as variable names
        if line.startswith("    ") or line.startswith("  "):
            unused.append(stripped)
        else:
            # Non-indented line once capture has started means the block is over
            if unused:
                break

    return unused


def run_cmake_and_report_unused(preset_name: str, repo_root: Path):
    """
    Run 'cmake --preset <preset_name>' from repo_root and
    print any unused manually-specified variables reported.
    """
    print("")
    print("Running CMake configure to check for unused manually-specified variables...")
    cmd = ["cmake", "--preset", preset_name]
    print("Command:", " ".join(cmd))

    try:
        proc = subprocess.run(
            cmd,
            cwd=str(repo_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError:
        print("cmake not found on PATH; skipping unused-variable check.", file=sys.stderr)
        return

    combined = ""
    if proc.stdout:
        combined += proc.stdout
    if proc.stderr:
        if combined:
            combined += "\n"
        combined += proc.stderr

    # If CMake failed, show the full output first so the error is visible.
    if proc.returncode != 0:
        print("")
        print("CMake configure for preset '{}' failed with exit code {}.".format(
            preset_name, proc.returncode
        ))
        print("Full CMake output follows:")
        print("==============================================")
        print("============= BEGIN CMAKE OUTPUT =============")
        print(combined)
        print("============= END CMAKE OUTPUT =============")
        print("==============================================")

    unused = extract_unused_vars(combined)

    if unused:
        print("")
        print(
            "CMake reported unused manually-specified variables for preset '{}' :".format(
                preset_name
            )
        )
        for name in unused:
            print("  {}".format(name))
    else:
        print("")
        print(
            "No unused manually-specified CMake variables detected for preset '{}'.".format(
                preset_name
            )
        )

    if proc.returncode != 0:
        print("")
        print("Note: 'cmake --preset {}' exited with code {}.".format(
            preset_name, proc.returncode
        ))


def check_workflow_for_preset(workflow_path: Path, preset_name: str):
    """
    Best-effort check: does the given workflow YAML mention this preset/target
    name anywhere? If not, print a warning so it is clear the CI matrix will
    not build it.
    """
    if not workflow_path.exists():
        print(
            "Note: workflow YAML '{}' not found; skipping workflow check.".format(
                workflow_path
            )
        )
        return

    try:
        text = workflow_path.read_text(encoding="utf-8")
    except OSError as e:
        print(
            "Warning: could not read workflow YAML '{}': {}".format(
                workflow_path, e
            ),
            file=sys.stderr,
        )
        return

    if preset_name in text:
        print(
            "Workflow check: preset/target '{}' is referenced in '{}'.".format(
                preset_name, workflow_path
            )
        )
    else:
        print(
            "WARNING: preset/target '{}' was NOT found in workflow '{}'. "
            "CI will not build this target until you add it to the matrix.".format(
                preset_name, workflow_path
            ),
            file=sys.stderr,
        )


def main():
    ap = argparse.ArgumentParser(description="Generate or merge a CMakePresets.json from a .config file")
    ap.add_argument(
        "config",
        help="Path to .config (relative to your current directory if not absolute)",
    )
    ap.add_argument(
        "--toolchain",
        default="cmake/toolchain_arm-none-eabi.cmake",
        help="Path to toolchain file as it should appear in CMAKE_TOOLCHAIN_FILE",
    )
    ap.add_argument(
        "--presets",
        default="CMakePresets.json",
        help="Path to CMakePresets.json to create/merge (relative to repo root if not absolute)",
    )
    ap.add_argument(
        "--generator",
        default="Ninja",
        help="CMake generator",
    )
    ap.add_argument(
        "--preset-name",
        default=None,
        help="Override preset name",
    )
    ap.add_argument(
        "--binary-dir",
        default=None,
        help="Override binaryDir",
    )
    ap.add_argument(
        "--display-name",
        default=None,
        help="Override displayName",
    )
    ap.add_argument(
        "--workflow",
        default=".github/workflows/test-build-cmake-presets.yml",
        help="Optional GitHub Actions workflow YAML to check for this preset name",
    )
    args = ap.parse_args()

    # Begin common dir init, for /tools/scripts
    script_path = Path(__file__).resolve()
    script_dir = script_path.parent.resolve()

    # repo root is parent of tools/scripts -- go up two levels
    repo_root = (script_dir / ".." / "..").resolve()

    caller_cwd = Path.cwd().resolve()

    # Print only if caller's current working directory is neither REPO_ROOT nor REPO_ROOT/tools/scripts
    if caller_cwd != repo_root and caller_cwd != (repo_root / "tools" / "scripts"):
        print("Script paths:")
        print(f"-- SCRIPT_PATH = {script_path}")
        print(f"-- SCRIPT_DIR  = {script_dir}")
        print(f"-- REPO_ROOT   = {repo_root}")

    # Always work from the repo root, regardless of where the script was invoked
    try:
        os.chdir(repo_root)
    except OSError as e:
        print(f"Failed to cd to: {repo_root}\n{e}", file=sys.stderr)
        sys.exit(1)
    print(f"Starting {script_path} from {Path.cwd().resolve()}")
    # End common dir init

    # Resolve paths:
    # - config: relative to caller's CWD (so user can pass local relative paths naturally)
    # - presets: relative to repo root (we already chdir there)
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = (caller_cwd / config_path).resolve()

    presets_path = Path(args.presets)
    if not presets_path.is_absolute():
        presets_path = (repo_root / presets_path).resolve()

    # Pre-validate the existing presets JSON so we can show a clear message
    # instead of crashing on a JSONDecodeError later.
    validate_presets_json(presets_path)

    kv = parse_config(config_path)
    if not kv:
        print(f"No settings parsed from .config: {config_path}", file=sys.stderr)
        sys.exit(2)

    # Handle inherited values like SIGN/HASH before converting to cache vars
    kv, inherited_messages = filter_inherited_values(kv)

    target = choose_target(kv)
    cache = to_cache_vars(kv)

    # Use the toolchain value exactly as passed on the command line,
    # but normalize backslashes for JSON/CMake friendliness.
    toolchain_value = args.toolchain.replace("\\", "/")
    cache = ensure_base_vars(cache, toolchain_value)

    # Build preset objects
    source_dir = "${sourceDir}"  # CMake variable; leave literal
    preset_name = args.preset_name or make_preset_name(target)
    binary_dir = args.binary_dir or make_binary_dir(source_dir, target)
    display_name = args.display_name or f"{target}"

    # For STM32 boards, inherit like stm32g0:
    #   [ "base", "stm32", "sign_hash_config" ]
    # For everything else, just inherit "base".
    inherits_value = "base"
    if isinstance(preset_name, str) and preset_name.startswith("stm32") and preset_name != "stm32":
        inherits_value = ["base", "stm32", "sign_hash_config"]

    cfg_preset = OrderedDict(
        [
            ("name", preset_name),
            ("displayName", display_name),
            ("inherits", inherits_value),
            ("generator", args.generator),
            ("binaryDir", binary_dir),
            ("cacheVariables", cache),
        ]
    )

    bld_preset = OrderedDict(
        [
            ("name", preset_name),
            ("configurePreset", preset_name),
            ("jobs", 4),
            ("verbose", True),
            ("targets", ["all"]),
        ]
    )

    # Ensure schema v3 unless existing file says otherwise
    doc = load_existing_presets(presets_path)
    if doc is None:
        doc = {"version": 3}
    if "version" not in doc:
        doc["version"] = 3

    result = merge_preset(doc, cfg_preset, bld_preset)

    # Pretty-print with stable key order
    with presets_path.open("w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")

    print(f"Updated {presets_path} with preset '{preset_name}' targeting '{target}'")

    # Best-effort: check if this preset/target name appears in the workflow matrix
    workflow_path = Path(args.workflow)
    if not workflow_path.is_absolute():
        workflow_path = (repo_root / workflow_path).resolve()
    check_workflow_for_preset(workflow_path, preset_name)
    if inherited_messages:
        print("")
        print("Inherited .config values summary:")
        for msg in inherited_messages:
            print("  " + msg)

    # After updating presets, run cmake and show any unused variables
    run_cmake_and_report_unused(preset_name, repo_root)


if __name__ == "__main__":
    main()
