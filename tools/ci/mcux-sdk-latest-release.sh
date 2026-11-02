#!/bin/sh
# mcux-sdk-latest-release.sh -- print the newest MCUXpresso SDK release tag
# (vYY.MM.NN) of the west manifests, skipping previews (-pvw) and LTS tags.
# The manifests' default branch tracks the next preview and can name commits
# that are not published yet, so CI checks out a release instead.

set -e

URL=https://github.com/nxp-mcuxpresso/mcuxsdk-manifests.git

TAG=$(git ls-remote --tags --refs "$URL" 'v*' | sed 's#.*refs/tags/##' |
    grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1)
if [ -z "$TAG" ]; then
    echo "no MCUXpresso SDK release tag found at $URL" >&2
    exit 1
fi
echo "$TAG"
