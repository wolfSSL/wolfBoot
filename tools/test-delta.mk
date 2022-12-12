test-delta-update:SIGN_ARGS?=--ecc256
test-delta-update:USBTTY?=/dev/ttyACM0
test-delta-update:EXPVER=tools/test-expect-version/test-expect-version /dev/ttyACM0

test-delta-update-ext:SIGN_ARGS?=--ecc256
test-delta-update-ext:USBTTY?=/dev/ttyACM0
test-delta-update-ext:TIMEOUT?=50
test-delta-update-ext:EXPVER=tools/test-expect-version/test-expect-version /dev/ttyACM0

test-delta-enc-update-ext:SIGN_ARGS=--ecc256 --sha256
test-delta-enc-update-ext:USBTTY?=/dev/ttyACM0
test-delta-enc-update-ext:TIMEOUT?=70
test-delta-enc-update-ext:EXPVER=tools/test-expect-version/test-expect-version /dev/ttyACM0
test-delta-enc-update-ext:PART_SIZE=131023
test-delta-enc-update-ext:APP=test-app/image_v7_signed_diff_encrypted.bin

test-delta-update: distclean factory.bin test-app/image.bin tools/uart-flash-server/ufserver tools/delta/bmdiff tools/test-expect-version/test-expect-version
	@killall ufserver || true
	@st-flash reset
	@sleep 2
	@dd if=/dev/zero of=zero.bin bs=4096 count=1
	@st-flash write zero.bin 0x0800B000
	@st-flash reset
	@st-flash write zero.bin 0x0802B000
	@st-flash reset
	@st-flash erase || st-flash erase
	@rm -f zero.bin
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
	@st-flash erase || st-flash erase
	@st-flash write factory.bin 0x08000000
	@sleep 2
	@st-flash reset
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

test-delta-update-ext: distclean factory.bin test-app/image.bin tools/uart-flash-server/ufserver tools/delta/bmdiff tools/test-expect-version/test-expect-version
	@killall ufserver || true
	@st-flash reset
	@dd if=/dev/zero of=zero.bin bs=4096 count=1
	@st-flash write zero.bin 0x0800B000
	@st-flash reset
	@st-flash write zero.bin 0x0802B000
	@st-flash reset
	@st-flash erase || st-flash erase
	@rm -f zero.bin
	@diff .config config/examples/stm32wb-delta-ext.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-delta-ext.config to .config to run this test\n\n" && exit 1)
	$(SIGN_TOOL) $(SIGN_ARGS) --delta test-app/image_v1_signed.bin test-app/image.bin \
		$(PRIVATE_KEY) 7
	@(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff.bin $(USBTTY))&
	@st-flash reset
	@st-flash write factory.bin 0x08000000
	@st-flash reset
	@sync
	@echo Waiting $(TIMEOUT) seconds...
	@sleep $(TIMEOUT)
	@killall ufserver
	@st-flash read boot_full.bin 0x0800C000 0x8000
	@SIZE=`wc -c test-app/image_v7_signed.bin | awk '{$$1=$$1};1' | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v7_signed.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"
	@echo
	@echo
	@echo TEST INVERSE
	@(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff.bin $(USBTTY))&
	@st-flash reset
	@sleep $(TIMEOUT)
	@killall ufserver
	@st-flash reset
	@st-flash read boot_full.bin 0x0800C000 0x8000
	@SIZE=`wc -c test-app/image_v1_signed.bin | awk '{$$1=$$1};1' | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v1_signed.bin || (echo "TEST INVERSE FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"

test-delta-enc-update-ext: distclean factory.bin test-app/image.bin tools/uart-flash-server/ufserver tools/delta/bmdiff tools/test-expect-version/test-expect-version
	   @killall ufserver || true
	   @st-flash reset
	   @dd if=/dev/zero of=zero.bin bs=4096 count=1
	   @st-flash write zero.bin 0x0800B000
	   @st-flash reset
	   @st-flash write zero.bin 0x0802B000
	   @st-flash reset
	   @st-flash erase || st-flash erase
	   @rm -f zero.bin
	   @diff .config config/examples/stm32wb-delta-enc-ext.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-delta-enc-ext.config to .config to run this test\n\n" && exit 1)
	   $(SIGN_TOOL) $(SIGN_ARGS) --delta test-app/image_v1_signed.bin \
	           $(ENCRYPT_STRING) --encrypt /tmp/enc_key.der \
	           test-app/image.bin \
	           $(PRIVATE_KEY) 7
	   @(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff_encrypted.bin $(USBTTY))&
	   @st-flash write factory.bin 0x08000000
	   @sync
	   @sleep 4
	   @st-flash reset
	   @sleep 2
	   @echo Waiting $(TIMEOUT) seconds...
	   @st-flash reset
	   @sleep $(TIMEOUT)
	   @st-flash reset
	   @st-flash read boot_full.bin 0x0800C000 0x8000
	   @SIZE=`wc -c test-app/image_v7_signed.bin | awk '{$$1=$$1};1' | cut -d" " -f 1`;  \
	           dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	   @diff boot.bin test-app/image_v7_signed.bin || (echo "TEST FAILED" && exit 1)
	   @rm boot.bin boot_full.bin
	   @echo "TEST SUCCESSFUL"
	   @sleep 1
	   @echo
	   @echo
	   @echo TEST INVERSE
	   @killall ufserver
	   @(tools/uart-flash-server/ufserver test-app/image_v7_signed_diff_encrypted.bin $(USBTTY))&
	   @st-flash reset
	   @sleep $(TIMEOUT)
	   @st-flash reset
	   @killall ufserver
	   @st-flash reset
	   @st-flash read boot_full.bin 0x0800C000 0x8000
	   @SIZE=`wc -c test-app/image_v1_signed.bin | awk '{$$1=$$1};1' | cut -d" " -f 1`;  \
	           dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	   @diff boot.bin test-app/image_v1_signed.bin || (echo "TEST INVERSE FAILED" && exit 1)
	   @rm boot.bin boot_full.bin
	   @echo "TEST SUCCESSFUL"

test-delta-chacha-update-ext:
	@printf "0123456789abcdef0123456789abcdef0123456789ab" > /tmp/enc_key.der
	@make test-delta-enc-update-ext ENCRYPT_WITH_CHACHA=1

test-delta-aes128-update-ext:
	@printf "0123456789abcdef0123456789abcdef" > /tmp/enc_key.der
	@make test-delta-enc-update-ext ENCRYPT_WITH_AES128=1 ENCRYPT_STRING=--aes128

test-delta-aes256-update-ext:
	@printf "0123456789abcdef0123456789abcdef0123456789abcdef" > /tmp/enc_key.der
	@make test-delta-enc-update-ext ENCRYPT_WITH_AES256=1 ENCRYPT_STRING=--aes256
