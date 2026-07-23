#!/bin/sh
# Compatibility wrapper. wolfBoot now vendors the canonical driver from
# wolfGlass under tools/sbom/.

set -e
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$SCRIPT_DIR/../sbom/sbom-driver" "$@"
