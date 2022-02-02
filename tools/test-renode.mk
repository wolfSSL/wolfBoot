TEST_UPDATE_VERSION?=2
WOLFBOOT_VERSION?=0
TARGET_UART=/tmp/wolfboot.uart
RENODE_PORT=5555
RENODE_OPTIONS=--pid-file=/tmp/renode.pid --hide-monitor
RENODE_CONFIG=tools/renode/stm32f4_discovery_wolfboot.resc

EXPVER=tools/test-expect-version/test-expect-version $(TARGET_UART)
BINASSEMBLE=tools/bin-assemble/bin-assemble

ifneq ("$(wildcard $(WOLFBOOT_ROOT)/tools/keytools/keygen)","")
	KEYGEN_TOOL=$(WOLFBOOT_ROOT)/tools/keytools/keygen
else
	ifneq ("$(wildcard $(WOLFBOOT_ROOT)/tools/keytools/keygen.exe)","")
		KEYGEN_TOOL=$(WOLFBOOT_ROOT)/tools/keytools/keygen.exe
	else
		KEYGEN_TOOL=python3 $(WOLFBOOT_ROOT)/tools/keytools/keygen.py
	endif
endif

ifneq ("$(wildcard $(WOLFBOOT_ROOT)/tools/keytools/sign)","")
	SIGN_TOOL=$(WOLFBOOT_ROOT)/tools/keytools/sign
else
	ifneq ("$(wildcard $(WOLFBOOT_ROOT)/tools/keytools/sign.exe)","")
		SIGN_TOOL=$(WOLFBOOT_ROOT)/tools/keytools/sign.exe
	else
		SIGN_TOOL=python3 $(WOLFBOOT_ROOT)/tools/keytools/sign.py
	endif
endif

ifeq ($(SIGN),NONE)
  SIGN_ARGS+=--no-sign
endif

ifeq ($(SIGN),ED25519)
  SIGN_ARGS+= --ed25519
endif

ifeq ($(SIGN),ED448)
  SIGN_ARGS+= --ed448
endif

ifeq ($(SIGN),ECC256)
  SIGN_ARGS+= --ecc256
endif

ifeq ($(SIGN),RSA2048)
  SIGN_ARGS+= --rsa2048
endif

ifeq ($(SIGN),RSA4096)
  SIGN_ARGS+= --rsa4096
endif

ifeq ($(HASH),SHA256)
  SIGN_ARGS+= --sha256
endif
ifeq ($(HASH),SHA3)
  SIGN_ARGS+= --sha3
endif

$(EXPVER):
	$(MAKE) -C $(dir $@)

$(BINASSEMBLE):
	$(MAKE) -C $(dir $@)

# Testbed actions
#
#
# tpm-mute mode is the default
#

renode-testbed-on: FORCE
	@(renode --disable-xwt -P $(RENODE_PORT) $(RENODE_CONFIG)) &
	@sleep 5
	@echo "Testbed on."

renode-testbed-off: FORCE
	@(echo && echo quit) | nc -q 1 localhost $(RENODE_PORT)
	@echo "Testbed off."

renode-factory: factory.bin test-app/image.bin FORCE
	@rm -f /tmp/wolfboot.uart
	@dd if=/dev/zero bs=131067 count=1 2>/dev/null | tr "\000" "\377" > test-update.bin
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 1
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	@dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin of=/tmp/renode-test-update.bin bs=1 conv=notrunc
	@printf "pBOOT" >> /tmp/renode-test-update.bin
	@cp test-app/image_v1_signed.bin /tmp/renode-test-v1.bin
	@cp wolfboot.elf /tmp/renode-wolfboot.elf
	@renode $(RENODE_OPTIONS) $(RENODE_CONFIG)&
	@while ! (test -e /tmp/wolfboot.uart); do sleep .1; done
	@echo "serial port activated"
	@test `$(EXPVER)` -eq 1
	@echo Waiting for renode to terminate...
	@tail --pid=`cat /tmp/renode.pid` -f /dev/null
	@rm -f /tmp/renode.pid
	@rm -f /tmp/renode-wolfboot.elf
	@rm -f /tmp/renode-test-v1.bin
	@rm -f /tmp/renode-test-update.bin
	@rm -f /tmp/wolfboot.uart

renode-factory-ed448: FORCE
	make renode-factory SIGN=ED448

renode-factory-ecc256: FORCE
	make renode-factory SIGN=ECC256

renode-factory-rsa2048: FORCE
	make renode-factory SIGN=RSA2048

renode-factory-rsa4096: FORCE
	make renode-factory SIGN=RSA4096

renode-factory-all:
	@make clean
	@make renode-factory
	@make clean
	@make renode-factory-ed448
	@make clean
	@make renode-factory-ecc256
	@make clean
	@make renode-factory-rsa2048
	@make clean
	@make renode-factory-rsa4096
	@echo All tests in $@ OK!

