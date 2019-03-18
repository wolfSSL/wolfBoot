TEST_UPDATE_VERSION?=2
EXPVER=tools/test-expect-version/test-expect-version
SPI_CHIP=SST25VF080B

$(EXPVER):
	make -C tools/test-expect-version

# Testbed actions
#
#

test-spi-on: FORCE
	@echo "8" >/sys/class/gpio/unexport || true
	@echo "9" >/sys/class/gpio/unexport || true
	@echo "10" >/sys/class/gpio/unexport || true
	@echo "11" >/sys/class/gpio/unexport || true
	@modprobe spi_bcm2835
	@modprobe spidev

test-spi-off: FORCE
	@rmmod spi_bcm2835
	@rmmod spidev
	@echo "8" >/sys/class/gpio/export
	@echo "9" >/sys/class/gpio/export
	@echo "10" >/sys/class/gpio/export
	@echo "11" >/sys/class/gpio/export
	@echo "in" >/sys/class/gpio/gpio8/direction
	@echo "in" >/sys/class/gpio/gpio9/direction
	@echo "in" >/sys/class/gpio/gpio10/direction
	@echo "in" >/sys/class/gpio/gpio11/direction


test-update: test-app/image.bin FORCE
	@$(SIGN_TOOL) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION) 131072 >/dev/null
	@dd if=test-app/image.bin.v$(TEST_UPDATE_VERSION).signed of=test-update.bin bs=1 count=131067
	@printf "pBOOT" >> test-update.bin
	@make test-reset
	@sleep 2
	@st-flash --reset write test-update.bin 0x08040000 || \
		(make test-reset && sleep 1 && st-flash --reset write test-update.bin 0x08040000) || \
		(make test-reset && sleep 1 && st-flash --reset write test-update.bin 0x08040000)

test-update-ext: test-app/image.bin FORCE
	@$(SIGN_TOOL) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION) 524288 >/dev/null
	@$$(dd if=/dev/zero bs=1M count=1 | tr '\000' '\377' > test-update.rom)
	@dd if=test-app/image.bin.v$(TEST_UPDATE_VERSION).signed of=test-update.rom bs=1 count=524283 conv=notrunc
	@printf "pBOOT" | dd of=test-update.rom obs=1 seek=524283 count=5 conv=notrunc
	@make test-spi-on
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
	@make test-spi-on
	@echo Mass-erasing the external SPI flash:
	flashrom -c $(SPI_CHIP) -p linux_spi:dev=/dev/spidev0.0 -E
	@make test-spi-off


test-factory: factory.bin
	@make test-reset
	@sleep 2
	@st-flash --reset write factory.bin 0x08000000 || \
		(make test-reset && sleep 1 && st-flash --reset write factory.bin 0x08000000) || \
		(make test-reset && sleep 1 && st-flash --reset write factory.bin 0x08000000)

test-reset: FORCE
	@$$(sleep 1 && st-info --reset) &



## Test cases:

test-01-forward-update-no-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	@$$(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=1
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-02-forward-update-allow-downgrade: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory ALLOW_DOWNGRADE=1
	@echo Expecting version '1'
	@$$(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=2
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 2)
	@make clean
	@echo TEST PASSED

test-03-rollback: $(EXPVER) FORCE
	@make test-erase
	@echo Creating and uploading factory image...
	@make test-factory
	@echo Expecting version '1'
	@$$(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=4
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update TEST_UPDATE_VERSION=5
	@echo Expecting version '5'
	@$$(test `$(EXPVER)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@$$(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-11-forward-update-no-downgrade-ECC: $(EXPVER) FORCE
	@make test-01-forward-update-no-downgrade SIGN=ECC256

test-13-rollback-ECC: $(EXPVER) FORCE
	@make test-03-rollback SIGN=ECC256

test-21-forward-update-no-downgrade-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory SPI_FLASH=1
	@echo Expecting version '1'
	@$$(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 SPI_FLASH=1
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=1 SPI_FLASH=1
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-23-rollback-SPI: $(EXPVER) FORCE
	@make test-erase-ext
	@echo Creating and uploading factory image...
	@make test-factory SPI_FLASH=1
	@echo Expecting version '1'
	@$$(test `$(EXPVER)` -eq 1)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=4 SPI_FLASH=1
	@echo Expecting version '4'
	@$$(test `$(EXPVER)` -eq 4)
	@echo
	@echo Creating and uploading update image...
	@make test-update-ext TEST_UPDATE_VERSION=5 SPI_FLASH=1
	@echo Expecting version '5'
	@$$(test `$(EXPVER)` -eq 5)
	@echo
	@echo Resetting to trigger rollback...
	@make test-reset
	@sleep 2
	@$$(test `$(EXPVER)` -eq 4)
	@make clean
	@echo TEST PASSED

test-all: clean test-01-forward-update-no-downgrade test-02-forward-update-allow-downgrade test-03-rollback test-11-forward-update-no-downgrade-ECC test-13-rollback-ECC test-21-forward-update-no-downgrade-SPI test-23-rollback-SPI
