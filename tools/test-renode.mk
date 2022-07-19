TEST_UPDATE_VERSION?=2
WOLFBOOT_VERSION?=0
TMP?=/tmp
RENODE_UART?=$(TMP)/wolfboot.uart
RENODE_LOG?=$(TMP)/wolfboot.log
RENODE_PIDFILE?=$(TMP)/renode.pid
RENODE_UPDATE_FILE=$(TMP)/renode-test-update.bin


RENODE_PORT=55155
RENODE_OPTIONS=--pid-file=$(RENODE_PIDFILE)
RENODE_OPTIONS+=--disable-xwt -P $(RENODE_PORT)
RENODE_CONFIG=tools/renode/stm32f4_discovery_wolfboot.resc
POFF=131067

EXPVER=tools/test-expect-version/test-expect-version
RENODE_EXPVER=$(EXPVER) $(RENODE_UART)
RENODE_BINASSEMBLE=tools/bin-assemble/bin-assemble

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


ifeq ($(TARGET),stm32f7)
  RENODE_CONFIG=tools/renode/stm32f746_wolfboot.resc
  POFF=393211
endif

ifeq ($(TARGET),hifive1)
  RENODE_CONFIG=tools/renode/sifive_fe310_wolfboot.resc
endif

ifeq ($(TARGET),nrf52)
  RENODE_CONFIG=tools/renode/nrf52840_wolfboot.resc
  POFF=262139
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

ifeq ($(SIGN),ECC384)
  SIGN_ARGS+= --ecc384
endif

# Already supported in sign tools, not yet in wolfBoot.
# Currently, a compile-time error is produced if selected.
ifeq ($(SIGN),ECC521)
  SIGN_ARGS+= --ecc521
endif

ifeq ($(SIGN),RSA2048)
  SIGN_ARGS+= --rsa2048
endif

ifeq ($(SIGN),RSA3072)
  SIGN_ARGS+= --rsa3072
endif

ifeq ($(SIGN),RSA4096)
  SIGN_ARGS+= --rsa4096
endif

ifeq ($(HASH),SHA256)
  SIGN_ARGS+= --sha256
endif
ifeq ($(HASH),SHA384)
  SIGN_ARGS+= --sha384
endif
ifeq ($(HASH),SHA3)
  SIGN_ARGS+= --sha3
endif

# Testbed actions
#
#
renode-on: FORCE
	${Q}rm -f $(RENODE_UART)
	${Q}renode $(RENODE_OPTIONS) $(RENODE_CONFIG) 2>&1 > $(RENODE_LOG) &
	${Q}while ! (test -e $(RENODE_UART)); do sleep .1; done
	${Q}echo "Renode up: uart port activated"
	${Q}echo "Renode running: renode has been started."

renode-off-force: FORCE
	${Q}killall renode 2>/dev/null || true
	${Q}killall mono 2>/dev/null || true
	${Q}rm -f $(RENODE_PIDFILE) $(RENODE_LOG) $(RENODE_UART)

renode-off: FORCE
	${Q}echo "Terminating renode..."
	${Q}(echo && echo quit) | nc -q 1 localhost $(RENODE_PORT) > /dev/null
	${Q}tail --pid=`cat $(RENODE_PIDFILE)` -f /dev/null
	${Q}echo "Renode exited."
	${Q}make renode-off-force


$(RENODE_UPDATE_FILE): test-app/image.bin FORCE
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) \
		$(TEST_UPDATE_VERSION)
	${Q}dd if=/dev/zero bs=$(POFF) count=1 2>/dev/null | tr "\000" "\377" \
		> $@
	${Q}dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin \
		of=$@ bs=1 conv=notrunc
	${Q}printf "pBOOT" >> $@

