#!/usr/bin/env python3
import argparse, json, os, re, sys
from collections import OrderedDict

COMMENT_RE = re.compile(r"\s*(#.*)?$")
LINE_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\??=\s*(.*)$')

BOOL_TRUE = {"1", "on", "true", "yes", "y"}
BOOL_FALSE = {"0", "off", "false", "no", "n"}

def normalize_bool(s: str):
    v = s.strip().lower()
    if v in BOOL_TRUE: return "ON"
    if v in BOOL_FALSE: return "OFF"
    return None

def parse_config(path: str):
    kv = OrderedDict()
    with open(path, "r", encoding="utf-8") as f:
        for ln in f:
            if COMMENT_RE.fullmatch(ln):
                continue
            m = LINE_RE.match(ln.rstrip("\n"))
            if not m:
                # skip silently; load_dot_config warns
                continue
            key, val = m.group(1), m.group(2).strip()
            kv[key] = val
    return kv

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

def ensure_base_vars(cache, toolchain_path):
    # Always ensure toolchain file is set
    cache.setdefault("CMAKE_TOOLCHAIN_FILE", toolchain_path)
    # Your build typically wants this on
    cache.setdefault("BUILD_TEST_APPS", "ON")
    # Force preset mode when generating from .config into presets
    cache["WOLFBOOT_CONFIG_MODE"] = "preset"
    return cache

def make_preset_name(target):
    return f"linux-{target}"

def make_binary_dir(source_dir, target):
    return os.path.join(source_dir, f"build-{target}")

def load_existing_presets(presets_path):
    try:
        with open(presets_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None

def merge_preset(doc, cfg_preset, bld_preset):
    if doc is None:
        return {
            "version": 3,
            "configurePresets": [cfg_preset],
            "buildPresets": [bld_preset],
        }

    # If file has newer schema that your CMake can't handle, you can still append,
    # but CMake 3.22.1 will choke. We keep the existing version as-is.
    if "configurePresets" not in doc: doc["configurePresets"] = []
    if "buildPresets" not in doc: doc["buildPresets"] = []

    # Replace presets with same name
    doc["configurePresets"] = [p for p in doc["configurePresets"] if p.get("name") != cfg_preset["name"]] + [cfg_preset]
    doc["buildPresets"] = [p for p in doc["buildPresets"] if p.get("name") != bld_preset["name"]] + [bld_preset]
    return doc

def main():
    ap = argparse.ArgumentParser(description="Generate or merge a CMakePresets.json from a .config file")
    ap.add_argument("config", help="Path to .config")
    ap.add_argument("--toolchain", default="cmake/toolchain_arm-none-eabi.cmake", help="Path to toolchain file (relative to repo)")
    ap.add_argument("--presets", default="CMakePresets.json", help="Path to CMakePresets.json to create/merge")
    ap.add_argument("--generator", default="Ninja", help="CMake generator")
    ap.add_argument("--preset-name", default=None, help="Override preset name")
    ap.add_argument("--binary-dir", default=None, help="Override binaryDir")
    ap.add_argument("--display-name", default=None, help="Override displayName")
    args = ap.parse_args()

    kv = parse_config(args.config)
    if not kv:
        print("No settings parsed from .config", file=sys.stderr)
        sys.exit(2)

    target = choose_target(kv)
    cache = to_cache_vars(kv)
    cache = ensure_base_vars(cache, args.toolchain)

    # Build preset objects
    source_dir = "${sourceDir}"  # CMake variable; leave literal
    preset_name = args.preset_name or make_preset_name(target)
    binary_dir = args.binary_dir or make_binary_dir(source_dir, target)
    display_name = args.display_name or f"Linux/WSL ARM ({target})"

    cfg_preset = OrderedDict([
        ("name", preset_name),
        ("displayName", display_name),
        ("generator", args.generator),
        ("binaryDir", binary_dir),
        ("cacheVariables", cache),
    ])
    bld_preset = OrderedDict([
        ("name", preset_name),
        ("configurePreset", preset_name),
        ("jobs", 0),
    ])

    # Ensure schema v3 unless existing file says otherwise
    doc = load_existing_presets(args.presets)
    if doc is None:
        doc = {"version": 3}

    # If existing file has version >3, we *keep it*, but remember your CMake 3.22 understands v3 only.
    if "version" not in doc:
        doc["version"] = 3

    result = merge_preset(doc, cfg_preset, bld_preset)

    # Pretty-print with stable key order
    with open(args.presets, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")

    print(f"Updated {args.presets} with preset '{preset_name}' targeting '{target}'")

if __name__ == "__main__":
    main()
