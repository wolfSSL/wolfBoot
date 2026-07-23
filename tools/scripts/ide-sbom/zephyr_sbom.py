#!/usr/bin/env python3
"""Compatibility wrapper for the vendored wolfGlass Zephyr frontend."""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(SCRIPT_DIR, "..", "..", "sbom", "frontends", "zephyr_sbom.py")
ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", ".."))


def with_default(argv, flag, value):
    if flag in argv:
        return argv
    return [*argv, flag, value]


args = sys.argv[1:]
args = with_default(args, "--name", "wolfboot-zephyr")
args = with_default(args, "--cmakelists",
                    os.path.join(ROOT, "zephyr", "CMakeLists.txt"))
args = with_default(args, "--version-file",
                    os.path.join(ROOT, "include", "wolfboot", "version.h"))
args = with_default(args, "--version-macro", "LIBWOLFBOOT_VERSION_STRING")
os.execv(sys.executable, [sys.executable, TARGET, *args])
