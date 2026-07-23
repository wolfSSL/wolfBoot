#!/usr/bin/env python3
"""Compatibility wrapper for the vendored wolfGlass SBOM validator."""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(SCRIPT_DIR, "..", "..", "sbom", "validate_sbom.py")

args = sys.argv[1:]
if "--name-prefix" not in args:
    args = ["--name-prefix", "wolfboot", *args]
os.execv(sys.executable, [sys.executable, TARGET, *args])
