TEST_UPDATE_VERSION?=2
WOLFBOOT_VERSION?=0
EXPVER=tools/test-expect-version/test-expect-version
EXPVER_CMD=$(EXPVER) /dev/ttyAMA0
BINASSEMBLE=tools/bin-assemble/bin-assemble
SPI_CHIP=SST25VF080B
SPI_OPTIONS=SPI_FLASH=1 WOLFBOOT_PARTITION_SIZE=0x80000 WOLFBOOT_PARTITION_UPDATE_ADDRESS=0x00000 WOLFBOOT_PARTITION_SWAP_ADDRESS=0x80000
SIGN_ARGS=

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

$(EXPVER):
	$(MAKE) -C $(dir $@)

$(BINASSEMBLE):
	$(MAKE) -C $(dir $@)

test-size: FORCE
	$(Q)make clean
	$(Q)make wolfboot.bin
	$(Q)FP=`$(SIZE) -A wolfboot.elf | awk -e ' /Total/ {print $$2;}'`; echo SIZE: $$FP LIMIT: $$LIMIT; test $$FP -le $$LIMIT

# Testbed actions
#
#
# tpm-mute mode is the default
#
tpm-mute:
	@if ! (test -d /sys/class/gpio/gpio7); then echo "7" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio7/direction || true
	@echo "1" >/sys/class/gpio/gpio7/value || true

tpm-unmute:
	@if ! (test -d /sys/class/gpio/gpio7); then echo "7" > /sys/class/gpio/export || true; fi
	@echo "in" >/sys/class/gpio/gpio7/direction || true

testbed-on: FORCE
	@if ! (test -d /sys/class/gpio/gpio4); then echo "4" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio4/direction || true
	@echo "0" >/sys/class/gpio/gpio4/value || true
	@make tpm-mute
	@echo "Testbed on."

testbed-off: FORCE
	@make tpm-mute
	@if ! (test -d /sys/class/gpio/gpio4); then echo "4" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio4/direction || true
	@echo "1" >/sys/class/gpio/gpio4/value || true
	@echo "Testbed off."


test-reset: FORCE
	@(sleep 1 && st-flash reset && sleep 1)&

test-spi-on: FORCE
	@make testbed-off
	@echo "8" >/sys/class/gpio/unexport || true
	@echo "9" >/sys/class/gpio/unexport || true
	@echo "10" >/sys/class/gpio/unexport || true
	@echo "11" >/sys/class/gpio/unexport || true
	@modprobe spi_bcm2835
	#@modprobe spidev

test-spi-off: FORCE
	@rmmod spi_bcm2835 || true
	@rmmod spidev || true
	@echo "8" >/sys/class/gpio/export || true
	@echo "9" >/sys/class/gpio/export || true
	@echo "10" >/sys/class/gpio/export || true
	@echo "11" >/sys/class/gpio/export || true
	@echo "in" >/sys/class/gpio/gpio8/direction || true
	@echo "in" >/sys/class/gpio/gpio9/direction || true
	@echo "in" >/sys/class/gpio/gpio10/direction || true
	@echo "in" >/sys/class/gpio/gpio11/direction || true
	@make testbed-on

test-update: test-app/image.bin FORCE
	@dd if=/dev/zero bs=131067 count=1 2>/dev/null | tr "\000" "\377" > test-update.bin
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	@dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin of=test-update.bin bs=1 conv=notrunc
	@printf "pBOOT" >> test-update.bin
	@make test-reset
	@sleep 2
	@st-flash --reset write test-update.bin 0x08040000 || \
		(make test-reset && sleep 1 && st-flash --reset write test-update.bin 0x08040000) || \
		(make test-reset && sleep 1 && st-flash --reset write test-update.bin 0x08040000)

