TEST_UPDATE_VERSION?=2
WOLFBOOT_VERSION?=0
EXPVER=tools/test-expect-version/test-expect-version
SPI_CHIP=SST25VF080B
SPI_OPTIONS=SPI_FLASH=1 WOLFBOOT_PARTITION_SIZE=0x80000 WOLFBOOT_PARTITION_UPDATE_ADDRESS=0x00000 WOLFBOOT_PARTITION_SWAP_ADDRESS=0x80000
SIGN_ARGS=

ifneq ("$(wildcard ./tools/keytools/keygen)","")
	KEYGEN_TOOL=./tools/keytools/keygen
else
	ifneq ("$(wildcard ./tools/keytools/keygen.exe)","")
		KEYGEN_TOOL=./tools/keytools/keygen.exe
	else
		KEYGEN_TOOL=python3 ./tools/keytools/keygen.py
	endif
endif

ifneq ("$(wildcard ./tools/keytools/sign)","")
	SIGN_TOOL=./tools/keytools/sign
else
	ifneq ("$(wildcard ./tools/keytools/sign.exe)","")
		SIGN_TOOL=./tools/keytools/sign.exe
	else
		SIGN_TOOL=python3 ./tools/keytools/sign.py
	endif
endif

ifeq ($(SIGN),ED25519)
  SIGN_ARGS+= --ed25519
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
	make -C tools/test-expect-version

# Testbed actions
#
#
# tpm-mute mode is the default
#
tpm-mute:
	@if ! (test -d /sys/class/gpio/gpio7); then echo "7" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio7/direction
	@echo "1" >/sys/class/gpio/gpio7/value || true

tpm-unmute:
	@if ! (test -d /sys/class/gpio/gpio7); then echo "7" > /sys/class/gpio/export || true; fi
	@echo "in" >/sys/class/gpio/gpio7/direction

testbed-on: FORCE
	@if ! (test -d /sys/class/gpio/gpio4); then echo "4" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio4/direction
	@echo "0" >/sys/class/gpio/gpio4/value || true
	@make tpm-mute
	@echo "Testbed on."

testbed-off: FORCE
	@make tpm-mute
	@if ! (test -d /sys/class/gpio/gpio4); then echo "4" > /sys/class/gpio/export || true; fi
	@echo "out" >/sys/class/gpio/gpio4/direction
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
	@modprobe spidev

test-spi-off: FORCE
	@rmmod spi_bcm2835 || true
	@rmmod spidev || true
	@echo "8" >/sys/class/gpio/export || true
	@echo "9" >/sys/class/gpio/export || true
	@echo "10" >/sys/class/gpio/export || true
	@echo "11" >/sys/class/gpio/export || true
	@echo "in" >/sys/class/gpio/gpio8/direction
	@echo "in" >/sys/class/gpio/gpio9/direction
	@echo "in" >/sys/class/gpio/gpio10/direction
	@echo "in" >/sys/class/gpio/gpio11/direction
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

test-self-update: wolfboot.bin test-app/image.bin FORCE
	@mv $(PRIVATE_KEY) private_key.old
	@make clean
	@rm src/*_pub_key.c
	@make factory.bin RAM_CODE=1 WOLFBOOT_VERSION=$(WOLFBOOT_VERSION) SIGN=$(SIGN)
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

test-01-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=1
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-02-forward-update-allow-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory ALLOW_DOWNGRADE=1
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=2
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 2)
	@make clean
	@echo TEST PASSED

test-03-rollback: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=5
	@echo Expecting version '5'
	@(test `$(EXPVER)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-11-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ECC256

test-13-rollback-ECC: $(EXPVER) FORCE
	@make test-03-rollback SIGN=ECC256

test-21-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory $(SPI_OPTIONS)
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=1 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-23-rollback-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory $(SPI_OPTIONS)
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 $(SPI_OPTIONS)
	@echo Expecting version '4'
	@(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=5 $(SPI_OPTIONS)
	@echo Expecting version '5'
	@(test `$(EXPVER)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@sleep 2
	@(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-34-forward-self-update: $(EXPVER) FORCE
	@echo Creating and uploading factory image...
	@make clean
	@make distclean
	@make test-factory RAM_CODE=1 SIGN=$(SIGN)
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@echo
	@echo Updating keys, firmware, bootloader
	@make test-self-update WOLFBOOT_VERSION=4 RAM_CODE=1 SIGN=$(SIGN)
	@sleep 2
	@(test `$(EXPVER)` -eq 2)
	@make clean
	@echo TEST PASSED

test-44-forward-self-update-ECC: $(EXPVER) FORCE
	@make test-34-forward-self-update SIGN=ECC256

test-51-forward-update-no-downgrade-RSA: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA2048

test-53-rollback-RSA: $(EXPVER) FORCE
	@make test-03-rollback SIGN=RSA2048

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

test-71-forward-update-no-downgrade-RSA-4096: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA4096

test-73-rollback-RSA-4096: $(EXPVER) FORCE
	@make test-03-rollback SIGN=RSA4096

test-81-forward-update-no-downgrade-ED25519-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ED25519 HASH=SHA3

test-91-forward-update-no-downgrade-ECC256-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ECC256 HASH=SHA3

test-101-forward-update-no-downgrade-RSA2048-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA2048 HASH=SHA3

test-111-forward-update-no-downgrade-RSA4096-SHA3: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=RSA4096 HASH=SHA3

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

test-all: clean test-01-forward-update-no-downgrade test-02-forward-update-allow-downgrade test-03-rollback \
	test-11-forward-update-no-downgrade-ECC test-13-rollback-ECC test-21-forward-update-no-downgrade-SPI test-23-rollback-SPI \
	test-34-forward-self-update \
	test-44-forward-self-update-ECC \
	test-51-forward-update-no-downgrade-RSA \
	test-53-rollback-RSA \
	test-61-forward-update-no-downgrade-TPM \
	test-63-rollback-TPM \
	test-71-forward-update-no-downgrade-RSA-4096 \
	test-73-rollback-RSA-4096 \
	test-81-forward-update-no-downgrade-ED25519-SHA3 \
	test-91-forward-update-no-downgrade-ECC256-SHA3 \
	test-101-forward-update-no-downgrade-RSA2048-SHA3 \
	test-111-forward-update-no-downgrade-RSA4096-SHA3 \
	test-161-forward-update-no-downgrade-TPM-RSA \
	test-163-rollback-TPM-RSA
