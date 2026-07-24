#!/bin/sh
# Build and push the wolfBoot Renesas RX CI container image.
#
# Prerequisites (one-time / per refresh):
#   1. Download the GNURX ELF Linux toolchain (login required, free account) from
#      https://llvm-gcc-renesas.com  (e.g. gcc-14.2.0.202511-GNURX-ELF for Linux)
#      and produce a gzipped tar of the toolchain tree named gnurx-linux.tar.gz
#      in this directory. If the download is a .run self-extractor:
#        sh gcc-*-GNURX-ELF.run --noexec --keep --target ./gnurx
#        tar czf gnurx-linux.tar.gz -C gnurx .
#      If it is already a .tar.gz of the toolchain, just rename/copy it here.
#   2. Log in to GHCR with a token that has write:packages for the wolfSSL org:
#        echo "$GHCR_TOKEN" | docker login ghcr.io -u <user> --password-stdin
#      (or: gh auth refresh -s write:packages && gh auth token | docker login \
#            ghcr.io -u <user> --password-stdin)
#
# Usage: ./docker-build-push.sh [tag]   (tag defaults to v1.0; must match the tag
#                                      in ../../workflows/test-build-renesas-rx.yml)
set -eu
IMAGE=ghcr.io/wolfssl/wolfboot-ci-rx
TAG="${1:-v1.0}"

cd "$(dirname "$0")"
[ -f gnurx-linux.tar.gz ] || {
    echo "error: gnurx-linux.tar.gz not found in $(pwd) (see step 1 above)" >&2
    exit 1
}

docker buildx build --platform linux/amd64 -t "${IMAGE}:${TAG}" --push .
echo "Pushed ${IMAGE}:${TAG}"
echo "Now enable the jobs: set repo variable ENABLE_RENESAS_RX_CI=true on wolfSSL/wolfBoot."