renode-factory: factory.bin test-app/image.bin $(RENODE_UPDATE_FILE) $(EXPVER) FORCE 
	${Q}rm -f $(RENODE_UART)
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 1
	${Q}cp test-app/image_v1_signed.bin $(TMP)/renode-test-v1.bin
	${Q}cp wolfboot.elf $(TMP)/renode-wolfboot.elf
	${Q}make renode-on
	${Q}date +%s.%N > .stime
	${Q}echo "Expecting version 1:"
	${Q}test `$(RENODE_EXPVER)` -eq 1 || (make renode-off && false)
	${Q}date +%s.%N > .etime
	${Q}make renode-off-force
	${Q}sleep 1
	${Q}killall renode 2>/dev/null || true
	${Q}killall mono 2>/dev/null || true
	${Q}rm -f $(RENODE_PIDFILE) $(RENODE_LOG) $(RENODE_UART)
	${Q}rm -f $(TMP)/renode-wolfboot.elf
	${Q}rm -f $(TMP)/renode-test-v1.bin
	${Q}rm -f $(RENODE_UPDATE_FILE)
	${Q}echo $@: BOOT TIME for $(SIGN) on $(TARGET) is $$(echo `cat .etime` - `cat .stime` | bc -l)
	${Q}echo $@: TEST PASSED

renode-update: factory.bin test-app/image.bin $(EXPVER) FORCE
	${Q} test "$(TARGET)" = "nrf52" || (echo && echo " *** Error: only TARGET=nrf52 supported by $@" \
		&& echo && echo && false)
	${Q}rm -f $(RENODE_UART)
	${Q}dd if=/dev/zero bs=$(POFF) count=1 2>/dev/null | tr "\000" "\377" \
		> $(RENODE_UPDATE_FILE)
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 1
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) \
		$(TEST_UPDATE_VERSION)
	${Q}dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin \
		of=$(RENODE_UPDATE_FILE) bs=1 conv=notrunc
	${Q}printf "pBOOT" >> $(RENODE_UPDATE_FILE)
	${Q}cp test-app/image_v1_signed.bin $(TMP)/renode-test-v1.bin
	${Q}cp wolfboot.elf $(TMP)/renode-wolfboot.elf
	${Q}make renode-on
	${Q}echo "Expecting version 1:"
	${Q}test `$(RENODE_EXPVER)` -eq 1 || (make renode-off && false)
	${Q}echo "Expecting version 2:"
	${Q}test `$(RENODE_EXPVER)` -eq $(TEST_UPDATE_VERSION) || \
		(make renode-off && false)
	${Q}make renode-off
	${Q}rm -f $(TMP)/renode-wolfboot.elf
	${Q}rm -f $(TMP)/renode-test-v1.bin
	${Q}rm -f $(RENODE_UPDATE_FILE)
	${Q}echo $@: TEST PASSED

renode-no-downgrade: factory.bin test-app/image.bin $(EXPVER) FORCE
	${Q} test "$(TARGET)" = "nrf52" || (echo && echo " *** Error: only TARGET=nrf52 supported by $@" \
		&& echo && echo && false)
	${Q}rm -f $(RENODE_UART)
	${Q}dd if=/dev/zero bs=$(POFF) count=1 2>/dev/null | tr "\000" "\377" \
		> $(RENODE_UPDATE_FILE)
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 7
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 5
	${Q}dd if=test-app/image_v5_signed.bin \
		of=$(RENODE_UPDATE_FILE) bs=1 conv=notrunc
	${Q}printf "pBOOT" >> $(RENODE_UPDATE_FILE)
	${Q}cp test-app/image_v7_signed.bin $(TMP)/renode-test-v1.bin
	${Q}cp wolfboot.elf $(TMP)/renode-wolfboot.elf
	${Q}make renode-on
	${Q}echo "Expecting version 7:"
	${Q}test `$(RENODE_EXPVER)` -eq 7 || (make renode-off && false)
	${Q}echo "Expecting version 7:"
	${Q}test `$(RENODE_EXPVER)` -eq 7 || (make renode-off && false)
	${Q}make renode-off
	${Q}rm -f $(TMP)/renode-wolfboot.elf
	${Q}rm -f $(TMP)/renode-test-v1.bin
	${Q}rm -f $(RENODE_UPDATE_FILE)
	${Q}echo $@: TEST PASSED

