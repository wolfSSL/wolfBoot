# Makefile - top-level wrapper for the wolfHSM TrustZone demo on STM32H5
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# This Makefile is a convenience wrapper around the top-level wolfBoot
# build. It stages the stm32h5-tz-wolfhsm config, builds the secure
# wolfBoot image (with the wolfHSM server linked in) and the non-secure
# test application (which connects to the server via the ARMv8-M NSC
# bridge and runs whTest_ClientConfig), and stages both binaries here
# for flashing.
#
# Quick start:
#   make                  # build wolfboot.bin + test app, stage in ./out/
#   ./load.sh             # flash to a NUCLEO-H563ZI and open a serial console
#   make emu              # run the m33mu emulator over the built binaries
#   make clean            # drop staged artifacts (keeps the wolfBoot tree)
#   make distclean        # also wipe wolfBoot build state

PORT_DIR        := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WOLFBOOT_ROOT   := $(abspath $(PORT_DIR)/../../..)
CONFIG_EXAMPLE  := $(WOLFBOOT_ROOT)/config/examples/stm32h5-tz-wolfhsm.config
CONFIG_TARGET   := $(WOLFBOOT_ROOT)/.config
OUT_DIR         := $(PORT_DIR)/out

# Default to the lib/wolfHSM submodule (matches wolfBoot's own
# Makefile). Override with `make WOLFBOOT_LIB_WOLFHSM=...` to point
# at a different wolfHSM checkout.
WOLFBOOT_LIB_WOLFHSM ?= $(WOLFBOOT_ROOT)/lib/wolfHSM

WOLFBOOT_BIN    := $(WOLFBOOT_ROOT)/wolfboot.bin
TEST_APP_BIN    := $(WOLFBOOT_ROOT)/test-app/image_v1_signed.bin

# Address the test app must be flashed to. Read from the staged .config
# so it stays in sync with what wolfBoot itself was built against.
BOOT_ADDR       := 0x08060000

# Forward extra options to the wolfBoot top-level make. Example:
#   make WOLFBOOT_MAKE_FLAGS='V=1'
WOLFBOOT_MAKE_FLAGS ?=

.PHONY: all build stage clean distclean emu flash help

all: build stage

# Stage the config and run the wolfBoot top-level build. wolfBoot itself
# emits wolfboot.bin (secure image) and test-app/image_v1_signed.bin
# (the non-secure test application). With WOLFCRYPT_TZ_WOLFHSM=1 in the
# staged config, the non-secure app calls cmd_wolfhsm_test(), which
# initialises the wolfHSM client over the NSC bridge and runs the
# wolfHSM client test suite against the in-secure-world server.
#
# We `cd` into WOLFBOOT_ROOT rather than using `$(MAKE) -C ...` because
# the wolfBoot sign step shells out with `./tools/keytools/sign`, which
# resolves against the original process cwd. From a sub-directory the
# relative path fails; cd-ing keeps the resolution correct.
build: stage-config
	@echo "==> wolfBoot build (stm32h5-tz-wolfhsm)"
	@echo "==> WOLFBOOT_LIB_WOLFHSM=$(WOLFBOOT_LIB_WOLFHSM)"
	cd $(WOLFBOOT_ROOT) && $(MAKE) wolfboot.bin WOLFBOOT_LIB_WOLFHSM=$(WOLFBOOT_LIB_WOLFHSM) $(WOLFBOOT_MAKE_FLAGS)
	cd $(WOLFBOOT_ROOT) && $(MAKE) test-app/image_v1_signed.bin WOLFBOOT_LIB_WOLFHSM=$(WOLFBOOT_LIB_WOLFHSM) $(WOLFBOOT_MAKE_FLAGS)

# Force-overwrite .config every build so a stale config from a
# previous CI step (PKCS11, fwTPM, DICE) cannot bleed through.
.PHONY: stage-config
stage-config:
	@echo "==> Staging config: $(CONFIG_EXAMPLE) -> $(CONFIG_TARGET)"
	cp $(CONFIG_EXAMPLE) $(CONFIG_TARGET)

# Copy the produced binaries into ./out so this directory is the single
# place the user has to look. Also drop the boot address into a small
# manifest so load.sh and CI scripts do not have to re-parse .config.
stage: build
	@mkdir -p $(OUT_DIR)
	cp $(WOLFBOOT_BIN) $(OUT_DIR)/wolfboot.bin
	cp $(TEST_APP_BIN) $(OUT_DIR)/image_v1_signed.bin
	@echo "BOOT_ADDR=$(BOOT_ADDR)" > $(OUT_DIR)/manifest.env
	@echo "WOLFBOOT_BIN=$(OUT_DIR)/wolfboot.bin" >> $(OUT_DIR)/manifest.env
	@echo "TEST_APP_BIN=$(OUT_DIR)/image_v1_signed.bin" >> $(OUT_DIR)/manifest.env
	@echo "==> Staged in $(OUT_DIR):"
	@ls -l $(OUT_DIR)

# Convenience: invoke load.sh from anywhere.
flash: stage
	$(PORT_DIR)/load.sh

# Run the wolfBoot m33mu emulator harness over the produced binaries.
# This uses wolfBoot's own emulator script so the test path is identical
# to what runs in CI.
emu: stage
	cd $(WOLFBOOT_ROOT)/test-app/emu-test-apps && \
	    TARGET=stm32h5 ./test.sh

clean:
	rm -rf $(OUT_DIR)

distclean: clean
	cd $(WOLFBOOT_ROOT) && $(MAKE) clean distclean
	rm -f $(CONFIG_TARGET)

help:
	@echo "Targets:"
	@echo "  make            Build wolfboot.bin + signed test app, stage in ./out"
	@echo "  make flash      Build (if needed) and flash via load.sh"
	@echo "  make emu        Build (if needed) and run wolfBoot m33mu harness"
	@echo "  make clean      Drop ./out"
	@echo "  make distclean  Also clean the wolfBoot tree and drop .config"
