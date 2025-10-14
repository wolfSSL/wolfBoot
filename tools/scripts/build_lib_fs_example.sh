#!/bin/sh

# build_lib_fs_example.sh
# Automates the build and verification steps for the wolfBoot lib-fs example.
# Run this script from the root of your wolfBoot project.

set -e

echo "Step 1: Setting simulation config and building signed boot partition..."
cp config/examples/sim.config .config
make clean
make

if [ ! -f internal_flash.dd ]; then
    echo "Error: internal_flash.dd not found. Build may have failed."
    exit 1
fi

echo "Step 2: Switching to library_fs config..."
cp config/examples/library_fs.config .config

echo "Step 3: Cleaning previous build artifacts and building lib-fs..."
make clean
make lib-fs

if [ ! -f lib-fs ]; then
    echo "Error: lib-fs executable not found. Build may have failed."
    exit 1
fi

echo "Step 4: Marking BOOT partition as SUCCESS..."
./lib-fs success

echo "Step 5: Verifying BOOT partition integrity and authenticity..."
./lib-fs verify-boot

echo "Build and verification steps complete."