renode-corrupted: factory.bin test-app/image.bin $(EXPVER) FORCE
	${Q} test "$(TARGET)" = "nrf52" || (echo && echo " *** Error: only TARGET=nrf52 supported by $@" \
		&& echo && echo && false)
	${Q}rm -f $(RENODE_UART)
	${Q}dd if=/dev/zero bs=$(POFF) count=1 2>/dev/null | tr "\000" "\377" \
		> $(RENODE_UPDATE_FILE)
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) 1
	${Q}$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) \
		$(TEST_UPDATE_VERSION)
	${Q}dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin \
		of=$(RENODE_UPDATE_FILE) bs=1 conv=notrunc
	${Q}dd if=/dev/zero bs=1 count=6 seek=1040 conv=notrunc of=$(RENODE_UPDATE_FILE) 2>/dev/null
	${Q}printf "pBOOT" >> $(RENODE_UPDATE_FILE)
	${Q}cp test-app/image_v1_signed.bin $(TMP)/renode-test-v1.bin
	${Q}cp wolfboot.elf $(TMP)/renode-wolfboot.elf
	${Q}make renode-on
	${Q}echo "Expecting version 1:"
	${Q}test `$(RENODE_EXPVER)` -eq 1 || (make renode-off && false)
	${Q}echo "Expecting version 1:"
	${Q}test `$(RENODE_EXPVER)` -eq 1 || (make renode-off && false)
	${Q}make renode-off
	${Q}rm -f $(TMP)/renode-wolfboot.elf
	${Q}rm -f $(TMP)/renode-test-v1.bin
	${Q}rm -f $(RENODE_UPDATE_FILE)
	${Q}echo $@: TEST PASSED

renode-factory-ed25519: FORCE
	make renode-factory SIGN=ED25519

renode-factory-ed448: FORCE
	make renode-factory SIGN=ED448

renode-factory-ecc256: FORCE
	make renode-factory SIGN=ECC256

renode-factory-ecc384: FORCE
	make renode-factory SIGN=ECC384

renode-factory-rsa2048: FORCE
	make renode-factory SIGN=RSA2048

renode-factory-rsa3072: FORCE
	make renode-factory SIGN=RSA3072

renode-factory-rsa4096: FORCE
	make renode-factory SIGN=RSA4096

renode-factory-all: FORCE
	${Q}make keysclean
	${Q}make renode-factory-ed25519
	${Q}make keysclean
	${Q}make renode-factory-ed448 RENODE_PORT=55156
	${Q}make keysclean
	${Q}make renode-factory-ecc256 RENODE_PORT=55157
	${Q}make keysclean
	${Q}make renode-factory-ecc384 RENODE_PORT=55158
	${Q}make keysclean
	${Q}make renode-factory-rsa2048 RENODE_PORT=55160
	${Q}make keysclean
	${Q}make renode-factory-rsa3072 RENODE_PORT=55161
	${Q}make keysclean
	${Q}make renode-factory-rsa4096 RENODE_PORT=55162
	${Q}make keysclean
	${Q}make renode-factory SIGN=NONE RENODE_PORT=55163
	${Q}echo All tests in $@ OK!

renode-update-ed25519: FORCE
	make renode-update SIGN=ED25519

renode-update-ed448: FORCE
	make renode-update SIGN=ED448

renode-update-ecc256: FORCE
	make renode-update SIGN=ECC256

renode-update-ecc384: FORCE
	make renode-update SIGN=ECC384

renode-update-rsa2048: FORCE
	make renode-update SIGN=RSA2048

renode-update-rsa3072: FORCE
	make renode-update SIGN=RSA3072

renode-update-rsa4096: FORCE
	make renode-update SIGN=RSA4096

renode-no-downgrade-ed25519: FORCE
	make renode-no-downgrade SIGN=ED448

renode-no-downgrade-ed448: FORCE
	make renode-no-downgrade SIGN=ED448

renode-no-downgrade-ecc256: FORCE
	make renode-no-downgrade SIGN=ECC256

renode-no-downgrade-ecc384: FORCE
	make renode-no-downgrade SIGN=ECC384

renode-no-downgrade-rsa2048: FORCE
	make renode-no-downgrade SIGN=RSA2048

renode-no-downgrade-rsa4096: FORCE
	make renode-no-downgrade SIGN=RSA4096

