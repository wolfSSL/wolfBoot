test-delta-update:SIGN_ARGS?=--ecc256
test-delta-update:SIGN_DELTA_ARGS?=--ecc256 --encrypt /tmp/enc_key.der
test-delta-update:USBTTY?=/dev/ttyACM0
test-delta-update:EXPVER=tools/test-expect-version/test-expect-version /dev/ttyACM0

test-delta-update-ext:SIGN_ARGS?=--ecc256
test-delta-update-ext:SIGN_DELTA_ARGS?=--ecc256 --encrypt /tmp/enc_key.der
test-delta-update-ext:USBTTY?=/dev/ttyACM0
test-delta-update-ext:TIMEOUT?=50
test-delta-update-ext:EXPVER=tools/test-expect-version/test-expect-version /dev/ttyACM0

test-delta-update: factory.bin test-app/image.bin tools/uart-flash-server/ufserver tools/delta/bmdiff tools/test-expect-version/test-expect-version
	@st-flash reset
	@sleep 2
	@st-flash erase || st-flash erase
	@diff .config config/examples/stm32wb-delta.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-delta.config to .config to run this test\n\n" && exit 1)
	$(SIGN_TOOL) $(SIGN_ARGS) --delta test-app/image_v1_signed.bin test-app/image.bin \
		$(PRIVATE_KEY) 7
	$(SIGN_TOOL) $(SIGN_ARGS) --delta test-app/image_v1_signed.bin test-app/image.bin \
		$(PRIVATE_KEY) 2
	@st-flash write factory.bin 0x08000000
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1) || (st-flash reset && test `$(EXPVER)` -eq 1)
	@echo
	@sleep 2
	@st-flash write test-app/image_v7_signed_diff.bin 0x0802C000
	@sleep 2
	@st-flash reset
	@echo Expecting version '7'
	@(test `$(EXPVER)` -eq 7) || (st-flash reset && test `$(EXPVER)` -eq 7)
	@sleep 2
	@st-flash reset
	@echo Expecting version '1'
	@(test `$(EXPVER)` -eq 1)
	@sleep 2
	@st-flash reset
	@sleep 2
	@st-flash erase || st-flash erase
	@st-flash write factory.bin 0x08000000
	@echo Expecting version '1'
	@test `$(EXPVER)` -eq 1
	@sleep 2
	@st-flash reset
	@sleep 2
	@st-flash write test-app/image_v2_signed_diff.bin 0x0802C000
	@st-flash reset
	@echo Expecting version '2'
	@(test `$(EXPVER)` -eq 2) || (st-flash reset && test `$(EXPVER)` -eq 2)
	@sleep 2
	@st-flash reset
	@echo Expecting version '2'
	@(test `$(EXPVER)` -eq 2)
	@sleep 2
	@st-flash reset
	@echo Expecting version '2'
	@(test `$(EXPVER)` -eq 2)
	@echo "TEST SUCCESSFUL"


test-delta-update-ext: factory.bin test-app/image.bin tools/uart-flash-server/ufserver tools/delta/bmdiff tools/test-expect-version/test-expect-version
	@st-flash reset
	@st-flash erase || st-flash erase
	@diff .config config/examples/stm32wb-delta-ext.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-delta-ext.config to .config to run this test\n\n" && exit 1)
	$(SIGN_TOOL) $(SIGN_ARGS) --delta test-app/image_v1_signed.bin test-app/image.bin \
		$(PRIVATE_KEY) 7
	@(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff.bin $(USBTTY))&
	@st-flash write factory.bin 0x08000000
	@sync
	@echo Waiting $(TIMEOUT) seconds...
	@sleep $(TIMEOUT)
	@killall ufserver
	@st-flash reset
	@sleep 10
	@st-flash reset
	@sleep 5
	@st-flash read boot_full.bin 0x0800C000 0x8000
	@SIZE=`wc -c test-app/image_v7_signed.bin | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v7_signed.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"
	@sleep 1
	@echo
	@echo
	@st-flash reset
	@(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff.bin $(USBTTY))&
	@echo TEST INVERSE
	@st-flash reset
	@sleep 1
	@st-flash reset
	@sleep $(TIMEOUT)
	@st-flash reset
	@killall ufserver
	@st-flash reset
	@st-flash read boot_full.bin 0x0800C000 0x8000
	@SIZE=`wc -c test-app/image_v1_signed.bin | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v1_signed.bin || (echo "TEST INVERSE FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"