test-sim-internal-flash-with-update: test-app/image.elf FORCE
	$(Q)$(SIGN_TOOL) $(SIGN_OPTIONS) test-app/image.elf $(PRIVATE_KEY) 1
	$(Q)$(SIGN_TOOL) $(SIGN_OPTIONS) test-app/image.elf $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	$(Q)dd if=/dev/zero bs=$$(($(WOLFBOOT_SECTOR_SIZE))) count=1 2>/dev/null | tr "\000" "\377" > erased_sec.dd
	$(Q)$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.elf $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	$(Q)$(BINASSEMBLE) internal_flash.dd 0  test-app/image_v1_signed.bin \
		$$(($(WOLFBOOT_PARTITION_UPDATE_ADDRESS)-$(WOLFBOOT_PARTITION_BOOT_ADDRESS))) test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin \
		$$(($(WOLFBOOT_PARTITION_UPDATE_ADDRESS)+$(WOLFBOOT_PARTITION_SIZE)-$(WOLFBOOT_PARTITION_BOOT_ADDRESS))) erased_sec.dd

test-sim-update-flash: wolfboot.elf test-sim-internal-flash-with-update FORCE
	$(Q)(test `./wolfboot.elf success update_trigger get_version` -eq 1)
	$(Q)(test `./wolfboot.elf success get_version` -eq $(TEST_UPDATE_VERSION))

test-sim-rollback-flash: wolfboot.elf test-sim-internal-flash-with-update FORCE
	$(Q)(test `./wolfboot.elf success update_trigger get_version` -eq 1)
	$(Q)(test `./wolfboot.elf get_version` -eq $(TEST_UPDATE_VERSION))
	$(Q)(test `./wolfboot.elf success get_version` -eq 1)
	$(Q)(test `./wolfboot.elf get_version` -eq 1)

test-self-update: FORCE
	@mv $(PRIVATE_KEY) private_key.old
	@make clean factory.bin RAM_CODE=1 WOLFBOOT_VERSION=1 SIGN=$(SIGN)
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	@st-flash --reset write test-app/image_v2_signed.bin 0x08020000 || \
		(make test-reset && sleep 1 && st-flash --reset write test-app/image_v2_signed.bin 0x08020000) || \
		(make test-reset && sleep 1 && st-flash --reset write test-app/image_v2_signed.bin 0x08020000)
	@dd if=/dev/zero bs=131067 count=1 2>/dev/null | tr "\000" "\377" > test-self-update.bin
	@$(SIGN_TOOL) $(SIGN_ARGS) --wolfboot-update wolfboot.bin private_key.old $(WOLFBOOT_VERSION)
	@dd if=wolfboot_v$(WOLFBOOT_VERSION)_signed.bin of=test-self-update.bin bs=1 conv=notrunc
	@printf "pBOOT" >> test-self-update.bin
	@st-flash --reset write test-self-update.bin 0x08040000 || \
		(make test-reset && sleep 1 && st-flash --reset write test-self-update.bin 0x08040000) || \
		(make test-reset && sleep 1 && st-flash --reset write test-self-update.bin 0x08040000)

test-update-ext: test-app/image.bin FORCE
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION)
	@(dd if=/dev/zero bs=1M count=1 | tr '\000' '\377' > test-update.rom)
	@dd if=test-app/image_v$(TEST_UPDATE_VERSION)_signed.bin of=test-update.rom bs=1 count=524283 conv=notrunc
	@printf "pBOOT" | dd of=test-update.rom obs=1 seek=524283 count=5 conv=notrunc
	@make test-spi-on || true
	flashrom -c $(SPI_CHIP) -p linux_spi:dev=/dev/spidev0.0 -w test-update.rom
	@make test-spi-off
	@make test-reset
	@sleep 2
	@make clean

test-erase: FORCE
	@echo Mass-erasing the internal flash:
	@make test-reset
	@sleep 2
	@st-flash erase

test-erase-ext: FORCE
	@make test-spi-on || true
	@echo Mass-erasing the external SPI flash:
	flashrom -c $(SPI_CHIP) -p linux_spi:dev=/dev/spidev0.0 -E
	@make test-spi-off || true

test-factory: factory.bin
	@make test-reset
	@sleep 2
	@st-flash --reset write factory.bin 0x08000000 || \
		((make test-reset && sleep 1 && st-flash --reset write factory.bin 0x08000000) || \
		(make test-reset && sleep 1 && st-flash --reset write factory.bin 0x08000000))&

