ENC_TEST_UPDATE_VERSION?=2
KEYGEN_TOOL=python3 ./tools/keytools/keygen.py
SIGN_ARGS?=--ecc256
SIGN_ENC_ARGS?=--ecc256 --encrypt /tmp/enc_key.der
USBTTY?=/dev/ttyACM0
TIMEOUT?=60

ifneq ("$(wildcard ./tools/keytools/sign)","")
    SIGN_TOOL=./tools/keytools/sign
else
    ifneq ("$(wildcard ./tools/keytools/sign.exe)","")
        SIGN_TOOL=./tools/keytools/sign.exe
    else
        SIGN_TOOL=python3 ./tools/keytools/sign.py
    endif
endif

tools/uart-flash-server/ufserver: FORCE
	@make -C `dirname $@`
	@rm -f src/libwolfboot.o

test-enc-update: factory.bin test-app/image.bin tools/uart-flash-server/ufserver
	@diff .config config/examples/stm32wb-uart-flash-encryption.config || (echo "\n\n*** Error: please copy config/examples/stm32wb-uart-flash-encryption.config to .config to run this test\n\n" && exit 1)
	@printf "0123456789abcdef0123456789abcdef0123456789ab" > /tmp/enc_key.der
	@$(SIGN_TOOL) $(SIGN_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@$(SIGN_TOOL) $(SIGN_ENC_ARGS) test-app/image.bin $(PRIVATE_KEY) $(ENC_TEST_UPDATE_VERSION)
	@st-flash write factory.bin 0x08000000
	@sleep 2
	@sudo true
	@(sudo tools/uart-flash-server/ufserver test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed_and_encrypted.bin $(USBTTY))&
	@sleep 5
	@st-flash reset
	@sleep $(TIMEOUT)
	@st-flash reset
	@sleep 1
	@sudo killall ufserver
	@st-flash read boot.bin 0x08010000 0x1000
	@dd if=test-app/image_v$(ENC_TEST_UPDATE_VERSION)_signed.bin of=boot_compare.bin bs=4096 count=1
	@diff boot.bin boot_compare.bin || (echo "TEST FAILED" && exit 1)
	@rm boot.bin boot_compare.bin
	@echo "TEST SUCCESSFUL"

