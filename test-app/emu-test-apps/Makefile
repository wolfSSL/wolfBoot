# Makefile
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
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

-include ../../.config
include ../../tools/config.mk
include ../../options.mk
include ../../tools/test.mk

ifneq ($(wildcard $(WOLFBOOT_ROOT)/lib/wolfssl/wolfcrypt/src/asn.c),)
  WOLFBOOT_LIB_WOLFSSL:=$(WOLFBOOT_ROOT)/lib/wolfssl
else ifneq ($(wildcard $(WOLFBOOT_ROOT)/../wolfssl/wolfcrypt/src/asn.c),)
  WOLFBOOT_LIB_WOLFSSL:=$(WOLFBOOT_ROOT)/../wolfssl
endif
export WOLFBOOT_LIB_WOLFSSL

WOLFBOOT_ROOT?=$(abspath ../..)

EMU_VERSION?=1
EMU_EXPECT_VERSION?=$(EMU_VERSION)
PRIVATE_KEY?=$(WOLFBOOT_ROOT)/wolfboot_signing_private_key.der

M33MU?=$(WOLFBOOT_ROOT)/../m33mu/build/m33mu

# Sign-tool parameters, passed through the environment as the top-level
# Makefile does (post-quantum schemes read their level from here).
SIGN_ENV=IMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE) \
	WOLFBOOT_PARTITION_SIZE=$(WOLFBOOT_PARTITION_SIZE) \
	WOLFBOOT_PARTITION_UPDATE_SIZE=$(WOLFBOOT_PARTITION_UPDATE_SIZE) \
	WOLFBOOT_SECTOR_SIZE=$(WOLFBOOT_SECTOR_SIZE) \
	NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
	ML_DSA_LEVEL=$(ML_DSA_LEVEL) \
	IMAGE_SIGNATURE_SIZE=$(IMAGE_SIGNATURE_SIZE) \
	LMS_LEVELS=$(LMS_LEVELS) \
	LMS_HEIGHT=$(LMS_HEIGHT) \
	LMS_WINTERNITZ=$(LMS_WINTERNITZ) \
	XMSS_PARAMS=$(XMSS_PARAMS)

ifeq ($(TARGET),stm32h563)
  EMU_DIR=stm32h563
  EMU_CPU=stm32h563
else ifeq ($(TARGET),stm32h5)
  EMU_DIR=stm32h563
  EMU_CPU=stm32h563
else ifeq ($(TARGET),stm32u585)
  EMU_DIR=stm32u585
  EMU_CPU=stm32u585
else ifeq ($(TARGET),stm32u5)
  EMU_DIR=stm32u585
  EMU_CPU=stm32u585
else ifeq ($(TARGET),stm32l552)
  EMU_DIR=stm32l552
  EMU_CPU=stm32l552
else ifeq ($(TARGET),stm32l5)
  EMU_DIR=stm32l552
  EMU_CPU=stm32l552
else ifeq ($(TARGET),nrf5340)
  EMU_DIR=nrf5340
  EMU_CPU=nrf5340
else ifeq ($(TARGET),mcxw)
  EMU_DIR=mcxw71
  EMU_CPU=mcxw71c
else ifeq ($(TARGET),mcxw71)
  EMU_DIR=mcxw71
  EMU_CPU=mcxw71c
else ifeq ($(TARGET),m2354)
  EMU_DIR=m2354
  EMU_CPU=m2354
else ifeq ($(TARGET),imx_rt7xx)
  EMU_DIR=imxrt700
  EMU_CPU=imxrt700
else
  EMU_DIR=
  EMU_CPU=
endif

ifeq ($(strip $(EMU_DIR)),)
  $(error Unsupported or unset TARGET=$(TARGET). Use TARGET=stm32h563|stm32h5|stm32u585|stm32u5|stm32l552|stm32l5|nrf5340|mcxw|m2354|imx_rt7xx)
endif

EMU_PATH=$(WOLFBOOT_ROOT)/test-app/emu-test-apps/$(EMU_DIR)

.PHONY: all clean test-emu sign-emu run-emu

all:
	$(MAKE) -C $(EMU_PATH) clean app.bin

sign-emu: all $(WOLFBOOT_ROOT)/wolfboot_signing_private_key.der
	@cp $(EMU_PATH)/app.bin $(EMU_PATH)/image.bin
	$(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) $(EMU_PATH)/image.bin $(PRIVATE_KEY) $(EMU_VERSION)

run-emu: sign-emu
	@echo "[EMU] CPU=$(EMU_CPU) VERSION=$(EMU_VERSION)"
	@$(M33MU) --cpu $(EMU_CPU) --uart-stdout --no-tz --boot-offset=$(IMAGE_HEADER_SIZE) \
		$(EMU_PATH)/image_v$(EMU_VERSION)_signed.bin --timeout 2 \
		| tee $(EMU_PATH)/emu_uart.log
	@grep -q "get_version=$(EMU_EXPECT_VERSION)" $(EMU_PATH)/emu_uart.log

# Entry point for top-level make test-emu
# Builds, signs, runs, and validates UART output.

test-emu: run-emu

clean:
	$(MAKE) -C $(EMU_PATH) clean
	@rm -f $(EMU_PATH)/image.bin $(EMU_PATH)/image_v*_signed.bin $(EMU_PATH)/emu_uart.log

$(WOLFBOOT_ROOT)/wolfboot_signing_private_key.der:
	$(MAKE) -C $(WOLFBOOT_ROOT) WOLFBOOT_LIB_WOLFSSL="$(WOLFBOOT_LIB_WOLFSSL)" wolfboot_signing_private_key.der