test-resetold: FORCE
	@(sleep 1 && st-info --reset) &




## Test cases:

# Group '0': ED25519 (default)
#
#

test-01-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=1
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@make clean
	@echo TEST PASSED

test-02-forward-update-allow-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory ALLOW_DOWNGRADE=1
	@echo Expecting version '1'
	@(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=2
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 2)
	@make clean
	@echo TEST PASSED

test-03-rollback: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	@(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=5
	@echo Expecting version '5'
	@(test `$(EXPVER_CMD)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@(test `$(EXPVER_CMD)` -eq 4)
	@make clean
	@echo TEST PASSED

# Group '1': ECC
#
#

test-11-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ECC256

test-13-rollback-ECC: $(EXPVER) FORCE
	@make test-03-rollback SIGN=ECC256

# Group '2': SPI flash
#
#

test-21-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory $(SPI_OPTIONS)
	@echo Expecting version '1'
	@(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=1 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@make clean
	@echo TEST PASSED

test-23-rollback-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory $(SPI_OPTIONS)
	@echo Expecting version '1'
	@(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER_CMD)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=5 $(SPI_OPTIONS)
	@echo Expecting version '5'
	@(test `$(EXPVER_CMD)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@sleep 2
	@(test `$(EXPVER_CMD)` -eq 4)
	@make clean
	@echo TEST PASSED

# Group '3,4': bootloader self-update
#
#

test-34-forward-self-update: $(EXPVER) FORCE
	@echo Creating and uploading factory image...
	@make test-factory WOLFBOOT_VERSION=1 RAM_CODE=1 SIGN=$(SIGN)
	@echo Expecting version '1'
	@(test `$(EXPVER_CMD)` -eq 1)
	@echo
	@echo Updating keys, firmware, bootloader
	@make test-self-update WOLFBOOT_VERSION=4 TEST_UPDATE_VERSION=2 RAM_CODE=1 SIGN=$(SIGN)
	@sleep 2
	@echo Expecting version '2'
	@(test `$(EXPVER_CMD)` -eq 2)
	@make clean
	@echo TEST PASSED

test-44-forward-self-update-ECC: $(EXPVER) FORCE
	@make test-34-forward-self-update SIGN=ECC256

# Group '5': RSA 2048 bit
#
#

test-51-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA2048

test-53-rollback-RSA: $(EXPVER) FORCE
	@make test-03-rollback SIGN=RSA2048

# Group '6': wolfTPM
#
#

test-61-forward-update-no-downgrade-TPM: $(EXPVER) FORCE
	@make test-spi-off || true
	@make tpm-unmute
	@make test-01-forward-update-no-downgrade SIGN=ECC256 WOLFTPM=1 TPM2=1
	@make tpm-mute

test-63-rollback-TPM: $(EXPVER) FORCE
	@make test-spi-off || true
	@make tpm-unmute
	@make test-03-rollback SIGN=ECC256 WOLFTPM=1
	@make tpm-mute

# Group '7': RSA 4096 bit, ED448 and RSA 3072
#
#
test-71-forward-update-no-downgrade-RSA-4096: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA4096

test-73-rollback-RSA-4096: $(EXPVER) FORCE
	@make test-03-rollback SIGN=RSA4096

test-74-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ED448

test-75-rollback-ED448: $(EXPVER) FORCE
	@make test-03-rollback SIGN=ED448

test-76-forward-update-no-downgrade-RSA3072: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA3072

test-77-rollback-RSA3072: $(EXPVER) FORCE
	@make test-03-rollback SIGN=RSA3072

# Group '8,9,10,11': SHA3 combined with the five ciphers
#
#

test-81-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ED25519 HASH=SHA3

test-91-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ECC256 HASH=SHA3

test-101-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA2048 HASH=SHA3

test-111-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA4096 HASH=SHA3

test-112-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ED448 HASH=SHA3 IMAGE_HEADER_SIZE=512

test-113-forward-update-no-downgrade-RSA3072-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA3072 HASH=SHA3

# Group 16: TPM with RSA
#
#
test-161-forward-update-no-downgrade-TPM-RSA: $(EXPVER) FORCE
	@make test-spi-off || true
	@make tpm-unmute
	@make test-01-forward-update-no-downgrade SIGN=RSA2048 WOLFTPM=1
	@make tpm-mute

test-163-rollback-TPM-RSA: $(EXPVER) FORCE
	@make test-spi-off || true
	@make tpm-unmute
	@make test-03-rollback SIGN=RSA2048 WOLFTPM=1
	@make tpm-mute

# Group 17: NULL cipher, no signature
#
#

test-171-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=NONE

test-173-rollback-NOSIGN: $(EXPVER) FORCE
	@make test-03-rollback SIGN=NONE

# Groups 20:31,37: Combinations of previous tests with WOLFBOOT_SMALL_STACK
#
#

test-201-smallstack-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade WOLFBOOT_SMALL_STACK=1

test-211-smallstack-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-11-forward-update-no-downgrade-ECC WOLFBOOT_SMALL_STACK=1

test-221-smallstack-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-21-forward-update-no-downgrade-SPI WOLFBOOT_SMALL_STACK=1

test-251-smallstack-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-51-forward-update-no-downgrade-RSA WOLFBOOT_SMALL_STACK=1

test-271-smallstack-forward-update-no-downgrade-RSA4096: $(EXPVER) FORCE
	@make test-71-forward-update-no-downgrade-RSA-4096 WOLFBOOT_SMALL_STACK=1

test-274-smallstack-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-74-forward-update-no-downgrade-ED448 WOLFBOOT_SMALL_STACK=1

test-281-smallstack-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-81-forward-update-no-downgrade-ED25519-SHA3 WOLFBOOT_SMALL_STACK=1

test-291-smallstack-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-91-forward-update-no-downgrade-ECC256-SHA3 WOLFBOOT_SMALL_STACK=1

test-301-smallstack-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-101-forward-update-no-downgrade-RSA2048-SHA3 WOLFBOOT_SMALL_STACK=1

test-311-smallstack-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-111-forward-update-no-downgrade-RSA4096-SHA3 WOLFBOOT_SMALL_STACK=1

test-312-smallstack-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-112-forward-update-no-downgrade-ED448-SHA3 WOLFBOOT_SMALL_STACK=1

test-371-smallstack-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-171-forward-update-no-downgrade-NOSIGN WOLFBOOT_SMALL_STACK=1

# Groups 40:51,57: Combinations of previous tests with USE_FAST_MATH
#
#
test-401-fastmath-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SPMATH=0

test-411-fastmath-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-11-forward-update-no-downgrade-ECC SPMATH=0

test-421-fastmath-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-21-forward-update-no-downgrade-SPI SPMATH=0

test-451-fastmath-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-51-forward-update-no-downgrade-RSA SPMATH=0

test-471-fastmath-forward-update-no-downgrade-RSA4096: $(EXPVER) FORCE
	@make test-71-forward-update-no-downgrade-RSA-4096 SPMATH=0

test-474-fastmath-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-74-forward-update-no-downgrade-ED448 SPMATH=0

test-481-fastmath-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-81-forward-update-no-downgrade-ED25519-SHA3 SPMATH=0

test-491-fastmath-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-91-forward-update-no-downgrade-ECC256-SHA3 SPMATH=0

test-501-fastmath-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-101-forward-update-no-downgrade-RSA2048-SHA3 SPMATH=0

test-511-fastmath-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-111-forward-update-no-downgrade-RSA4096-SHA3 SPMATH=0

test-512-fastmath-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-112-forward-update-no-downgrade-ED448-SHA3 SPMATH=0

test-571-fastmath-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-171-forward-update-no-downgrade-NOSIGN SPMATH=0

# Groups 60:71,77: Combinations of previous tests with NO_ASM
#
#
test-601-no-asm-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade NO_ASM=1

test-611-no-asm-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-11-forward-update-no-downgrade-ECC NO_ASM=1

test-621-no-asm-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-21-forward-update-no-downgrade-SPI NO_ASM=1

test-651-no-asm-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-51-forward-update-no-downgrade-RSA NO_ASM=1

test-671-no-asm-forward-update-no-downgrade-RSA4096: $(EXPVER) FORCE
	@make test-71-forward-update-no-downgrade-RSA-4096 NO_ASM=1

test-674-no-asm-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-74-forward-update-no-downgrade-ED448 NO_ASM=1

test-681-no-asm-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-81-forward-update-no-downgrade-ED25519-SHA3 NO_ASM=1

test-691-no-asm-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-91-forward-update-no-downgrade-ECC256-SHA3 NO_ASM=1

test-701-no-asm-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-101-forward-update-no-downgrade-RSA2048-SHA3 NO_ASM=1

test-711-no-asm-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-111-forward-update-no-downgrade-RSA4096-SHA3 NO_ASM=1

test-712-no-asm-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-112-forward-update-no-downgrade-ED448-SHA3 NO_ASM=1

test-771-no-asm-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-171-forward-update-no-downgrade-NOSIGN NO_ASM=1

# Groups 80:91,97: Combinations of previous tests with NO_ASM
#
#
test-801-no-asm-smallstack-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-811-no-asm-smallstack-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-11-forward-update-no-downgrade-ECC NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-821-no-asm-smallstack-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-21-forward-update-no-downgrade-SPI NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-851-no-asm-smallstack-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-51-forward-update-no-downgrade-RSA NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-871-no-asm-smallstack-forward-update-no-downgrade-RSA4096: $(EXPVER) FORCE
	@make test-71-forward-update-no-downgrade-RSA-4096 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-874-no-asm-smallstack-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-74-forward-update-no-downgrade-ED448 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-881-no-asm-smallstack-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-81-forward-update-no-downgrade-ED25519-SHA3 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-891-no-asm-smallstack-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-91-forward-update-no-downgrade-ECC256-SHA3 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-901-no-asm-smallstack-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-101-forward-update-no-downgrade-RSA2048-SHA3 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-911-no-asm-smallstack-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-111-forward-update-no-downgrade-RSA4096-SHA3 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-912-no-asm-smallstack-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-112-forward-update-no-downgrade-ED448-SHA3 NO_ASM=1 WOLFBOOT_SMALL_STACK=1

test-971-no-asm-smallstack-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-171-forward-update-no-downgrade-NOSIGN NO_ASM=1 WOLFBOOT_SMALL_STACK=1

# Groups 100:111,117: Combinations of previous tests with USE_FAST_MATH
#
#
test-1001-fastmath-smallstack-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1011-fastmath-smallstack-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-11-forward-update-no-downgrade-ECC SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1021-fastmath-smallstack-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-21-forward-update-no-downgrade-SPI SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1051-fastmath-smallstack-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-51-forward-update-no-downgrade-RSA SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1071-fastmath-smallstack-forward-update-no-downgrade-RSA4096: $(EXPVER) FORCE
	@make test-71-forward-update-no-downgrade-RSA-4096 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1074-fastmath-smallstack-forward-update-no-downgrade-ED448: $(EXPVER) FORCE
	@make test-74-forward-update-no-downgrade-ED448 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1081-fastmath-smallstack-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-81-forward-update-no-downgrade-ED25519-SHA3 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1091-fastmath-smallstack-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-91-forward-update-no-downgrade-ECC256-SHA3 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1101-fastmath-smallstack-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-101-forward-update-no-downgrade-RSA2048-SHA3 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1111-fastmath-smallstack-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-111-forward-update-no-downgrade-RSA4096-SHA3 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1112-fastmath-smallstack-forward-update-no-downgrade-ED448-SHA3: $(EXPVER) FORCE
	@make test-112-forward-update-no-downgrade-ED448-SHA3 SPMATH=0 WOLFBOOT_SMALL_STACK=1

test-1171-fastmath-smallstack-forward-update-no-downgrade-NOSIGN: $(EXPVER) FORCE
	@make test-171-forward-update-no-downgrade-NOSIGN SPMATH=0 WOLFBOOT_SMALL_STACK=1



test-base: clean
	@echo BASE Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-01-forward-update-no-downgrade
	make test-02-forward-update-allow-downgrade test-03-rollback
	@make keysclean
	make test-11-forward-update-no-downgrade-ECC test-13-rollback-ECC
	@make keysclean
	make test-21-forward-update-no-downgrade-SPI test-23-rollback-SPI
	make test-34-forward-self-update
	@make keysclean
	make test-44-forward-self-update-ECC
	@make keysclean
	make test-51-forward-update-no-downgrade-RSA
	make test-53-rollback-RSA
	@make keysclean
	make test-61-forward-update-no-downgrade-TPM
	make test-63-rollback-TPM
	@make keysclean
	make test-71-forward-update-no-downgrade-RSA-4096
	make test-73-rollback-RSA-4096
	@make keysclean
	make test-74-forward-update-no-downgrade-ED448
	make test-75-rollback-ED448
	@make keysclean
	make test-76-forward-update-no-downgrade-RSA3072
	make test-77-rollback-RSA3072

test-sha3: clean
	@echo SHA3 Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-81-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-91-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-101-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	make test-111-forward-update-no-downgrade-RSA4096-SHA3
	@make keysclean
	make test-112-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-113-forward-update-no-downgrade-RSA3072-SHA3

test-tpm: clean
	@echo TPM Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-161-forward-update-no-downgrade-TPM-RSA
	make test-163-rollback-TPM-RSA

test-nosign: clean
	@echo SIGN=NONE Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-171-forward-update-no-downgrade-NOSIGN
	make test-173-rollback-NOSIGN

test-smallstack: clean
	@echo WOLFBOOT_SMALL_STACK Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-201-smallstack-forward-update-no-downgrade
	@make keysclean
	make test-211-smallstack-forward-update-no-downgrade-ECC
	@make keysclean
	make test-221-smallstack-forward-update-no-downgrade-SPI
	@make keysclean
	make test-251-smallstack-forward-update-no-downgrade-RSA
	@make keysclean
	make test-271-smallstack-forward-update-no-downgrade-RSA4096
	@make keysclean
	make test-274-smallstack-forward-update-no-downgrade-ED448
	@make keysclean
	make test-281-smallstack-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-291-smallstack-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-301-smallstack-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	make test-311-smallstack-forward-update-no-downgrade-RSA4096-SHA3
	@make keysclean
	make test-312-smallstack-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-371-smallstack-forward-update-no-downgrade-NOSIGN

test-fastmath: clean
	@echo USE_FAST_MATH Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-401-fastmath-forward-update-no-downgrade
	@make keysclean
	make test-411-fastmath-forward-update-no-downgrade-ECC
	@make keysclean
	make test-421-fastmath-forward-update-no-downgrade-SPI
	@make keysclean
	make test-451-fastmath-forward-update-no-downgrade-RSA
	@make keysclean
	true || make test-471-fastmath-forward-update-no-downgrade-RSA4096 #Not enough RAM
	@make keysclean
	make test-474-fastmath-forward-update-no-downgrade-ED448
	@make keysclean
	make test-481-fastmath-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-491-fastmath-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-501-fastmath-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	true || make test-511-fastmath-forward-update-no-downgrade-RSA4096-SHA3 #Not enough RAM
	@make keysclean
	make test-512-fastmath-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-571-fastmath-forward-update-no-downgrade-NOSIGN

test-no-asm: clean
	@echo NO_ASM Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-601-no-asm-forward-update-no-downgrade
	@make keysclean
	make test-611-no-asm-forward-update-no-downgrade-ECC
	@make keysclean
	make test-621-no-asm-forward-update-no-downgrade-SPI
	@make keysclean
	make test-651-no-asm-forward-update-no-downgrade-RSA
	@make keysclean
	make test-671-no-asm-forward-update-no-downgrade-RSA4096
	@make keysclean
	make test-674-no-asm-forward-update-no-downgrade-ED448
	@make keysclean
	make test-681-no-asm-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-691-no-asm-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-701-no-asm-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	make test-711-no-asm-forward-update-no-downgrade-RSA4096-SHA3
	@make keysclean
	make test-712-no-asm-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-771-no-asm-forward-update-no-downgrade-NOSIGN

test-no-asm-smallstack: clean
	@echo NO_ASM+SMALLSTACK Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-801-no-asm-smallstack-forward-update-no-downgrade
	@make keysclean
	make test-811-no-asm-smallstack-forward-update-no-downgrade-ECC
	@make keysclean
	make test-821-no-asm-smallstack-forward-update-no-downgrade-SPI
	@make keysclean
	make test-851-no-asm-smallstack-forward-update-no-downgrade-RSA
	@make keysclean
	make test-871-no-asm-smallstack-forward-update-no-downgrade-RSA4096
	@make keysclean
	make test-874-no-asm-smallstack-forward-update-no-downgrade-ED448
	@make keysclean
	make test-881-no-asm-smallstack-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-891-no-asm-smallstack-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-901-no-asm-smallstack-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	make test-911-no-asm-smallstack-forward-update-no-downgrade-RSA4096-SHA3
	@make keysclean
	make test-912-no-asm-smallstack-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-971-no-asm-smallstack-forward-update-no-downgrade-NOSIGN

test-fastmath-smallstack: clean
	@echo USE_FAST_MATH + WOLFBOOT_SMALL_STACK Tests
	@echo ==========
	@echo
	@echo
	@make keysclean
	make test-1001-fastmath-smallstack-forward-update-no-downgrade
	@make keysclean
	make test-1011-fastmath-smallstack-forward-update-no-downgrade-ECC
	@make keysclean
	make test-1021-fastmath-smallstack-forward-update-no-downgrade-SPI
	@make keysclean
	make test-1051-fastmath-smallstack-forward-update-no-downgrade-RSA
	@make keysclean
	make test-1071-fastmath-smallstack-forward-update-no-downgrade-RSA4096
	@make keysclean
	make test-1074-fastmath-smallstack-forward-update-no-downgrade-ED448
	@make keysclean
	make test-1081-fastmath-smallstack-forward-update-no-downgrade-ED25519-SHA3
	@make keysclean
	make test-1091-fastmath-smallstack-forward-update-no-downgrade-ECC256-SHA3
	@make keysclean
	make test-1101-fastmath-smallstack-forward-update-no-downgrade-RSA2048-SHA3
	@make keysclean
	make test-1111-fastmath-smallstack-forward-update-no-downgrade-RSA4096-SHA3
	@make keysclean
	make test-1112-fastmath-smallstack-forward-update-no-downgrade-ED448-SHA3
	@make keysclean
	make test-1171-fastmath-smallstack-forward-update-no-downgrade-NOSIGN


test-all: clean
	make test-base
	make test-sha3
	make test-tpm
	make test-nosign
	make test-smallstack
	make test-fastmath
	make test-no-asm
	make test-no-asm-smallstack
	make test-fastmath-smallstack
	make test-delta-update


test-size-all:
	make test-size SIGN=NONE LIMIT=4646
	make keysclean
	make test-size SIGN=ED25519 LIMIT=11262
	make keysclean
	make test-size SIGN=ECC256  LIMIT=22134
	make keysclean
	make test-size SIGN=ECC256 NO_ASM=1 LIMIT=13586
	make keysclean
	make test-size SIGN=RSA2048 LIMIT=11038
	make keysclean
	make test-size SIGN=RSA2048 NO_ASM=1 LIMIT=11058
	make keysclean
	make test-size SIGN=RSA4096 LIMIT=11386
	make keysclean
	make test-size SIGN=RSA4096 NO_ASM=1 LIMIT=11314
	make keysclean
	make test-size SIGN=ECC384 LIMIT=17470
	make keysclean
	make test-size SIGN=ECC384 NO_ASM=1 LIMIT=15022
	make keysclean
	make test-size SIGN=ED448 LIMIT=13278
	make keysclean
	make test-size SIGN=RSA3072 LIMIT=11234
	make keysclean
	make test-size SIGN=RSA3072 NO_ASM=1 LIMIT=11154
	make keysclean