renode-corrupted-ed25519: FORCE
	make renode-corrupted SIGN=ED448

renode-corrupted-ed448: FORCE
	make renode-corrupted SIGN=ED448

renode-corrupted-ecc256: FORCE
	make renode-corrupted SIGN=ECC256

renode-corrupted-ecc384: FORCE
	make renode-corrupted SIGN=ECC384

renode-corrupted-rsa2048: FORCE
	make renode-corrupted SIGN=RSA2048

renode-corrupted-rsa4096: FORCE
	make renode-corrupted SIGN=RSA4096

renode-boot-time-all: FORCE
	tools/scripts/renode-test-all.sh 2>/dev/null |grep "BOOT TIME" 

renode-update-all: FORCE
	${Q}make keysclean
	${Q}make renode-update-ed25519 RENODE_PORT=55155
	${Q}make keysclean
	${Q}make renode-update-ed448 RENODE_PORT=55156
	${Q}make keysclean
	${Q}make renode-update-ecc256 RENODE_PORT=55157
	${Q}make keysclean
	${Q}make renode-update-ecc384 RENODE_PORT=55158
	${Q}make keysclean
	${Q}make renode-update-rsa2048 RENODE_PORT=55160
	${Q}make keysclean
	${Q}make renode-update-rsa3072 RENODE_PORT=55161
	${Q}make keysclean
	${Q}make renode-update-rsa4096 RENODE_PORT=55162
	${Q}make keysclean
	${Q}make renode-update SIGN=NONE RENODE_PORT=55163
	${Q}echo All tests in $@ OK!

renode-no-downgrade-all: FORCE
	${Q}make keysclean
	${Q}make renode-no-downgrade-ed25519 RENODE_PORT=55155
	${Q}make keysclean
	${Q}make renode-no-downgrade-ed448 RENODE_PORT=55156
	${Q}make keysclean
	${Q}make renode-no-downgrade-ecc256 RENODE_PORT=55157
	${Q}make keysclean
	${Q}make renode-no-downgrade-ecc384 RENODE_PORT=55158
	${Q}make keysclean
	${Q}make renode-no-downgrade-rsa2048 RENODE_PORT=55160
	${Q}make keysclean
	${Q}make renode-no-downgrade-rsa3072 RENODE_PORT=55161
	${Q}make keysclean
	${Q}make renode-no-downgrade-rsa4096 RENODE_PORT=55162
	${Q}make keysclean
	${Q}make renode-no-downgrade SIGN=NONE RENODE_PORT=55163
	${Q}echo All tests in $@ OK!

renode-corrupted-all: FORCE
	${Q}make keysclean
	${Q}make renode-corrupted-ed25519 RENODE_PORT=55155
	${Q}make keysclean
	${Q}make renode-corrupted-ed448 RENODE_PORT=55156
	${Q}make keysclean
	${Q}make renode-corrupted-ecc256 RENODE_PORT=55157
	${Q}make keysclean
	${Q}make renode-corrupted-ecc384 RENODE_PORT=55158
	${Q}make keysclean
	${Q}make renode-corrupted-rsa2048 RENODE_PORT=55160
	${Q}make keysclean
	${Q}make renode-corrupted-rsa3072 RENODE_PORT=55161
	${Q}make keysclean
	${Q}make renode-corrupted-rsa4096 RENODE_PORT=55162
	${Q}make keysclean
	${Q}make renode-corrupted SIGN=NONE RENODE_PORT=55163
	${Q}echo All tests in $@ OK!

renode-update-all-armored: FORCE
	${Q}make renode-update-all ARMORED=1

renode-update-all-smallstack: FORCE
	${Q}make renode-update-all WOLFBOOT_SMALL_STACK=1

renode-update-all-smallstack-noasm: FORCE
	${Q}make renode-update-all WOLFBOOT_SMALL_STACK=1 NO_ASM=1

renode-update-all-fastmath: FORCE
	${Q}make renode-update-all SPMATH=0

renode-update-all-smallstack-fastmath: FORCE
	${Q}make renode-update-all SPMATH=0 WOLFBOOT_SMALL_STACK=1
