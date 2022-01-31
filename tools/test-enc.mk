test-enc-update:ENC_TEST_UPDATE_VERSION?=2
test-enc-update:SIGN_ARGS?=--ecc256
test-enc-update:SIGN_ENC_ARGS?=--ecc256 --encrypt /tmp/enc_key.der
test-enc-update:USBTTY?=/dev/ttyACM0
test-enc-update:TIMEOUT?=60

test-enc-aes128-update:ENC_TEST_UPDATE_VERSION?=2
test-enc-aes128-update:SIGN_ARGS?=--ecc256
test-enc-aes128-update:SIGN_ENC_ARGS?=--ecc256 --aes128 --encrypt /tmp/enc_key.der
test-enc-aes128-update:USBTTY?=/dev/ttyACM0
test-enc-aes128-update:TIMEOUT?=60

test-enc-aes256-update:ENC_TEST_UPDATE_VERSION?=2
test-enc-aes256-update:SIGN_ARGS?=--ecc256
test-enc-aes256-update:SIGN_ENC_ARGS?=--ecc256 --aes256 --encrypt /tmp/enc_key.der
test-enc-aes256-update:USBTTY?=/dev/ttyACM0
test-enc-aes256-update:TIMEOUT?=60

tools/uart-flash-server/ufserver: FORCE
	@make -C `dirname $@`
	@rm -f src/libwolfboot.o
	@killall ufserver || true

test-enc-update: factory.bin test-app/image.bin tools/uart-flash-server/ufserver
	@diff .config config/examples/stm32wb-uart-flash-encryption.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-uart-flash-encryption.config to .config to run this test\n\n" && exit 1)
	@printf "0123456789abcdef0123456789abcdef0123456789ab" > /tmp/enc_key.der
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@$(SIGN_TOOL) $(SIGN_ENC_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@(tools/uart-flash-server/ufserver test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed_and_encrypted.bin $(USBTTY))&
	@st-flash erase
	@st-flash write factory.bin 0x08000000
	@sleep 3
	@sync
	@st-flash reset
	@sync
	@sleep $(TIMEOUT)
	@st-flash reset
	@sleep 3
	@killall ufserver
	@st-flash read boot_full.bin 0x08010000 0x8000
	@SIZE=`wc -c test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"

test-enc-aes128-update: factory.bin test-app/image.bin tools/uart-flash-server/ufserver
	@diff .config config/examples/stm32wb-uart-flash-encryption-aes128.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-uart-flash-encryption-aes128.config to .config to run this test\n\n" && exit 1)
	@printf "0123456789abcdef0123456789abcdef" > /tmp/enc_key.der
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@$(SIGN_TOOL) $(SIGN_ENC_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@(tools/uart-flash-server/ufserver test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed_and_encrypted.bin $(USBTTY))&
	@st-flash erase
	@st-flash write factory.bin 0x08000000
	@sleep 3
	@sync
	@st-flash reset
	@sync
	@sleep $(TIMEOUT)
	@st-flash reset
	@sleep 3
	@killall ufserver
	@st-flash read boot_full.bin 0x08010000 0x8000
	@SIZE=`wc -c test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"

test-enc-aes256-update: factory.bin test-app/image.bin tools/uart-flash-server/ufserver
	@diff .config config/examples/stm32wb-uart-flash-encryption-aes256.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-uart-flash-encryption-aes256.config to .config to run this test\n\n" && exit 1)
	@printf "0123456789abcdef0123456789abcdef0123456789abcdef" > /tmp/enc_key.der
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@$(SIGN_TOOL) $(SIGN_ENC_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@(tools/uart-flash-server/ufserver test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed_and_encrypted.bin $(USBTTY))&
	@st-flash erase
	@st-flash write factory.bin 0x08000000
	@sleep 3
	@sync
	@st-flash reset
	@sync
	@sleep $(TIMEOUT)
	@st-flash reset
	@sleep 3
	@killall ufserver
	@st-flash read boot_full.bin 0x08010000 0x8000
	@SIZE=`wc -c test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin | cut -d" " -f 1`;  \
		dd if=boot_full.bin of=boot.bin bs=1 count=$$SIZE
	@diff boot.bin test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_full.bin
	@echo "TEST SUCCESSFUL"
