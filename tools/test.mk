TEST_UPDATE_VERSION?=2
EXPVER=tools/test-expect-version/test-expect-version

$(EXPVER):
	make -C tools/test-expect-version

test-update: test-app/image.bin FORCE
	$(SIGN_TOOL) test-app/image.bin $(PRIVATE_KEY) $(TEST_UPDATE_VERSION) 131072
	dd if=test-app/image.bin.v$(TEST_UPDATE_VERSION).signed of=test-update.bin bs=1 count=131067
	printf "pBOOT" >> test-update.bin
	sudo st-term reset init || true
	sleep 2
	sudo st-flash --reset write test-update.bin 0x08040000

test-erase: FORCE
	sudo st-term reset init || true
	sleep 2
	sudo st-flash erase


test-factory: factory.bin
	sudo st-term reset init || true
	sleep 2
	sudo st-flash --reset write factory.bin 0x08000000

test-reset: FORCE
	sudo st-info --reset || true



## Test cases:

test-00-forward-update: $(EXPVER) FORCE
	make test-factory
	$(EXPVER)
